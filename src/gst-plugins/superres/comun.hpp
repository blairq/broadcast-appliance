// ============================================================================
// src/gst-plugins/superres/comun.hpp — lo que comparten los motores x2
//
// Para que cada motor (bavtsuperres, baonnxsr) sea un GstBaseTransform como
// corresponde:
//   - CAPS: `transformar_caps` duplica (hacia abajo) o divide (hacia arriba)
//     ancho y alto, sean valores fijos, rangos o listas; lo demás (formato,
//     cuadros por segundo, aspecto del píxel, color) pasa igual. La forma sin
//     agrandar también se ofrece (para cuando el tamaño no entra o el motor
//     está apagado: queda en modo pasante). `fijar_caps` prefiere el doble exacto.
//   - METADATOS: se copian los que no dependen del tamaño (los de tamaño u
//     orientación de GstVideoMeta y regiones no sirven en la imagen agrandada).
//   - LATENCIA: la consulta LATENCY suma lo que tarda el motor (el peor de los
//     últimos cuadros, con margen), y si eso cambia bastante se avisa a la
//     cadena (GST_MESSAGE_LATENCY) para que la recalcule: así, en una cadena
//     que sincroniza (audio y video con sinks sync=true), el audio se atrasa
//     lo mismo y no se desfasa.
//   - QoS activado: si la cadena viene atrasada, GstBaseTransform descarta
//     cuadros antes de procesarlos.
//   - Marcas de tiempo: PTS, DTS, duración y offsets se copian (lo hace
//     GstBaseTransform al no trabajar en el lugar).
// ============================================================================
#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace ba::sr {

/// Multiplica (`por` > 0) o divide (`por` < 0) un campo entero: fijo, rango o lista.
inline bool escalarCampo(GstStructure* st, const char* campo, int por) {
    const GValue* v = gst_structure_get_value(st, campo);
    if (v == nullptr) return true;
    const auto f = [por](int x, bool& ok) {
        if (por > 0) return x > G_MAXINT / por ? (ok = false, x) : x * por;
        if (x % -por != 0) ok = false;
        return x / -por;
    };
    bool ok = true;
    if (G_VALUE_HOLDS_INT(v)) {
        const int r = f(g_value_get_int(v), ok);
        if (ok) gst_structure_set(st, campo, G_TYPE_INT, r, nullptr);
    } else if (GST_VALUE_HOLDS_INT_RANGE(v)) {
        int a = gst_value_get_int_range_min(v), b = gst_value_get_int_range_max(v);
        if (por > 0) {
            a = std::min(a, G_MAXINT / por) * por;
            b = std::min(b, G_MAXINT / por) * por;
        } else {
            a = std::max(1, (a + (-por - 1)) / -por);
            b = b / -por;
        }
        if (a > b) return false;
        if (a == b)
            gst_structure_set(st, campo, G_TYPE_INT, a, nullptr);
        else
            gst_structure_set(st, campo, GST_TYPE_INT_RANGE, a, b, nullptr);
    } else if (GST_VALUE_HOLDS_LIST(v)) {
        GValue lista = G_VALUE_INIT;
        g_value_init(&lista, GST_TYPE_LIST);
        for (guint i = 0; i < gst_value_list_get_size(v); ++i) {
            const GValue* x = gst_value_list_get_value(v, i);
            if (!G_VALUE_HOLDS_INT(x)) continue;
            bool okx = true;
            const int r = f(g_value_get_int(x), okx);
            if (!okx) continue;
            GValue e = G_VALUE_INIT;
            g_value_init(&e, G_TYPE_INT);
            g_value_set_int(&e, r);
            gst_value_list_append_and_take_value(&lista, &e);
        }
        if (gst_value_list_get_size(&lista) == 0) {
            g_value_unset(&lista);
            return false;
        }
        gst_structure_take_value(st, campo, &lista);
    }
    return ok;
}

/// caps del otro lado: el doble (o la mitad) PRIMERO y, detrás, la misma forma (pasante).
/// `puede(ancho, alto)` (opcional): si el motor acepta ese tamaño de ENTRADA (cuando es fijo).
inline GstCaps* transformarCaps(GstPadDirection dir, GstCaps* caps, GstCaps* filtro, bool activo,
                                bool (*puede)(int, int) = nullptr) {
    GstCaps* doble = gst_caps_new_empty();
    GstCaps* igual = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        const GstStructure* st = gst_caps_get_structure(caps, i);
        GstCapsFeatures* feat = gst_caps_get_features(caps, i);
        gst_caps_append_structure_full(igual, gst_structure_copy(st),
                                       feat ? gst_caps_features_copy(feat) : nullptr);
        if (!activo) continue;
        int w = 0, h = 0;
        if (puede != nullptr && gst_structure_get_int(st, "width", &w) &&
            gst_structure_get_int(st, "height", &h)) {
            const bool entrada = dir == GST_PAD_SINK;
            if (entrada ? !puede(w, h) : (w % 2 != 0 || h % 2 != 0 || !puede(w / 2, h / 2))) continue;
        }
        GstStructure* x = gst_structure_copy(st);
        const int por = dir == GST_PAD_SINK ? 2 : -2;
        if (escalarCampo(x, "width", por) && escalarCampo(x, "height", por))
            gst_caps_append_structure_full(doble, x, feat ? gst_caps_features_copy(feat) : nullptr);
        else
            gst_structure_free(x);
    }
    GstCaps* out = gst_caps_merge(doble, igual);
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = f;
    }
    return out;
}

/// Al fijar el formato de salida: el doble exacto de la entrada si se puede.
inline GstCaps* fijarCaps(GstBaseTransform*, GstPadDirection dir, GstCaps* caps, GstCaps* otras) {
    otras = gst_caps_make_writable(otras);
    if (dir == GST_PAD_SINK && gst_caps_get_size(caps) > 0 && gst_caps_get_size(otras) > 0) {
        const GstStructure* in = gst_caps_get_structure(caps, 0);
        GstStructure* out = gst_caps_get_structure(otras, 0);
        int w = 0, h = 0;
        if (gst_structure_get_int(in, "width", &w) && gst_structure_get_int(in, "height", &h)) {
            gst_structure_fixate_field_nearest_int(out, "width", w * 2);
            gst_structure_fixate_field_nearest_int(out, "height", h * 2);
        }
    }
    return gst_caps_fixate(otras);
}

/// Copiar un metadato a la imagen agrandada: solo si no depende del tamaño ni de la orientación.
inline gboolean transformarMeta(GstBaseTransform*, GstBuffer*, GstMeta* meta, GstBuffer*) {
    const GType api = meta->info->api;
    const gchar* const* tags = gst_meta_api_type_get_tags(api);
    if (tags == nullptr) return TRUE;
    return !gst_meta_api_type_has_tag(api, g_quark_from_string(GST_META_TAG_VIDEO_SIZE_STR)) &&
           !gst_meta_api_type_has_tag(api, g_quark_from_string(GST_META_TAG_VIDEO_ORIENTATION_STR));
}

/// Lo que tarda un motor: el peor de los últimos 30 cuadros, para la consulta LATENCY.
struct Demora {
    std::mutex mu;
    std::array<double, 30> ms{};
    size_t n = 0;
    double promedio = 0;
    GstClockTime informada = 0; ///< la última que se declaró (para avisar si cambia)

    void anotar(double v) {
        std::lock_guard<std::mutex> l(mu);
        ms[n % ms.size()] = v;
        ++n;
        promedio = n == 1 ? v : promedio * 0.9 + v * 0.1;
    }
    double media() {
        std::lock_guard<std::mutex> l(mu);
        return promedio;
    }
    /// El peor reciente, con 20 % de margen y al menos 1 ms (en tiempo de GStreamer).
    GstClockTime latencia() {
        std::lock_guard<std::mutex> l(mu);
        double peor = 0;
        for (size_t i = 0; i < std::min(n, ms.size()); ++i)
            peor = std::max(peor, ms[i]);
        return static_cast<GstClockTime>(std::max(1.0, peor * 1.2) * GST_MSECOND);
    }
    void reiniciar() {
        std::lock_guard<std::mutex> l(mu);
        n = 0;
        promedio = 0;
        ms.fill(0);
    }
};

/// La consulta LATENCY: la de arriba más la del motor. Devuelve lo que hizo el padre.
inline gboolean consultaLatencia(GstBaseTransform* bt, GstQuery* q, Demora& d,
                                 gboolean (*padre)(GstBaseTransform*, GstPadDirection, GstQuery*),
                                 GstPadDirection dir) {
    if (!padre(bt, dir, q)) return FALSE;
    if (dir == GST_PAD_SRC && GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        gboolean vivo = FALSE;
        GstClockTime min = 0, max = GST_CLOCK_TIME_NONE;
        gst_query_parse_latency(q, &vivo, &min, &max);
        const GstClockTime propia = d.latencia();
        d.informada = propia;
        gst_query_set_latency(q, vivo, min + propia, GST_CLOCK_TIME_IS_VALID(max) ? max + propia : max);
    }
    return TRUE;
}

/// Si lo que tarda cambió bastante (±25 % y ±2 ms), se avisa a la cadena para que recalcule.
inline void quizasAvisarLatencia(GstBaseTransform* bt, Demora& d) {
    const GstClockTime ahora = d.latencia(), antes = d.informada;
    if (antes == 0 || d.n < 10) return;
    const double a = static_cast<double>(ahora), b = static_cast<double>(antes);
    // Sube: se avisa enseguida (si no, los cuadros llegan tarde). Baja: solo si
    // bajó a la mitad (no vale la pena rearmar la latencia por cada vaivén).
    if ((a > b * 1.25 && a - b > 2.0 * GST_MSECOND) || (a < b * 0.5 && b - a > 2.0 * GST_MSECOND)) {
        d.informada = ahora;
        gst_element_post_message(GST_ELEMENT(bt), gst_message_new_latency(GST_OBJECT(bt)));
    }
}

} // namespace ba::sr
