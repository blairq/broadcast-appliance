// ============================================================================
// src/obs-module/ba_gst_fuente.cpp — Un pipeline de GStreamer como entrada de video (y audio)
//
// FUENTE `ba_gst_fuente`. Dos formas de decir qué entra:
//   - `uri`: srt://…, rtmp://…, rtsp://…, udp://…, http(s)://… o un archivo.
//     Se arma `uridecodebin3` con una rama de video y otra de audio. En RTSP
//     se pide `ntp-sync` (los cuadros llegan con la hora de CAPTURA del que
//     manda, por RTCP) y la latencia de su jitter buffer.
//   - `pipeline`: la parte de origen de un pipeline, como en gst-launch
//     («videotestsrc pattern=smpte is-live=true»). Solo video.
//
// SINCRONÍA. Los cuadros NO se entregan a libobs al llegar: van a un colchón
// (el jitter buffer de esta fuente) y cada uno se presenta cuando le toca:
//     hora de captura (en el reloj del pipeline) + `latencia` + `desfase`.
// Si dos fuentes usan el mismo reloj y la misma latencia, lo que se capturó
// a la vez sale a la vez; `desfase` corrige lo que no se puede saber solo
// (RTMP y SRT no traen la hora absoluta de captura: se mide y se ajusta).
//
// EL RELOJ del pipeline (`reloj`):
//   sistema  el monotónico de la máquina (de fábrica)
//   ntp      un servidor NTP (`reloj_servidor`, `reloj_puerto`)
//   ptp      PTP/IEEE 1588 (`reloj_dominio`); necesita gst-ptp-helper con permisos
//   red      el reloj que publica otro aparato (GstNetTimeProvider; ver
//            ba::gst::publicarReloj), para que varios estudios marchen juntos
// Los relojes se comparten: dos fuentes con el mismo NTP usan el mismo objeto.
//
// EL COLOR: el video llega en su formato (NV12, I420, BGRA…) con su
// colorimetría (BT.601/709, rango limitado o completo) y así se le pasa a
// libobs, que convierte bien y en la GPU.
//
// El proc `estado` da todo lo que hace falta para diagnosticar: si anda, el
// error, el reloj y si está sincronizado, resolución, cuadros por segundo,
// formato, colorimetría, el colchón y cuántos cuadros llegaron tarde.
// ============================================================================

#if defined(BA_HAVE_GST)

#include "ba_gst.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/net/net.h>
#include <gst/video/video.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <new>
#include <string>

namespace {

// --- Relojes compartidos ----------------------------------------------------------

std::mutex g_relojesLock;
std::map<std::string, GstClock*>& relojes() {
    static std::map<std::string, GstClock*> m;
    return m;
}

/// El reloj pedido (con referencia), o nullptr con el motivo.
GstClock* relojDe(const std::string& tipo, const std::string& servidor, int puerto, int dominio, std::string& error) {
    const std::string clave = tipo + "|" + servidor + "|" + std::to_string(puerto) + "|" + std::to_string(dominio);
    std::lock_guard<std::mutex> l(g_relojesLock);
    auto it = relojes().find(clave);
    if (it != relojes().end()) return GST_CLOCK(gst_object_ref(it->second));
    GstClock* c = nullptr;
    if (tipo.empty() || tipo == "sistema") {
        c = gst_system_clock_obtain(); // monotónico
    } else if (tipo == "ntp") {
        if (servidor.empty()) {
            error = "falta `reloj_servidor` (el servidor NTP)";
            return nullptr;
        }
        c = gst_ntp_clock_new("ba-ntp", servidor.c_str(), puerto > 0 ? puerto : 123, 0);
    } else if (tipo == "red") {
        if (servidor.empty()) {
            error = "falta `reloj_servidor` (el aparato que publica el reloj)";
            return nullptr;
        }
        c = gst_net_client_clock_new("ba-red", servidor.c_str(), puerto > 0 ? puerto : 8554, 0);
    } else if (tipo == "ptp") {
        static bool ptpIniciado = false;
        if (!ptpIniciado) ptpIniciado = gst_ptp_init(GST_PTP_CLOCK_ID_NONE, nullptr);
        if (!ptpIniciado) {
            error = "PTP no arrancó: necesita gst-ptp-helper con permisos para los puertos 319/320";
            return nullptr;
        }
        c = gst_ptp_clock_new("ba-ptp", static_cast<guint>(std::max(0, dominio)));
    } else {
        error = "reloj desconocido «" + tipo + "» (sistema, ntp, ptp o red)";
        return nullptr;
    }
    if (c == nullptr) {
        error = "no se pudo crear el reloj " + tipo;
        return nullptr;
    }
    relojes()[clave] = GST_CLOCK(gst_object_ref(c));
    return c;
}

// --- La fuente ---------------------------------------------------------------------

struct Pendiente {
    GstSample* muestra = nullptr;
    GstClockTime vence = 0; ///< cuándo presentarla, en el reloj del pipeline
};

struct Fuente {
    obs_source_t* ctx = nullptr;
    std::mutex lock;
    // Lo pedido
    std::string uri, descripcion, reloj, relojServidor;
    int relojPuerto = 0, relojDominio = 0;
    std::int64_t latenciaMs = 0, desfaseMs = 0;
    bool conAudio = true;
    bool normalizar = true;
    bool ntpSync = true; ///< RTSP: usar la hora de captura del que manda (RTCP); si su reloj está mal, conviene apagarlo
    // Lo armado
    std::string error;
    GstElement* pipeline = nullptr;
    GstClock* clock = nullptr;
    std::atomic<std::uint64_t> reintento{0}; ///< cuándo volver a armar (os_gettime_ns)
    // El colchón (lo llenan los hilos de GStreamer, lo vacía el tic)
    std::mutex colaLock;
    std::deque<Pendiente> video, audio;
    // Para diagnosticar
    std::uint64_t recibidos = 0, tarde = 0, saltados = 0, descartados = 0;
    std::int64_t colchonMs = 0; ///< cuánto falta para el último cuadro del colchón
    double adelantoMs = 0;      ///< promedio de cuánto le falta a cada cuadro al llegar (≈ latencia si el reloj está bien)
    std::uint64_t ventanaNs = 0, tardeVentana = 0, recibidosVentana = 0, tarde10 = 0, recibidos10 = 0;
    std::uint64_t primeroNs = 0, cuentaFps = 0;
    double fpsMedido = 0;
    std::string formato, colorimetria;
    int ancho = 0, alto = 0;
    bool entrelazado = false;
    double fpsDeclarado = 0;
    std::string origen; ///< lo que llega, ANTES de normalizar (JSON: formato, tamaño, fps, color…)
};

/// Las caps de lo que llega, en castellano y por campo (para el diagnóstico).
std::string describirOrigen(GstCaps* c) {
    GstVideoInfo info;
    if (c == nullptr || !gst_video_info_from_caps(&info, c)) return {};
    obs_data_t* o = obs_data_create();
    obs_data_set_string(o, "formato", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info)));
    obs_data_set_int(o, "ancho", GST_VIDEO_INFO_WIDTH(&info));
    obs_data_set_int(o, "alto", GST_VIDEO_INFO_HEIGHT(&info));
    const int fn = GST_VIDEO_INFO_FPS_N(&info), fd = GST_VIDEO_INFO_FPS_D(&info);
    obs_data_set_double(o, "fps", fn > 0 && fd > 0 ? static_cast<double>(fn) / fd : 0.0);
    obs_data_set_bool(o, "fps_variable", fn == 0);
    obs_data_set_bool(o, "entrelazado", GST_VIDEO_INFO_IS_INTERLACED(&info));
    const int pn = GST_VIDEO_INFO_PAR_N(&info), pd = GST_VIDEO_INFO_PAR_D(&info);
    obs_data_set_string(o, "pixel", pn == pd ? "cuadrado" : (std::to_string(pn) + "/" + std::to_string(pd) + " (no cuadrado)").c_str());
    const GstVideoColorimetry& k = GST_VIDEO_INFO_COLORIMETRY(&info);
    const char* matriz = k.matrix == GST_VIDEO_COLOR_MATRIX_BT601   ? "BT.601"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT709 ? "BT.709"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT2020 ? "BT.2020"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_RGB   ? "RGB"
                                                                    : "sin declarar";
    obs_data_set_string(o, "matriz", matriz);
    obs_data_set_string(o, "rango", k.range == GST_VIDEO_COLOR_RANGE_0_255     ? "completo"
                                    : k.range == GST_VIDEO_COLOR_RANGE_16_235 ? "limitado"
                                                                              : "sin declarar");
    obs_data_set_bool(o, "hdr", k.transfer == GST_VIDEO_TRANSFER_SMPTE2084 || k.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67);
    const std::string r = obs_data_get_json(o);
    obs_data_release(o);
    return r;
}

constexpr size_t kMaxColchon = 300; ///< ~10 s a 30 fps: más es un error de reloj, no un colchón

void vaciar(Fuente* f) {
    std::lock_guard<std::mutex> l(f->colaLock);
    for (auto& p : f->video) gst_sample_unref(p.muestra);
    for (auto& p : f->audio) gst_sample_unref(p.muestra);
    f->video.clear();
    f->audio.clear();
}

/// Cuándo hay que presentar esta muestra: su hora de captura en el reloj del
/// pipeline, más la latencia y el desfase.
GstClockTime venceDe(Fuente* f, GstSample* s) {
    GstBuffer* b = gst_sample_get_buffer(s);
    const GstSegment* seg = gst_sample_get_segment(s);
    const GstClockTime base = f->pipeline != nullptr ? gst_element_get_base_time(f->pipeline) : 0;
    GstClockTime t = GST_CLOCK_TIME_NONE;
    if (b != nullptr && GST_BUFFER_PTS_IS_VALID(b) && seg != nullptr)
        t = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
    // Sin hora (algunas fuentes no la ponen): la de llegada.
    const GstClockTime ahora = gst_clock_get_time(f->clock);
    const GstClockTime captura = GST_CLOCK_TIME_IS_VALID(t) ? base + t : ahora;
    const std::int64_t extra = (f->latenciaMs + f->desfaseMs) * static_cast<std::int64_t>(GST_MSECOND);
    const std::int64_t v = static_cast<std::int64_t>(captura) + extra;
    return v > 0 ? static_cast<GstClockTime>(v) : 0;
}

GstFlowReturn alVideo(GstAppSink* sink, gpointer datos) {
    auto* f = static_cast<Fuente*>(datos);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (s == nullptr) return GST_FLOW_OK;
    if (f->clock == nullptr) {
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    const GstClockTime vence = venceDe(f, s);
    const GstClockTime ahora = gst_clock_get_time(f->clock);
    std::lock_guard<std::mutex> l(f->colaLock);
    ++f->recibidos;
    ++f->recibidosVentana;
    if (vence + 40 * GST_MSECOND < ahora) {
        ++f->tarde; // llegó cuando ya tenía que estar en pantalla
        ++f->tardeVentana;
    }
    // Cuánto le falta al llegar (promedio móvil): con el reloj bien, ronda la
    // latencia; mucho más es que la hora de captura del que manda está corrida.
    const double falta = (static_cast<double>(vence) - static_cast<double>(ahora)) / 1e6;
    f->adelantoMs = f->recibidos == 1 ? falta : f->adelantoMs * 0.95 + falta * 0.05;
    // Ventanas de 10 s: lo de ahora, no lo del arranque.
    const std::uint64_t nv = os_gettime_ns();
    if (f->ventanaNs == 0) f->ventanaNs = nv;
    if (nv - f->ventanaNs >= 10'000'000'000ULL) {
        f->tarde10 = f->tardeVentana;
        f->recibidos10 = f->recibidosVentana;
        f->tardeVentana = f->recibidosVentana = 0;
        f->ventanaNs = nv;
    }
    f->video.push_back({s, vence});
    while (f->video.size() > kMaxColchon) {
        gst_sample_unref(f->video.front().muestra);
        f->video.pop_front();
        ++f->descartados;
    }
    // Cuadros por segundo que llegan, medidos.
    const std::uint64_t n = os_gettime_ns();
    if (f->primeroNs == 0) f->primeroNs = n;
    ++f->cuentaFps;
    if (n - f->primeroNs >= 2'000'000'000ULL) {
        f->fpsMedido = static_cast<double>(f->cuentaFps) * 1e9 / static_cast<double>(n - f->primeroNs);
        f->primeroNs = n;
        f->cuentaFps = 0;
    }
    return GST_FLOW_OK;
}

GstFlowReturn alAudio(GstAppSink* sink, gpointer datos) {
    auto* f = static_cast<Fuente*>(datos);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (s == nullptr) return GST_FLOW_OK;
    if (f->clock == nullptr) {
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    const GstClockTime vence = venceDe(f, s);
    std::lock_guard<std::mutex> l(f->colaLock);
    f->audio.push_back({s, vence});
    while (f->audio.size() > kMaxColchon * 4) {
        gst_sample_unref(f->audio.front().muestra);
        f->audio.pop_front();
    }
    return GST_FLOW_OK;
}

/// GStreamer -> libobs: el formato.
video_format formatoObs(GstVideoFormat g) {
    switch (g) {
    case GST_VIDEO_FORMAT_NV12: return VIDEO_FORMAT_NV12;
    case GST_VIDEO_FORMAT_I420: return VIDEO_FORMAT_I420;
    case GST_VIDEO_FORMAT_YUY2: return VIDEO_FORMAT_YUY2;
    case GST_VIDEO_FORMAT_UYVY: return VIDEO_FORMAT_UYVY;
    case GST_VIDEO_FORMAT_BGRA: return VIDEO_FORMAT_BGRA;
    case GST_VIDEO_FORMAT_BGRx: return VIDEO_FORMAT_BGRX;
    case GST_VIDEO_FORMAT_RGBA: return VIDEO_FORMAT_RGBA;
    default: return VIDEO_FORMAT_NONE;
    }
}

void presentarVideo(Fuente* f, GstSample* s) {
    GstCaps* caps = gst_sample_get_caps(s);
    GstVideoInfo info;
    if (caps == nullptr || !gst_video_info_from_caps(&info, caps)) return;
    const video_format fmt = formatoObs(GST_VIDEO_INFO_FORMAT(&info));
    if (fmt == VIDEO_FORMAT_NONE) return;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstVideoFrame vf;
    if (b == nullptr || !gst_video_frame_map(&vf, &info, b, GST_MAP_READ)) return;
    obs_source_frame frame = {};
    for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES(&vf) && i < MAX_AV_PLANES; ++i) {
        frame.data[i] = static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&vf, i));
        frame.linesize[i] = static_cast<uint32_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&vf, i));
    }
    frame.width = static_cast<uint32_t>(GST_VIDEO_INFO_WIDTH(&info));
    frame.height = static_cast<uint32_t>(GST_VIDEO_INFO_HEIGHT(&info));
    frame.format = fmt;
    frame.timestamp = os_gettime_ns(); // se presenta ahora: la hora ya la decidió el colchón
    const GstVideoColorimetry& c = GST_VIDEO_INFO_COLORIMETRY(&info);
    const bool rgb = GST_VIDEO_INFO_IS_RGB(&info);
    const bool completo = rgb || c.range == GST_VIDEO_COLOR_RANGE_0_255;
    video_colorspace cs = VIDEO_CS_709;
    if (rgb)
        cs = VIDEO_CS_SRGB;
    else if (c.matrix == GST_VIDEO_COLOR_MATRIX_BT601)
        cs = VIDEO_CS_601;
    frame.full_range = completo;
    video_format_get_parameters_for_format(cs, completo ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, fmt, frame.color_matrix,
                                           frame.color_range_min, frame.color_range_max);
    obs_source_output_video(f->ctx, &frame);
    gst_video_frame_unmap(&vf);
    // Lo que se informa (el tic ya tiene `lock`).
    gchar* col = gst_video_colorimetry_to_string(&c);
    f->formato = gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info));
    f->colorimetria = std::string(col != nullptr ? col : "") + (completo ? " (rango completo)" : " (rango limitado)");
    g_free(col);
    f->ancho = static_cast<int>(frame.width);
    f->alto = static_cast<int>(frame.height);
    f->entrelazado = GST_VIDEO_INFO_IS_INTERLACED(&info);
    f->fpsDeclarado = GST_VIDEO_INFO_FPS_D(&info) > 0 ? static_cast<double>(GST_VIDEO_INFO_FPS_N(&info)) / GST_VIDEO_INFO_FPS_D(&info) : 0;
}

void presentarAudio(Fuente* f, GstSample* s) {
    if (obs_get_audio() == nullptr) return; // sin salida de audio (p. ej. un test): no hay a dónde mandarlo
    GstCaps* caps = gst_sample_get_caps(s);
    GstAudioInfo info;
    if (caps == nullptr || !gst_audio_info_from_caps(&info, caps)) return;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo m;
    if (b == nullptr || !gst_buffer_map(b, &m, GST_MAP_READ)) return;
    obs_source_audio a = {};
    a.data[0] = m.data;
    a.frames = static_cast<uint32_t>(m.size / static_cast<gsize>(GST_AUDIO_INFO_BPF(&info)));
    a.speakers = GST_AUDIO_INFO_CHANNELS(&info) == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
    a.format = AUDIO_FORMAT_FLOAT;
    a.samples_per_sec = static_cast<uint32_t>(GST_AUDIO_INFO_RATE(&info));
    a.timestamp = os_gettime_ns();
    obs_source_output_audio(f->ctx, &a);
    gst_buffer_unmap(b, &m);
}

void desarmar(Fuente* f) {
    if (f->pipeline != nullptr) {
        gst_element_set_state(f->pipeline, GST_STATE_NULL);
        gst_object_unref(f->pipeline);
        f->pipeline = nullptr;
    }
    vaciar(f);
    if (f->clock != nullptr) {
        gst_object_unref(f->clock);
        f->clock = nullptr;
    }
}

/// RTSP y SRT: la latencia de SU jitter buffer, y en RTSP la hora de captura del que manda.
void alPrepararOrigen(GstElement*, GstElement* origen, gpointer datos) {
    auto* f = static_cast<Fuente*>(datos);
    GObjectClass* k = G_OBJECT_GET_CLASS(origen);
    const std::string lat = std::to_string(std::max<std::int64_t>(f->latenciaMs, 20));
    if (g_object_class_find_property(k, "latency") != nullptr) gst_util_set_object_arg(G_OBJECT(origen), "latency", lat.c_str());
    if (g_object_class_find_property(k, "ntp-sync") != nullptr)
        gst_util_set_object_arg(G_OBJECT(origen), "ntp-sync", f->ntpSync ? "true" : "false");
    if (g_object_class_find_property(k, "buffer-mode") != nullptr)
        gst_util_set_object_arg(G_OBJECT(origen), "buffer-mode", "synced");
}

void enganchar(Fuente* f, GstElement* p, const char* nombre, GstFlowReturn (*cb)(GstAppSink*, gpointer)) {
    GstElement* sink = gst_bin_get_by_name(GST_BIN(p), nombre);
    if (sink == nullptr) return;
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = cb;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, f, nullptr);
    gst_object_unref(sink);
}

void armar(Fuente* f) {
    desarmar(f);
    f->error.clear();
    if (!ba::gst::listo()) {
        if (ba::gst::fallo()) {
            f->error = ba::gst::errorInicio();
        } else {
            f->error = "GStreamer se está iniciando";
            f->reintento = os_gettime_ns() + 250'000'000ULL;
        }
        return;
    }
    if (f->uri.empty() && f->descripcion.empty()) return;
    f->clock = relojDe(f->reloj, f->relojServidor, f->relojPuerto, f->relojDominio, f->error);
    if (f->clock == nullptr) return;
    // Los sinks NO esperan al reloj (sync=false): la hora la pone el colchón.
    // async=false: una rama sin datos (un stream sin audio) no traba el arranque.
    const char* sinkVideo = "appsink name=ba_video sync=false async=false max-buffers=8 drop=false emit-signals=false";
    const char* sinkAudio = "appsink name=ba_audio sync=false async=false max-buffers=64 drop=false emit-signals=false";
    // NORMALIZAR (de fábrica): GStreamer deja todo igual antes de libobs
    // —desentrelaza solo si viene entrelazado (`deinterlace mode=auto`), lleva
    // a los cuadros del lienzo (`videorate`) y al color del estudio: NV12,
    // BT.709, rango limitado—. Así una cámara rara (JPEG de rango completo,
    // 601 en HD, 25i) entra como las demás. Sin normalizar, pasa tal cual.
    std::string capsVideo = "video/x-raw,format={NV12,I420,BGRA,BGRx,RGBA,UYVY,YUY2}";
    std::string antesSink = "videoconvert ! ";
    if (f->normalizar) {
        obs_video_info ovi = {};
        const bool hay = obs_get_video_info(&ovi) && ovi.fps_num > 0;
        antesSink = "deinterlace mode=auto ! videorate ! videoconvertscale ! ";
        // pixel-aspect-ratio=1/1: libobs no sabe de píxeles no cuadrados (una
        // cámara PAL de 704x576 con píxel 12/11 se vería apretada): se escala.
        capsVideo = "video/x-raw,format=NV12,colorimetry=bt709,interlace-mode=progressive,pixel-aspect-ratio=1/1";
        if (hay) capsVideo += ",framerate=" + std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den);
    }
    std::string completo;
    if (!f->uri.empty()) {
        completo = "uridecodebin3 name=ba_dec uri=\"" + f->uri + "\" ba_dec. ! queue name=ba_origen ! " + antesSink + capsVideo +
                   " ! " + sinkVideo;
        if (f->conAudio)
            completo += std::string(" ba_dec. ! queue ! audioconvert ! audioresample ! "
                                    "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! ") +
                        sinkAudio;
    } else {
        completo = f->descripcion + " ! queue name=ba_origen ! " + antesSink + capsVideo + " ! " + sinkVideo;
    }
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(completo.c_str(), &err);
    if (err != nullptr) {
        f->error = err->message;
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        blog(LOG_WARNING, "[ba-sources] fuente GStreamer: %s", f->error.c_str());
        return;
    }
    if (GstElement* dec = gst_bin_get_by_name(GST_BIN(p), "ba_dec")) {
        g_signal_connect(dec, "source-setup", G_CALLBACK(alPrepararOrigen), f);
        gst_object_unref(dec);
    }
    // Cómo llega la señal (antes de normalizar): para el diagnóstico.
    if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), "ba_origen")) {
        if (GstPad* pad = gst_element_get_static_pad(q, "sink")) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                              [](GstPad*, GstPadProbeInfo* info, gpointer d) -> GstPadProbeReturn {
                                  GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
                                  if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
                                      GstCaps* c = nullptr;
                                      gst_event_parse_caps(ev, &c);
                                      const std::string desc = describirOrigen(c);
                                      auto* x = static_cast<Fuente*>(d);
                                      std::lock_guard<std::mutex> l(x->colaLock);
                                      x->origen = desc;
                                  }
                                  return GST_PAD_PROBE_OK;
                              },
                              f, nullptr);
            gst_object_unref(pad);
        }
        gst_object_unref(q);
    }
    enganchar(f, p, "ba_video", alVideo);
    enganchar(f, p, "ba_audio", alAudio);
    gst_pipeline_use_clock(GST_PIPELINE(p), f->clock);
    f->pipeline = p;
    {
        std::lock_guard<std::mutex> l(f->colaLock);
        f->recibidos = f->tarde = f->saltados = f->descartados = 0;
        f->ventanaNs = f->tardeVentana = f->recibidosVentana = f->tarde10 = f->recibidos10 = 0;
        f->adelantoMs = 0;
    }
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        f->error = "el pipeline no arrancó";
        desarmar(f);
    }
}

const char* nombre(void*) { return obs_module_text("Fuente GStreamer (BA)"); }

void actualizar(void* data, obs_data_t* s) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    const std::string uri = obs_data_get_string(s, "uri"), d = obs_data_get_string(s, "pipeline");
    const std::string reloj = obs_data_get_string(s, "reloj"), servidor = obs_data_get_string(s, "reloj_servidor");
    const int puerto = static_cast<int>(obs_data_get_int(s, "reloj_puerto"));
    const int dominio = static_cast<int>(obs_data_get_int(s, "reloj_dominio"));
    const bool audio = obs_data_get_bool(s, "audio");
    const bool normalizar = obs_data_get_bool(s, "normalizar");
    const bool ntpSync = obs_data_get_bool(s, "ntp_sync");
    const std::int64_t latencia = obs_data_get_int(s, "latencia"), desfase = obs_data_get_int(s, "desfase");
    // La latencia y el desfase se cambian EN VIVO (solo mueven el colchón); lo
    // demás rearma el pipeline. (Con `uri`, la latencia también va al jitter
    // buffer de RTSP/SRT: se rearma.)
    const bool rearmar = uri != f->uri || d != f->descripcion || reloj != f->reloj || servidor != f->relojServidor ||
                         puerto != f->relojPuerto || dominio != f->relojDominio || audio != f->conAudio || normalizar != f->normalizar || ntpSync != f->ntpSync ||
                         (latencia != f->latenciaMs && !uri.empty()) || f->pipeline == nullptr;
    const std::int64_t corrimiento =
        (latencia + desfase - f->latenciaMs - f->desfaseMs) * static_cast<std::int64_t>(GST_MSECOND);
    f->uri = uri;
    f->descripcion = d;
    f->reloj = reloj;
    f->relojServidor = servidor;
    f->relojPuerto = puerto;
    f->relojDominio = dominio;
    f->conAudio = audio;
    f->normalizar = normalizar;
    f->ntpSync = ntpSync;
    f->latenciaMs = latencia;
    f->desfaseMs = desfase;
    if (rearmar) {
        armar(f);
        return;
    }
    // Lo que ya está en el colchón se corre lo mismo.
    std::lock_guard<std::mutex> lc(f->colaLock);
    for (auto* q : {&f->video, &f->audio})
        for (auto& p : *q)
            p.vence = static_cast<GstClockTime>(std::max<std::int64_t>(0, static_cast<std::int64_t>(p.vence) + corrimiento));
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* f = new (std::nothrow) Fuente();
    if (f == nullptr) return nullptr;
    f->ctx = ctx;
    // El colchón ya decide cuándo va cada cuadro: libobs lo muestra al llegar.
    obs_source_set_async_unbuffered(ctx, true);
    actualizar(f, settings);
    proc_handler_add(obs_source_get_proc_handler(ctx), "void estado(out string error, out bool andando, out string info)",
                     [](void* d, calldata_t* cd) {
                         auto* x = static_cast<Fuente*>(d);
                         std::lock_guard<std::mutex> l(x->lock);
                         calldata_set_string(cd, "error", x->error.c_str());
                         calldata_set_bool(cd, "andando", x->pipeline != nullptr);
                         obs_data_t* o = obs_data_create();
                         obs_data_set_string(o, "reloj", x->reloj.empty() ? "sistema" : x->reloj.c_str());
                         if (x->clock != nullptr) {
                             obs_data_set_bool(o, "reloj_sincronizado", gst_clock_is_synced(x->clock));
                             obs_data_set_string(o, "reloj_nombre", GST_OBJECT_NAME(x->clock));
                         }
                         obs_data_set_int(o, "latencia_ms", x->latenciaMs);
                         obs_data_set_bool(o, "normalizado", x->normalizar);
                         obs_data_set_bool(o, "ntp_sync", x->ntpSync);
                         obs_data_set_int(o, "desfase_ms", x->desfaseMs);
                         obs_data_set_string(o, "formato", x->formato.c_str());
                         obs_data_set_string(o, "colorimetria", x->colorimetria.c_str());
                         obs_data_set_int(o, "ancho", x->ancho);
                         obs_data_set_int(o, "alto", x->alto);
                         obs_data_set_bool(o, "entrelazado", x->entrelazado);
                         obs_data_set_double(o, "fps_declarados", x->fpsDeclarado);
                         {
                             std::lock_guard<std::mutex> lc(x->colaLock);
                             obs_data_set_double(o, "fps_recibidos", x->fpsMedido);
                             obs_data_set_int(o, "recibidos", static_cast<long long>(x->recibidos));
                             obs_data_set_int(o, "llegaron_tarde", static_cast<long long>(x->tarde));
                             obs_data_set_int(o, "saltados", static_cast<long long>(x->saltados));
                             obs_data_set_int(o, "descartados", static_cast<long long>(x->descartados));
                             obs_data_set_int(o, "colchon_ms", x->colchonMs);
                             obs_data_set_int(o, "adelanto_ms", static_cast<long long>(x->adelantoMs));
                             // Lo de los últimos 10 s (o lo que va, si todavía no pasaron).
                             obs_data_set_int(o, "tarde_10s", static_cast<long long>(x->recibidos10 ? x->tarde10 : x->tardeVentana));
                             obs_data_set_int(o, "recibidos_10s", static_cast<long long>(x->recibidos10 ? x->recibidos10 : x->recibidosVentana));
                             obs_data_set_int(o, "en_colchon", static_cast<long long>(x->video.size()));
                             if (!x->origen.empty()) {
                                 obs_data_t* og = obs_data_create_from_json(x->origen.c_str());
                                 obs_data_set_obj(o, "origen", og);
                                 obs_data_release(og);
                             }
                         }
                         calldata_set_string(cd, "info", obs_data_get_json(o));
                         obs_data_release(o);
                     },
                     f);
    return f;
}

void destruir(void* data) {
    auto* f = static_cast<Fuente*>(data);
    {
        std::lock_guard<std::mutex> l(f->lock);
        desarmar(f);
    }
    delete f;
}

/// Cada cuadro de libobs: presenta lo que ya venció, y revisa el bus (un
/// error se anota y se reintenta a los 2 s).
void tic(void* data, float) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline == nullptr) {
        if ((!f->descripcion.empty() || !f->uri.empty()) && f->reintento != 0 && os_gettime_ns() >= f->reintento) {
            f->reintento = 0;
            armar(f);
        }
        return;
    }
    // El colchón: el último cuadro vencido se muestra; los vencidos de antes
    // se saltean (llegaron juntos, o la máquina se atrasó).
    GstSample* video = nullptr;
    std::deque<GstSample*> audios;
    {
        std::lock_guard<std::mutex> lc(f->colaLock);
        const GstClockTime ahora = gst_clock_get_time(f->clock);
        while (!f->video.empty() && f->video.front().vence <= ahora) {
            if (video != nullptr) {
                gst_sample_unref(video);
                ++f->saltados;
            }
            video = f->video.front().muestra;
            f->video.pop_front();
        }
        while (!f->audio.empty() && f->audio.front().vence <= ahora) {
            audios.push_back(f->audio.front().muestra);
            f->audio.pop_front();
        }
        f->colchonMs = f->video.empty() ? 0
                                         : (static_cast<std::int64_t>(f->video.back().vence) - static_cast<std::int64_t>(ahora)) /
                                               static_cast<std::int64_t>(GST_MSECOND);
    }
    if (video != nullptr) {
        presentarVideo(f, video);
        gst_sample_unref(video);
    }
    for (GstSample* a : audios) {
        presentarAudio(f, a);
        gst_sample_unref(a);
    }
    GstBus* bus = gst_element_get_bus(f->pipeline);
    if (bus == nullptr) return;
    GstMessage* m = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    gst_object_unref(bus);
    if (m == nullptr) return;
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
        GError* e = nullptr;
        gst_message_parse_error(m, &e, nullptr);
        f->error = e != nullptr ? e->message : "error";
        if (e != nullptr) g_error_free(e);
        blog(LOG_WARNING, "[ba-sources] fuente GStreamer: %s (reintento en 2 s)", f->error.c_str());
    } else {
        f->error = "terminó (EOS)";
    }
    gst_message_unref(m);
    desarmar(f);
    f->reintento = os_gettime_ns() + 2'000'000'000ULL;
}

obs_properties_t* propiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "uri", obs_module_text("URI (srt://, rtmp://, rtsp://, udp://, http://, archivo)"), OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "pipeline", obs_module_text("O un pipeline de GStreamer (origen)"), OBS_TEXT_MULTILINE);
    obs_properties_add_int(p, "latencia", obs_module_text("Latencia (ms)"), 0, 10000, 10);
    obs_properties_add_int(p, "desfase", obs_module_text("Desfase (ms)"), -5000, 5000, 1);
    obs_property_t* r = obs_properties_add_list(p, "reloj", obs_module_text("Reloj"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(r, "Sistema", "sistema");
    obs_property_list_add_string(r, "NTP", "ntp");
    obs_property_list_add_string(r, "PTP", "ptp");
    obs_property_list_add_string(r, "De otro aparato (red)", "red");
    obs_properties_add_text(p, "reloj_servidor", obs_module_text("Servidor del reloj"), OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "reloj_puerto", obs_module_text("Puerto del reloj"), 0, 65535, 1);
    obs_properties_add_int(p, "reloj_dominio", obs_module_text("Dominio PTP"), 0, 127, 1);
    obs_properties_add_bool(p, "audio", obs_module_text("Con audio"));
    obs_properties_add_bool(p, "normalizar", obs_module_text("Normalizar (desentrelazar, cuadros y color del estudio)"));
    return p;
}

void porDefecto(obs_data_t* s) {
    obs_data_set_default_string(s, "pipeline", "videotestsrc pattern=smpte is-live=true");
    obs_data_set_default_string(s, "reloj", "sistema");
    obs_data_set_default_bool(s, "audio", true);
    obs_data_set_default_bool(s, "normalizar", true);
    obs_data_set_default_bool(s, "ntp_sync", true);
}

} // namespace

namespace ba::gst {

namespace {
GstNetTimeProvider* g_proveedor = nullptr;
int g_puertoPublicado = 0;
} // namespace

bool publicarReloj(int puerto, std::string& error) {
    if (!listo()) {
        error = fallo() ? errorInicio() : "GStreamer se está iniciando";
        return false;
    }
    if (g_proveedor != nullptr) {
        gst_object_unref(g_proveedor);
        g_proveedor = nullptr;
        g_puertoPublicado = 0;
    }
    if (puerto <= 0) return true; // apagado
    GstClock* c = gst_system_clock_obtain();
    g_proveedor = gst_net_time_provider_new(c, nullptr, puerto);
    gst_object_unref(c);
    if (g_proveedor == nullptr) {
        error = "no se pudo publicar el reloj en el puerto " + std::to_string(puerto) + " (¿ocupado?)";
        return false;
    }
    g_puertoPublicado = puerto;
    return true;
}

int relojPublicado() { return g_puertoPublicado; }

} // namespace ba::gst

extern "C" {

static struct obs_source_info ba_gst_fuente_info = {};

void ba_register_gst_fuente(void) {
    // Que GStreamer empiece a iniciarse ya (en su hilo), no al primer cuadro.
    ba::gst::listo();
    ba_gst_fuente_info.id = "ba_gst_fuente";
    ba_gst_fuente_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_gst_fuente_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
    ba_gst_fuente_info.get_name = nombre;
    ba_gst_fuente_info.create = crear;
    ba_gst_fuente_info.destroy = destruir;
    ba_gst_fuente_info.update = actualizar;
    ba_gst_fuente_info.video_tick = tic;
    ba_gst_fuente_info.get_properties = propiedades;
    ba_gst_fuente_info.get_defaults = porDefecto;
    obs_register_source(&ba_gst_fuente_info);
    // El reloj del estudio, para que otros aparatos se sincronicen con este:
    // proc global «ba_reloj_publicar» (puerto 0 = apagar).
    proc_handler_add(obs_get_proc_handler(),
                     "void ba_reloj_publicar(in int puerto, out bool ok, out string error, out int puerto_publicado)",
                     [](void*, calldata_t* cd) {
                         std::string e;
                         const bool ok = ba::gst::publicarReloj(static_cast<int>(calldata_int(cd, "puerto")), e);
                         calldata_set_bool(cd, "ok", ok);
                         calldata_set_string(cd, "error", e.c_str());
                         calldata_set_int(cd, "puerto_publicado", ba::gst::relojPublicado());
                     },
                     nullptr);
}

} // extern "C"

#else

extern "C" void ba_register_gst_fuente(void) {}

#endif
