// ============================================================================
// src/motor/Salidas.cpp — ver Salidas.hpp
// ============================================================================
#include "Salidas.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <algorithm>
#include <filesystem>

namespace ba::motor {

using json = nlohmann::json;

namespace {

std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

const std::vector<std::string>& codificadoresDeLaPlaca() {
#if defined(__APPLE__)
    static const std::vector<std::string> v{"vtenc_h264_hw", "vtenc_h264"};
#elif defined(_WIN32)
    static const std::vector<std::string> v{"nvh264enc", "qsvh264enc", "d3d12h264enc", "amfh264enc", "mfh264enc"};
#else
    static const std::vector<std::string> v{"nvh264enc", "qsvh264enc", "vah264enc", "vah264lpenc"};
#endif
    return v;
}

bool tiene(GstElement* e, const char* p) { return g_object_class_find_property(G_OBJECT_GET_CLASS(e), p) != nullptr; }

/// El sufijo de un tramo de splitmuxsink en la ruta que arma.
constexpr int kMargenSalidaMs = 300;

} // namespace

std::string elegirCodificador(const std::vector<std::string>& preferidos) {
    for (const auto& f : preferidos.empty() ? codificadoresDeLaPlaca() : preferidos)
        if (hayFabrica(f)) return f;
    return hayFabrica("x264enc") ? "x264enc" : std::string();
}

GstElement* crearCodificador(const std::string& fabrica, int kbps, int gop) {
    GstElement* e = gst_element_factory_make(fabrica.c_str(), "codificador");
    if (e == nullptr) return nullptr;
    const auto u = static_cast<guint>(std::max(100, kbps));
    if (tiene(e, "bitrate")) {
        // nvh264enc/qsv/vt/x264: kbps; algunos (d3d12) también en kbps.
        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), "bitrate");
        if (G_PARAM_SPEC_VALUE_TYPE(ps) == G_TYPE_UINT)
            g_object_set(e, "bitrate", u, nullptr);
        else if (G_PARAM_SPEC_VALUE_TYPE(ps) == G_TYPE_INT)
            g_object_set(e, "bitrate", static_cast<gint>(u), nullptr);
    }
    const auto g = std::max(1, gop);
    if (tiene(e, "max-keyframe-interval")) g_object_set(e, "max-keyframe-interval", g, nullptr); // vtenc
    if (tiene(e, "key-int-max")) g_object_set(e, "key-int-max", static_cast<guint>(g), nullptr); // x264
    if (tiene(e, "gop-size")) g_object_set(e, "gop-size", g, nullptr);                           // nv, qsv, d3d12
    if (tiene(e, "realtime")) g_object_set(e, "realtime", TRUE, nullptr);
    if (tiene(e, "allow-frame-reordering")) g_object_set(e, "allow-frame-reordering", FALSE, nullptr);
    if (fabrica == "x264enc") {
        gst_util_set_object_arg(G_OBJECT(e), "tune", "zerolatency");
        gst_util_set_object_arg(G_OBJECT(e), "speed-preset", "veryfast");
    }
    if (tiene(e, "zerolatency")) g_object_set(e, "zerolatency", TRUE, nullptr); // nvh264enc
    return e;
}

// --- Grabación ---------------------------------------------------------------------------

Grabacion::Grabacion(Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {}

Grabacion::~Grabacion() { parar(3000); }

std::string Grabacion::iniciar(const Fuentes& f) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ != nullptr) return {};
    error_.clear();
    {
        std::lock_guard<std::mutex> la(arMu_);
        archivos_.clear();
    }
    if (f.video.empty() && f.audios.empty()) return error_ = "no hay qué grabar";
    std::error_code ec;
    std::filesystem::create_directories(cfg_.carpeta, ec);
    const std::string patron = (std::filesystem::path(cfg_.carpeta) / (cfg_.nombre + "-%05d.mkv")).string();
    std::string d = "splitmuxsink name=mux async-finalize=false location=" + entreComillas(patron) + " max-size-time=" +
                    std::to_string(static_cast<guint64>(std::max(0, cfg_.tramoSeg)) * GST_SECOND) + " ";
    if (!f.video.empty())
        d += "intersrc producer-name=" + entreComillas(f.video) +
             " ! queue name=cv max-size-time=3000000000 max-size-buffers=0 max-size-bytes=0 ! videoconvert ! "
             "video/x-raw,format=NV12 ! identity name=antes_cod ";
    for (std::size_t i = 0; i < f.audios.size(); ++i)
        d += "intersrc producer-name=" + entreComillas(f.audios[i]) +
             " ! queue max-size-time=3000000000 max-size-buffers=0 max-size-bytes=0 ! audioconvert ! "
             "avenc_aac bitrate=" + std::to_string(std::max(64, cfg_.kbpsAudio) * 1000) + " ! aacparse ! mux.audio_" +
             std::to_string(i) + " ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        error_ = e->message;
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return error_;
    }
    // El muxer de los tramos: Matroska (aguanta un corte: se puede abrir lo escrito).
    GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
    if (GstElement* mk = gst_element_factory_make("matroskamux", nullptr)) g_object_set(mux, "muxer", mk, nullptr);
    if (!f.video.empty()) {
        codificador_ = elegirCodificador(cfg_.codificadores);
        GstElement* cod = crearCodificador(codificador_, cfg_.kbps, cfg_.gopSeg * std::max(1, cfg_.fps));
        GstElement* parse = gst_element_factory_make("h264parse", nullptr);
        GstElement* antes = gst_bin_get_by_name(GST_BIN(p), "antes_cod");
        if (cod == nullptr || parse == nullptr || antes == nullptr) {
            error_ = "no hay codificador de video (" + codificador_ + ")";
            for (GstElement* x : {cod, parse, antes})
                if (x != nullptr) gst_object_unref(x);
            gst_object_unref(mux);
            gst_object_unref(p);
            return error_;
        }
        gst_bin_add_many(GST_BIN(p), cod, parse, nullptr);
        GstPad* vpad = gst_element_request_pad_simple(mux, "video");
        GstPad* sale = gst_element_get_static_pad(parse, "src");
        const bool ok = gst_element_link_many(antes, cod, parse, nullptr) && gst_pad_link(sale, vpad) == GST_PAD_LINK_OK;
        gst_object_unref(sale);
        gst_object_unref(vpad);
        gst_object_unref(antes);
        if (!ok) {
            error_ = "no se pudo enganchar el codificador " + codificador_;
            gst_object_unref(mux);
            gst_object_unref(p);
            return error_;
        }
    }
    // Cada tramo nuevo: anotarlo.
    g_signal_connect(mux, "format-location-full",
                     G_CALLBACK(+[](GstElement*, guint id, GstSample*, gpointer d) -> gchar* {
                         auto* g = static_cast<Grabacion*>(d);
                         char nom[32];
                         std::snprintf(nom, sizeof nom, "-%05u.mkv", id);
                         const std::string ruta =
                             (std::filesystem::path(g->cfg_.carpeta) / (g->cfg_.nombre + nom)).string();
                         std::lock_guard<std::mutex> la(g->arMu_); // (mu_ no: parar lo tiene mientras espera el EOS)
                         g->archivos_.push_back(ruta);
                         return g_strdup(ruta.c_str());
                     }),
                     this);
    gst_object_unref(mux);
    sinRelojPropio(p);
    reloj_.adoptar(p);
    const std::int64_t lat = std::max(f.latVideoNs, f.latAudioNs) + kMargenSalidaMs * static_cast<std::int64_t>(GST_MSECOND);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "la grabación no arrancó";
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        return error_;
    }
    pipeline_ = p;
    desdeNs_ = ahoraNs();
    registrar(Nivel::Info, "grabación: " + patron + " (" + (codificador_.empty() ? "solo audio" : codificador_) + ")");
    return {};
}

bool Grabacion::parar(int esperaMs) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return true;
    // EOS por todas las ramas: splitmuxsink cierra el tramo (índice incluido).
    gst_element_send_event(pipeline_, gst_event_new_eos());
    GstBus* bus = gst_element_get_bus(pipeline_);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(esperaMs) * GST_MSECOND,
                                               static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
    if (m != nullptr) gst_message_unref(m);
    gst_object_unref(bus);
    if (!ok) registrar(Nivel::Aviso, "grabación: no cerró a tiempo (el último tramo puede quedar sin índice)");
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return ok;
}

bool Grabacion::grabando() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipeline_ != nullptr;
}

void Grabacion::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? e->message : "error";
            registrar(Nivel::Error, "grabación (" + std::string(GST_OBJECT_NAME(GST_MESSAGE_SRC(m))) + "): " + error_);
            if (e != nullptr) g_error_free(e);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

json Grabacion::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    return {{"grabando", pipeline_ != nullptr},
            {"error", error_},
            {"codificador", codificador_},
            {"segundos", pipeline_ != nullptr ? static_cast<double>(ahoraNs() - desdeNs_) / 1e9 : 0.0},
            {"archivos", archivos()}};
}

std::vector<std::string> Grabacion::archivos() const {
    std::lock_guard<std::mutex> l(arMu_);
    return archivos_;
}

// --- Transmisión ---------------------------------------------------------------------------

namespace {
/// La URL para mostrar: sin la clave de transmisión (lo que va después del último «/» en RTMP)
/// ni la passphrase de SRT.
std::string sinClave(const std::string& u) {
    if (u.rfind("rtmp", 0) == 0) {
        const auto p = u.rfind('/');
        return p == std::string::npos ? u : u.substr(0, p + 1) + "••••";
    }
    const auto q = u.find("passphrase=");
    if (q == std::string::npos) return u;
    auto fin = u.find('&', q);
    return u.substr(0, q) + "passphrase=••••" + (fin == std::string::npos ? std::string() : u.substr(fin));
}
} // namespace

Transmision::Transmision(Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {}

Transmision::~Transmision() { parar(); }

GstPadProbeReturn Transmision::alSalir(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* t = static_cast<Transmision*>(d);
    if (GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info)) t->bytes_ += gst_buffer_get_size(b);
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Transmision::alCodificar(GstPad*, GstPadProbeInfo*, gpointer d) {
    ++static_cast<Transmision*>(d)->cuadros_;
    return GST_PAD_PROBE_OK;
}

std::string Transmision::armar() {
    desarmar();
    const bool srt = cfg_.url.rfind("srt://", 0) == 0;
    const bool rtmp = cfg_.url.rfind("rtmp://", 0) == 0 || cfg_.url.rfind("rtmps://", 0) == 0;
    if (!srt && !rtmp) return "la dirección tiene que empezar con rtmp://, rtmps:// o srt://";
    std::string d = srt ? "mpegtsmux name=mux alignment=7 ! identity name=sale ! srtsink name=red wait-for-connection=false uri=" +
                              entreComillas(cfg_.url) + " "
                        : "flvmux name=mux streamable=true ! identity name=sale ! rtmp2sink name=red async-connect=true location=" +
                              entreComillas(cfg_.url) + " ";
    if (!fuentes_.video.empty())
        d += "intersrc producer-name=" + entreComillas(fuentes_.video) +
             " ! queue name=cv leaky=downstream max-size-time=1000000000 max-size-buffers=0 max-size-bytes=0 ! videoconvert ! "
             "video/x-raw,format=NV12 ! identity name=antes_cod ";
    if (!fuentes_.audios.empty())
        d += "intersrc producer-name=" + entreComillas(fuentes_.audios.front()) +
             " ! queue max-size-time=1000000000 max-size-buffers=0 max-size-bytes=0 ! audioconvert ! avenc_aac bitrate=" +
             std::to_string(std::max(64, cfg_.kbpsAudio) * 1000) + " ! aacparse ! queue ! mux. ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = e->message;
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    if (!fuentes_.video.empty()) {
        codificador_ = elegirCodificador(cfg_.codificadores);
        GstElement* cod = crearCodificador(codificador_, cfg_.kbps, cfg_.gopSeg * std::max(1, cfg_.fps));
        GstElement* parse = gst_element_factory_make("h264parse", nullptr);
        GstElement* cola = gst_element_factory_make("queue", nullptr);
        GstElement* antes = gst_bin_get_by_name(GST_BIN(p), "antes_cod");
        GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
        bool ok = cod != nullptr && parse != nullptr && cola != nullptr && antes != nullptr && mux != nullptr;
        if (ok) {
            g_object_set(parse, "config-interval", -1, nullptr); // SPS/PPS con cada clave (quien entra a mitad, ve)
            gst_bin_add_many(GST_BIN(p), cod, parse, cola, nullptr);
            ok = gst_element_link_many(antes, cod, parse, cola, mux, nullptr);
            if (GstPad* pc = gst_element_get_static_pad(cod, "src")) {
                gst_pad_add_probe(pc, GST_PAD_PROBE_TYPE_BUFFER, &Transmision::alCodificar, this, nullptr);
                gst_object_unref(pc);
            }
        } else {
            for (GstElement* x : {cod, parse, cola})
                if (x != nullptr) gst_object_unref(x);
        }
        if (antes != nullptr) gst_object_unref(antes);
        if (mux != nullptr) gst_object_unref(mux);
        if (!ok) {
            gst_object_unref(p);
            return "no hay codificador de video (" + codificador_ + ")";
        }
        // Lo que tira la cola de antes del codificador (la máquina no da abasto): cuadros perdidos.
        if (GstElement* cv = gst_bin_get_by_name(GST_BIN(p), "cv")) {
            g_signal_connect(cv, "overrun", G_CALLBACK(+[](GstElement*, gpointer d) { ++static_cast<Transmision*>(d)->perdidos_; }), this);
            gst_object_unref(cv);
        }
    }
    if (GstElement* sale = gst_bin_get_by_name(GST_BIN(p), "sale")) {
        if (GstPad* ps = gst_element_get_static_pad(sale, "src")) {
            gst_pad_add_probe(ps, GST_PAD_PROBE_TYPE_BUFFER, &Transmision::alSalir, this, nullptr);
            gst_object_unref(ps);
        }
        gst_object_unref(sale);
    }
    sinRelojPropio(p);
    reloj_.adoptar(p);
    const std::int64_t lat = std::max(fuentes_.latVideoNs, fuentes_.latAudioNs) + kMargenSalidaMs * static_cast<std::int64_t>(GST_MSECOND);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        return "no se pudo conectar con " + sinClave(cfg_.url);
    }
    pipeline_ = p;
    medidoNs_ = ahoraNs();
    bytesAntes_ = bytes_.load();
    return {};
}

void Transmision::desarmar() {
    if (pipeline_ == nullptr) return;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
}

std::string Transmision::iniciar(const Fuentes& f) {
    std::lock_guard<std::mutex> l(mu_);
    if (pedida_) return {};
    fuentes_ = f;
    pedida_ = true;
    intentos_ = 0;
    bytes_ = cuadros_ = perdidos_ = 0;
    error_ = armar();
    if (!error_.empty()) {
        if (error_.find("tiene que empezar") != std::string::npos) {
            pedida_ = false;
            return error_;
        }
        ++intentos_;
        reintentoNs_ = ahoraNs() + static_cast<std::int64_t>(GST_SECOND);
        registrar(Nivel::Aviso, "transmisión: " + error_ + " (se reintenta)");
    } else {
        registrar(Nivel::Info, "transmisión a " + sinClave(cfg_.url) + " (" + (codificador_.empty() ? "solo audio" : codificador_) + ")");
    }
    return {};
}

void Transmision::parar() {
    std::lock_guard<std::mutex> l(mu_);
    pedida_ = false;
    if (pipeline_ != nullptr) {
        // EOS: rtmp2sink manda el cierre al servidor antes de cortar.
        gst_element_send_event(pipeline_, gst_event_new_eos());
        GstBus* bus = gst_element_get_bus(pipeline_);
        if (GstMessage* m = gst_bus_timed_pop_filtered(bus, 2 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))
            gst_message_unref(m);
        gst_object_unref(bus);
    }
    desarmar();
}

bool Transmision::andando() const {
    std::lock_guard<std::mutex> l(mu_);
    return pedida_;
}

bool Transmision::conectada() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipeline_ != nullptr && ultimoDatoNs_ != 0 && ahoraNs() - ultimoDatoNs_ < 2'000'000'000LL;
}

void Transmision::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (!pedida_) return;
    const std::int64_t ahora = ahoraNs();
    if (pipeline_ == nullptr) {
        if (ahora < reintentoNs_) return;
        error_ = armar();
        if (!error_.empty()) {
            ++intentos_;
            static const int kEsperas[] = {1, 2, 4, 8, 10};
            reintentoNs_ = ahora + static_cast<std::int64_t>(kEsperas[std::min(intentos_ - 1, 4)]) * static_cast<std::int64_t>(GST_SECOND);
        }
        return;
    }
    // Lo que sale (kbps), una vez por segundo.
    if (ahora - medidoNs_ >= 1'000'000'000LL) {
        const std::uint64_t b = bytes_.load();
        kbps_ = static_cast<double>(b - bytesAntes_) * 8.0 / 1000.0 / (static_cast<double>(ahora - medidoNs_) / 1e9);
        if (b != bytesAntes_) {
            ultimoDatoNs_ = ahora;
            if (intentos_ > 0) {
                registrar(Nivel::Info, "transmisión: volvió (después de " + std::to_string(intentos_) + " intentos)");
                intentos_ = 0;
            }
        }
        bytesAntes_ = b;
        medidoNs_ = ahora;
    }
    GstBus* bus = gst_element_get_bus(pipeline_);
    bool caida = false;
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR || GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
            GError* e = nullptr;
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? e->message : "se cortó";
            if (e != nullptr) g_error_free(e);
            caida = true;
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    if (caida) {
        registrar(Nivel::Aviso, "transmisión: " + error_ + " (se reconecta)");
        desarmar();
        ++intentos_;
        static const int kEsperas[] = {1, 2, 4, 8, 10};
        reintentoNs_ = ahora + static_cast<std::int64_t>(kEsperas[std::min(intentos_ - 1, 4)]) * static_cast<std::int64_t>(GST_SECOND);
    }
}

json Transmision::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    const bool conectada = pipeline_ != nullptr && ultimoDatoNs_ != 0 && ahoraNs() - ultimoDatoNs_ < 2'000'000'000LL;
    return {{"andando", pedida_},
            {"conectada", conectada},
            {"reconectando", pedida_ && !conectada && intentos_ > 0},
            {"intentos", intentos_},
            {"kbps", std::round(kbps_)},
            {"cuadros", cuadros_.load()},
            {"perdidos", perdidos_.load()},
            {"error", error_},
            {"codificador", codificador_},
            {"url", sinClave(cfg_.url)}};
}

} // namespace ba::motor
