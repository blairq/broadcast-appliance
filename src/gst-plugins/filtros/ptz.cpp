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
// no cuesta nada (en GPU, baptzgl también deja de dibujar).
//
// ENCUADRE PEDIDO (seguimiento): un evento «ba/encuadre» que viene de arriba
// (lo manda baseguir) o la propiedad `encuadre` («x,y,zoom,suavidad»; la usa la
// app cuando el seguimiento está después del PTZ) dicen adónde ir; el PTZ va
// hacia ahí con una curva suave (constante de tiempo `suavidad`, s). El
// joystick o un preset mandan sobre el encuadre; si no llega otro en 1,5 s, se
// queda donde está. zoom 0 = no tocar el zoom.
//
// LA VENTANA, RÍO ABAJO: cada vez que cambia (y una vez por segundo), un evento
// «ba/ptz» (x, y, zoom, moviendo): lo que viene después (rostros, seguimiento)
// sabe que la imagen que recibe es esa parte de la toma.
//
// Formatos: RGB de 4 bytes (con o sin alfa). El tamaño no cambia.
// Concurrencia: propiedades y cuadro se cruzan bajo GST_OBJECT_LOCK.
//
// baptzgl: lo mismo en la GPU (GLMemory RGBA, docs/MAPA-GPU-CPU.md). Es un bin
//   gltransformation ! glshader
// con las mismas propiedades: las guarda un baptz que no está en la cadena
// (solo lleva la cuenta del movimiento) y, en cada cuadro, la ventana pasa a
// la escala y el corrimiento de gltransformation. El glshader de atrás obliga
// a dibujar (si no, gltransformation deja una meta afín que el mezclador
// aplica sin recortar a la caja de la capa). El motor lo usa en lugar de baptz
// cuando el video de la entrada va en la GPU.
//
// baptzd3d12: lo mismo en D3D12 (Windows, D3D12Memory). Es un bin
//   d3d12convert ! capsfilter(el tamaño de la entrada)
// En cada cuadro se le marca la ventana (meta de recorte: sin tocar la memoria
// ni cambiar el tamaño, así nada se renegocia mientras se mueve) y d3d12convert
// la amplía en la GPU al tamaño original. La ventana va en píxeles enteros.
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
    // El encuadre pedido (seguimiento).
    bool encuadrando = false;
    double ex = 0.5, ey = 0.5, ez = 0, etau = 0.6;
    gint64 encuadreUs = 0;
    // Lo último que se avisó río abajo (ba/ptz).
    double avisoX = -1, avisoY = -1, avisoZ = -1;
    int cuadrosSinAviso = 0;
};

G_DEFINE_TYPE(BaPtz, ba_ptz, GST_TYPE_VIDEO_FILTER)

enum { PROP_0, PROP_VX, PROP_VY, PROP_VZ, PROP_IR, PROP_X, PROP_Y, PROP_ZOOM, PROP_MOVIENDO, PROP_ESTADO, PROP_ENCUADRE, PROP_ENCUADRANDO };

namespace {

void limitar(BaPtz* p) {
    p->zoom = std::clamp(p->zoom, 1.0, kZoomMax);
    const double medio = 0.5 / p->zoom;
    p->x = std::clamp(p->x, medio, 1.0 - medio);
    p->y = std::clamp(p->y, medio, 1.0 - medio);
}

bool moviendo(const BaPtz* p) {
    return p->yendo || p->encuadrando || p->ax != 0 || p->ay != 0 || p->az != 0 || p->vx != 0 || p->vy != 0 || p->vz != 0;
}

/// «x,y,zoom[,suavidad]» -> el encuadre pedido (bajo el lock).
void pedirEncuadre(BaPtz* p, const char* texto) {
    if (texto == nullptr) return;
    double v[4] = {0.5, 0.5, 0, 0.6};
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
    if (n < 2) {
        p->encuadrando = false; // "" = soltar
        return;
    }
    p->ex = std::clamp(v[0], 0.0, 1.0);
    p->ey = std::clamp(v[1], 0.0, 1.0);
    p->ez = n >= 3 ? v[2] : 0.0;
    p->etau = n >= 4 ? std::clamp(v[3], 0.05, 10.0) : 0.6;
    p->encuadrando = true;
    p->encuadreUs = g_get_monotonic_time();
}

/// Un evento «ba/encuadre» (del seguimiento): true si lo era.
bool leerEncuadre(BaPtz* p, GstEvent* e) {
    if (GST_EVENT_TYPE(e) != GST_EVENT_CUSTOM_DOWNSTREAM && GST_EVENT_TYPE(e) != GST_EVENT_CUSTOM_DOWNSTREAM_OOB) return false;
    const GstStructure* s = gst_event_get_structure(e);
    if (s == nullptr || !gst_structure_has_name(s, "ba/encuadre")) return false;
    GST_OBJECT_LOCK(p);
    gboolean soltar = FALSE;
    if (gst_structure_get_boolean(s, "soltar", &soltar) && soltar) {
        p->encuadrando = false;
    } else {
        double x = 0.5, y = 0.5, z = 0, tau = 0.6;
        gst_structure_get_double(s, "x", &x);
        gst_structure_get_double(s, "y", &y);
        gst_structure_get_double(s, "zoom", &z);
        gst_structure_get_double(s, "suavidad", &tau);
        p->ex = std::clamp(x, 0.0, 1.0);
        p->ey = std::clamp(y, 0.0, 1.0);
        p->ez = z;
        p->etau = std::clamp(tau, 0.05, 10.0);
        p->encuadrando = true;
        p->encuadreUs = g_get_monotonic_time();
    }
    GST_OBJECT_UNLOCK(p);
    return true;
}

/// El evento «ba/ptz» si la ventana cambió (o pasó un segundo). Bajo el lock.
GstEvent* avisoSiCambio(BaPtz* p) {
    const bool cambio = std::fabs(p->x - p->avisoX) > 1e-4 || std::fabs(p->y - p->avisoY) > 1e-4 ||
                        std::fabs(p->zoom - p->avisoZ) > 1e-4;
    if (!cambio && ++p->cuadrosSinAviso < 30) return nullptr;
    p->cuadrosSinAviso = 0;
    p->avisoX = p->x;
    p->avisoY = p->y;
    p->avisoZ = p->zoom;
    return gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
                                gst_structure_new("ba/ptz", "x", G_TYPE_DOUBLE, p->x, "y", G_TYPE_DOUBLE, p->y, "zoom",
                                                  G_TYPE_DOUBLE, p->zoom, "moviendo", G_TYPE_BOOLEAN, moviendo(p), nullptr));
}

/// Curva suave (arranca y llega despacio) para ir a un preset.
double suave(double u) { return u * u * (3.0 - 2.0 * u); }

void avanzar(BaPtz* p, double dt) {
    // El encuadre pedido: hacia ahí, con una curva suave, si nada manda más
    // (joystick, preset) y no venció (1,5 s sin pedidos: se queda).
    if (p->encuadrando && g_get_monotonic_time() - p->encuadreUs > 1'500'000) p->encuadrando = false;
    if (p->encuadrando && !p->yendo && p->vx == 0 && p->vy == 0 && p->vz == 0) {
        const double k = dt > 0 ? 1.0 - std::exp(-dt / p->etau) : 0.0;
        if (p->ez >= 1.0) p->zoom = std::exp(std::log(p->zoom) + (std::log(std::clamp(p->ez, 1.0, kZoomMax)) - std::log(p->zoom)) * k);
        p->x += (p->ex - p->x) * k;
        p->y += (p->ey - p->y) * k;
        limitar(p);
        return;
    }
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
        case PROP_ENCUADRE: pedirEncuadre(p, g_value_get_string(v)); break;
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
        case PROP_ENCUADRE: g_value_set_string(v, ""); break;
        case PROP_ENCUADRANDO: g_value_set_boolean(v, p->encuadrando); break;
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
    GstEvent* aviso = avisoSiCambio(p);
    GST_OBJECT_UNLOCK(p);
    gst_base_transform_set_passthrough(bt, identidad);
    if (aviso != nullptr) gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(bt), aviso);
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
    leerEncuadre(BA_PTZ(bt), e); // y sigue de largo (lo que venga después también lo ve)
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
    g_object_class_install_property(go, PROP_ENCUADRE,
                                    g_param_spec_string("encuadre", "Encuadre",
                                                        "«x,y,zoom,suavidad»: ir siguiendo a ese encuadre (zoom 0 = no tocarlo; vacío = soltar)",
                                                        "", rw));
    g_object_class_install_property(go, PROP_ENCUADRANDO,
                                    g_param_spec_boolean("encuadrando", "Encuadrando", "Está siguiendo un encuadre pedido", FALSE, ro));

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

// ---------------------------------------------------------------- baptzgl

G_BEGIN_DECLS
#define BA_TYPE_PTZ_GL (ba_ptz_gl_get_type())
G_DECLARE_FINAL_TYPE(BaPtzGl, ba_ptz_gl, BA, PTZ_GL, GstBin)
G_END_DECLS

struct _BaPtzGl {
    GstBin parent;
    BaPtz* cuenta;        ///< el estado y las propiedades (no está en la cadena)
    GstElement* transf;   ///< gltransformation
    GstElement* fijar;    ///< glshader (obliga a dibujar)
    gboolean pasante;     ///< quieto y sin zoom: ninguno de los dos dibuja
};

G_DEFINE_TYPE(BaPtzGl, ba_ptz_gl, GST_TYPE_BIN)

namespace {

void glPoner(GObject* o, guint, const GValue* v, GParamSpec* ps) {
    g_object_set_property(G_OBJECT(BA_PTZ_GL(o)->cuenta), ps->name, v);
}

void glLeer(GObject* o, guint, GValue* v, GParamSpec* ps) {
    g_object_get_property(G_OBJECT(BA_PTZ_GL(o)->cuenta), ps->name, v);
}

/// Cada cuadro: avanza el movimiento con su hora y le pasa la ventana a gltransformation.
GstPadProbeReturn alCuadroGl(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* s = static_cast<BaPtzGl*>(d);
    BaPtz* p = s->cuenta;
    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) != 0) {
        GstEvent* e = GST_PAD_PROBE_INFO_EVENT(info);
        leerEncuadre(p, e);
        if (GST_EVENT_TYPE(e) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(e) == GST_EVENT_SEGMENT) {
            GST_OBJECT_LOCK(p);
            p->ultimo = GST_CLOCK_TIME_NONE;
            GST_OBJECT_UNLOCK(p);
        }
        return GST_PAD_PROBE_OK;
    }
    const GstClockTime t = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_OBJECT_LOCK(p);
    double dt = 0;
    if (GST_CLOCK_TIME_IS_VALID(t) && GST_CLOCK_TIME_IS_VALID(p->ultimo) && t > p->ultimo)
        dt = std::min(0.1, static_cast<double>(t - p->ultimo) / 1e9);
    p->ultimo = t;
    avanzar(p, dt);
    const double x = p->x, y = p->y, z = p->zoom;
    const gboolean quieto = z <= 1.0 + 1e-6 && !moviendo(p);
    GstEvent* aviso = avisoSiCambio(p);
    GST_OBJECT_UNLOCK(p);
    if (aviso != nullptr) {
        GstPad* sale = gst_element_get_static_pad(s->fijar, "src");
        gst_pad_push_event(sale, aviso);
        gst_object_unref(sale);
    }
    // QUIETO Y SIN ZOOM: ni gltransformation ni glshader dibujan (pasa la misma
    // textura). Eran una copia por cuadro en cada entrada para nada.
    if (quieto != s->pasante) {
        s->pasante = quieto;
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(s->transf), quieto);
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(s->fijar), quieto);
    }
    if (quieto) return GST_PAD_PROBE_OK;
    // Con ortho, la ventana de centro (x, y) y lado 1/zoom ocupa todo el cuadro:
    // escala zoom y corrimiento (0,5 − centro) × zoom (igual que el recorte de la composición).
    g_object_set(s->transf, "scale-x", static_cast<gfloat>(z), "scale-y", static_cast<gfloat>(z), "translation-x",
                 static_cast<gfloat>((0.5 - x) * z), "translation-y", static_cast<gfloat>((0.5 - y) * z), nullptr);
    return GST_PAD_PROBE_OK;
}

} // namespace

static void ba_ptz_gl_init(BaPtzGl* s) {
    s->cuenta = BA_PTZ(g_object_ref_sink(g_object_new(BA_TYPE_PTZ, nullptr)));
    s->transf = gst_element_factory_make("gltransformation", nullptr);
    GstElement* fijar = gst_element_factory_make("glshader", nullptr);
    if (s->transf == nullptr || fijar == nullptr) {
        GST_ERROR_OBJECT(s, "faltan los elementos de OpenGL de GStreamer");
        if (s->transf != nullptr) gst_object_unref(s->transf);
        if (fijar != nullptr) gst_object_unref(fijar);
        s->transf = nullptr;
        return;
    }
    g_object_set(s->transf, "ortho", TRUE, nullptr);
    s->fijar = fijar;
    s->pasante = FALSE;
    gst_bin_add_many(GST_BIN(s), s->transf, fijar, nullptr);
    gst_element_link(s->transf, fijar);
    GstPad* in = gst_element_get_static_pad(s->transf, "sink");
    GstPad* out = gst_element_get_static_pad(fijar, "src");
    gst_pad_add_probe(in, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      alCuadroGl, s, nullptr);
    GstElementClass* ek = GST_ELEMENT_GET_CLASS(s);
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("sink", in, gst_element_class_get_pad_template(ek, "sink")));
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("src", out, gst_element_class_get_pad_template(ek, "src")));
    gst_object_unref(in);
    gst_object_unref(out);
}

static void ba_ptz_gl_finalize(GObject* o) {
    gst_object_unref(BA_PTZ_GL(o)->cuenta);
    G_OBJECT_CLASS(ba_ptz_gl_parent_class)->finalize(o);
}

static void ba_ptz_gl_class_init(BaPtzGlClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = glPoner;
    go->get_property = glLeer;
    go->finalize = ba_ptz_gl_finalize;
    // Las mismas propiedades que baptz (copias: un GParamSpec es de una sola clase).
    auto* kp = static_cast<GObjectClass*>(g_type_class_ref(BA_TYPE_PTZ));
    guint n = 0;
    GParamSpec** v = g_object_class_list_properties(kp, &n);
    guint id = 1;
    for (guint i = 0; i < n; ++i) {
        GParamSpec* ps = v[i];
        if (ps->owner_type != BA_TYPE_PTZ) continue;
        const auto fl = static_cast<GParamFlags>(ps->flags & ~G_PARAM_STATIC_STRINGS);
        GParamSpec* copia = nullptr;
        if (G_IS_PARAM_SPEC_DOUBLE(ps)) {
            auto* d = G_PARAM_SPEC_DOUBLE(ps);
            copia = g_param_spec_double(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps), d->minimum,
                                        d->maximum, d->default_value, fl);
        } else if (G_IS_PARAM_SPEC_STRING(ps)) {
            copia = g_param_spec_string(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps),
                                        G_PARAM_SPEC_STRING(ps)->default_value, fl);
        } else if (G_IS_PARAM_SPEC_BOOLEAN(ps)) {
            copia = g_param_spec_boolean(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps),
                                         G_PARAM_SPEC_BOOLEAN(ps)->default_value, fl);
        }
        if (copia != nullptr) g_object_class_install_property(go, id++, copia);
    }
    g_free(v);
    g_type_class_unref(kp);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "PTZ digital (GPU)", "Filter/Effect/Video",
                                          "Pan, tilt y zoom recortando y ampliando la imagen en OpenGL",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw(memory:GLMemory),format=RGBA");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}

// ---------------------------------------------------------------- baptzd3d12

G_BEGIN_DECLS
#define BA_TYPE_PTZ_D3D12 (ba_ptz_d3d12_get_type())
G_DECLARE_FINAL_TYPE(BaPtzD3d12, ba_ptz_d3d12, BA, PTZ_D3D12, GstBin)
G_END_DECLS

struct _BaPtzD3d12 {
    GstBin parent;
    BaPtz* cuenta;      ///< el estado y las propiedades (no está en la cadena)
    GstElement* tam;    ///< capsfilter: sale del tamaño de la entrada
    gint ancho, alto;   ///< el de la entrada (en el hilo del streaming)
};

G_DEFINE_TYPE(BaPtzD3d12, ba_ptz_d3d12, GST_TYPE_BIN)

namespace {

void d3dPoner(GObject* o, guint, const GValue* v, GParamSpec* ps) {
    g_object_set_property(G_OBJECT(BA_PTZ_D3D12(o)->cuenta), ps->name, v);
}

void d3dLeer(GObject* o, guint, GValue* v, GParamSpec* ps) {
    g_object_get_property(G_OBJECT(BA_PTZ_D3D12(o)->cuenta), ps->name, v);
}

/// La entrada: su tamaño (para que la salida sea igual) y, en cada cuadro, la ventana.
GstPadProbeReturn alCuadroD3d(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* s = static_cast<BaPtzD3d12*>(d);
    BaPtz* p = s->cuenta;
    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) != 0) {
        GstEvent* e = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(e) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(e, &c);
            gint w = 0, h = 0;
            if (c != nullptr && gst_caps_get_size(c) > 0 && gst_structure_get_int(gst_caps_get_structure(c, 0), "width", &w) &&
                gst_structure_get_int(gst_caps_get_structure(c, 0), "height", &h) && (w != s->ancho || h != s->alto)) {
                s->ancho = w;
                s->alto = h;
                GstCaps* sale = gst_caps_copy(c);
                gst_caps_set_simple(sale, "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, nullptr);
                g_object_set(s->tam, "caps", sale, nullptr);
                gst_caps_unref(sale);
            }
        } else if (GST_EVENT_TYPE(e) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(e) == GST_EVENT_SEGMENT) {
            GST_OBJECT_LOCK(p);
            p->ultimo = GST_CLOCK_TIME_NONE;
            GST_OBJECT_UNLOCK(p);
        } else {
            leerEncuadre(p, e);
        }
        return GST_PAD_PROBE_OK;
    }
    const GstClockTime t = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_OBJECT_LOCK(p);
    double dt = 0;
    if (GST_CLOCK_TIME_IS_VALID(t) && GST_CLOCK_TIME_IS_VALID(p->ultimo) && t > p->ultimo)
        dt = std::min(0.1, static_cast<double>(t - p->ultimo) / 1e9);
    p->ultimo = t;
    avanzar(p, dt);
    const double x = p->x, y = p->y, z = p->zoom;
    GstEvent* aviso = avisoSiCambio(p);
    GST_OBJECT_UNLOCK(p);
    if (aviso != nullptr) {
        GstPad* sale = gst_element_get_static_pad(s->tam, "src");
        gst_pad_push_event(sale, aviso);
        gst_object_unref(sale);
    }
    if (s->ancho <= 0 || s->alto <= 0) return GST_PAD_PROBE_OK;
    // La ventana de centro (x, y) y lado 1/zoom, en píxeles; sin zoom, nada que marcar.
    if (z <= 1.0 + 1e-6) return GST_PAD_PROBE_OK;
    const int w = std::max(2, static_cast<int>(std::lround(s->ancho / z)));
    const int h = std::max(2, static_cast<int>(std::lround(s->alto / z)));
    const int izq = std::clamp(static_cast<int>(std::lround(x * s->ancho - w / 2.0)), 0, s->ancho - w);
    const int arr = std::clamp(static_cast<int>(std::lround(y * s->alto - h / 2.0)), 0, s->alto - h);
    GstBuffer* b = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info)); // comparte la memoria
    GST_PAD_PROBE_INFO_DATA(info) = b;
    GstVideoCropMeta* m = gst_buffer_get_video_crop_meta(b);
    if (m == nullptr) m = gst_buffer_add_video_crop_meta(b);
    m->x = static_cast<guint>(izq);
    m->y = static_cast<guint>(arr);
    m->width = static_cast<guint>(w);
    m->height = static_cast<guint>(h);
    return GST_PAD_PROBE_OK;
}

} // namespace

static void ba_ptz_d3d12_init(BaPtzD3d12* s) {
    s->cuenta = BA_PTZ(g_object_ref_sink(g_object_new(BA_TYPE_PTZ, nullptr)));
    s->ancho = s->alto = 0;
    GstElement* conv = gst_element_factory_make("d3d12convert", nullptr);
    s->tam = gst_element_factory_make("capsfilter", nullptr);
    if (conv == nullptr || s->tam == nullptr) {
        GST_ERROR_OBJECT(s, "falta d3d12convert");
        for (GstElement* e : {conv, s->tam})
            if (e != nullptr) gst_object_unref(e);
        s->tam = nullptr;
        return;
    }
    gst_bin_add_many(GST_BIN(s), conv, s->tam, nullptr);
    gst_element_link(conv, s->tam);
    GstPad* in = gst_element_get_static_pad(conv, "sink");
    GstPad* out = gst_element_get_static_pad(s->tam, "src");
    gst_pad_add_probe(in, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      alCuadroD3d, s, nullptr);
    GstElementClass* ek = GST_ELEMENT_GET_CLASS(s);
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("sink", in, gst_element_class_get_pad_template(ek, "sink")));
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("src", out, gst_element_class_get_pad_template(ek, "src")));
    gst_object_unref(in);
    gst_object_unref(out);
}

static void ba_ptz_d3d12_finalize(GObject* o) {
    gst_object_unref(BA_PTZ_D3D12(o)->cuenta);
    G_OBJECT_CLASS(ba_ptz_d3d12_parent_class)->finalize(o);
}

/// Las propiedades de baptz copiadas en otra clase (un GParamSpec es de una sola clase).
static void copiarPropiedadesPtz(GObjectClass* go) {
    auto* kp = static_cast<GObjectClass*>(g_type_class_ref(BA_TYPE_PTZ));
    guint n = 0;
    GParamSpec** v = g_object_class_list_properties(kp, &n);
    guint id = 1;
    for (guint i = 0; i < n; ++i) {
        GParamSpec* ps = v[i];
        if (ps->owner_type != BA_TYPE_PTZ) continue;
        const auto fl = static_cast<GParamFlags>(ps->flags & ~G_PARAM_STATIC_STRINGS);
        GParamSpec* copia = nullptr;
        if (G_IS_PARAM_SPEC_DOUBLE(ps)) {
            auto* d = G_PARAM_SPEC_DOUBLE(ps);
            copia = g_param_spec_double(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps), d->minimum,
                                        d->maximum, d->default_value, fl);
        } else if (G_IS_PARAM_SPEC_STRING(ps)) {
            copia = g_param_spec_string(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps),
                                        G_PARAM_SPEC_STRING(ps)->default_value, fl);
        } else if (G_IS_PARAM_SPEC_BOOLEAN(ps)) {
            copia = g_param_spec_boolean(ps->name, g_param_spec_get_nick(ps), g_param_spec_get_blurb(ps),
                                         G_PARAM_SPEC_BOOLEAN(ps)->default_value, fl);
        }
        if (copia != nullptr) g_object_class_install_property(go, id++, copia);
    }
    g_free(v);
    g_type_class_unref(kp);
}

static void ba_ptz_d3d12_class_init(BaPtzD3d12Class* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = d3dPoner;
    go->get_property = d3dLeer;
    go->finalize = ba_ptz_d3d12_finalize;
    copiarPropiedadesPtz(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "PTZ digital (D3D12)", "Filter/Effect/Video",
                                          "Pan, tilt y zoom recortando y ampliando la imagen en Direct3D 12",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw(memory:D3D12Memory)");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}

extern "C" gboolean ba_registrar_ptz(GstPlugin* p) {
    bool ok = gst_element_register(p, "baptz", GST_RANK_NONE, BA_TYPE_PTZ) &&
              gst_element_register(p, "baptzgl", GST_RANK_NONE, BA_TYPE_PTZ_GL);
#if defined(_WIN32)
    // Solo en Windows (D3D12): el motor lo elige cuando su video va en D3D12.
    ok = ok && gst_element_register(p, "baptzd3d12", GST_RANK_NONE, BA_TYPE_PTZ_D3D12);
#endif
    return ok;
}
