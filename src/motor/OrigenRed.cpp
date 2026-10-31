// ============================================================================
// src/motor/OrigenRed.cpp — ver OrigenRed.hpp
// ============================================================================
#include "OrigenRed.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gio/gio.h>
#include "platform/Hilo.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace ba::motor::origen_red {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

struct Origen {
    std::string uri;
    Opciones op;
    const Reloj* reloj = nullptr;
    std::string prefijo;
    GstElement* pipeline = nullptr;
    int usuarios = 0;
    gint64 sinUsoDesde = 0;   ///< µs (monotónico) desde que nadie lo usa
    gint64 reintentoEn = 0;   ///< µs: cuándo volver a conectar (0 = no hace falta)
    gint64 esperaMs = 1000;   ///< arranca en op.reconectarMs y crece con cada caída seguida (hasta 10 s)
    /// El pipeline anterior se está desarmando aparte (ver desarmar): hasta que
    /// termine no se arma otro (los productores inter llevan el mismo nombre).
    std::shared_ptr<std::atomic<bool>> desarmando;
};

// Nunca se destruyen: un estático que se destruye al salir con otro hilo usándolo mata el proceso.
std::mutex& mu() {
    static auto* m = new std::mutex;
    return *m;
}
std::map<std::string, Origen>& origenes() { // clave: la uri y las Opciones
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

/// La espera de la primera reconexión, en su rango.
gint64 esperaInicial(const Opciones& op) { return std::clamp<gint64>(op.reconectarMs, 200, 10000); }

/// Las raíces de un archivo PEM para verificar TLS: una base por archivo y para
/// siempre (como los orígenes, nunca se destruye). nullptr si no se pudo leer.
GTlsDatabase* raices(const std::string& ca) {
    static auto* m = new std::mutex;
    static auto* bases = new std::map<std::string, GTlsDatabase*>;
    std::lock_guard<std::mutex> l(*m);
    if (const auto it = bases->find(ca); it != bases->end()) return it->second;
    GError* e = nullptr;
    GTlsDatabase* db = g_tls_file_database_new(ca.c_str(), &e);
    if (e != nullptr) {
        registrar(Nivel::Aviso, llenar(traducir("motor", "raíces de TLS %1: %2"), {ca, ba::diag::utf8Valido(e->message)}));
        g_error_free(e);
    }
    (*bases)[ca] = db;
    return db;
}

// SE DESARMA APARTE. Pasar a NULL un RTSP que no contesta tarda lo que tarde la
// conexión en rendirse (decenas de segundos): con el candado de los orígenes
// tomado, y en el bucle del motor, trababa el motor entero. Un hilo suelto (no
// std::async: su futuro espera al destruirse) y una bandera de «terminó».
void desarmar(Origen& o) {
    if (o.pipeline == nullptr) return;
    GstElement* p = o.pipeline;
    o.pipeline = nullptr;
    auto listo = std::make_shared<std::atomic<bool>>(false);
    o.desarmando = listo;
    std::thread([p, listo]() {
        ba::platform::nombrarHilo("red-desarmar");
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        listo->store(true);
    }).detach();
}

void armar(Origen& o) {
    desarmar(o);
    if (o.desarmando && !o.desarmando->load()) {
        // El anterior todavía se está desarmando: se arma en un rato (sin esperar acá).
        o.reintentoEn = g_get_monotonic_time() + 300'000;
        return;
    }
    o.desarmando.reset();
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
        registrar(Nivel::Aviso, llenar(traducir("motor", "origen %1: %2"), {o.uri, ba::diag::utf8Valido(e->message)}));
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        o.reintentoEn = g_get_monotonic_time() + o.esperaMs * 1000;
        return;
    }
    if (GstElement* dec = gst_bin_get_by_name(GST_BIN(p), "ba_dec")) {
        // RTSP y SRT: la latencia de su colchón y, en RTSP, la hora de captura del que manda
        // y el transporte elegido; con TLS, las raíces con las que se verifica al servidor.
        g_signal_connect(dec, "source-setup",
                         G_CALLBACK(+[](GstElement*, GstElement* fuente, gpointer d) {
                             const Opciones& op = static_cast<const Origen*>(d)->op;
                             GObjectClass* k = G_OBJECT_GET_CLASS(fuente);
                             const std::string lat = std::to_string(std::max(op.latenciaMs, 20));
                             if (g_object_class_find_property(k, "latency") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "latency", lat.c_str());
                             if (g_object_class_find_property(k, "ntp-sync") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "ntp-sync", op.ntp ? "true" : "false");
                             if (g_object_class_find_property(k, "buffer-mode") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "buffer-mode", op.ntp ? "synced" : "slave");
                             if (!op.transporteRtsp.empty() && g_object_class_find_property(k, "protocols") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(fuente), "protocols", op.transporteRtsp.c_str());
                             if (!op.ca.empty()) {
                                 if (g_object_class_find_property(k, "tls-database") != nullptr) {
                                     if (GTlsDatabase* db = raices(op.ca)) g_object_set(fuente, "tls-database", db, nullptr);
                                 } else if (g_object_class_find_property(k, "ssl-ca-file") != nullptr) {
                                     g_object_set(fuente, "ssl-ca-file", op.ca.c_str(), nullptr);
                                 }
                             }
                         }),
                         &o);
        gst_object_unref(dec);
    }
    sinRelojPropio(p);
    o.reloj->adoptar(p);
    o.pipeline = p;
    o.reintentoEn = 0;
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        registrar(Nivel::Aviso, llenar(traducir("motor", "origen %1: no arrancó; se reintenta"), {o.uri}));
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

std::string usar(const Reloj& reloj, const std::string& uri, const Opciones& op) {
    std::lock_guard<std::mutex> l(mu());
    const std::string clave = uri + "|" + std::to_string(op.latenciaMs) + "|" + (op.ntp ? "1" : "0") + "|" +
                              op.transporteRtsp + "|" + op.ca + "|" + std::to_string(op.reconectarMs);
    Origen& o = origenes()[clave];
    if (o.prefijo.empty()) {
        o.uri = uri;
        o.op = op;
        o.esperaMs = esperaInicial(op);
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
                    registrar(Nivel::Aviso,
                              llenar(traducir("motor", "origen %1: %2 (se reconecta en %3 s)"),
                                     {o.uri, e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("motor", "error"),
                                      std::to_string(o.esperaMs / 1000)}));
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
                    if (nuevo == GST_STATE_PLAYING) o.esperaMs = esperaInicial(o.op); // anduvo: la próxima caída espera poco
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
