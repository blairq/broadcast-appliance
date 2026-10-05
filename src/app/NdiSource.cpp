// ============================================================================
// src/app/NdiSource.cpp — ver NdiSource.hpp
// ============================================================================

#include "NdiSource.hpp"

#include <obs.h>
#include <util/platform.h>

extern "C" {
#include <extensions/discovery.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <noidea/noidea.h>
}

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif

namespace ba::app::ndi {

namespace {

// --- Descubrimiento (mDNS) -------------------------------------------------------------

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

// --- Instancias ------------------------------------------------------------------------

struct Ctx {
    obs_source_t* source = nullptr;
    std::mutex lock;
    std::string name, host;
    int port = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> restart{false};
    std::thread worker;
    Status status;
    // Decodificador (solo lo usa el thread de recepcion).
    AVCodecContext* dec = nullptr;
    uint32_t decTag = 0;
    int decW = 0, decH = 0;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int frames = 0;
    std::chrono::steady_clock::time_point fpsSince = std::chrono::steady_clock::now();
};

std::mutex g_liveLock;
std::vector<Ctx*> g_live;

video_format toObs(int fmt, video_range_type& range) {
    range = VIDEO_RANGE_PARTIAL;
    switch (fmt) {
        case AV_PIX_FMT_YUV420P: return VIDEO_FORMAT_I420;
        case AV_PIX_FMT_YUV422P: return VIDEO_FORMAT_I422;
        case AV_PIX_FMT_YUV444P: return VIDEO_FORMAT_I444;
        case AV_PIX_FMT_YUVA420P: return VIDEO_FORMAT_I40A;
        case AV_PIX_FMT_YUVA422P: return VIDEO_FORMAT_I42A;
        case AV_PIX_FMT_YUVA444P: return VIDEO_FORMAT_YUVA;
        default: return VIDEO_FORMAT_NONE;
    }
}

bool ensureDecoder(Ctx* c, const noidea_packet_video_t* v) {
    if (c->dec != nullptr && c->decTag == v->fourcc && c->decW == static_cast<int>(v->width) &&
        c->decH == static_cast<int>(v->height))
        return true;
    avcodec_free_context(&c->dec);
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_SPEEDHQ);
    if (codec == nullptr) return false;
    c->dec = avcodec_alloc_context3(codec);
    // SpeedHQ toma el submuestreo (4:2:0/4:2:2/4:4:4, alfa) del fourcc (SHQ0..SHQ9).
    c->dec->codec_tag = v->fourcc;
    c->dec->width = static_cast<int>(v->width);
    c->dec->height = static_cast<int>(v->height);
    c->dec->thread_count = 0; // automatico
    if (avcodec_open2(c->dec, codec, nullptr) < 0) {
        avcodec_free_context(&c->dec);
        return false;
    }
    c->decTag = v->fourcc;
    c->decW = static_cast<int>(v->width);
    c->decH = static_cast<int>(v->height);
    return true;
}

int onPacket(noidea_packet_t* p, void* user) {
    auto* c = static_cast<Ctx*>(user);
    const uint64_t ts = os_gettime_ns();
    if (p->type == NDI_DATA_VIDEO) {
        auto* v = static_cast<noidea_packet_video_t*>(p->packet);
        // SHQ0..SHQ9: todas SpeedHQ ("SHQ" + variante).
        if ((v->fourcc & 0x00FFFFFFu) != (NDI_FOURCC('S', 'H', 'Q', 0) & 0x00FFFFFFu))
            return 0; // HX: no soportado
        if (!ensureDecoder(c, v)) return 0;
        c->pkt->data = v->data;
        c->pkt->size = static_cast<int>(v->size);
        if (avcodec_send_packet(c->dec, c->pkt) < 0) return 0;
        while (avcodec_receive_frame(c->dec, c->frame) == 0) {
            video_range_type range;
            const video_format fmt = toObs(c->frame->format, range);
            if (fmt == VIDEO_FORMAT_NONE) continue;
            obs_source_frame f = {};
            for (int i = 0; i < 4 && i < AV_NUM_DATA_POINTERS; ++i) {
                f.data[i] = c->frame->data[i];
                f.linesize[i] = static_cast<uint32_t>(c->frame->linesize[i]);
            }
            f.width = static_cast<uint32_t>(c->frame->width);
            f.height = static_cast<uint32_t>(c->frame->height);
            f.format = fmt;
            f.timestamp = ts;
            // El color que dice el cuadro; si no dice, el de NDI: 601 en SD, 709 en HD,
            // rango limitado.
            const AVColorSpace m = c->frame->colorspace;
            const video_colorspace cs =
                m == AVCOL_SPC_BT470BG || m == AVCOL_SPC_SMPTE170M ? VIDEO_CS_601
                : m == AVCOL_SPC_BT709                             ? VIDEO_CS_709
                : m == AVCOL_SPC_BT2020_NCL || m == AVCOL_SPC_BT2020_CL
                    ? (c->frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ)
                : c->frame->height < 720 ? VIDEO_CS_601
                                         : VIDEO_CS_709;
            if (c->frame->color_range == AVCOL_RANGE_JPEG) range = VIDEO_RANGE_FULL;
            f.full_range = range == VIDEO_RANGE_FULL;
            f.trc = c->frame->color_trc == AVCOL_TRC_SMPTE2084      ? VIDEO_TRC_PQ
                    : c->frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? VIDEO_TRC_HLG
                                                                    : VIDEO_TRC_DEFAULT;
            video_format_get_parameters_for_format(cs, range, fmt, f.color_matrix, f.color_range_min,
                                                   f.color_range_max);
            obs_source_output_video(c->source, &f);
            ++c->frames;
            std::lock_guard<std::mutex> lk(c->lock);
            c->status.width = f.width;
            c->status.height = f.height;
            const auto now = std::chrono::steady_clock::now();
            const double secs = std::chrono::duration<double>(now - c->fpsSince).count();
            if (secs >= 2.0) {
                c->status.fps = c->frames / secs;
                c->frames = 0;
                c->fpsSince = now;
            }
        }
    } else if (p->type == NDI_DATA_AUDIO) {
        auto* a = static_cast<noidea_packet_audio_t*>(p->packet);
        obs_source_audio au = {};
        const uint32_t ch = std::min<uint32_t>(a->num_channels, MAX_AV_PLANES);
        for (uint32_t i = 0; i < ch; ++i)
            au.data[i] = a->data[i];
        au.frames = a->num_samples;
        au.samples_per_sec = a->sample_rate;
        au.format = a->fourcc == NDI_FOURCC_SOWT ? AUDIO_FORMAT_16BIT_PLANAR : AUDIO_FORMAT_FLOAT_PLANAR;
        au.speakers = ch == 1   ? SPEAKERS_MONO
                      : ch == 2 ? SPEAKERS_STEREO
                      : ch <= 4 ? SPEAKERS_4POINT0
                      : ch <= 6 ? SPEAKERS_5POINT1
                                : SPEAKERS_7POINT1;
        au.timestamp = ts;
        obs_source_output_audio(c->source, &au);
    }
    return 0;
}

void setError(Ctx* c, const std::string& e, bool connected) {
    std::lock_guard<std::mutex> lk(c->lock);
    c->status.error = e;
    c->status.connected = connected;
}

/// Espera el socket (o la orden de parar). false = hay que abandonar.
bool waitSocket(Ctx* c, noidea_recv_ctx_t r) {
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

void receiveLoop(Ctx* c) {
    c->frame = av_frame_alloc();
    c->pkt = av_packet_alloc();
    while (!c->stop) {
        c->restart = false;
        std::string host;
        int port = 0;
        std::string name;
        {
            std::lock_guard<std::mutex> lk(c->lock);
            host = c->host;
            port = c->port;
            name = c->name;
        }
        // Solo el nombre: se busca en la red.
        if (host.empty() && !name.empty()) {
            for (const auto& f : discover(3000))
                if (f.name == name) {
                    host = f.host;
                    port = f.port;
                }
        }
        if (host.empty() || port <= 0) {
            setError(c, name.empty() ? "sin fuente elegida" : "no se encuentra '" + name + "' en la red",
                     false);
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
            if (getsockopt(noidea_recv_get_socket_fd(r), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&st),
                           &len) < 0 ||
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
            setError(c,
                     ok ? "se corto la señal (reintentando)"
                        : "no se pudo conectar a " + host + ":" + std::to_string(port),
                     false);
            // Sin señal: pantalla negra en vez de congelar el ultimo cuadro.
            obs_source_output_video(c->source, nullptr);
            for (int i = 0; i < 8 && !c->stop && !c->restart; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    avcodec_free_context(&c->dec);
    av_frame_free(&c->frame);
    av_packet_free(&c->pkt);
}

const char* getName(void*) { return "NDI® (entrada de red, libNoIDea)"; }

void readSettings(Ctx* c, obs_data_t* s) {
    std::lock_guard<std::mutex> lk(c->lock);
    c->name = obs_data_get_string(s, "ndi_name");
    c->host = obs_data_get_string(s, "host");
    c->port = static_cast<int>(obs_data_get_int(s, "port"));
    c->status.source = c->name;
}

void* create(obs_data_t* settings, obs_source_t* source) {
    auto* c = new Ctx();
    c->source = source;
    // Sin colchon: una camara por NDI es video EN VIVO. El colchon que libobs
    // le pone por defecto a una fuente asincrona empareja la cadencia a costa
    // de varios cuadros de retardo, que es lo contrario de lo que se quiere
    // cuando lo que se ve tiene que ir con lo que pasa en la sala.
    obs_source_set_async_unbuffered(source, true);
    readSettings(c, settings);
    {
        std::lock_guard<std::mutex> lk(g_liveLock);
        g_live.push_back(c);
    }
    c->worker = std::thread(receiveLoop, c);
    return c;
}

void destroy(void* data) {
    auto* c = static_cast<Ctx*>(data);
    {
        std::lock_guard<std::mutex> lk(g_liveLock);
        g_live.erase(std::remove(g_live.begin(), g_live.end(), c), g_live.end());
    }
    c->stop = true;
    if (c->worker.joinable()) c->worker.join();
    delete c;
}

void update(void* data, obs_data_t* settings) {
    auto* c = static_cast<Ctx*>(data);
    readSettings(c, settings);
    c->restart = true;
}

void defaults(obs_data_t* s) {
    obs_data_set_default_string(s, "ndi_name", "");
    obs_data_set_default_string(s, "host", "");
    obs_data_set_default_int(s, "port", 0);
}

obs_properties_t* properties(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_property_t* list = obs_properties_add_list(p, "ndi_name", "Fuente NDI", OBS_COMBO_TYPE_EDITABLE,
                                                   OBS_COMBO_FORMAT_STRING);
    for (const auto& f : discover(1500))
        obs_property_list_add_string(list, f.name.c_str(), f.name.c_str());
    obs_properties_add_text(p, "host", "IP (opcional)", OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "port", "Puerto (opcional)", 0, 65535, 1);
    return p;
}

obs_source_info g_info = {};

} // namespace

std::vector<Found> discover(int ms) {
    static std::mutex runLock;
    static noidea_discovery_ctx_t ctx = nullptr;
    {
        std::lock_guard<std::mutex> lk(runLock);
        if (ctx == nullptr) {
            ctx = noidea_discovery_create(onDiscovered, nullptr);
            if (ctx != nullptr) noidea_discovery_start(ctx); // queda escuchando (mDNS) toda la sesion
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::vector<Found> out;
    std::lock_guard<std::mutex> lk(g_foundLock);
    for (const auto& [n, f] : g_found)
        out.push_back(f);
    return out;
}

std::vector<Status> statuses() {
    std::vector<Status> out;
    std::lock_guard<std::mutex> lk(g_liveLock);
    for (Ctx* c : g_live) {
        std::lock_guard<std::mutex> l2(c->lock);
        out.push_back(c->status);
    }
    return out;
}

void registerSourceType() {
    g_info.id = kSourceId;
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    g_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
    g_info.get_name = getName;
    g_info.create = create;
    g_info.destroy = destroy;
    g_info.update = update;
    g_info.get_defaults = defaults;
    g_info.get_properties = properties;
    g_info.icon_type = OBS_ICON_TYPE_CAMERA;
    obs_register_source(&g_info);
}

} // namespace ba::app::ndi
