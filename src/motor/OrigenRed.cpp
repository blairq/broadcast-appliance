// ============================================================================
// src/motor/OrigenRed.cpp — ver OrigenRed.hpp
// ============================================================================
#include "OrigenRed.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>

namespace ba::motor::origen_red {

namespace {

struct Origen {
    std::string uri;
    int latenciaMs = 200;
    bool ntp = false;
    const Reloj* reloj = nullptr;
    std::string prefijo;
    GstElement* pipeline = nullptr;
    int usuarios = 0;
    gint64 sinUsoDesde = 0;   ///< µs (monotónico) desde que nadie lo usa
    gint64 reintentoEn = 0;   ///< µs: cuándo volver a conectar (0 = no hace falta)
    gint64 esperaMs = 1000;   ///< crece con cada caída seguida (hasta 10 s)
};

// Nunca se destruyen: un estático que se destruye al salir con otro hilo usándolo mata el proceso.
std::mutex& mu() {
    static auto* m = new std::mutex;
    return *m;
}
std::map<std::string, Origen>& origenes() { // clave: uri|latencia|ntp
    static auto* m = new std::map<std::string, Origen>;
    return *m;
}
int& contador() {
    static int n = 0;
    return n;
}

std::string conComillasGst(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

void desarmar(Origen& o) {
    if (o.pipeline == nullptr) return;
    gst_element_set_state(o.pipeline, GST_STATE_NULL);
    gst_object_unref(o.pipeline);
    o.pipeline = nullptr;
}

void armar(Origen& o) {
    desarmar(o);
    // Con la GPU del motor, lo decodificado se sube UNA vez y se publica la
    // textura: el decodificador se la da a la subida sin copiar (en la Mac,
    // vtdec → GL) y cada entrada la toma tal cual (su cadena arranca con la
    // subida, que la deja pasar). Todos los pipelines del motor comparten el
    // contexto (Reloj::adoptar). Publicarlo en memoria de sistema obligaba a
    // cada entrada a subir el cuadro de nuevo: medido con 8 entradas de una
    // cámara, 270 % de CPU y el programa a 10 cuadros/s, contra 170 % y 30.
    // Sin GPU, memoria de sistema. El EOS no pasa (event-types vacío).
    const std::string video = gpuVideo() ? subirGpu() + " ! " + texturaGpu() : std::string("video/x-raw");
    const std::string d =
        "uridecodebin3 name=ba_dec uri=" + conComillasGst(o.uri) +
        " ba_dec.video_0 ! queue max-size-buffers=4 max-size-time=0 max-size-bytes=0 ! " + video + " ! intersink producer-name=" +
        conComillasGst(o.prefijo + ":video") + " sync=false event-types=\"<>\"" +
        " ba_dec.audio_0 ! queue ! audioconvert ! intersink producer-name=" + conComillasGst(o.prefijo + ":audio") +
        " sync=false event-types=\"<>\"";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        registrar(Nivel::Aviso, "origen " + o.uri + ": " + e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        o.reintentoEn = g_get_monotonic_time() + o.esperaMs * 1000;
        return;
    }
    if (GstElement* dec = gst_bin_get_by_name(GST_BIN(p), "ba_dec")) {
        // RTSP y SRT: la latencia de su colchón y, en RTSP, la hora de captura del que manda.
        g_signal_connect(dec, "source-setup",
                         G_CALLBACK(+[](GstElement*, GstElement* fuente, gpointer d) {
                             const auto* o = static_cast<const Origen*>(d);
                             GObjectClass* k = G_OBJECT_GET_CLASS(fuente);
                             const std::string lat = std::to_string(std::max(o->latenciaMs, 20));
                             if (g_object_class_find_property(k, "latency") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "latency", lat.c_str());
                             if (g_object_class_find_property(k, "ntp-sync") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "ntp-sync", o->ntp ? "true" : "false");
                             if (g_object_class_find_property(k, "buffer-mode") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "buffer-mode", o->ntp ? "synced" : "slave");
                         }),
                         &o);
        gst_object_unref(dec);
    }
    sinRelojPropio(p);
    o.reloj->adoptar(p);
    o.pipeline = p;
    o.reintentoEn = 0;
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        registrar(Nivel::Aviso, "origen " + o.uri + ": no arrancó; se reintenta");
        desarmar(o);
        o.reintentoEn = g_get_monotonic_time() + o.esperaMs * 1000;
        o.esperaMs = std::min<gint64>(o.esperaMs * 2, 10000);
    }
}

} // namespace

bool esDeRed(const std::string& uri) {
    // Para medir (BA_SIN_ORIGEN_RED=1): cada entrada con su propia conexión, como antes.
    if (const char* v = std::getenv("BA_SIN_ORIGEN_RED"); v != nullptr && std::string(v) == "1") return false;
    for (const char* p : {"rtsp://", "rtsps://", "rtmp://", "rtmps://", "srt://", "http://", "https://", "udp://", "rtp://"})
        if (uri.rfind(p, 0) == 0) return true;
    return false;
}

std::string usar(const Reloj& reloj, const std::string& uri, int latenciaMs, bool ntp) {
    std::lock_guard<std::mutex> l(mu());
    const std::string clave = uri + "|" + std::to_string(latenciaMs) + "|" + (ntp ? "1" : "0");
    Origen& o = origenes()[clave];
    if (o.prefijo.empty()) {
        o.uri = uri;
        o.latenciaMs = latenciaMs;
        o.ntp = ntp;
        o.reloj = &reloj;
        o.prefijo = "origen:" + std::to_string(++contador());
    }
    ++o.usuarios;
    o.sinUsoDesde = 0;
    if (o.pipeline == nullptr && o.reintentoEn == 0) armar(o);
    return o.prefijo;
}

void soltar(const std::string& prefijo) {
    std::lock_guard<std::mutex> l(mu());
    for (auto& [k, o] : origenes())
        if (o.prefijo == prefijo && o.usuarios > 0) {
            if (--o.usuarios == 0) o.sinUsoDesde = g_get_monotonic_time();
            return;
        }
}

void tic() {
    std::lock_guard<std::mutex> l(mu());
    const gint64 ahora = g_get_monotonic_time();
    for (auto it = origenes().begin(); it != origenes().end();) {
        Origen& o = it->second;
        // Sin nadie hace 5 s: afuera (un rearmado de la entrada no llega a cortarlo).
        if (o.usuarios == 0 && o.sinUsoDesde != 0 && ahora - o.sinUsoDesde > 5'000'000) {
            desarmar(o);
            it = origenes().erase(it);
            continue;
        }
        if (o.pipeline != nullptr) {
            GstBus* bus = gst_element_get_bus(o.pipeline);
            bool caido = false;
            while (GstMessage* m = gst_bus_pop(bus)) {
                if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                    GError* e = nullptr;
                    gst_message_parse_error(m, &e, nullptr);
                    registrar(Nivel::Aviso, "origen " + o.uri + ": " + (e != nullptr ? e->message : "error") +
                                                " (se reconecta en " + std::to_string(o.esperaMs / 1000) + " s)");
                    if (e != nullptr) g_error_free(e);
                    caido = true;
                } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
                    caido = true;
                } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_LATENCY) {
                    recalcularLatenciaAparte(o.pipeline);
                } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_STATE_CHANGED &&
                           GST_MESSAGE_SRC(m) == GST_OBJECT(o.pipeline)) {
                    GstState viejo, nuevo;
                    gst_message_parse_state_changed(m, &viejo, &nuevo, nullptr);
                    if (nuevo == GST_STATE_PLAYING) o.esperaMs = 1000; // anduvo: la próxima caída espera poco
                }
                gst_message_unref(m);
            }
            gst_object_unref(bus);
            if (caido) {
                desarmar(o);
                o.reintentoEn = ahora + o.esperaMs * 1000;
                o.esperaMs = std::min<gint64>(o.esperaMs * 2, 10000);
            }
        } else if (o.reintentoEn != 0 && ahora >= o.reintentoEn && o.usuarios > 0) {
            armar(o);
        }
        ++it;
    }
}

} // namespace ba::motor::origen_red
