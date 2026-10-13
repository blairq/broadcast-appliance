// ============================================================================
// src/gst-plugins/filtros/legal.cpp — balegal: legalizador broadcast-safe (T39)
//
// Para entregar a un canal de TV: deja la imagen dentro de lo que una
// emisora acepta sin rebotar el material.
//
//   * LUMA en el rango legal (16..235 en rango limitado). `modo=recortar`
//     corta en seco; `modo=suave` comprime con una rodilla desde el 95 % para
//     que los blancos no se vean planchados.
//   * CROMA en 16..240.
//   * GAMA COMPUESTA (`compuesto`, 1.33 de fábrica = el +133 % de PAL; 0 lo
//     apaga): luma + amplitud de croma no puede pasar el límite, ni luma −
//     croma bajar de −(límite − 1). Donde se pasa, se baja la SATURACIÓN de
//     ese píxel lo justo (la luma no se toca). Es lo que hace un legalizador
//     de hardware: los amarillos y rojos al 100 % son los que caen.
//
// En rango completo (0..255, p. ej. una cámara JPEG) la luma y la croma ya
// están en rango: solo actúa la gama compuesta. En RGB, igual: se pasa a
// Y/Pb/Pr BT.709, se baja la croma donde haga falta y se vuelve.
//
// Formatos: NV12, I420, Y444 y RGB de 4 bytes. En el lugar; pasante si
// `activo=false`.
// ============================================================================

#include "comun.hpp"

#include <mutex>

G_BEGIN_DECLS
#define BA_TYPE_LEGAL (ba_legal_get_type())
G_DECLARE_FINAL_TYPE(BaLegal, ba_legal, BA, LEGAL, GstVideoFilter)
G_END_DECLS

struct _BaLegal {
    GstVideoFilter parent;
    std::mutex* mu;
    gboolean activo;
    gint modo; ///< 0 = recortar, 1 = suave
    gdouble compuesto;
};

G_DEFINE_TYPE(BaLegal, ba_legal, GST_TYPE_VIDEO_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_legal_debug);
#define GST_CAT_DEFAULT ba_legal_debug

enum { PROP_0, PROP_ACTIVO, PROP_MODO, PROP_COMPUESTO };

#define FORMATOS "{ NV12, I420, Y444, BGRA, RGBA, ARGB, ABGR, BGRx, RGBx, xRGB, xBGR }"
static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(FORMATOS)));

namespace {

/// La amplitud de la subportadora de croma (U, V de la compuesta) desde Pb/Pr BT.709.
inline float amplitudCroma(float pb, float pr) {
    const float u = 0.913f * pb, v = 1.381f * pr;
    return std::sqrt(u * u + v * v);
}

/// Cuánto hay que achicar la croma para que y ± c quede adentro (1 = nada).
inline float factorCompuesto(float yMax, float yMin, float c, float limite) {
    if (limite <= 0 || c < 1e-5f) return 1.0f;
    float k = 1.0f;
    if (yMax + c > limite) k = std::min(k, std::max(0.0f, (limite - yMax) / c));
    const float piso = -(limite - 1.0f);
    if (yMin - c < piso) k = std::min(k, std::max(0.0f, (yMin - piso) / c));
    return k;
}

/// La luma (0..1 en su escala) con rodilla suave desde el 95 %.
inline float rodilla(float y) {
    constexpr float k = 0.95f;
    if (y <= k) return std::max(0.0f, y);
    const float x = (y - k) / (1.0f - k);
    return k + (1.0f - k) * std::tanh(x);
}

} // namespace

static void actualizarPasante(BaLegal* c) {
    bool pasa;
    {
        std::lock_guard<std::mutex> l(*c->mu);
        pasa = !c->activo;
    }
    ba::filtros::pasante(GST_BASE_TRANSFORM(c), pasa);
}

static GstFlowReturn transformar(GstVideoFilter* vf, GstVideoFrame* fr) {
    auto* c = BA_LEGAL(vf);
    gint modo;
    float limite;
    {
        std::lock_guard<std::mutex> l(*c->mu);
        modo = c->modo;
        limite = static_cast<float>(c->compuesto);
    }
    const GstVideoFormat f = GST_VIDEO_FRAME_FORMAT(fr);
    const int w = GST_VIDEO_FRAME_WIDTH(fr), h = GST_VIDEO_FRAME_HEIGHT(fr);

    ba::filtros::Orden o{};
    if (ba::filtros::ordenDe(f, o)) {
        if (limite <= 0) return GST_FLOW_OK; // en RGB solo hay gama compuesta para corregir
        auto* d = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
        const int st = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
        for (int y = 0; y < h; ++y) {
            std::uint8_t* p = d + static_cast<size_t>(y) * st;
            for (int x = 0; x < w; ++x, p += 4) {
                const float r = p[o.r] / 255.0f, g = p[o.g] / 255.0f, b = p[o.b] / 255.0f;
                const float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                const float pb = (b - yy) / 1.8556f, pr = (r - yy) / 1.5748f;
                const float k = factorCompuesto(yy, yy, amplitudCroma(pb, pr), limite);
                if (k >= 1.0f) continue;
                const float nb = yy + 1.8556f * pb * k, nr = yy + 1.5748f * pr * k;
                const float ng = (yy - 0.2126f * nr - 0.0722f * nb) / 0.7152f;
                p[o.r] = ba::filtros::a8(nr);
                p[o.g] = ba::filtros::a8(ng);
                p[o.b] = ba::filtros::a8(nb);
            }
        }
        return GST_FLOW_OK;
    }

    const bool completo = GST_VIDEO_INFO_COLORIMETRY(&fr->info).range == GST_VIDEO_COLOR_RANGE_0_255;
    const float yPiso = completo ? 0.0f : 16.0f, yEscala = completo ? 255.0f : 219.0f;
    const float cEscala = completo ? 255.0f : 224.0f;
    const int cMin = completo ? 0 : 16, cMax = completo ? 255 : 240;

    // 1) La luma: recorte o rodilla, por tabla.
    auto* py = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 0));
    const int sy = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 0);
    if (!completo) {
        std::uint8_t tabla[256];
        for (int i = 0; i < 256; ++i) {
            const float yn = (i - yPiso) / yEscala;
            const float v = modo == 1 ? rodilla(yn) : std::clamp(yn, 0.0f, 1.0f);
            tabla[i] = static_cast<std::uint8_t>(std::clamp(yPiso + v * yEscala + 0.5f, 16.0f, 235.0f));
        }
        for (int y = 0; y < h; ++y) {
            std::uint8_t* p = py + static_cast<size_t>(y) * sy;
            for (int x = 0; x < w; ++x)
                p[x] = tabla[p[x]];
        }
    }

    // 2) La croma: a su rango y, si hace falta, menos saturación donde la
    //    compuesta se pasa (mirando la luma más alta y más baja que cubre cada
    //    muestra de croma: en 4:2:0 son cuatro píxeles).
    const int cw = GST_VIDEO_FRAME_COMP_WIDTH(fr, 1), ch = GST_VIDEO_FRAME_COMP_HEIGHT(fr, 1);
    const int sx = std::max(1, w / std::max(1, cw)), syc = std::max(1, h / std::max(1, ch));
    const auto lumas = [&](int cx, int cy, float& mx, float& mn) {
        mx = 0.0f;
        mn = 1.0f;
        for (int dy = 0; dy < syc; ++dy) {
            const int yy = std::min(h - 1, cy * syc + dy);
            const std::uint8_t* fila = py + static_cast<size_t>(yy) * sy;
            for (int dx = 0; dx < sx; ++dx) {
                const float v = (fila[std::min(w - 1, cx * sx + dx)] - yPiso) / yEscala;
                mx = std::max(mx, v);
                mn = std::min(mn, v);
            }
        }
    };
    const auto corregir = [&](std::uint8_t& cb, std::uint8_t& cr, int cx, int cy) {
        int b = std::clamp<int>(cb, cMin, cMax), r = std::clamp<int>(cr, cMin, cMax);
        if (limite > 0) {
            const float pb = (b - 128) / cEscala, pr = (r - 128) / cEscala;
            float mx, mn;
            lumas(cx, cy, mx, mn);
            const float k = factorCompuesto(mx, mn, amplitudCroma(pb, pr), limite);
            if (k < 1.0f) {
                b = static_cast<int>(std::lround(128 + (b - 128) * k));
                r = static_cast<int>(std::lround(128 + (r - 128) * k));
            }
        }
        cb = static_cast<std::uint8_t>(b);
        cr = static_cast<std::uint8_t>(r);
    };
    if (f == GST_VIDEO_FORMAT_NV12) {
        auto* puv = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 1));
        const int suv = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 1);
        for (int y = 0; y < ch; ++y) {
            std::uint8_t* p = puv + static_cast<size_t>(y) * suv;
            for (int x = 0; x < cw; ++x)
                corregir(p[2 * x], p[2 * x + 1], x, y);
        }
    } else {
        auto* pu = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 1));
        auto* pv = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(fr, 2));
        const int su = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 1), sv = GST_VIDEO_FRAME_PLANE_STRIDE(fr, 2);
        for (int y = 0; y < ch; ++y) {
            std::uint8_t* u = pu + static_cast<size_t>(y) * su;
            std::uint8_t* v = pv + static_cast<size_t>(y) * sv;
            for (int x = 0; x < cw; ++x)
                corregir(u[x], v[x], x, y);
        }
    }
    return GST_FLOW_OK;
}

static void antes(GstBaseTransform* bt, GstBuffer* b) { ba::filtros::sincronizarValores(bt, b); }

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = BA_LEGAL(o);
    {
        std::lock_guard<std::mutex> l(*c->mu);
        switch (id) {
            case PROP_ACTIVO: c->activo = g_value_get_boolean(v); break;
            case PROP_MODO: c->modo = g_value_get_int(v); break;
            case PROP_COMPUESTO: c->compuesto = g_value_get_double(v); break;
            default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
        }
    }
    actualizarPasante(c);
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = BA_LEGAL(o);
    std::lock_guard<std::mutex> l(*c->mu);
    switch (id) {
        case PROP_ACTIVO: g_value_set_boolean(v, c->activo); break;
        case PROP_MODO: g_value_set_int(v, c->modo); break;
        case PROP_COMPUESTO: g_value_set_double(v, c->compuesto); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    delete BA_LEGAL(o)->mu;
    G_OBJECT_CLASS(ba_legal_parent_class)->finalize(o);
}

static void ba_legal_class_init(BaLegalClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_legal_debug, "balegal", 0, "Legalizador");
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    using ba::filtros::kVivo;
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo", "false = pasa sin tocar", TRUE, kVivo));
    g_object_class_install_property(
        go, PROP_MODO, g_param_spec_int("modo", "Modo", "0 = recortar, 1 = suave (rodilla en los blancos)", 0, 1, 1, kVivo));
    g_object_class_install_property(
        go, PROP_COMPUESTO,
        g_param_spec_double("compuesto", "Gama compuesta",
                            "Límite de luma + croma (1.33 = PAL +133 %, 1.2 = NTSC; 0 = no mirar)", 0, 1.5, 1.33,
                            kVivo));
    gst_element_class_set_static_metadata(ge, "Legalizador", "Filter/Effect/Video",
                                          "Luma, croma y gama compuesta dentro del rango legal para TV",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    GST_BASE_TRANSFORM_CLASS(k)->before_transform = GST_DEBUG_FUNCPTR(antes);
    GST_VIDEO_FILTER_CLASS(k)->transform_frame_ip = GST_DEBUG_FUNCPTR(transformar);
}

static void ba_legal_init(BaLegal* c) {
    c->mu = new std::mutex;
    c->activo = TRUE;
    c->modo = 1;
    c->compuesto = 1.33;
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(c), TRUE);
}

extern "C" gboolean ba_registrar_legal(GstPlugin* p) {
    return gst_element_register(p, "balegal", GST_RANK_NONE, BA_TYPE_LEGAL);
}
