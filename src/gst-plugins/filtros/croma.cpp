// ============================================================================
// src/gst-plugins/filtros/croma.cpp — bacroma: saca un fondo de color (croma)
//
// La tarjeta «Croma» del grafo, como elemento de GStreamer. Lo mismo que el
// efecto de la GPU (video-grafo.effect, PSCroma): la distancia en Cb/Cr
// (BT.709) al color de fondo, promediada con los cuatro vecinos (el borde no
// queda dentado por el ruido), da la transparencia; cerca del fondo, además,
// se le saca el color (el derrame verde en el borde).
//
// Formatos con alfa de 4 bytes (BGRA, RGBA, ARGB, ABGR): el que arma la
// cadena pone un videoconvert delante (y pide un formato con alfa al final).
// En el lugar; alfa recto (no premultiplicado), como lo recibe libobs.
// ============================================================================

#include "comun.hpp"

#include <mutex>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_CROMA (ba_croma_get_type())
G_DECLARE_FINAL_TYPE(BaCroma, ba_croma, BA, CROMA, GstVideoFilter)
G_END_DECLS

struct _BaCroma {
    GstVideoFilter parent;
    std::mutex* mu;
    guint clave; ///< 0xRRGGBB
    gdouble similitud, suavidad, derrame;
    gboolean activo;
    std::vector<float>* dist;
};

G_DEFINE_TYPE(BaCroma, ba_croma, GST_TYPE_VIDEO_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_croma_debug);
#define GST_CAT_DEFAULT ba_croma_debug

enum { PROP_0, PROP_CLAVE, PROP_SIMILITUD, PROP_SUAVIDAD, PROP_DERRAME, PROP_ACTIVO };

#define FORMATOS "{ BGRA, RGBA, ARGB, ABGR }"
static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));

static inline void cbcr(float r, float g, float b, float& cb, float& cr) {
    cb = -0.1146f * r - 0.3854f * g + 0.5f * b + 0.5f;
    cr = 0.5f * r - 0.4542f * g - 0.0458f * b + 0.5f;
}

static GstFlowReturn transformar(GstVideoFilter* vf, GstVideoFrame* fr) {
    auto* c = BA_CROMA(vf);
    // Los ajustes, copiados bajo el lock; el cuadro va sin él.
    guint clave;
    gdouble similitud, suavidad, derrame;
    {
        std::lock_guard<std::mutex> l(*c->mu);
        clave = c->clave;
        similitud = c->similitud;
        suavidad = c->suavidad;
        derrame = c->derrame;
    }
    ba::filtros::Orden o{};
    if (!ba::filtros::ordenDe(GST_VIDEO_FRAME_FORMAT(fr), o) || o.a < 0) return GST_FLOW_NOT_NEGOTIATED;
    const int w = GST_VIDEO_FRAME_WIDTH(fr), h = GST_VIDEO_FRAME_HEIGHT(fr);
    auto* d = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
    const int st = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
    float kcb = 0, kcr = 0;
    cbcr(((clave >> 16) & 0xFF) / 255.0f, ((clave >> 8) & 0xFF) / 255.0f, (clave & 0xFF) / 255.0f, kcb,
         kcr);
    std::vector<float>& dist = *c->dist;
    dist.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* p = d + static_cast<size_t>(y) * st;
        float* q = dist.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x, p += 4) {
            float cb = 0, cr = 0;
            cbcr(p[o.r] / 255.0f, p[o.g] / 255.0f, p[o.b] / 255.0f, cb, cr);
            q[x] = std::hypot(cb - kcb, cr - kcr);
        }
    }
    const float sim = static_cast<float>(similitud) * 0.5f;
    const float suav = std::max(static_cast<float>(suavidad) * 0.5f, 0.001f);
    const float der = std::max(static_cast<float>(derrame) * 0.5f, 0.001f);
    for (int y = 0; y < h; ++y) {
        std::uint8_t* p = d + static_cast<size_t>(y) * st;
        const float* q = dist.data() + static_cast<size_t>(y) * w;
        const float* arriba = dist.data() + static_cast<size_t>(std::max(y - 1, 0)) * w;
        const float* abajo = dist.data() + static_cast<size_t>(std::min(y + 1, h - 1)) * w;
        for (int x = 0; x < w; ++x, p += 4) {
            const float m = (q[x] * 2 + q[std::max(x - 1, 0)] + q[std::min(x + 1, w - 1)] + arriba[x] + abajo[x]) / 6;
            const float base = m - sim;
            const float mascara = std::clamp(base / suav, 0.0f, 1.0f);
            const float quitar = std::clamp(base / der, 0.0f, 1.0f);
            if (quitar < 1.0f) {
                const float r = p[o.r], g = p[o.g], b = p[o.b];
                const float lu = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                p[o.r] = static_cast<std::uint8_t>(lu + (r - lu) * quitar + 0.5f);
                p[o.g] = static_cast<std::uint8_t>(lu + (g - lu) * quitar + 0.5f);
                p[o.b] = static_cast<std::uint8_t>(lu + (b - lu) * quitar + 0.5f);
            }
            p[o.a] = static_cast<std::uint8_t>(p[o.a] * mascara + 0.5f);
        }
    }
    return GST_FLOW_OK;
}

static void antes(GstBaseTransform* bt, GstBuffer* b) { ba::filtros::sincronizarValores(bt, b); }

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = BA_CROMA(o);
    std::lock_guard<std::mutex> l(*c->mu);
    switch (id) {
        case PROP_CLAVE: c->clave = g_value_get_uint(v) & 0xFFFFFF; break;
        case PROP_SIMILITUD: c->similitud = g_value_get_double(v); break;
        case PROP_SUAVIDAD: c->suavidad = g_value_get_double(v); break;
        case PROP_DERRAME: c->derrame = g_value_get_double(v); break;
        case PROP_ACTIVO:
            c->activo = g_value_get_boolean(v);
            gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(c), !c->activo);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = BA_CROMA(o);
    std::lock_guard<std::mutex> l(*c->mu);
    switch (id) {
        case PROP_CLAVE: g_value_set_uint(v, c->clave); break;
        case PROP_SIMILITUD: g_value_set_double(v, c->similitud); break;
        case PROP_SUAVIDAD: g_value_set_double(v, c->suavidad); break;
        case PROP_DERRAME: g_value_set_double(v, c->derrame); break;
        case PROP_ACTIVO: g_value_set_boolean(v, c->activo); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* c = BA_CROMA(o);
    delete c->mu;
    delete c->dist;
    G_OBJECT_CLASS(ba_croma_parent_class)->finalize(o);
}

static void ba_croma_class_init(BaCromaClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_croma_debug, "bacroma", 0, "Croma");
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    using ba::filtros::kVivo;
    g_object_class_install_property(go, PROP_CLAVE,
                                    g_param_spec_uint("clave", "Clave", "El color de fondo, 0xRRGGBB", 0,
                                                      0xFFFFFF, 0x00B140, kVivo));
    g_object_class_install_property(go, PROP_SIMILITUD,
                                    g_param_spec_double("similitud", "Similitud",
                                                        "Cuánto se parece y se va (0..1)", 0, 1, 0.4, kVivo));
    g_object_class_install_property(
        go, PROP_SUAVIDAD, g_param_spec_double("suavidad", "Suavidad", "El borde (0..1)", 0, 1, 0.08, kVivo));
    g_object_class_install_property(go, PROP_DERRAME,
                                    g_param_spec_double("derrame", "Derrame",
                                                        "Cuánto color se le saca al borde (0..1)", 0, 1, 0.1,
                                                        kVivo));
    g_object_class_install_property(
        go, PROP_ACTIVO, g_param_spec_boolean("activo", "Activo", "FALSE: pasa sin tocar", TRUE, kVivo));
    gst_element_class_set_static_metadata(ge, "Croma", "Filter/Effect/Video",
                                          "Saca un fondo de color: queda transparente (como la tarjeta Croma)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    GST_BASE_TRANSFORM_CLASS(k)->before_transform = GST_DEBUG_FUNCPTR(antes);
    GST_VIDEO_FILTER_CLASS(k)->transform_frame_ip = GST_DEBUG_FUNCPTR(transformar);
}

static void ba_croma_init(BaCroma* c) {
    c->mu = new std::mutex;
    c->dist = new std::vector<float>;
    c->clave = 0x00B140;
    c->similitud = 0.4;
    c->suavidad = 0.08;
    c->derrame = 0.1;
    c->activo = TRUE;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(c), TRUE);
}

extern "C" gboolean ba_registrar_croma(GstPlugin* p) {
    return gst_element_register(p, "bacroma", GST_RANK_NONE, BA_TYPE_CROMA);
}
