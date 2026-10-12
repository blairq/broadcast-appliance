// ============================================================================
// src/gst-plugins/filtros/recorte.cpp — barecorte: recorte (en fracciones) y espejo
//
// La tarjeta «Recorte y espejo» del grafo, como elemento de GStreamer (con lo
// de videoflip de gst-plugins-good). Los bordes van en fracciones del cuadro
// (0..1), así no dependen del tamaño de la cámara. Dos modos:
//   transparente (el de la tarjeta): el cuadro queda del mismo tamaño y lo
//                recortado, transparente. Mientras recorta, negocia SOLO
//                formatos con alfa; sin recorte (solo espejo) acepta cualquiera.
//   recortar:    el cuadro sale MÁS CHICO (las caps cambian: `transform_caps`
//                resta los bordes; al cambiar un borde se renegocia).
// El espejo (horizontal/vertical) se aplica a la imagen; los bordes, a la
// salida (como el efecto de la GPU, PSRecorte).
//
// Como videoflip:
//   - interfaz GstVideoDirection (propiedad `video-direction`: identity,
//     horiz, vert, 180) y el tag `image-orientation` de la cámara (si
//     `orientacion-auto`): una cámara montada al revés se endereza sola.
//   - eventos de navegación (el puntero) traducidos a la imagen de entrada.
//   - propiedades controlables (GstController), QoS, en el lugar si el
//     tamaño no cambia, pasante si no hace nada.
// Formatos: RGB de 4 bytes (con o sin alfa), AYUV, NV12, NV21, I420, YV12,
// Y444, GRAY8 (sin pasar a RGB: una cámara normalizada en NV12 no se convierte
// para darla vuelta). En los submuestreados, los bordes van a píxel par.
//
// Concurrencia: las propiedades (hilo de la aplicación) y el cuadro (hilo de
// streaming) se cruzan bajo GST_OBJECT_LOCK; el cuadro toma una copia de los
// valores y trabaja sin el lock.
// ============================================================================

#include "comun.hpp"

#include <gst/video/navigation.h>
#include <gst/video/videodirection.h>

#include <cstring>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_RECORTE (ba_recorte_get_type())
G_DECLARE_FINAL_TYPE(BaRecorte, ba_recorte, BA, RECORTE, GstVideoFilter)
G_END_DECLS

struct Ajustes {
    gdouble izq = 0, arr = 0, der = 0, aba = 0;
    gboolean espejoH = FALSE, espejoV = FALSE;
    gint modo = 0; ///< 0 transparente, 1 recortar
};

struct _BaRecorte {
    GstVideoFilter parent;
    Ajustes a;                     ///< bajo GST_OBJECT_LOCK
    GstVideoOrientationMethod tag; ///< lo que dijo la cámara (image-orientation)
    gboolean orientacionAuto;
};

static void interfazDireccion(GstVideoDirectionInterface*) {}

G_DEFINE_TYPE_WITH_CODE(BaRecorte, ba_recorte, GST_TYPE_VIDEO_FILTER,
                        G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_DIRECTION, interfazDireccion))

GST_DEBUG_CATEGORY_STATIC(ba_recorte_debug);
#define GST_CAT_DEFAULT ba_recorte_debug

enum {
    PROP_0,
    PROP_IZQ,
    PROP_ARR,
    PROP_DER,
    PROP_ABA,
    PROP_ESPEJO_H,
    PROP_ESPEJO_V,
    PROP_MODO,
    PROP_ORIENTACION_AUTO,
    PROP_VIDEO_DIRECTION
};

static GType tipoModo() {
    static GType t = 0;
    if (g_once_init_enter(&t)) {
        static const GEnumValue v[] = {{0, "Mismo tamaño, lo recortado queda transparente", "transparente"},
                                       {1, "El cuadro sale más chico", "recortar"},
                                       {0, nullptr, nullptr}};
        g_once_init_leave(&t, g_enum_register_static("BaRecorteModo", v));
    }
    return t;
}

#define CON_ALFA "{ BGRA, RGBA, ARGB, ABGR, AYUV }"
#define FORMATOS "{ BGRA, RGBA, ARGB, ABGR, AYUV, BGRx, RGBx, xRGB, xBGR, NV12, NV21, I420, YV12, Y444, GRAY8 }"
static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));

static bool recortaAlgo(const Ajustes& a) { return a.izq > 0 || a.arr > 0 || a.der > 0 || a.aba > 0; }

static Ajustes copia(BaRecorte* r) {
    GST_OBJECT_LOCK(r);
    Ajustes a = r->a;
    // La orientación de la cámara se suma al espejo pedido.
    if (r->orientacionAuto) {
        if (r->tag == GST_VIDEO_ORIENTATION_HORIZ || r->tag == GST_VIDEO_ORIENTATION_180) a.espejoH = !a.espejoH;
        if (r->tag == GST_VIDEO_ORIENTATION_VERT || r->tag == GST_VIDEO_ORIENTATION_180) a.espejoV = !a.espejoV;
    }
    GST_OBJECT_UNLOCK(r);
    return a;
}

/// Los píxeles que se van de cada lado, para un ancho/alto dado (a par si hace falta).
static void pixeles(const Ajustes& r, int w, int h, int par, int& i, int& a, int& d, int& b) {
    const auto px = [par](double f, int n) {
        int v = static_cast<int>(std::lround(std::clamp(f, 0.0, 1.0) * n));
        return v - v % par;
    };
    i = px(r.izq, w);
    d = px(r.der, w);
    a = px(r.arr, h);
    b = px(r.aba, h);
    if (i + d >= w) d = std::max(0, w - i - par);
    if (a + b >= h) b = std::max(0, h - a - par);
}

static void actualizarPasante(BaRecorte* r) {
    const Ajustes a = copia(r);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(r), !recortaAlgo(a) && !a.espejoH && !a.espejoV);
}

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* r = BA_RECORTE(bt);
    const Ajustes a = copia(r);
    GstCaps* out = gst_caps_copy(caps);
    if (a.modo == 1 && recortaAlgo(a)) {
        for (guint k = 0; k < gst_caps_get_size(out); ++k) {
            GstStructure* st = gst_caps_get_structure(out, k);
            int w = 0, h = 0;
            if (dir == GST_PAD_SINK && gst_structure_get_int(st, "width", &w) &&
                gst_structure_get_int(st, "height", &h)) {
                int i, ar, d, b;
                pixeles(a, w, h, 2, i, ar, d, b);
                gst_structure_set(st, "width", G_TYPE_INT, w - i - d, "height", G_TYPE_INT, h - ar - b, nullptr);
            } else {
                gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1,
                                  G_MAXINT, nullptr);
            }
        }
    } else if (a.modo == 0 && recortaAlgo(a)) {
        // Transparente: solo con alfa (si no, lo recortado quedaría negro).
        GstCaps* alfa = gst_caps_from_string(GST_VIDEO_CAPS_MAKE(CON_ALFA));
        for (guint k = 0; k < gst_caps_get_size(alfa); ++k)
            gst_structure_remove_fields(gst_caps_get_structure(alfa, k), "width", "height", "framerate", nullptr);
        GstCaps* x = gst_caps_intersect_full(out, alfa, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(alfa);
        gst_caps_unref(out);
        out = x;
    }
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = f;
    }
    GST_DEBUG_OBJECT(r, "%s: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT, dir == GST_PAD_SINK ? "abajo" : "arriba",
                     caps, out);
    return out;
}

static gboolean alFormato(GstVideoFilter* vf, GstCaps*, GstVideoInfo* in, GstCaps*, GstVideoInfo* out) {
    const bool igual = GST_VIDEO_INFO_WIDTH(in) == GST_VIDEO_INFO_WIDTH(out) &&
                       GST_VIDEO_INFO_HEIGHT(in) == GST_VIDEO_INFO_HEIGHT(out) &&
                       GST_VIDEO_INFO_FORMAT(in) == GST_VIDEO_INFO_FORMAT(out);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(vf), igual);
    actualizarPasante(BA_RECORTE(vf));
    return TRUE;
}

/// Un plano: bytes por «unidad» (lo que se mueve junto al dar vuelta) y su submuestreo.
struct Plano {
    guint8* datos;
    int stride, ancho, alto, unidad, subX, subY;
};

static Plano planoDe(GstVideoFrame* f, guint p) {
    const GstVideoFormatInfo* fi = f->info.finfo;
    guint c = 0; // el primer componente que vive en este plano
    for (guint k = 0; k < GST_VIDEO_FORMAT_INFO_N_COMPONENTS(fi); ++k)
        if (GST_VIDEO_FORMAT_INFO_PLANE(fi, k) == p) {
            c = k;
            break;
        }
    Plano r;
    r.datos = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(f, p));
    r.stride = GST_VIDEO_FRAME_PLANE_STRIDE(f, p);
    r.ancho = GST_VIDEO_FRAME_COMP_WIDTH(f, c);
    r.alto = GST_VIDEO_FRAME_COMP_HEIGHT(f, c);
    r.unidad = GST_VIDEO_FORMAT_INFO_PSTRIDE(fi, c);
    r.subX = std::max(1, GST_VIDEO_FRAME_WIDTH(f) / std::max(r.ancho, 1));
    r.subY = std::max(1, GST_VIDEO_FRAME_HEIGHT(f) / std::max(r.alto, 1));
    return r;
}

static void espejarPlano(const Plano& p, bool eh, bool ev) {
    if (eh)
        for (int y = 0; y < p.alto; ++y) {
            guint8* fila = p.datos + static_cast<size_t>(y) * p.stride;
            for (int a = 0, b = p.ancho - 1; a < b; ++a, --b)
                std::swap_ranges(fila + a * p.unidad, fila + (a + 1) * p.unidad, fila + b * p.unidad);
        }
    if (ev)
        for (int y = 0; y < p.alto / 2; ++y)
            std::swap_ranges(p.datos + static_cast<size_t>(y) * p.stride,
                             p.datos + static_cast<size_t>(y) * p.stride + static_cast<size_t>(p.ancho) * p.unidad,
                             p.datos + static_cast<size_t>(p.alto - 1 - y) * p.stride);
}

/// El valor de «vacío» de un plano: transparente con alfa; negro sin él.
static void vacio(GstVideoFormat f, guint plano, guint8 v[4]) {
    std::memset(v, 0, 4);
    switch (f) {
        case GST_VIDEO_FORMAT_AYUV:
            v[1] = 16;
            v[2] = v[3] = 128;
            break;
        case GST_VIDEO_FORMAT_NV12:
        case GST_VIDEO_FORMAT_NV21:
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
        case GST_VIDEO_FORMAT_Y444:
        case GST_VIDEO_FORMAT_GRAY8: std::memset(v, plano == 0 ? 16 : 128, 4); break;
        default: break; // RGB: 0 (transparente o negro)
    }
}

static GstFlowReturn enLugar(GstVideoFilter* vf, GstVideoFrame* fr) {
    auto* r = BA_RECORTE(vf);
    const Ajustes a = copia(r);
    const GstVideoFormat f = GST_VIDEO_FRAME_FORMAT(fr);
    const int w = GST_VIDEO_FRAME_WIDTH(fr), h = GST_VIDEO_FRAME_HEIGHT(fr);
    int i = 0, ar = 0, de = 0, b = 0;
    if (recortaAlgo(a)) pixeles(a, w, h, 2, i, ar, de, b);
    for (guint p = 0; p < GST_VIDEO_FRAME_N_PLANES(fr); ++p) {
        const Plano pl = planoDe(fr, p);
        espejarPlano(pl, a.espejoH, a.espejoV);
        if (!recortaAlgo(a)) continue;
        guint8 v[4];
        vacio(f, p, v);
        const auto limpiar = [&](guint8* q, int n) {
            for (int k = 0; k < n; ++k, q += pl.unidad)
                std::memcpy(q, v, static_cast<size_t>(std::min(pl.unidad, 4)));
        };
        const int pi = i / pl.subX, pd = de / pl.subX, pa = ar / pl.subY, pb = b / pl.subY;
        for (int y = 0; y < pl.alto; ++y) {
            guint8* q = pl.datos + static_cast<size_t>(y) * pl.stride;
            if (y < pa || y >= pl.alto - pb) {
                limpiar(q, pl.ancho);
                continue;
            }
            limpiar(q, pi);
            limpiar(q + static_cast<size_t>(pl.ancho - pd) * pl.unidad, pd);
        }
    }
    return GST_FLOW_OK;
}

static GstFlowReturn recortar(GstVideoFilter* vf, GstVideoFrame* in, GstVideoFrame* out) {
    auto* r = BA_RECORTE(vf);
    const Ajustes a = copia(r);
    const int w = GST_VIDEO_FRAME_WIDTH(in), h = GST_VIDEO_FRAME_HEIGHT(in);
    int i, ar, d, b;
    pixeles(a, w, h, 2, i, ar, d, b);
    for (guint p = 0; p < GST_VIDEO_FRAME_N_PLANES(in); ++p) {
        const Plano s = planoDe(in, p), o = planoDe(out, p);
        // La caja se toma de la imagen ya espejada.
        const int ox = i / s.subX, oy = ar / s.subY;
        for (int y = 0; y < o.alto; ++y) {
            const int yo = oy + y;
            const int ys = a.espejoV ? s.alto - 1 - yo : yo;
            if (ys < 0 || ys >= s.alto) continue;
            const guint8* fs = s.datos + static_cast<size_t>(ys) * s.stride;
            guint8* fo = o.datos + static_cast<size_t>(y) * o.stride;
            if (!a.espejoH) {
                std::memcpy(fo, fs + static_cast<size_t>(ox) * s.unidad,
                            static_cast<size_t>(std::max(0, std::min(o.ancho, s.ancho - ox))) * s.unidad);
            } else {
                for (int x = 0; x < o.ancho; ++x) {
                    const int xs = s.ancho - 1 - (ox + x);
                    if (xs >= 0 && xs < s.ancho)
                        std::memcpy(fo + static_cast<size_t>(x) * o.unidad, fs + static_cast<size_t>(xs) * s.unidad,
                                    static_cast<size_t>(s.unidad));
                }
            }
        }
    }
    return GST_FLOW_OK;
}

static void antes(GstBaseTransform* bt, GstBuffer* b) { ba::filtros::sincronizarValores(bt, b); }

/// El puntero (navegación) en la imagen de salida -> en la de entrada.
static gboolean eventoSalida(GstBaseTransform* bt, GstEvent* ev) {
    auto* r = BA_RECORTE(bt);
    gdouble x = 0, y = 0;
    if (GST_EVENT_TYPE(ev) == GST_EVENT_NAVIGATION && gst_navigation_event_get_coordinates(ev, &x, &y)) {
        GstVideoInfo* in = &GST_VIDEO_FILTER(r)->in_info;
        const Ajustes a = copia(r);
        const int w = GST_VIDEO_INFO_WIDTH(in), h = GST_VIDEO_INFO_HEIGHT(in);
        if (w > 0 && h > 0) {
            if (a.modo == 1 && recortaAlgo(a)) {
                int i, ar, d, b;
                pixeles(a, w, h, 2, i, ar, d, b);
                x += i;
                y += ar;
            }
            if (a.espejoH) x = w - 1 - x;
            if (a.espejoV) y = h - 1 - y;
            ev = gst_event_make_writable(ev);
            gst_navigation_event_set_coordinates(ev, x, y);
        }
    }
    return GST_BASE_TRANSFORM_CLASS(ba_recorte_parent_class)->src_event(bt, ev);
}

/// El tag `image-orientation` de la cámara (espejos y 180°; las de 90° cambian
/// el tamaño: eso es videoflip).
static gboolean eventoEntrada(GstBaseTransform* bt, GstEvent* ev) {
    auto* r = BA_RECORTE(bt);
    if (GST_EVENT_TYPE(ev) == GST_EVENT_TAG) {
        GstTagList* tags = nullptr;
        gst_event_parse_tag(ev, &tags);
        GstVideoOrientationMethod m;
        if (tags != nullptr && gst_video_orientation_from_tag(tags, &m)) {
            GST_OBJECT_LOCK(r);
            r->tag = m == GST_VIDEO_ORIENTATION_HORIZ || m == GST_VIDEO_ORIENTATION_VERT ||
                             m == GST_VIDEO_ORIENTATION_180
                         ? m
                         : GST_VIDEO_ORIENTATION_IDENTITY;
            GST_OBJECT_UNLOCK(r);
            GST_INFO_OBJECT(r, "orientación de la cámara: %d", m);
            actualizarPasante(r);
        }
    }
    return GST_BASE_TRANSFORM_CLASS(ba_recorte_parent_class)->sink_event(bt, ev);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE(o);
    GST_OBJECT_LOCK(r);
    const bool antesRecortaba = recortaAlgo(r->a);
    const gint antesModo = r->a.modo;
    switch (id) {
        case PROP_IZQ: r->a.izq = g_value_get_double(v); break;
        case PROP_ARR: r->a.arr = g_value_get_double(v); break;
        case PROP_DER: r->a.der = g_value_get_double(v); break;
        case PROP_ABA: r->a.aba = g_value_get_double(v); break;
        case PROP_ESPEJO_H: r->a.espejoH = g_value_get_boolean(v); break;
        case PROP_ESPEJO_V: r->a.espejoV = g_value_get_boolean(v); break;
        case PROP_MODO: r->a.modo = g_value_get_enum(v); break;
        case PROP_ORIENTACION_AUTO: r->orientacionAuto = g_value_get_boolean(v); break;
        case PROP_VIDEO_DIRECTION: {
            const auto m = static_cast<GstVideoOrientationMethod>(g_value_get_enum(v));
            r->a.espejoH = m == GST_VIDEO_ORIENTATION_HORIZ || m == GST_VIDEO_ORIENTATION_180;
            r->a.espejoV = m == GST_VIDEO_ORIENTATION_VERT || m == GST_VIDEO_ORIENTATION_180;
            if (m != GST_VIDEO_ORIENTATION_IDENTITY && !r->a.espejoH && !r->a.espejoV)
                GST_WARNING_OBJECT(r, "barecorte solo da vuelta (espejos y 180°); para 90° está videoflip");
            break;
        }
        default:
            GST_OBJECT_UNLOCK(r);
            G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
            return;
    }
    // Cambia el tamaño (recortar) o si hace falta alfa (transparente): nuevas caps.
    const bool renegociar = r->a.modo == 1 ? (id <= PROP_ABA || id == PROP_MODO)
                                           : (antesRecortaba != recortaAlgo(r->a) || antesModo != r->a.modo);
    GST_OBJECT_UNLOCK(r);
    actualizarPasante(r);
    if (renegociar) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(r));
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE(o);
    GST_OBJECT_LOCK(r);
    switch (id) {
        case PROP_IZQ: g_value_set_double(v, r->a.izq); break;
        case PROP_ARR: g_value_set_double(v, r->a.arr); break;
        case PROP_DER: g_value_set_double(v, r->a.der); break;
        case PROP_ABA: g_value_set_double(v, r->a.aba); break;
        case PROP_ESPEJO_H: g_value_set_boolean(v, r->a.espejoH); break;
        case PROP_ESPEJO_V: g_value_set_boolean(v, r->a.espejoV); break;
        case PROP_MODO: g_value_set_enum(v, r->a.modo); break;
        case PROP_ORIENTACION_AUTO: g_value_set_boolean(v, r->orientacionAuto); break;
        case PROP_VIDEO_DIRECTION:
            g_value_set_enum(v, r->a.espejoH && r->a.espejoV ? GST_VIDEO_ORIENTATION_180
                                : r->a.espejoH              ? GST_VIDEO_ORIENTATION_HORIZ
                                : r->a.espejoV              ? GST_VIDEO_ORIENTATION_VERT
                                                            : GST_VIDEO_ORIENTATION_IDENTITY);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
    GST_OBJECT_UNLOCK(r);
}

static void ba_recorte_class_init(BaRecorteClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    GST_DEBUG_CATEGORY_INIT(ba_recorte_debug, "barecorte", 0, "Recorte y espejo");
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    using ba::filtros::kVivo;
    const char* lados[] = {"izquierda", "arriba", "derecha", "abajo"};
    for (int i = 0; i < 4; ++i)
        g_object_class_install_property(go, PROP_IZQ + i,
                                        g_param_spec_double(lados[i], lados[i], "Fracción que se va (0..1)", 0, 1,
                                                            0, kVivo));
    g_object_class_install_property(
        go, PROP_ESPEJO_H,
        g_param_spec_boolean("espejo-h", "Espejo horizontal", "Izquierda <-> derecha", FALSE, kVivo));
    g_object_class_install_property(
        go, PROP_ESPEJO_V, g_param_spec_boolean("espejo-v", "Espejo vertical", "Arriba <-> abajo", FALSE, kVivo));
    g_object_class_install_property(
        go, PROP_MODO,
        g_param_spec_enum("modo", "Modo", "Transparente (mismo tamaño) o recortar (más chico)", tipoModo(), 0,
                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                   GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        go, PROP_ORIENTACION_AUTO,
        g_param_spec_boolean("orientacion-auto", "Orientación automática",
                             "Seguir el tag image-orientation de la cámara (montada al revés)", TRUE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_override_property(go, PROP_VIDEO_DIRECTION, "video-direction");
    gst_element_class_set_static_metadata(ge, "Recorte y espejo", "Filter/Effect/Video",
                                          "Recorta los bordes (en fracciones) y da vuelta la imagen",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    bt->before_transform = GST_DEBUG_FUNCPTR(antes);
    bt->transform_caps = GST_DEBUG_FUNCPTR(transformarCaps);
    bt->src_event = GST_DEBUG_FUNCPTR(eventoSalida);
    bt->sink_event = GST_DEBUG_FUNCPTR(eventoEntrada);
    auto* vf = GST_VIDEO_FILTER_CLASS(k);
    vf->set_info = GST_DEBUG_FUNCPTR(alFormato);
    vf->transform_frame_ip = GST_DEBUG_FUNCPTR(enLugar);
    vf->transform_frame = GST_DEBUG_FUNCPTR(recortar);
    gst_type_mark_as_plugin_api(tipoModo(), static_cast<GstPluginAPIFlags>(0));
}

static void ba_recorte_init(BaRecorte* r) {
    r->a = Ajustes{};
    r->tag = GST_VIDEO_ORIENTATION_IDENTITY;
    r->orientacionAuto = TRUE;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(r), TRUE);
    actualizarPasante(r);
}

extern "C" gboolean ba_registrar_recorte(GstPlugin* p) {
    return gst_element_register(p, "barecorte", GST_RANK_NONE, BA_TYPE_RECORTE);
}
