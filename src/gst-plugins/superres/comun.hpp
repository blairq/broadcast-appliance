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
#include <cstdint>
#include <mutex>
#include <string>

namespace ba::sr {

/// Las variantes de una red compiladas para un tamaño fijo (propiedad
/// `variantes`: "1024x576=/ruta/a;640x360=/ruta/b", la arma la app con los
/// variante.json del plugin). La del tamaño de entrada `w`x`h` o, si no hay,
/// `modelo` (la general).
inline std::string rutaVariante(const char* modelo, const char* variantes, int w, int h) {
    const std::string clave = std::to_string(w) + "x" + std::to_string(h) + "=";
    const std::string v = variantes != nullptr ? variantes : "";
    size_t i = 0;
    while (i < v.size()) {
        size_t fin = v.find(';', i);
        if (fin == std::string::npos) fin = v.size();
        if (v.compare(i, clave.size(), clave) == 0) return v.substr(i + clave.size(), fin - i - clave.size());
        i = fin + 1;
    }
    return modelo != nullptr ? modelo : "";
}

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
                                bool (*puede)(int, int) = nullptr, int factor = 2) {
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
            if (entrada ? !puede(w, h) : (w % factor != 0 || h % factor != 0 || !puede(w / factor, h / factor))) continue;
        }
        GstStructure* x = gst_structure_copy(st);
        const int por = dir == GST_PAD_SINK ? factor : -factor;
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

/// Al fijar el formato de salida: `factor` veces la entrada, exacto si se puede.
inline GstCaps* fijarCapsPor(GstPadDirection dir, GstCaps* caps, GstCaps* otras, int factor) {
    otras = gst_caps_make_writable(otras);
    if (dir == GST_PAD_SINK && gst_caps_get_size(caps) > 0 && gst_caps_get_size(otras) > 0) {
        const GstStructure* in = gst_caps_get_structure(caps, 0);
        GstStructure* out = gst_caps_get_structure(otras, 0);
        int w = 0, h = 0;
        if (gst_structure_get_int(in, "width", &w) && gst_structure_get_int(in, "height", &h)) {
            gst_structure_fixate_field_nearest_int(out, "width", w * factor);
            gst_structure_fixate_field_nearest_int(out, "height", h * factor);
        }
    }
    return gst_caps_fixate(otras);
}

/// Al fijar el formato de salida: el doble exacto de la entrada si se puede
/// (los elementos cuyo motor agranda x2; los que no, usan fijarCapsPor).
inline GstCaps* fijarCaps(GstBaseTransform*, GstPadDirection dir, GstCaps* caps, GstCaps* otras) {
    return fijarCapsPor(dir, caps, otras, 2);
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
    double suma = 0;          ///< de todos los cuadros (para el resumen al parar)
    GstClockTime informada = 0; ///< la última que se declaró (para avisar si cambia)

    void anotar(double v) {
        std::lock_guard<std::mutex> l(mu);
        ms[n % ms.size()] = v;
        ++n;
        suma += v;
        promedio = n == 1 ? v : promedio * 0.9 + v * 0.1;
    }
    double media() {
        std::lock_guard<std::mutex> l(mu);
        return promedio;
    }
    /// {cuadros, suma de ms} desde el último reinicio (para medir una ventana:
    /// la diferencia entre dos lecturas; ver el banco de pruebas).
    std::pair<std::uint64_t, double> totales() {
        std::lock_guard<std::mutex> l(mu);
        return {static_cast<std::uint64_t>(n), suma};
    }
    /// {cuadros, promedio exacto de todos}.
    std::pair<size_t, double> resumen() {
        std::lock_guard<std::mutex> l(mu);
        return {n, n == 0 ? 0.0 : suma / static_cast<double>(n)};
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
        suma = 0;
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

// --- Modos de una red: activo, reposo, barato -----------------------------------------
// Para tener cargadas (y calientes) las redes de todas las cámaras sin gastar el
// acelerador en las que no se ven: la app (el programador) las prende al pasar a
// preview. En todos los modos la salida es x2 (el tamaño nunca cambia: nada se
// renegocia ni salta en la escena) y nunca hay un cuadro en negro:
//   activo  la red
//   reposo  agrandado común en la GPU (décimos de ms); la red queda cargada y
//           una pasada de mantenimiento cada tanto la tiene caliente
//   barato  como reposo, más un afilado (para lo que se ve pero no tiene turno)
// Al cambiar, mezcla gradual entre el agrandado común y la red en
// `transicion-ms` (de fábrica 400 ms): la mejora entra sin saltos.
// Se cambia con la propiedad `modo` o con el evento estándar del canal de control
// de la entrada (GST_EVENT_CUSTOM_DOWNSTREAM_OOB «ba/control», campos `superres`
// y `transicion_ms`), que todos los elementos dejan pasar.

enum ModoSr { kActivo = 0, kReposo = 1, kBarato = 2 };

inline GType tipoModoSr() {
    static GType t = 0;
    if (g_once_init_enter(&t)) {
        static const GEnumValue v[] = {{kActivo, "La red", "activo"},
                                       {kReposo, "Cargada y caliente; sale el agrandado común", "reposo"},
                                       {kBarato, "Cargada; sale el agrandado común afilado", "barato"},
                                       {0, nullptr, nullptr}};
        g_once_init_leave(&t, g_enum_register_static("BaSuperResModo", v));
    }
    return t;
}

/// Lee un evento del canal de control. true si era «ba/control» y traía `superres`.
inline bool leerControlSr(GstEvent* ev, int& modo, int& transicionMs) {
    if (GST_EVENT_TYPE(ev) != GST_EVENT_CUSTOM_DOWNSTREAM_OOB && GST_EVENT_TYPE(ev) != GST_EVENT_CUSTOM_DOWNSTREAM)
        return false;
    const GstStructure* st = gst_event_get_structure(ev);
    if (st == nullptr || !gst_structure_has_name(st, "ba/control")) return false;
    gint t = 0;
    if (gst_structure_get_int(st, "transicion_ms", &t)) transicionMs = std::clamp(t, 0, 10000);
    const gchar* m = gst_structure_get_string(st, "superres");
    if (m == nullptr) return false;
    const std::string x = m;
    modo = x == "reposo" ? kReposo : x == "barato" ? kBarato : kActivo;
    return true;
}

/// La mezcla gradual (solo el hilo de streaming): 0 = agrandado común, 1 = la red.
struct Mezcla {
    double valor = 1.0;
    /// Avanza un cuadro hacia lo que pide el modo y devuelve el valor para ese cuadro.
    double avanzar(int modo, int transicionMs, double fps) {
        const double objetivo = modo == kActivo ? 1.0 : 0.0;
        const double cuadros = std::max(1.0, fps * transicionMs / 1000.0);
        const double paso = 1.0 / cuadros;
        if (valor < objetivo)
            valor = std::min(objetivo, valor + paso);
        else if (valor > objetivo)
            valor = std::max(objetivo, valor - paso);
        return valor;
    }
};

/// dst = dst*(1-k) + src*k, byte a byte (cualquier formato de 8 bits por canal).
inline void mezclarPlano(guint8* dst, int pasoDst, const guint8* src, int pasoSrc, int anchoBytes, int alto,
                         double k) {
    const int kk = static_cast<int>(std::lround(std::clamp(k, 0.0, 1.0) * 256.0));
    for (int y = 0; y < alto; ++y) {
        guint8* d = dst + static_cast<size_t>(y) * pasoDst;
        const guint8* s = src + static_cast<size_t>(y) * pasoSrc;
        for (int x = 0; x < anchoBytes; ++x)
            d[x] = static_cast<guint8>((d[x] * (256 - kk) + s[x] * kk) >> 8);
    }
}

} // namespace ba::sr
