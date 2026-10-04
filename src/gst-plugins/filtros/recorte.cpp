// ============================================================================
// src/gst-plugins/filtros/recorte.cpp — barecorte: recorte (en fracciones) y espejo
//
// La tarjeta «Recorte y espejo» del grafo, como elemento de GStreamer. Los
// bordes van en fracciones del cuadro (0..1), así no dependen del tamaño de la
// cámara. Dos modos:
//   transparente (el de la tarjeta): el cuadro queda del mismo tamaño y lo
//                recortado, transparente (negro en formatos sin alfa). En el
//                lugar; pasante si no recorta ni da vuelta.
//   recortar:    el cuadro sale MÁS CHICO (las caps cambian: `transform_caps`
//                resta los bordes; al cambiar un borde se renegocia).
// El espejo (horizontal/vertical) se aplica a la imagen; los bordes, a la
// salida (como el efecto de la GPU, PSRecorte).
// ============================================================================

#include "comun.hpp"

#include <cstring>
#include <mutex>

G_BEGIN_DECLS
#define BA_TYPE_RECORTE (ba_recorte_get_type())
G_DECLARE_FINAL_TYPE(BaRecorte, ba_recorte, BA, RECORTE, GstVideoFilter)
G_END_DECLS

struct _BaRecorte {
    GstVideoFilter parent;
    std::mutex* mu;
    gdouble izq, arr, der, aba;
    gboolean espejoH, espejoV;
    gint modo; ///< 0 transparente, 1 recortar
};

G_DEFINE_TYPE(BaRecorte, ba_recorte, GST_TYPE_VIDEO_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_recorte_debug);
#define GST_CAT_DEFAULT ba_recorte_debug

enum { PROP_0, PROP_IZQ, PROP_ARR, PROP_DER, PROP_ABA, PROP_ESPEJO_H, PROP_ESPEJO_V, PROP_MODO };

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

#define FORMATOS "{ BGRA, RGBA, ARGB, ABGR, BGRx, RGBx, xRGB, xBGR }"
static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));

/// Los píxeles que se van de cada lado, para un ancho/alto dado.
static void pixeles(BaRecorte* r, int w, int h, int& i, int& a, int& d, int& b) {
    i = static_cast<int>(std::lround(std::clamp(r->izq, 0.0, 1.0) * w));
    d = static_cast<int>(std::lround(std::clamp(r->der, 0.0, 1.0) * w));
    a = static_cast<int>(std::lround(std::clamp(r->arr, 0.0, 1.0) * h));
    b = static_cast<int>(std::lround(std::clamp(r->aba, 0.0, 1.0) * h));
    if (i + d >= w) d = std::max(0, w - i - 1);
    if (a + b >= h) b = std::max(0, h - a - 1);
}

static bool recortaAlgo(BaRecorte* r) { return r->izq > 0 || r->arr > 0 || r->der > 0 || r->aba > 0; }

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* r = BA_RECORTE(bt);
    GstCaps* out = gst_caps_copy(caps);
    std::lock_guard<std::mutex> l(*r->mu);
    if (r->modo == 1 && recortaAlgo(r)) {
        for (guint k = 0; k < gst_caps_get_size(out); ++k) {
            GstStructure* st = gst_caps_get_structure(out, k);
            int w = 0, h = 0;
            if (dir == GST_PAD_SINK && gst_structure_get_int(st, "width", &w) &&
                gst_structure_get_int(st, "height", &h)) {
                int i, a, d, b;
                pixeles(r, w, h, i, a, d, b);
                gst_structure_set(st, "width", G_TYPE_INT, w - i - d, "height", G_TYPE_INT, h - a - b, nullptr);
            } else {
                // De abajo hacia arriba (o con rangos): cualquier tamaño.
                gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1,
                                  G_MAXINT, nullptr);
            }
        }
    }
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = f;
    }
    return out;
}

static gboolean alFormato(GstVideoFilter* vf, GstCaps*, GstVideoInfo* in, GstCaps*, GstVideoInfo* out) {
    auto* r = BA_RECORTE(vf);
    const bool igual = GST_VIDEO_INFO_WIDTH(in) == GST_VIDEO_INFO_WIDTH(out) &&
                       GST_VIDEO_INFO_HEIGHT(in) == GST_VIDEO_INFO_HEIGHT(out);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(r), igual);
    return TRUE;
}

static void espejarEnLugar(std::uint8_t* d, int st, int w, int h, bool eh, bool ev) {
    if (eh)
        for (int y = 0; y < h; ++y) {
            auto* p = reinterpret_cast<std::uint32_t*>(d + static_cast<size_t>(y) * st);
            std::reverse(p, p + w);
        }
    if (ev)
        for (int y = 0; y < h / 2; ++y) {
            std::uint8_t* a = d + static_cast<size_t>(y) * st;
            std::uint8_t* b = d + static_cast<size_t>(h - 1 - y) * st;
            std::swap_ranges(a, a + static_cast<size_t>(w) * 4, b);
        }
}

static GstFlowReturn enLugar(GstVideoFilter* vf, GstVideoFrame* fr) {
    auto* r = BA_RECORTE(vf);
    std::lock_guard<std::mutex> l(*r->mu);
    ba::filtros::Orden o{};
    if (!ba::filtros::ordenDe(GST_VIDEO_FRAME_FORMAT(fr), o)) return GST_FLOW_NOT_NEGOTIATED;
    const int w = GST_VIDEO_FRAME_WIDTH(fr), h = GST_VIDEO_FRAME_HEIGHT(fr);
    auto* d = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
    const int st = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
    espejarEnLugar(d, st, w, h, r->espejoH, r->espejoV);
    if (!recortaAlgo(r)) return GST_FLOW_OK;
    int i, a, de, b;
    pixeles(r, w, h, i, a, de, b);
    // Fuera de la caja: transparente (negro, en un formato sin alfa).
    const std::uint8_t vacio[4] = {0, 0, 0, 0};
    const auto limpiar = [&](std::uint8_t* p, int n) {
        for (int k = 0; k < n; ++k, p += 4)
            std::memcpy(p, vacio, 4);
    };
    for (int y = 0; y < h; ++y) {
        std::uint8_t* p = d + static_cast<size_t>(y) * st;
        if (y < a || y >= h - b) {
            limpiar(p, w);
            continue;
        }
        limpiar(p, i);
        limpiar(p + static_cast<size_t>(w - de) * 4, de);
    }
    return GST_FLOW_OK;
}

static GstFlowReturn recortar(GstVideoFilter* vf, GstVideoFrame* in, GstVideoFrame* out) {
    auto* r = BA_RECORTE(vf);
    std::lock_guard<std::mutex> l(*r->mu);
    const int w = GST_VIDEO_FRAME_WIDTH(in), h = GST_VIDEO_FRAME_HEIGHT(in);
    const int ow = GST_VIDEO_FRAME_WIDTH(out), oh = GST_VIDEO_FRAME_HEIGHT(out);
    int i, a, d, b;
    pixeles(r, w, h, i, a, d, b);
    const auto* s = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, 0));
    auto* o = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, 0));
    const int ss = GST_VIDEO_FRAME_PLANE_STRIDE(in, 0), so = GST_VIDEO_FRAME_PLANE_STRIDE(out, 0);
    for (int y = 0; y < oh; ++y) {
        // La caja se toma de la imagen ya espejada: la fila/columna espejada de la entrada.
        const int yo = a + y;
        const int ys = r->espejoV ? h - 1 - yo : yo;
        if (ys < 0 || ys >= h) continue;
        const auto* fs = reinterpret_cast<const std::uint32_t*>(s + static_cast<size_t>(ys) * ss);
        auto* fo = reinterpret_cast<std::uint32_t*>(o + static_cast<size_t>(y) * so);
        if (!r->espejoH) {
            std::memcpy(fo, fs + i, static_cast<size_t>(std::min(ow, w - i)) * 4);
        } else {
            for (int x = 0; x < ow; ++x) {
                const int xs = w - 1 - (i + x);
                fo[x] = xs >= 0 && xs < w ? fs[xs] : 0;
            }
        }
    }
    return GST_FLOW_OK;
}

static void antes(GstBaseTransform* bt, GstBuffer* b) { ba::filtros::sincronizarValores(bt, b); }

static void actualizarPasante(BaRecorte* r) {
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(r), !recortaAlgo(r) && !r->espejoH && !r->espejoV);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE(o);
    bool renegociar = false;
    {
        std::lock_guard<std::mutex> l(*r->mu);
        switch (id) {
            case PROP_IZQ: r->izq = g_value_get_double(v); break;
            case PROP_ARR: r->arr = g_value_get_double(v); break;
            case PROP_DER: r->der = g_value_get_double(v); break;
            case PROP_ABA: r->aba = g_value_get_double(v); break;
            case PROP_ESPEJO_H: r->espejoH = g_value_get_boolean(v); break;
            case PROP_ESPEJO_V: r->espejoV = g_value_get_boolean(v); break;
            case PROP_MODO: r->modo = g_value_get_enum(v); break;
            default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
        }
        renegociar = r->modo == 1 || id == PROP_MODO;
    }
    actualizarPasante(r);
    // En modo «recortar» el tamaño de salida cambia: nuevas caps.
    if (renegociar) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(r));
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE(o);
    std::lock_guard<std::mutex> l(*r->mu);
    switch (id) {
        case PROP_IZQ: g_value_set_double(v, r->izq); break;
        case PROP_ARR: g_value_set_double(v, r->arr); break;
        case PROP_DER: g_value_set_double(v, r->der); break;
        case PROP_ABA: g_value_set_double(v, r->aba); break;
        case PROP_ESPEJO_H: g_value_set_boolean(v, r->espejoH); break;
        case PROP_ESPEJO_V: g_value_set_boolean(v, r->espejoV); break;
        case PROP_MODO: g_value_set_enum(v, r->modo); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    delete BA_RECORTE(o)->mu;
    G_OBJECT_CLASS(ba_recorte_parent_class)->finalize(o);
}

static void ba_recorte_class_init(BaRecorteClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_recorte_debug, "barecorte", 0, "Recorte y espejo");
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    using ba::filtros::kVivo;
    const char* lados[] = {"izquierda", "arriba", "derecha", "abajo"};
    for (int i = 0; i < 4; ++i)
        g_object_class_install_property(go, PROP_IZQ + i,
                                        g_param_spec_double(lados[i], lados[i], "Fracción que se va (0..1)",
                                                            0, 1, 0, kVivo));
    g_object_class_install_property(
        go, PROP_ESPEJO_H, g_param_spec_boolean("espejo-h", "Espejo horizontal", "Izquierda <-> derecha", FALSE, kVivo));
    g_object_class_install_property(
        go, PROP_ESPEJO_V, g_param_spec_boolean("espejo-v", "Espejo vertical", "Arriba <-> abajo", FALSE, kVivo));
    g_object_class_install_property(
        go, PROP_MODO,
        g_param_spec_enum("modo", "Modo", "Transparente (mismo tamaño) o recortar (más chico)", tipoModo(), 0,
                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                   GST_PARAM_MUTABLE_PLAYING)));
    gst_element_class_set_static_metadata(ge, "Recorte y espejo", "Filter/Effect/Video",
                                          "Recorta los bordes (en fracciones) y da vuelta la imagen",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    bt->before_transform = GST_DEBUG_FUNCPTR(antes);
    bt->transform_caps = GST_DEBUG_FUNCPTR(transformarCaps);
    auto* vf = GST_VIDEO_FILTER_CLASS(k);
    vf->set_info = GST_DEBUG_FUNCPTR(alFormato);
    vf->transform_frame_ip = GST_DEBUG_FUNCPTR(enLugar);
    vf->transform_frame = GST_DEBUG_FUNCPTR(recortar);
}

static void ba_recorte_init(BaRecorte* r) {
    r->mu = new std::mutex;
    r->izq = r->arr = r->der = r->aba = 0;
    r->espejoH = r->espejoV = FALSE;
    r->modo = 0;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(r), TRUE);
    actualizarPasante(r);
}

extern "C" gboolean ba_registrar_recorte(GstPlugin* p) {
    return gst_element_register(p, "barecorte", GST_RANK_NONE, BA_TYPE_RECORTE);
}
