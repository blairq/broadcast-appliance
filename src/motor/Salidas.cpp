// ============================================================================
// src/motor/Salidas.cpp — ver Salidas.hpp
// ============================================================================
#include "Salidas.hpp"
#include "diag/Utf8.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <algorithm>

#include "ajustes/Ajustes.hpp"
#include <atomic>
#include <filesystem>

namespace ba::motor {

namespace {
std::atomic<int> g_reintentoTransmisionMaxS{10};
/// 1, 2, 4, 8… s hasta el tope (Configuración → Transmisión). Un solo lugar: estaba copiado dos veces.
std::int64_t esperaReconexionNs(int intentos) {
    const int tope = g_reintentoTransmisionMaxS.load();
    int s = 1;
    for (int i = 1; i < intentos && s < tope; ++i) s *= 2;
    return static_cast<std::int64_t>(std::min(s, tope)) * static_cast<std::int64_t>(GST_SECOND);
}
} // namespace

void ponerReintentoTransmision(int maxS) { g_reintentoTransmisionMaxS = std::clamp(maxS, 1, 300); }

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
/// Margen de latencia que suman las salidas (Configuración → avanzado: «salidas.margenMs»).
inline std::int64_t margenSalidaNs() { return ajustes::msANs("salidas.margenMs", 300); }

} // namespace

std::string elegirCodificador(const std::vector<std::string>& preferidos) {
    // Con el video en D3D12, primero los que reciben D3D12Memory (sin bajar cada
    // cuadro): en AMD d3d12h264enc antes que AMF (que pide D3D11), en NVIDIA
    // d3d12h264enc antes que NVENC por CUDA. Dentro de la lista pedida (si el
    // usuario eligió uno solo, ese).
    if (gpu() == Gpu::D3D12)
        for (const auto& f : preferidos.empty() ? codificadoresDeLaPlaca() : preferidos)
            if (hayFabrica(f) && aceptaMemoria(f, "D3D12Memory")) return f;
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

// --- Remux ---------------------------------------------------------------------------------

std::string remuxar(const std::string& origen, const std::string& destino, const std::function<void(double)>& progreso) {
    const bool mov = destino.size() > 4 && destino.substr(destino.size() - 4) == ".mov";
    const std::string d = "filesrc location=" + entreComillas(origen) + " ! matroskademux name=d " + (mov ? "qtmux" : "mp4mux") +
                          " name=mux faststart=true ! filesink location=" + entreComillas(destino);
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    GstElement* dmx = gst_bin_get_by_name(GST_BIN(p), "d");
    GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
    struct Ctx {
        GstElement* pipeline;
        GstElement* mux;
    };
    auto* ctx = new Ctx{p, mux};
    // Cada pista que aparece: su parser y al muxer (sin recodificar).
    g_signal_connect_data(dmx, "pad-added",
                          G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer data) {
                              auto* c = static_cast<Ctx*>(data);
                              GstCaps* caps = gst_pad_get_current_caps(pad);
                              const std::string tipo = caps != nullptr ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
                              if (caps != nullptr) gst_caps_unref(caps);
                              const char* parser = tipo == "video/x-h264"   ? "h264parse"
                                                   : tipo == "video/x-h265" ? "h265parse"
                                                   : tipo == "audio/mpeg"   ? "aacparse"
                                                                            : nullptr;
                              if (parser == nullptr) return;
                              GstElement* q = gst_element_factory_make("queue", nullptr);
                              GstElement* pr = gst_element_factory_make(parser, nullptr);
                              gst_bin_add_many(GST_BIN(c->pipeline), q, pr, nullptr);
                              GstPad* entra = gst_element_get_static_pad(q, "sink");
                              gst_pad_link(pad, entra);
                              gst_object_unref(entra);
                              gst_element_link(q, pr);
                              GstPad* sale = gst_element_get_static_pad(pr, "src");
                              GstPad* destinoPad =
                                  gst_element_request_pad_simple(c->mux, tipo.rfind("video", 0) == 0 ? "video_%u" : "audio_%u");
                              if (destinoPad != nullptr) {
                                  gst_pad_link(sale, destinoPad);
                                  gst_object_unref(destinoPad);
                              }
                              gst_object_unref(sale);
                              gst_element_sync_state_with_parent(pr);
                              gst_element_sync_state_with_parent(q);
                          }),
                          ctx, [](gpointer data, GClosure*) { delete static_cast<Ctx*>(data); }, static_cast<GConnectFlags>(0));
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    std::string error;
    for (;;) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 300 * GST_MSECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (m == nullptr) {
            if (progreso) {
                gint64 pos = 0, dur = 0;
                if (gst_element_query_position(p, GST_FORMAT_TIME, &pos) && gst_element_query_duration(p, GST_FORMAT_TIME, &dur) && dur > 0)
                    progreso(std::clamp(static_cast<double>(pos) / static_cast<double>(dur), 0.0, 1.0));
            }
            continue;
        }
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* er = nullptr;
            gst_message_parse_error(m, &er, nullptr);
            error = er != nullptr ? ba::diag::utf8Valido(er->message) : "error";
            if (er != nullptr) g_error_free(er);
        }
        gst_message_unref(m);
        break;
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(dmx);
    gst_object_unref(mux);
    gst_object_unref(p);
    if (error.empty() && progreso) progreso(1.0);
    return error;
}

namespace {
/// Engancha cada pista nueva de un demux (o splitmuxsrc) a su parser y al muxer.
void enlazarPistas(GstElement* origen, GstElement* pipeline, GstElement* mux) {
    struct Ctx {
        GstElement* pipeline;
        GstElement* mux;
    };
    g_signal_connect_data(origen, "pad-added",
                          G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer data) {
                              auto* c = static_cast<Ctx*>(data);
                              GstCaps* caps = gst_pad_get_current_caps(pad);
                              if (caps == nullptr) caps = gst_pad_query_caps(pad, nullptr);
                              const std::string tipo =
                                  caps != nullptr && gst_caps_get_size(caps) > 0 ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
                              if (caps != nullptr) gst_caps_unref(caps);
                              const char* parser = tipo == "video/x-h264"   ? "h264parse"
                                                   : tipo == "video/x-h265" ? "h265parse"
                                                   : tipo == "audio/mpeg"   ? "aacparse"
                                                                            : nullptr;
                              if (parser == nullptr) return;
                              GstElement* q = gst_element_factory_make("queue", nullptr);
                              GstElement* pr = gst_element_factory_make(parser, nullptr);
                              gst_bin_add_many(GST_BIN(c->pipeline), q, pr, nullptr);
                              GstPad* entra = gst_element_get_static_pad(q, "sink");
                              gst_pad_link(pad, entra);
                              gst_object_unref(entra);
                              gst_element_link(q, pr);
                              GstPad* sale = gst_element_get_static_pad(pr, "src");
                              GstPad* destino =
                                  gst_element_request_pad_simple(c->mux, tipo.rfind("video", 0) == 0 ? "video_%u" : "audio_%u");
                              if (destino != nullptr) {
                                  gst_pad_link(sale, destino);
                                  gst_object_unref(destino);
                              }
                              gst_object_unref(sale);
                              gst_element_sync_state_with_parent(pr);
                              gst_element_sync_state_with_parent(q);
                          }),
                          new Ctx{pipeline, mux}, [](gpointer data, GClosure*) { delete static_cast<Ctx*>(data); },
                          static_cast<GConnectFlags>(0));
}

/// Corre un pipeline hasta EOS o error. "" o el error.
std::string correrHastaElFinal(GstElement* p) {
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    std::string error;
    GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(ajustes::sANs("record.replayJuntarS", 120)), static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (m == nullptr) {
        error = "no terminó en 2 minutos";
    } else {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* er = nullptr;
            gst_message_parse_error(m, &er, nullptr);
            error = er != nullptr ? ba::diag::utf8Valido(er->message) : "error";
            if (er != nullptr) g_error_free(er);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    return error;
}
} // namespace

std::string juntar(const std::vector<std::string>& tramos, const std::string& destino) {
    if (tramos.empty()) return "no hay tramos";
    GstElement* p = gst_pipeline_new("juntar");
    GstElement* src = gst_element_factory_make("splitmuxsrc", nullptr);
    GstElement* mux = gst_element_factory_make("matroskamux", nullptr);
    GstElement* sink = gst_element_factory_make("filesink", nullptr);
    if (src == nullptr || mux == nullptr || sink == nullptr) {
        for (GstElement* e : {src, mux, sink})
            if (e != nullptr) gst_object_unref(e);
        gst_object_unref(p);
        return "falta splitmuxsrc o matroskamux";
    }
    g_object_set(sink, "location", destino.c_str(), nullptr);
    // La lista de archivos, en orden (en vez de un patrón).
    auto* lista = new std::vector<std::string>(tramos);
    g_signal_connect_data(src, "format-location",
                          G_CALLBACK(+[](GstElement*, gpointer d) -> gchar** {
                              auto* l = static_cast<std::vector<std::string>*>(d);
                              auto** v = g_new0(gchar*, l->size() + 1);
                              for (std::size_t i = 0; i < l->size(); ++i) v[i] = g_strdup((*l)[i].c_str());
                              return v;
                          }),
                          lista, [](gpointer d, GClosure*) { delete static_cast<std::vector<std::string>*>(d); },
                          static_cast<GConnectFlags>(0));
    gst_bin_add_many(GST_BIN(p), src, mux, sink, nullptr);
    gst_element_link(mux, sink);
    enlazarPistas(src, p, mux);
    const std::string e = correrHastaElFinal(p);
    gst_object_unref(p);
    return e;
}

Repeticion::Repeticion(Reloj& reloj, std::string carpetaTramos, int segundos, int kbps)
    : carpeta_(std::move(carpetaTramos)), segundos_(std::clamp(segundos, 4, 600)) {
    Grabacion::Config c;
    c.carpeta = carpeta_;
    c.nombre = "repeticion";
    c.tramoSeg = std::max(1, ajustes::entero("record.replayTramoS", 2));
    c.gopSeg = 1; // un tramo empieza siempre en un cuadro clave
    c.maxTramos = segundos_ / 2 + 2;
    c.kbps = kbps;
    c.colchonSeg = 10;
    grab_ = std::make_unique<Grabacion>(reloj, c);
}

Repeticion::~Repeticion() {
    grab_.reset();
    std::error_code ec;
    std::filesystem::remove_all(carpeta_, ec);
}

std::string Repeticion::iniciar(const Fuentes& f) { return grab_->iniciar(f); }

bool Repeticion::andando() const { return grab_ && grab_->grabando(); }

void Repeticion::tic() {
    if (grab_) grab_->tic();
}

std::string Repeticion::guardar(const std::string& destino) {
    // Los tramos que siguen en disco, menos el que se está escribiendo (el último).
    std::vector<std::string> hay;
    for (const auto& a : grab_->archivos()) {
        std::error_code ec;
        if (std::filesystem::exists(a, ec)) hay.push_back(a);
    }
    if (hay.size() < 2) return "todavía no hay nada guardado (esperá unos segundos)";
    hay.pop_back();
    const std::size_t n = static_cast<std::size_t>(segundos_ / 2);
    if (hay.size() > n) hay.erase(hay.begin(), hay.end() - static_cast<std::ptrdiff_t>(n));
    return juntar(hay, destino);
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
    // Matroska (aguanta un corte: se puede abrir lo escrito). Tiene que ir en la
    // descripción: las pistas se piden al armar y con «muxer» puesto después ya
    // había creado su mp4mux de fábrica (los archivos salían MP4 con nombre .mkv).
    std::string d = "splitmuxsink name=mux async-finalize=true muxer-factory=matroskamux location=" + entreComillas(patron) +
                    (cfg_.maxTramos > 0 ? " max-files=" + std::to_string(cfg_.maxTramos) : std::string()) + " max-size-time=" +
                    std::to_string(static_cast<guint64>(std::max(0, cfg_.tramoSeg)) * GST_SECOND) + " ";
    // Cada rama: válvula (pausar sin cerrar) ! cola cruda ! codificador ! colchón
    // comprimido (lo que aguanta un disco trabado) ! mux.
    const std::string colchon = " max-size-time=" + std::to_string(static_cast<guint64>(std::max(1, cfg_.colchonSeg)) * GST_SECOND) +
                                " max-size-buffers=0 max-size-bytes=" + std::to_string(static_cast<guint64>(ajustes::entero("record.colchonMB", 512)) << 20) + " ";
    if (!f.video.empty())
        d += "intersrc producer-name=" + entreComillas(f.video) +
             " ! valve name=pausa_v ! queue name=cv leaky=downstream max-size-time=" + std::to_string(ajustes::sANs("record.colaEncoderS", 3)) + " max-size-buffers=0 max-size-bytes=0 ! " +
             aCodificador(elegirCodificador(cfg_.codificadores)) + " ! identity name=antes_cod ";
    for (std::size_t i = 0; i < f.audios.size(); ++i)
        d += "intersrc producer-name=" + entreComillas(f.audios[i]) + " ! valve name=pausa_a" + std::to_string(i) +
             " ! queue leaky=downstream max-size-time=" + std::to_string(ajustes::sANs("record.colaEncoderS", 3)) + " max-size-buffers=0 max-size-bytes=0 ! audioconvert ! "
             "avenc_aac bitrate=" + std::to_string(std::max(64, cfg_.kbpsAudio) * 1000) + " ! aacparse ! queue name=colchon_a" +
             std::to_string(i) + colchon + "! mux.audio_" + std::to_string(i) + " ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        error_ = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return error_;
    }
    GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
    if (!f.video.empty()) {
        codificador_ = elegirCodificador(cfg_.codificadores);
        GstElement* cod = crearCodificador(codificador_, cfg_.kbps, cfg_.gopSeg * std::max(1, cfg_.fps));
        GstElement* parse = gst_element_factory_make("h264parse", nullptr);
        GstElement* antes = gst_bin_get_by_name(GST_BIN(p), "antes_cod");
        GstElement* colV = gst_element_factory_make("queue", "colchon_v");
        if (colV != nullptr)
            g_object_set(colV, "max-size-time", static_cast<guint64>(std::max(1, cfg_.colchonSeg)) * GST_SECOND, "max-size-buffers", 0u,
                         "max-size-bytes", static_cast<guint>(std::min<guint64>(static_cast<guint64>(ajustes::entero("record.colchonMB", 512)) << 20, G_MAXUINT)), nullptr);
        if (cod == nullptr || parse == nullptr || antes == nullptr || colV == nullptr) {
            error_ = "no hay codificador de video (" + codificador_ + ")";
            for (GstElement* x : {cod, parse, antes, colV})
                if (x != nullptr) gst_object_unref(x);
            gst_object_unref(mux);
            gst_object_unref(p);
            return error_;
        }
        gst_bin_add_many(GST_BIN(p), cod, parse, colV, nullptr);
        GstPad* vpad = gst_element_request_pad_simple(mux, "video");
        GstPad* sale = gst_element_get_static_pad(colV, "src");
        const bool ok = gst_element_link_many(antes, cod, parse, colV, nullptr) && gst_pad_link(sale, vpad) == GST_PAD_LINK_OK;
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
    // Las válvulas y los colchones: para pausar, medir el atraso y contar lo escrito.
    desbordes_ = 0;
    bytes_ = 0;
    pausada_ = false;
    {
        std::vector<std::string> valv, colch;
        if (!f.video.empty()) {
            valv.push_back("pausa_v");
            colch.push_back("colchon_v");
        }
        for (std::size_t i = 0; i < f.audios.size(); ++i) {
            valv.push_back("pausa_a" + std::to_string(i));
            colch.push_back("colchon_a" + std::to_string(i));
        }
        for (const auto& n : valv)
            if (GstElement* v = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
                // Que pausada deje pasar el EOS y los segmentos (si no, al parar no cierra).
                if (tiene(v, "drop-mode")) gst_util_set_object_arg(G_OBJECT(v), "drop-mode", "forward-sticky-events");
                valvulas_.push_back(v);
            }
        for (const auto& n : colch)
            if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
                GstPad* sp = gst_element_get_static_pad(q, "src");
                gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                                  +[](GstPad*, GstPadProbeInfo* i, gpointer d) -> GstPadProbeReturn {
                                      static_cast<Grabacion*>(d)->bytes_ += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(i));
                                      return GST_PAD_PROBE_OK;
                                  },
                                  this, nullptr);
                gst_object_unref(sp);
                colchones_.push_back(q);
            }
        // La cola cruda llena = el codificador o el disco no dan abasto: se pierden cuadros.
        if (GstElement* cv = gst_bin_get_by_name(GST_BIN(p), "cv")) {
            g_signal_connect(cv, "overrun", G_CALLBACK(+[](GstElement*, gpointer d) { ++static_cast<Grabacion*>(d)->desbordes_; }), this);
            gst_object_unref(cv);
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
    edadSalida_.ponerEnEntrada(mux, reloj_); // la edad de lo que entra al archivo (medida)
    gst_object_unref(mux);
    sinRelojPropio(p);
    reloj_.adoptar(p);
    const std::int64_t lat = std::max(f.latVideoNs, f.latAudioNs) + margenSalidaNs();
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    latenciaNs_ = lat;
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
    for (GstElement* v : valvulas_) g_object_set(v, "drop", FALSE, nullptr); // pausada: que pase el EOS
    pausada_ = false;
    // EOS por todas las ramas: splitmuxsink cierra el tramo (índice incluido).
    gst_element_send_event(pipeline_, gst_event_new_eos());
    GstBus* bus = gst_element_get_bus(pipeline_);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(esperaMs) * GST_MSECOND,
                                               static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
    if (m != nullptr) gst_message_unref(m);
    gst_object_unref(bus);
    if (!ok) registrar(Nivel::Aviso, "grabación: no cerró a tiempo (el último tramo puede quedar sin índice)");
    edadSalida_.sacar();
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    for (GstElement* e : valvulas_) gst_object_unref(e);
    for (GstElement* e : colchones_) gst_object_unref(e);
    valvulas_.clear();
    colchones_.clear();
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return ok;
}

double Grabacion::atrasoS() const {
    std::lock_guard<std::mutex> l(mu_);
    return static_cast<double>(atrasoNs()) / 1e9;
}

void Grabacion::simularDiscoTrabado(int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (colchones_.empty()) return;
    GstPad* sp = gst_element_get_static_pad(colchones_.front(), "src");
    gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                      +[](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                          g_usleep(static_cast<gulong>(GPOINTER_TO_INT(d)) * 1000);
                          return GST_PAD_PROBE_REMOVE;
                      },
                      GINT_TO_POINTER(ms), nullptr);
    gst_object_unref(sp);
}

void Grabacion::pausar(bool si) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr || pausada_.load() == si) return;
    for (GstElement* v : valvulas_) g_object_set(v, "drop", si ? TRUE : FALSE, nullptr);
    pausada_ = si;
    registrar(Nivel::Info, std::string("grabación ") + cfg_.nombre + (si ? ": en pausa (el disco, para otra)" : ": sigue"));
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
            error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : "error";
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
            {"archivos", archivos()},
            {"atraso_s", static_cast<double>(atrasoNs()) / 1e9},
            {"desbordes", desbordes_.load()},
            {"bytes", bytes_.load()},
            {"pausada", pausada_.load()},
            {"edad_salida", edadSalida_.json()}, // la medida (Tiempos)
            {"latencia_ms", static_cast<double>(latenciaNs_.load()) / 1e6}}; // la fijada al pipeline (Tiempos)
}

guint64 Grabacion::atrasoNs() const {
    guint64 max = 0;
    for (GstElement* q : colchones_) {
        guint64 t = 0;
        g_object_get(q, "current-level-time", &t, nullptr);
        max = std::max(max, t);
    }
    return max;
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
             " ! queue name=cv leaky=downstream max-size-time=" + std::to_string(ajustes::msANs("stream.colaMs", 1000)) + " max-size-buffers=0 max-size-bytes=0 ! " + aCodificador(elegirCodificador(cfg_.codificadores)) +
             " ! identity name=antes_cod ";
    if (!fuentes_.audios.empty())
        d += "intersrc producer-name=" + entreComillas(fuentes_.audios.front()) +
             " ! queue max-size-time=" + std::to_string(ajustes::msANs("stream.colaMs", 1000)) + " max-size-buffers=0 max-size-bytes=0 ! audioconvert ! avenc_aac bitrate=" +
             std::to_string(std::max(64, cfg_.kbpsAudio) * 1000) + " ! aacparse ! queue ! mux. ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = ba::diag::utf8Valido(e->message);
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
        edadSalida_.ponerEnEntrada(sale, reloj_); // la edad de lo que sale a la red (medida)
        gst_object_unref(sale);
    }
    sinRelojPropio(p);
    reloj_.adoptar(p);
    const std::int64_t lat = std::max(fuentes_.latVideoNs, fuentes_.latAudioNs) + margenSalidaNs();
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    latenciaNs_ = lat;
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
    edadSalida_.sacar();
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
        if (GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(ajustes::msANs("stream.cierreMs", 2000)), static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))
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
    return pipeline_ != nullptr && ultimoDatoNs_ != 0 && ahoraNs() - ultimoDatoNs_ < ajustes::sANs("stream.sinDatosS", 2);
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
            reintentoNs_ = ahora + esperaReconexionNs(intentos_);
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
            error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : "se cortó";
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
        reintentoNs_ = ahora + esperaReconexionNs(intentos_);
    }
}

json Transmision::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    const bool conectada = pipeline_ != nullptr && ultimoDatoNs_ != 0 && ahoraNs() - ultimoDatoNs_ < ajustes::sANs("stream.sinDatosS", 2);
    return {{"andando", pedida_},
            {"conectada", conectada},
            {"reconectando", pedida_ && !conectada && intentos_ > 0},
            {"intentos", intentos_},
            {"kbps", std::round(kbps_)},
            {"bytes", bytes_.load()},
            {"cuadros", cuadros_.load()},
            {"perdidos", perdidos_.load()},
            {"error", error_},
            {"codificador", codificador_},
            {"url", sinClave(cfg_.url)},
            {"edad_salida", edadSalida_.json()}, // la medida (Tiempos)
            {"latencia_ms", static_cast<double>(latenciaNs_.load()) / 1e6}}; // la fijada al pipeline (Tiempos)
}

} // namespace ba::motor
