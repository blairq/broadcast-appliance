// ============================================================================
// src/gst-plugins/filtros/ptz.cpp — baptz: PTZ digital (pan, tilt y zoom recortando)
//
// Pan, tilt y zoom sin mover la cámara: recorta una ventana del cuadro y la
// amplía al tamaño original (bilineal). Es el PTZ que tiene CUALQUIER entrada;
// el módulo PTZ de la app (src/app/ptz) lo maneja igual que a una cámara
// motorizada, por propiedades:
//   vx, vy, vz   velocidades -1..1, como un joystick (0 = quieto). Arranca y
//                frena con una rampa corta, como una cabeza de verdad.
//   ir           "x,y,zoom,segundos": a una posición (un preset), con una curva
//                suave; 0 segundos = corte.
//   x, y, zoom, moviendo (solo lectura): para guardar presets.
//   estado       «x,y,zoom» (lectura y escritura): el motor lo conserva cuando
//                la cadena de la entrada se rearma.
// La posición es el centro de la ventana (0..1 del cuadro) y el zoom (1 = todo
// el cuadro, hasta 8). La ventana nunca sale del cuadro. El tiempo de las
// animaciones es el de los cuadros (PTS), así se mueve parejo aunque la app no.
//
// Sin zoom y quieto, PASANTE (sin copia): tenerlo puesto en todas las entradas
// no cuesta nada.
//
// Formatos: RGB de 4 bytes (con o sin alfa). El tamaño no cambia.
// Concurrencia: propiedades y cuadro se cruzan bajo GST_OBJECT_LOCK.
// ============================================================================

#include "comun.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_PTZ (ba_ptz_get_type())
G_DECLARE_FINAL_TYPE(BaPtz, ba_ptz, BA, PTZ, GstVideoFilter)
G_END_DECLS

namespace {
constexpr double kZoomMax = 8.0;
/// A velocidad 1, la ventana recorre medio ancho de ella misma por segundo.
constexpr double kPanPorSegundo = 0.5;
/// A velocidad 1, el zoom se duplica en ~1,4 s.
constexpr double kZoomPorSegundo = 0.5;
/// Rampa de arranque y frenado del joystick (constante de tiempo, s).
constexpr double kRampa = 0.12;
/// Por debajo de esta velocidad (imperceptible) se considera quieto.
constexpr double kQuieto = 0.02;
} // namespace

struct _BaPtz {
    GstVideoFilter parent;
    // Bajo GST_OBJECT_LOCK.
    double x = 0.5, y = 0.5, zoom = 1.0;
    double vx = 0, vy = 0, vz = 0;
    double ax = 0, ay = 0, az = 0;
    bool yendo = false;
    double desdeX = 0.5, desdeY = 0.5, desdeZ = 1, haciaX = 0.5, haciaY = 0.5, haciaZ = 1;
    double t = 0, duracion = 0;
    GstClockTime ultimo = GST_CLOCK_TIME_NONE;
};

G_DEFINE_TYPE(BaPtz, ba_ptz, GST_TYPE_VIDEO_FILTER)

enum { PROP_0, PROP_VX, PROP_VY, PROP_VZ, PROP_IR, PROP_X, PROP_Y, PROP_ZOOM, PROP_MOVIENDO, PROP_ESTADO };

namespace {

void limitar(BaPtz* p) {
    p->zoom = std::clamp(p->zoom, 1.0, kZoomMax);
    const double medio = 0.5 / p->zoom;
    p->x = std::clamp(p->x, medio, 1.0 - medio);
    p->y = std::clamp(p->y, medio, 1.0 - medio);
}

bool moviendo(const BaPtz* p) { return p->yendo || p->ax != 0 || p->ay != 0 || p->az != 0 || p->vx != 0 || p->vy != 0 || p->vz != 0; }

/// Curva suave (arranca y llega despacio) para ir a un preset.
double suave(double u) { return u * u * (3.0 - 2.0 * u); }

void avanzar(BaPtz* p, double dt) {
    if (p->yendo) {
        p->t += dt;
        const double u = suave(std::min(1.0, p->t / std::max(1e-6, p->duracion)));
        // El zoom se interpola en escala logarítmica: se ve parejo.
        p->zoom = std::exp(std::log(p->desdeZ) + (std::log(p->haciaZ) - std::log(p->desdeZ)) * u);
        p->x = p->desdeX + (p->haciaX - p->desdeX) * u;
        p->y = p->desdeY + (p->haciaY - p->desdeY) * u;
        if (p->t >= p->duracion) p->yendo = false;
        limitar(p);
        return;
    }
    const double k = 1.0 - std::exp(-dt / kRampa);
    p->ax += (p->vx - p->ax) * k;
    p->ay += (p->vy - p->ay) * k;
    p->az += (p->vz - p->az) * k;
    if (std::fabs(p->ax) < kQuieto && p->vx == 0) p->ax = 0;
    if (std::fabs(p->ay) < kQuieto && p->vy == 0) p->ay = 0;
    if (std::fabs(p->az) < kQuieto && p->vz == 0) p->az = 0;
    p->zoom *= std::exp(p->az * kZoomPorSegundo * dt);
    p->x += p->ax * kPanPorSegundo * dt / p->zoom;
    p->y += p->ay * kPanPorSegundo * dt / p->zoom;
    limitar(p);
}

void ir(BaPtz* p, const char* texto) {
    if (texto == nullptr) return;
    // Decimales con punto, sin depender del locale (g_ascii_strtod); separados por coma o punto y coma.
    double v[4] = {0.5, 0.5, 1, 0};
    int n = 0;
    const char* c = texto;
    while (n < 4 && *c != '\0') {
        char* fin = nullptr;
        v[n] = g_ascii_strtod(c, &fin);
        if (fin == c) break;
        ++n;
        c = fin;
        while (*c == ',' || *c == ';' || *c == ' ') ++c;
    }
    if (n < 3) return;
    double x = v[0], y = v[1], z = v[2];
    const double s = v[3];
    p->vx = p->vy = p->vz = p->ax = p->ay = p->az = 0;
    z = std::clamp(z, 1.0, kZoomMax);
    const double medio = 0.5 / z;
    p->haciaZ = z;
    p->haciaX = std::clamp(x, medio, 1.0 - medio);
    p->haciaY = std::clamp(y, medio, 1.0 - medio);
    p->duracion = std::max(0.0, s);
    if (p->duracion <= 0.0) {
        p->x = p->haciaX;
        p->y = p->haciaY;
        p->zoom = p->haciaZ;
        p->yendo = false;
        return;
    }
    p->desdeX = p->x;
    p->desdeY = p->y;
    p->desdeZ = p->zoom;
    p->t = 0;
    p->yendo = true;
}

void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* p = BA_PTZ(o);
    GST_OBJECT_LOCK(p);
    switch (id) {
        case PROP_VX: p->vx = std::clamp(g_value_get_double(v), -1.0, 1.0); break;
        case PROP_VY: p->vy = std::clamp(g_value_get_double(v), -1.0, 1.0); break;
        case PROP_VZ: p->vz = std::clamp(g_value_get_double(v), -1.0, 1.0); break;
        case PROP_IR: ir(p, g_value_get_string(v)); break;
        case PROP_ESTADO: { // «x,y,zoom»: ir sin animación
            const gchar* t = g_value_get_string(v);
            if (t != nullptr) ir(p, (std::string(t) + ",0").c_str());
            break;
        }
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    if (id <= PROP_VZ && (p->vx != 0 || p->vy != 0 || p->vz != 0)) p->yendo = false; // el joystick manda
    GST_OBJECT_UNLOCK(p);
    // Puede dejar de ser pasante.
    gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(o));
}

void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* p = BA_PTZ(o);
    GST_OBJECT_LOCK(p);
    switch (id) {
        case PROP_VX: g_value_set_double(v, p->vx); break;
        case PROP_VY: g_value_set_double(v, p->vy); break;
        case PROP_VZ: g_value_set_double(v, p->vz); break;
        case PROP_IR: g_value_set_string(v, ""); break;
        case PROP_X: g_value_set_double(v, p->x); break;
        case PROP_Y: g_value_set_double(v, p->y); break;
        case PROP_ZOOM: g_value_set_double(v, p->zoom); break;
        case PROP_MOVIENDO: g_value_set_boolean(v, moviendo(p)); break;
        case PROP_ESTADO: {
            // Con punto decimal siempre (lo vuelve a leer `ir`).
            gchar bx[G_ASCII_DTOSTR_BUF_SIZE], by[G_ASCII_DTOSTR_BUF_SIZE], bz[G_ASCII_DTOSTR_BUF_SIZE];
            const double x = p->yendo ? p->haciaX : p->x, y = p->yendo ? p->haciaY : p->y, z = p->yendo ? p->haciaZ : p->zoom;
            gchar* t = g_strdup_printf("%s,%s,%s", g_ascii_dtostr(bx, sizeof bx, x), g_ascii_dtostr(by, sizeof by, y),
                                       g_ascii_dtostr(bz, sizeof bz, z));
            g_value_take_string(v, t);
            break;
        }
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(p);
}

void antes(GstBaseTransform* bt, GstBuffer* b) {
    auto* p = BA_PTZ(bt);
    const GstClockTime t = GST_BUFFER_PTS(b);
    GST_OBJECT_LOCK(p);
    double dt = 0;
    if (GST_CLOCK_TIME_IS_VALID(t) && GST_CLOCK_TIME_IS_VALID(p->ultimo) && t > p->ultimo)
        dt = std::min(0.1, static_cast<double>(t - p->ultimo) / 1e9);
    p->ultimo = t;
    avanzar(p, dt);
    const bool identidad = p->zoom <= 1.0 + 1e-6 && !moviendo(p);
    GST_OBJECT_UNLOCK(p);
    gst_base_transform_set_passthrough(bt, identidad);
}

/// Bilineal de 4 bytes por píxel: la ventana [x0, x0+w) x [y0, y0+h) del origen al destino entero.
void escalar(const std::uint8_t* src, int sstride, std::uint8_t* dst, int dstride, int W, int H, double x0, double y0,
             double w, double h, int fila0, int fila1) {
    const double sx = w / W, sy = h / H;
    std::vector<int> xi(static_cast<size_t>(W));
    std::vector<int> xf(static_cast<size_t>(W));
    for (int i = 0; i < W; ++i) {
        const double fx = std::clamp(x0 + (i + 0.5) * sx - 0.5, 0.0, static_cast<double>(W - 1));
        xi[static_cast<size_t>(i)] = std::min(static_cast<int>(fx), W - 2 < 0 ? 0 : W - 2);
        xf[static_cast<size_t>(i)] = static_cast<int>((fx - xi[static_cast<size_t>(i)]) * 256.0);
    }
    for (int j = fila0; j < fila1; ++j) {
        const double fy = std::clamp(y0 + (j + 0.5) * sy - 0.5, 0.0, static_cast<double>(H - 1));
        const int yi = std::min(static_cast<int>(fy), H - 2 < 0 ? 0 : H - 2);
        const int yf = static_cast<int>((fy - yi) * 256.0);
        const std::uint8_t* r0 = src + static_cast<size_t>(yi) * sstride;
        const std::uint8_t* r1 = r0 + (H > 1 ? sstride : 0);
        std::uint8_t* d = dst + static_cast<size_t>(j) * dstride;
        for (int i = 0; i < W; ++i) {
            const int x = xi[static_cast<size_t>(i)] * 4, f = xf[static_cast<size_t>(i)];
            const int x1 = W > 1 ? x + 4 : x;
            for (int c = 0; c < 4; ++c) {
                const int a = r0[x + c] * (256 - f) + r0[x1 + c] * f;
                const int b = r1[x + c] * (256 - f) + r1[x1 + c] * f;
                d[i * 4 + c] = static_cast<std::uint8_t>((a * (256 - yf) + b * yf + 32768) >> 16);
            }
        }
    }
}

GstFlowReturn transformar(GstVideoFilter* vf, GstVideoFrame* in, GstVideoFrame* out) {
    auto* p = BA_PTZ(vf);
    GST_OBJECT_LOCK(p);
    const double x = p->x, y = p->y, zoom = p->zoom;
    GST_OBJECT_UNLOCK(p);
    const int W = GST_VIDEO_FRAME_WIDTH(in), H = GST_VIDEO_FRAME_HEIGHT(in);
    const auto* src = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, 0));
    auto* dst = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, 0));
    const int ss = GST_VIDEO_FRAME_PLANE_STRIDE(in, 0), ds = GST_VIDEO_FRAME_PLANE_STRIDE(out, 0);
    const double w = W / zoom, h = H / zoom;
    const double x0 = x * W - w / 2, y0 = y * H - h / 2;
    // En franjas, en varios hilos (un 1080p es ~2 millones de píxeles).
    const int hilos = H >= 480 ? static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u)) : 1;
    std::vector<std::thread> v;
    for (int k = 1; k < hilos; ++k)
        v.emplace_back(escalar, src, ss, dst, ds, W, H, x0, y0, w, h, H * k / hilos, H * (k + 1) / hilos);
    escalar(src, ss, dst, ds, W, H, x0, y0, w, h, 0, H / hilos);
    for (auto& t : v)
        t.join();
    return GST_FLOW_OK;
}

gboolean alEvento(GstBaseTransform* bt, GstEvent* e) {
    if (GST_EVENT_TYPE(e) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(e) == GST_EVENT_SEGMENT) {
        auto* p = BA_PTZ(bt);
        GST_OBJECT_LOCK(p);
        p->ultimo = GST_CLOCK_TIME_NONE;
        GST_OBJECT_UNLOCK(p);
    }
    return GST_BASE_TRANSFORM_CLASS(ba_ptz_parent_class)->sink_event(bt, e);
}

} // namespace

static void ba_ptz_init(BaPtz* p) {
    p->x = p->y = 0.5;
    p->zoom = 1.0;
    p->vx = p->vy = p->vz = p->ax = p->ay = p->az = 0;
    p->yendo = false;
    p->desdeX = p->desdeY = p->haciaX = p->haciaY = 0.5;
    p->desdeZ = p->haciaZ = 1;
    p->t = p->duracion = 0;
    p->ultimo = GST_CLOCK_TIME_NONE;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(p), TRUE);
}

static void ba_ptz_class_init(BaPtzClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_VX, g_param_spec_double("vx", "Pan", "Velocidad horizontal (-1..1)", -1, 1, 0, rw));
    g_object_class_install_property(go, PROP_VY, g_param_spec_double("vy", "Tilt", "Velocidad vertical, + es abajo (-1..1)", -1, 1, 0, rw));
    g_object_class_install_property(go, PROP_VZ, g_param_spec_double("vz", "Zoom", "Velocidad de zoom (-1..1)", -1, 1, 0, rw));
    g_object_class_install_property(go, PROP_IR, g_param_spec_string("ir", "Ir a", "«x,y,zoom,segundos»: a una posición", "", rw));
    g_object_class_install_property(go, PROP_X, g_param_spec_double("x", "X", "Centro de la ventana (0..1)", 0, 1, 0.5, ro));
    g_object_class_install_property(go, PROP_Y, g_param_spec_double("y", "Y", "Centro de la ventana (0..1)", 0, 1, 0.5, ro));
    g_object_class_install_property(go, PROP_ZOOM, g_param_spec_double("zoom", "Zoom", "Zoom actual (1..8)", 1, kZoomMax, 1, ro));
    g_object_class_install_property(go, PROP_ESTADO,
                                    g_param_spec_string("estado", "Estado", "«x,y,zoom»: la posición (el motor la conserva al rearmar)",
                                                        "0.5,0.5,1", rw));
    g_object_class_install_property(go, PROP_MOVIENDO, g_param_spec_boolean("moviendo", "Moviendo", "Se está moviendo", FALSE, ro));

    auto* ek = GST_ELEMENT_CLASS(k);
    gst_element_class_set_static_metadata(ek, "PTZ digital", "Filter/Effect/Video",
                                          "Pan, tilt y zoom recortando y ampliando la imagen", "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw,format={BGRA,RGBA,ARGB,ABGR,BGRx,RGBx,xRGB,xBGR}");
    gst_element_class_add_pad_template(ek, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(ek, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);

    auto* bk = GST_BASE_TRANSFORM_CLASS(k);
    bk->before_transform = antes;
    bk->sink_event = alEvento;
    GST_VIDEO_FILTER_CLASS(k)->transform_frame = transformar;
}

extern "C" gboolean ba_registrar_ptz(GstPlugin* p) {
    return gst_element_register(p, "baptz", GST_RANK_NONE, BA_TYPE_PTZ);
}
