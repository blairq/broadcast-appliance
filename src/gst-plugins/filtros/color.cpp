// ============================================================================
// src/gst-plugins/filtros/color.cpp — bacolor: brillo, contraste, saturación y gamma
//
// La tarjeta «Color» del grafo, como elemento de GStreamer. Lo mismo que el
// efecto de la GPU (video-grafo.effect, PSColor): gamma, después contraste
// alrededor del gris medio más brillo, y la saturación contra la luma BT.709.
//
// Formatos: NV12, I420, Y444 (luma por tabla, croma escalada: sin pasar a
// RGB, así una cámara normalizada en NV12 no se convierte) y RGB de 4 bytes
// (tabla por canal y saturación por píxel). En el lugar; pasante si todo
// está en su valor neutro.
// ============================================================================

#include "comun.hpp"

#include <array>
#include <mutex>

G_BEGIN_DECLS
#define BA_TYPE_COLOR (ba_color_get_type())
G_DECLARE_FINAL_TYPE(BaColor, ba_color, BA, COLOR, GstVideoFilter)
G_END_DECLS

struct _BaColor {
    GstVideoFilter parent;
    std::mutex* mu;
    gdouble brillo, contraste, saturacion, gamma;
    bool cambio; ///< hay que rehacer las tablas
    std::array<std::uint8_t, 256>* tablaRgb;
    std::array<std::uint8_t, 256>* tablaY;
    bool yCompleto; ///< la tabla de luma es de rango completo (0..255) y no limitado (16..235)
};

G_DEFINE_TYPE(BaColor, ba_color, GST_TYPE_VIDEO_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_color_debug);
#define GST_CAT_DEFAULT ba_color_debug

enum { PROP_0, PROP_BRILLO, PROP_CONTRASTE, PROP_SATURACION, PROP_GAMMA };

#define FORMATOS "{ NV12, I420, Y444, BGRA, RGBA, ARGB, ABGR, BGRx, RGBx, xRGB, xBGR }"
static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));

static bool neutro(BaColor* c) {
    return std::abs(c->brillo) < 1e-4 && std::abs(c->contraste - 1) < 1e-4 && std::abs(c->saturacion - 1) < 1e-4 &&
           std::abs(c->gamma - 1) < 1e-4;
}

static void rehacer(BaColor* c) {
    const float g = static_cast<float>(c->gamma), k = static_cast<float>(c->contraste),
                b = static_cast<float>(c->brillo);
    for (int i = 0; i < 256; ++i) {
        const float x = std::pow(i / 255.0f, g);
        (*c->tablaRgb)[i] = ba::filtros::a8((x - 0.5f) * k + 0.5f + b);
        // La luma de video: lo mismo sobre su escala (la que digan las caps:
        // rango limitado 16..235 o completo 0..255, p. ej. una cámara JPEG).
        const float piso = c->yCompleto ? 0.0f : 16.0f, escala = c->yCompleto ? 255.0f : 219.0f;
        const float y = std::clamp((i - piso) / escala, 0.0f, 1.0f);
        const float yy = std::clamp((std::pow(y, g) - 0.5f) * k + 0.5f + b, 0.0f, 1.0f);
        (*c->tablaY)[i] = static_cast<std::uint8_t>(piso + yy * escala + 0.5f);
    }
    c->cambio = false;
}

static void actualizarPasante(BaColor* c) {
    bool n;
    {
        std::lock_guard<std::mutex> l(*c->mu);
        n = neutro(c);
    }
    ba::filtros::pasante(GST_BASE_TRANSFORM(c), n);
}

static GstFlowReturn transformar(GstVideoFilter* vf, GstVideoFrame* fr) {
    auto* c = BA_COLOR(vf);
    // Bajo el lock, solo copiar (y rehacer las tablas si cambió algo: 256
    // valores). Las tablas las toca solo este hilo; el cuadro va sin el lock.
    gdouble saturacion, contraste;
    const bool completo = GST_VIDEO_INFO_COLORIMETRY(&fr->info).range == GST_VIDEO_COLOR_RANGE_0_255;
    {
        std::lock_guard<std::mutex> l(*c->mu);
        if (completo != c->yCompleto) {
            c->yCompleto = completo;
            c->cambio = true;
        }
        if (c->cambio) rehacer(c);
        saturacion = c->saturacion;
        contraste = c->contraste;
    }
    const GstVideoFormat f = GST_VIDEO_FRAME_FORMAT(fr);
    const int w = GST_VIDEO_FRAME_WIDTH(fr), h = GST_VIDEO_FRAME_HEIGHT(fr);
    const auto& tr = *c->tablaRgb;
    const auto& ty = *c->tablaY;
    const float sat = static_cast<float>(saturacion);
    ba::filtros::Orden o{};
    if (ba::filtros::ordenDe(f, o)) {
        auto* d = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
        const int st = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
        const int s = static_cast<int>(sat * 256.0f);
        for (int y = 0; y < h; ++y) {
            std::uint8_t* p = d + static_cast<size_t>(y) * st;
            for (int x = 0; x < w; ++x, p += 4) {
                int r = tr[p[o.r]], g = tr[p[o.g]], b = tr[p[o.b]];
                if (s != 256) {
                    const int lu = (54 * r + 183 * g + 19 * b) >> 8; // 0.2126, 0.7152, 0.0722
                    r = std::clamp(lu + (((r - lu) * s) >> 8), 0, 255);
                    g = std::clamp(lu + (((g - lu) * s) >> 8), 0, 255);
                    b = std::clamp(lu + (((b - lu) * s) >> 8), 0, 255);
                }
                p[o.r] = static_cast<std::uint8_t>(r);
                p[o.g] = static_cast<std::uint8_t>(g);
                p[o.b] = static_cast<std::uint8_t>(b);
            }
        }
        return GST_FLOW_OK;
    }
    // YUV: la luma por tabla; la croma, alejada del centro por saturación y contraste.
    auto* py = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
    const int sy = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
    for (int y = 0; y < h; ++y) {
        std::uint8_t* p = py + static_cast<size_t>(y) * sy;
        for (int x = 0; x < w; ++x)
            p[x] = ty[p[x]];
    }
    const int kc = static_cast<int>(sat * std::max(0.0, contraste) * 256.0);
    if (kc == 256) return GST_FLOW_OK;
    const int cmin = completo ? 0 : 16, cmax = completo ? 255 : 240;
    const auto croma = [kc, cmin, cmax](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(128 + (((v - 128) * kc) >> 8), cmin, cmax));
    };
    const int cw = GST_VIDEO_FRAME_COMP_WIDTH(fr, 1), ch = GST_VIDEO_FRAME_COMP_HEIGHT(fr, 1);
    if (f == GST_VIDEO_FORMAT_NV12) {
        auto* puv = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 1));
        const int suv = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 1);
        for (int y = 0; y < ch; ++y) {
            std::uint8_t* p = puv + static_cast<size_t>(y) * suv;
            for (int x = 0; x < cw * 2; ++x)
                p[x] = croma(p[x]);
        }
    } else {
        for (int pl = 1; pl <= 2; ++pl) {
            auto* pp = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, pl));
            const int sp = GST_VIDEO_FRAME_PLANE_STRIDE(fr, pl);
            for (int y = 0; y < ch; ++y) {
                std::uint8_t* p = pp + static_cast<size_t>(y) * sp;
                for (int x = 0; x < cw; ++x)
                    p[x] = croma(p[x]);
            }
        }
    }
    return GST_FLOW_OK;
}

static void antes(GstBaseTransform* bt, GstBuffer* b) { ba::filtros::sincronizarValores(bt, b); }

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR(o);
    {
        std::lock_guard<std::mutex> l(*c->mu);
        switch (id) {
            case PROP_BRILLO: c->brillo = g_value_get_double(v); break;
            case PROP_CONTRASTE: c->contraste = g_value_get_double(v); break;
            case PROP_SATURACION: c->saturacion = g_value_get_double(v); break;
            case PROP_GAMMA: c->gamma = g_value_get_double(v); break;
            default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
        }
        c->cambio = true;
    }
    actualizarPasante(c);
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR(o);
    std::lock_guard<std::mutex> l(*c->mu);
    switch (id) {
        case PROP_BRILLO: g_value_set_double(v, c->brillo); break;
        case PROP_CONTRASTE: g_value_set_double(v, c->contraste); break;
        case PROP_SATURACION: g_value_set_double(v, c->saturacion); break;
        case PROP_GAMMA: g_value_set_double(v, c->gamma); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* c = BA_COLOR(o);
    delete c->mu;
    delete c->tablaRgb;
    delete c->tablaY;
    G_OBJECT_CLASS(ba_color_parent_class)->finalize(o);
}

static void ba_color_class_init(BaColorClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_color_debug, "bacolor", 0, "Color");
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    using ba::filtros::kVivo;
    g_object_class_install_property(
        go, PROP_BRILLO, g_param_spec_double("brillo", "Brillo", "Se suma (-1..1)", -1, 1, 0, kVivo));
    g_object_class_install_property(go, PROP_CONTRASTE,
                                    g_param_spec_double("contraste", "Contraste",
                                                        "Alrededor del gris medio (0..2)", 0, 2, 1, kVivo));
    g_object_class_install_property(
        go, PROP_SATURACION,
        g_param_spec_double("saturacion", "Saturación", "0 = blanco y negro (0..2)", 0, 2, 1, kVivo));
    g_object_class_install_property(
        go, PROP_GAMMA, g_param_spec_double("gamma", "Gamma", "Exponente (0.2..5)", 0.2, 5, 1, kVivo));
    gst_element_class_set_static_metadata(ge, "Color", "Filter/Effect/Video",
                                          "Brillo, contraste, saturación y gamma (como la tarjeta Color)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    GST_BASE_TRANSFORM_CLASS(k)->before_transform = GST_DEBUG_FUNCPTR(antes);
    GST_VIDEO_FILTER_CLASS(k)->transform_frame_ip = GST_DEBUG_FUNCPTR(transformar);
}

static void ba_color_init(BaColor* c) {
    c->mu = new std::mutex;
    c->tablaRgb = new std::array<std::uint8_t, 256>{};
    c->tablaY = new std::array<std::uint8_t, 256>{};
    c->yCompleto = false;
    c->brillo = 0;
    c->contraste = c->saturacion = c->gamma = 1;
    c->cambio = true;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(c), TRUE);
    actualizarPasante(c);
}

extern "C" gboolean ba_registrar_color(GstPlugin* p) {
    return gst_element_register(p, "bacolor", GST_RANK_NONE, BA_TYPE_COLOR);
}
