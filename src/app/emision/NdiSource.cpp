// ============================================================================
// src/app/emision/NdiSource.cpp — ver NdiSource.hpp
// ============================================================================

#include "emision/NdiSource.hpp"

#include <cstdint> // los encabezados de libNoIDea usan uint16_t sin incluirlo

extern "C" {
#include <extensions/discovery.h>
#include <noidea/noidea.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif

#if defined(BA_HAVE_GST)
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#endif

namespace ba::app::ndi {

namespace {

std::mutex g_foundLock;
std::map<std::string, Found> g_found; // clave: nombre

void onDiscovered(noidea_source_t* s, void*) {
    if (s == nullptr || s->ip == nullptr) return;
    Found f;
    f.name = s->source_name != nullptr ? s->source_name : (s->name != nullptr ? s->name : s->ip);
    f.host = s->ip;
    f.port = s->port;
    std::lock_guard<std::mutex> lk(g_foundLock);
    g_found[f.name] = f;
}

/// Un receptor: conecta (por nombre o ip:puerto), reconecta solo y entrega lo
/// que llega a sus funciones (en su hilo).
struct Receptor {
    std::mutex lock;
    std::string name, host;
    int port = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> restart{false};
    std::thread worker;
    Status status;
    int frames = 0;
    std::chrono::steady_clock::time_point fpsSince = std::chrono::steady_clock::now();
    std::function<void(const noidea_packet_video_t&)> alVideo;
    std::function<void(const noidea_packet_audio_t&)> alAudio;
    std::function<void()> alCortarse; ///< se cortó la señal (pantalla negra en vez de congelar)
};

std::mutex g_liveLock;
std::vector<Receptor*> g_live;

int onPacket(noidea_packet_t* p, void* user) {
    auto* r = static_cast<Receptor*>(user);
    if (p->type == NDI_DATA_VIDEO) {
        auto* v = static_cast<noidea_packet_video_t*>(p->packet);
        // SHQ0..SHQ9: todas SpeedHQ ("SHQ" + variante).
        if ((v->fourcc & 0x00FFFFFFu) != (NDI_FOURCC('S', 'H', 'Q', 0) & 0x00FFFFFFu)) return 0; // HX: no
        if (r->alVideo) r->alVideo(*v);
        ++r->frames;
        std::lock_guard<std::mutex> lk(r->lock);
        r->status.width = static_cast<int>(v->width);
        r->status.height = static_cast<int>(v->height);
        const auto now = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(now - r->fpsSince).count();
        if (secs >= 2.0) {
            r->status.fps = r->frames / secs;
            r->frames = 0;
            r->fpsSince = now;
        }
    } else if (p->type == NDI_DATA_AUDIO) {
        if (r->alAudio) r->alAudio(*static_cast<noidea_packet_audio_t*>(p->packet));
    }
    return 0;
}

void setError(Receptor* r, const std::string& e, bool connected) {
    std::lock_guard<std::mutex> lk(r->lock);
    r->status.error = e;
    r->status.connected = connected;
}

/// Espera el socket (o la orden de parar). false = hay que abandonar.
bool waitSocket(Receptor* c, noidea_recv_ctx_t r) {
#ifdef _WIN32
    WSAPOLLFD fd{};
    fd.fd = static_cast<SOCKET>(noidea_recv_get_socket_fd(r));
    fd.events = static_cast<short>(noidea_recv_which_events(r));
    for (int i = 0; i < 10 && !c->stop && !c->restart; ++i)
        if (WSAPoll(&fd, 1, 500) != 0) return true;
#else
    pollfd fd{};
    fd.fd = noidea_recv_get_socket_fd(r);
    fd.events = static_cast<short>(noidea_recv_which_events(r));
    for (int i = 0; i < 10 && !c->stop && !c->restart; ++i)
        if (poll(&fd, 1, 500) != 0) return true;
#endif
    return false;
}

void receiveLoop(Receptor* c) {
    while (!c->stop) {
        c->restart = false;
        std::string host, name;
        int port = 0;
        {
            std::lock_guard<std::mutex> lk(c->lock);
            host = c->host;
            port = c->port;
            name = c->name;
        }
        // Solo el nombre: se busca en la red (y se sigue buscando si no está:
        // el emisor puede prenderse después).
        if (host.empty() && !name.empty()) {
            for (const auto& f : discover(3000))
                if (f.name == name) {
                    host = f.host;
                    port = f.port;
                }
        }
        if (host.empty() || port <= 0) {
            setError(c, name.empty() ? "sin fuente elegida" : "no se encuentra '" + name + "' en la red (esperando)", false);
            for (int i = 0; i < 20 && !c->stop && !c->restart; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        noidea_opts opts{};
        opts.host = host.c_str();
        opts.port = static_cast<uint16_t>(port);
        opts.initial_tally_state = NDI_TALLY_LIVE;
        noidea_recv_ctx_t r = noidea_recv_create(&opts, onPacket, c, 1);
        bool ok = r != nullptr;
        while (ok && noidea_recv_connect(r) < 0) {
            if (errno != EINPROGRESS || !waitSocket(c, r)) {
                ok = false;
                break;
            }
            int st = 0;
#ifdef _WIN32
            int len = sizeof(st);
#else
            socklen_t len = sizeof(st);
#endif
            if (getsockopt(noidea_recv_get_socket_fd(r), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&st), &len) < 0 ||
                st != 0) {
                ok = false;
                break;
            }
            break;
        }
        while (ok && !c->stop && noidea_recv_send_metadata(r) < 0) {
            if (errno != EAGAIN || !waitSocket(c, r)) ok = false;
        }
        if (ok) {
            {
                std::lock_guard<std::mutex> lk(c->lock);
                c->status.host = host;
                c->status.port = port;
            }
            setError(c, "", true);
            while (!c->stop && !c->restart && noidea_recv_is_connected(r)) {
                if (noidea_recv_wait_capture(r, 500) < 0 && errno != EAGAIN) break;
            }
        }
        if (r != nullptr) noidea_recv_close(r);
        if (!c->stop) {
            setError(c, ok ? "se cortó la señal (reintentando)" : "no se pudo conectar a " + host + ":" + std::to_string(port),
                     false);
            if (c->alCortarse) c->alCortarse();
            for (int i = 0; i < 8 && !c->stop && !c->restart; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
}

Receptor* abrir(const std::string& name, const std::string& host, int port, std::function<void(const noidea_packet_video_t&)> v,
                std::function<void(const noidea_packet_audio_t&)> a, std::function<void()> corte) {
    auto* r = new Receptor();
    r->name = name;
    r->host = host;
    r->port = port;
    r->status.source = name;
    r->alVideo = std::move(v);
    r->alAudio = std::move(a);
    r->alCortarse = std::move(corte);
    {
        std::lock_guard<std::mutex> lk(g_liveLock);
        g_live.push_back(r);
    }
    r->worker = std::thread(receiveLoop, r);
    return r;
}

void cerrar(Receptor* r) {
    if (r == nullptr) return;
    {
        std::lock_guard<std::mutex> lk(g_liveLock);
        g_live.erase(std::remove(g_live.begin(), g_live.end(), r), g_live.end());
    }
    r->stop = true;
    if (r->worker.joinable()) r->worker.join();
    delete r;
}

} // namespace

std::vector<Found> discover(int ms) {
    static std::mutex runLock;
    static noidea_discovery_ctx_t ctx = nullptr;
    {
        std::lock_guard<std::mutex> lk(runLock);
        if (ctx == nullptr) {
            ctx = noidea_discovery_create(onDiscovered, nullptr);
            if (ctx != nullptr) noidea_discovery_start(ctx); // queda escuchando (mDNS) toda la sesión
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::vector<Found> out;
    std::lock_guard<std::mutex> lk(g_foundLock);
    for (const auto& [n, f] : g_found) out.push_back(f);
    return out;
}

std::vector<Status> statuses() {
    std::vector<Status> out;
    std::lock_guard<std::mutex> lk(g_liveLock);
    for (Receptor* c : g_live) {
        std::lock_guard<std::mutex> l2(c->lock);
        out.push_back(c->status);
    }
    return out;
}

void registerSourceType() {}

#if !defined(BA_HAVE_GST)

bool registrarElemento() { return false; }

} // namespace ba::app::ndi

#else

} // namespace ba::app::ndi

// --- bandisrc: el receptor como elemento de GStreamer ---------------------------------
//
// Un GstBin con dos appsrc en vivo y sus pads «video» (video/x-speedhq con su
// variante: lo decodifica avdec_speedhq) y «audio» (F32 intercalado). La hora
// de cada cosa: la del reloj del pipeline al llegar. Sin señal: no empuja nada
// (la entrada deja de recibir y queda en negro/silencio).

G_BEGIN_DECLS
#define BA_TYPE_NDI_SRC (ba_ndi_src_get_type())
G_DECLARE_FINAL_TYPE(BaNdiSrc, ba_ndi_src, BA, NDI_SRC, GstBin)
G_END_DECLS

struct _BaNdiSrc {
    GstBin parent;
    GstElement* video;
    GstElement* audio;
    std::mutex* mu;
    std::string* fuente;
    std::string* host;
    int port;
    ba::app::ndi::Receptor* rx;
    std::string* capsVideo; ///< las últimas puestas (para no renegociar en cada cuadro)
    std::string* capsAudio;
};

G_DEFINE_TYPE(BaNdiSrc, ba_ndi_src, GST_TYPE_BIN)

enum { PROP_0, PROP_FUENTE, PROP_HOST, PROP_PORT };

static GstClockTime ahoraEn(GstElement* e) {
    GstClock* reloj = gst_element_get_clock(e);
    if (reloj == nullptr) return GST_CLOCK_TIME_NONE;
    const GstClockTime t = gst_clock_get_time(reloj), base = gst_element_get_base_time(e);
    gst_object_unref(reloj);
    return t >= base ? t - base : 0;
}

static void alVideo(BaNdiSrc* s, const noidea_packet_video_t& v) {
    char variante[5] = {static_cast<char>(v.fourcc & 0xFF), static_cast<char>((v.fourcc >> 8) & 0xFF),
                        static_cast<char>((v.fourcc >> 16) & 0xFF), static_cast<char>((v.fourcc >> 24) & 0xFF), 0};
    const int fn = v.fps_num > 0 ? static_cast<int>(v.fps_num) : 30, fd = v.fps_den > 0 ? static_cast<int>(v.fps_den) : 1;
    const std::string c = "video/x-speedhq, variant=(string)" + std::string(variante) + ", width=(int)" + std::to_string(v.width) +
                          ", height=(int)" + std::to_string(v.height) + ", framerate=(fraction)" + std::to_string(fn) + "/" +
                          std::to_string(fd);
    if (c != *s->capsVideo) {
        GstCaps* caps = gst_caps_from_string(c.c_str());
        gst_app_src_set_caps(GST_APP_SRC(s->video), caps);
        gst_caps_unref(caps);
        *s->capsVideo = c;
    }
    GstBuffer* b = gst_buffer_new_allocate(nullptr, v.size, nullptr);
    gst_buffer_fill(b, 0, v.data, v.size);
    GST_BUFFER_PTS(b) = ahoraEn(GST_ELEMENT(s));
    GST_BUFFER_DURATION(b) = gst_util_uint64_scale(GST_SECOND, fd, fn);
    gst_app_src_push_buffer(GST_APP_SRC(s->video), b);
}

static void alAudio(BaNdiSrc* s, const noidea_packet_audio_t& a) {
    const uint32_t ch = std::min<uint32_t>(a.num_channels, NDI_NUM_DATA_POINTERS), n = a.num_samples;
    if (ch == 0 || n == 0 || a.sample_rate == 0) return;
    const std::string c = "audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)" +
                          std::to_string(a.sample_rate) + ", channels=(int)" + std::to_string(ch);
    if (c != *s->capsAudio) {
        GstCaps* caps = gst_caps_from_string(c.c_str());
        gst_app_src_set_caps(GST_APP_SRC(s->audio), caps);
        gst_caps_unref(caps);
        *s->capsAudio = c;
    }
    GstBuffer* b = gst_buffer_new_allocate(nullptr, static_cast<gsize>(n) * ch * sizeof(float), nullptr);
    GstMapInfo m;
    gst_buffer_map(b, &m, GST_MAP_WRITE);
    auto* o = reinterpret_cast<float*>(m.data);
    const bool s16 = a.fourcc == NDI_FOURCC_SOWT;
    // NDI manda planar: se intercala (y los enteros de 16 bits se pasan a float).
    for (uint32_t k = 0; k < ch; ++k)
        for (uint32_t i = 0; i < n; ++i)
            o[i * ch + k] = s16 ? reinterpret_cast<const int16_t*>(a.data[k])[i] / 32768.0f
                                : reinterpret_cast<const float*>(a.data[k])[i];
    gst_buffer_unmap(b, &m);
    GST_BUFFER_PTS(b) = ahoraEn(GST_ELEMENT(s));
    GST_BUFFER_DURATION(b) = gst_util_uint64_scale(n, GST_SECOND, a.sample_rate);
    gst_app_src_push_buffer(GST_APP_SRC(s->audio), b);
}

static GstStateChangeReturn cambiarEstado(GstElement* e, GstStateChange t) {
    auto* s = BA_NDI_SRC(e);
    if (t == GST_STATE_CHANGE_READY_TO_PAUSED) {
        std::string f, h;
        int p;
        {
            std::lock_guard<std::mutex> l(*s->mu);
            f = *s->fuente;
            h = *s->host;
            p = s->port;
        }
        s->capsVideo->clear();
        s->capsAudio->clear();
        s->rx = ba::app::ndi::abrir(
            f, h, p, [s](const noidea_packet_video_t& v) { alVideo(s, v); },
            [s](const noidea_packet_audio_t& a) { alAudio(s, a); }, nullptr);
    }
    const GstStateChangeReturn r = GST_ELEMENT_CLASS(ba_ndi_src_parent_class)->change_state(e, t);
    if (t == GST_STATE_CHANGE_PAUSED_TO_READY) {
        ba::app::ndi::cerrar(s->rx);
        s->rx = nullptr;
    }
    // En vivo: sin preroll.
    if (r == GST_STATE_CHANGE_SUCCESS && (t == GST_STATE_CHANGE_READY_TO_PAUSED || t == GST_STATE_CHANGE_PLAYING_TO_PAUSED))
        return GST_STATE_CHANGE_NO_PREROLL;
    return r;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_NDI_SRC(o);
    std::lock_guard<std::mutex> l(*s->mu);
    switch (id) {
        case PROP_FUENTE: *s->fuente = g_value_get_string(v) != nullptr ? g_value_get_string(v) : ""; break;
        case PROP_HOST: *s->host = g_value_get_string(v) != nullptr ? g_value_get_string(v) : ""; break;
        case PROP_PORT: s->port = g_value_get_int(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_NDI_SRC(o);
    std::lock_guard<std::mutex> l(*s->mu);
    switch (id) {
        case PROP_FUENTE: g_value_set_string(v, s->fuente->c_str()); break;
        case PROP_HOST: g_value_set_string(v, s->host->c_str()); break;
        case PROP_PORT: g_value_set_int(v, s->port); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* s = BA_NDI_SRC(o);
    ba::app::ndi::cerrar(s->rx);
    delete s->mu;
    delete s->fuente;
    delete s->host;
    delete s->capsVideo;
    delete s->capsAudio;
    G_OBJECT_CLASS(ba_ndi_src_parent_class)->finalize(o);
}

static GstStaticPadTemplate videoTpl = GST_STATIC_PAD_TEMPLATE("video", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-speedhq"));
static GstStaticPadTemplate audioTpl = GST_STATIC_PAD_TEMPLATE("audio", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("audio/x-raw"));

static void ba_ndi_src_class_init(BaNdiSrcClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    ge->change_state = cambiarEstado;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_FUENTE, g_param_spec_string("fuente", "Fuente", "El nombre NDI (se busca en la red)", "", rw));
    g_object_class_install_property(go, PROP_HOST, g_param_spec_string("host", "IP", "IP del emisor (opcional)", "", rw));
    g_object_class_install_property(go, PROP_PORT, g_param_spec_int("port", "Puerto", "Puerto del emisor (opcional)", 0, 65535, 0, rw));
    gst_element_class_set_static_metadata(ge, "Entrada NDI (libNoIDea)", "Source/Video/Audio/Network",
                                          "Recibe NDI (SpeedHQ) sin el SDK propietario; NDI es marca de Vizrt NDI AB",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &videoTpl);
    gst_element_class_add_static_pad_template(ge, &audioTpl);
}

static void ba_ndi_src_init(BaNdiSrc* s) {
    s->mu = new std::mutex();
    s->fuente = new std::string();
    s->host = new std::string();
    s->port = 0;
    s->rx = nullptr;
    s->capsVideo = new std::string();
    s->capsAudio = new std::string();
    for (int i = 0; i < 2; ++i) {
        GstElement* a = gst_element_factory_make("appsrc", i == 0 ? "video" : "audio");
        g_object_set(a, "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "max-buffers",
                     static_cast<guint64>(i == 0 ? 4 : 64), "block", FALSE, nullptr);
        // Si atrás se atrasan, se tira lo viejo (en vivo) en vez de acumular demora.
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(a), "leaky-type") != nullptr)
            gst_util_set_object_arg(G_OBJECT(a), "leaky-type", "downstream");
        gst_bin_add(GST_BIN(s), a);
        GstPad* p = gst_element_get_static_pad(a, "src");
        GstPadTemplate* tpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(s), i == 0 ? "video" : "audio");
        gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template(i == 0 ? "video" : "audio", p, tpl));
        gst_object_unref(p);
        (i == 0 ? s->video : s->audio) = a;
    }
}

namespace ba::app::ndi {

bool registrarElemento() {
    static std::once_flag una;
    static std::atomic<bool> ok{false};
    std::call_once(una, []() {
        if (gst_is_initialized() && gst_element_register(nullptr, "bandisrc", GST_RANK_NONE, BA_TYPE_NDI_SRC)) ok = true;
    });
    return ok.load();
}

} // namespace ba::app::ndi

#endif
