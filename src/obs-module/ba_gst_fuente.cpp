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
//   - `camara`: una cámara o capturadora del equipo por su id estable (el mismo
//     que usaba libobs): macOS el uniqueID de AVFoundation, Windows la ruta del
//     dispositivo, Linux /dev/videoN. Al armar se busca cuál es AHORA
//     (avfvideosrc solo sabe de índices, que cambian al enchufar otra) y se arma
//     avfvideosrc / mfvideosrc / v4l2src; `camara_modo` («1920x1080@30») fija
//     tamaño y cuadros (vacío = el de la cámara que mejor va con el lienzo). Si
//     no está enchufada, se reintenta sola.
//
// SINCRONÍA ESTÁNDAR DE GSTREAMER (de fábrica, `sincronia` = gstreamer): los
// appsink de video y de audio sincronizan con el reloj (sync=true), con una cola
// de espera delante; TODAS las cadenas usan el mismo reloj, el mismo base_time
// (el del estudio) y la misma latencia (gst_pipeline_set_latency con la mayor
// del estudio: lo que pide cada cadena en la consulta LATENCY —jitter buffer de
// RTSP/SRT, filtros que declaran lo que tardan— y el colchón configurado). Así
// lo capturado a la vez se entrega a la vez, en todas las entradas, y el audio
// de una entrada sale con su video (los dos sinks esperan la misma hora). El
// desfase es el ts-offset de los sinks. Cada cuadro se le pasa a libobs cuando
// el sink lo libera.
//
// SINCRONÍA VIEJA (`sincronia` = colchon, de respaldo). Los cuadros NO se entregan a libobs al llegar: van a un colchón
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
// LOS FILTROS (`filtros`, JSON; y `filtros_audio`, un arreglo aparte): las
// tarjetas del grafo de video y los filtros de audio de la entrada (p. ej.
// `baplugin medio=audio` de un plugin de Python, audiodynamic, un eco…)
// corren ACÁ, como elementos de GStreamer en la cadena de la entrada —no en
// libobs—:
//     {"video": [{"id", "elemento", "propiedades": {…}}, …],
//      "audio": [{"id", "elemento", "propiedades": {…}}, …],
//      "alfa": true|false}       (alfa: sale BGRA con transparencia, p. ej. croma)
// Cada uno es un elemento con nombre `ba_f_<id>` (`ba_fa_<id>` los de audio),
// entre videoconvert/audioconvert, así la negociación de caps elige solo el
// formato (y la memoria) que le sirve a cada uno. Cambiar una propiedad que el
// elemento acepta andando (GST_PARAM_MUTABLE_PLAYING) se aplica en vivo; lo
// demás (otro elemento, otro motor) rearma la cadena. La demora de los filtros
// se mide (lo viejo que llega cada cuadro) y el colchón la absorbe para audio y
// video por igual: siguen sincronizados.
//
// MEDIOS (`ba_gst_medio`, el mismo código con otro tipo de fuente): un archivo
// (video o audio) como el ffmpeg_source de libobs, con sus mismos ajustes
// (`local_file`, `looping`, `restart_on_activate`, `clear_on_media_end`,
// `close_when_inactive`) y los controles de medios de libobs (reproducir y
// pausar, reiniciar, parar, ir a un tiempo, tiempo, duración y estado): así la
// playlist, el guion del evento y las herramientas de medios lo manejan igual.
// Se reproduce en tiempo real con la sincronía estándar de GStreamer (los
// sinks esperan al reloj) y sin la compensación del estudio (no hace falta
// alinear un archivo con las cámaras). Termina: se queda quieto (o en negro con
// `clear_on_media_end`), o vuelve a empezar con `looping`.
//
// EL RACK (`rack` = nombre de la fuente): el rack de audio de la app (módulos
// LSP/CLAP, el grafo tipo JACK) corre ACÁ, como el elemento `barack` que
// registra la app, primero en la cadena del audio (a 48 kHz estéreo), antes de
// los filtros de audio. Si la app todavía no lo registró, la entrada arranca
// sin él y se rearma sola cuando aparece.
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
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <filesystem>
#include <mutex>
#include <set>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

// --- Relojes compartidos ----------------------------------------------------------

std::mutex g_relojesLock;
std::map<std::string, GstClock*>& relojes() {
    static std::map<std::string, GstClock*> m;
    return m;
}

/// El reloj pedido (con referencia), o nullptr con el motivo.
GstClock* relojDe(const std::string& tipo, const std::string& servidor, int puerto, int dominio,
                  std::string& error) {
    const std::string clave =
        tipo + "|" + servidor + "|" + std::to_string(puerto) + "|" + std::to_string(dominio);
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

/// Un filtro de la cadena: un elemento de GStreamer con sus propiedades (como texto).
struct Filtro {
    std::string id, elemento;
    std::string nombre; ///< el id sin caracteres raros: el nombre del elemento es ba_f_<nombre>
    std::string memoria; ///< la que pide (CUDAMemory, GLMemory, D3D11Memory, D3D12Memory; "" = sistema)
    bool saleEnSistema = false; ///< entrega en memoria de sistema (no hace falta bajar después)
    std::vector<std::pair<std::string, std::string>> props;
};

struct Pendiente {
    GstSample* muestra = nullptr;
    GstClockTime vence = 0; ///< cuándo presentarla, en el reloj del pipeline
};

struct Fuente {
    obs_source_t* ctx = nullptr;
    std::mutex lock;
    // Lo pedido
    std::string uri, descripcion, descripcionAudio, reloj, relojServidor;
    std::string camara, camaraModo; ///< cámara del equipo (id estable) y su modo («WxH@fps»)
    std::string microfono;          ///< micrófono del equipo (id estable; "default" = el del sistema)
    // Esperando un dispositivo que no está (vigía de dispositivos): sin rearmar en
    // vano; se arma apenas aparece.
    bool esperaDispositivo = false;
    std::uint64_t genVista = 0;       ///< la generación del vigía que ya se miró
    std::uint64_t esperaDesdeNs = 0;  ///< para el reintento de respaldo (cada 15 s)
    /// Se alinea con el estudio (la compensación/latencia de todas las entradas).
    /// false: solo su propia demora (p. ej. el audio de un invitado, cuyo video
    /// todavía va por libobs sin esperar).
    bool estudio = true;
    // Sin señal (P0.4): el último cuadro unos segundos y después una placa con el
    // nombre de la entrada, en vez de negro o congelado.
    std::uint64_t ultimoCuadroNs = 0;  ///< cuándo llegó el último cuadro a libobs
    std::uint64_t placaEnviadaNs = 0;
    bool enPlaca = false;
    std::vector<std::uint8_t>* placa = nullptr; ///< BGRA, hecha una vez con GStreamer
    int placaAncho = 0, placaAlto = 0;
    std::string placaNombre;
    // Medios (ba_gst_medio)
    bool medio = false;
    bool bucle = false, limpiarAlTerminar = true, reiniciarAlActivar = true, cerrarInactiva = false;
    std::atomic<int> estadoMedio{OBS_MEDIA_STATE_NONE};
    std::int64_t buscarNs = -1; ///< ir a este tiempo apenas la cadena (rearmada) pase datos
    bool segmentoListo = false; ///< `looping`: ya se pidió el salto por segmentos (vueltas sin cortes)
    int relojPuerto = 0, relojDominio = 0;
    std::int64_t latenciaMs = 0, desfaseMs = 0;
    bool conAudio = true;
    bool normalizar = true;
    bool superres = false;      ///< duplicar la resolución (bavtsuperres, macOS 26) si el cuadro es chico
    std::string superresMotor;  ///< "" (según `superres`), apple, fsr, onnx
    std::string superresModelo; ///< el .onnx (motor onnx)
    std::string filtrosJson;    ///< lo pedido (para no rearmar si no cambió)
    std::vector<Filtro> filtrosV, filtrosA;
    bool alfa = false;
    std::string firmaFiltros;            ///< lo que obliga a rearmar (elementos y propiedades no vivas)
    std::vector<std::string> avisosFiltros;
    bool ntpSync =
        true; ///< RTSP: usar la hora de captura del que manda (RTCP); si su reloj está mal, conviene apagarlo
    /// La hora de la cámara estaba muy corrida (la entrada salía con segundos de
    /// atraso): se dejó de usar sola, hasta que alguien vuelva a pedir ntp_sync.
    bool ntpApagadoSolo = false;
    std::uint64_t ntpMalDesde = 0;
    // Lo armado
    std::string error;
    GstElement* pipeline = nullptr;
    GstClock* clock = nullptr;
    std::atomic<std::uint64_t> reintento{0}; ///< cuándo volver a armar (os_gettime_ns)
    std::string rack;                ///< el rack de la app que corre en la cadena del audio ("" = ninguno)
    std::uint64_t rackFaltaDesde = 0; ///< se armó sin `barack` (la app no lo registró aún): cuándo mirar de nuevo
    // El colchón (lo llenan los hilos de GStreamer, lo vacía el tic)
    std::mutex colaLock;
    std::deque<Pendiente> video, audio;
    // Para diagnosticar
    std::uint64_t recibidos = 0, tarde = 0, saltados = 0, descartados = 0;
    std::uint64_t bloquesA = 0; ///< bloques de audio que llegaron (una entrada solo de audio no tiene cuadros)
    std::int64_t colchonMs = 0; ///< cuánto falta para el último cuadro del colchón
    double adelantoMs =
        0; ///< promedio de cuánto le falta a cada cuadro al llegar (≈ latencia si el reloj está bien)
    std::uint64_t ventanaNs = 0, tardeVentana = 0, recibidosVentana = 0, tarde10 = 0, recibidos10 = 0;
    std::uint64_t primeroNs = 0, cuentaFps = 0;
    double fpsMedido = 0;
    // La demora de los filtros: lo viejo que llega cada cuadro/bloque (pico que
    // baja despacio), y lo que se agregó al colchón para absorberla.
    double retrasoV = 0, retrasoA = 0;
    std::int64_t compMs = 0;
    double declaradoMs = 0;            ///< la latencia que declara la cadena (LATENCY: filtros incluidos)
    // Sincronía estándar (sync=true, base_time y latencia del estudio).
    bool estandar = true;
    std::string sincronia = "colchon";
    std::int64_t latObjetivoNs = 0;    ///< la latencia puesta en la cadena (la del estudio)
    std::deque<double>* edades = nullptr; ///< lo viejo que llegaron los últimos cuadros (ms, bajo colaLock)
    std::uint64_t sobraDesdeNs = 0;    ///< desde cuándo sobra latencia (para bajarla sin apuro)
    double edadP50 = 0, edadP90 = 0;
    std::uint64_t consultaNs = 0;      ///< cuándo se preguntó por última vez
    // El canal de control (evento «ba/control»): lo último pedido, para volver a
    // mandarlo si la cadena se rearma.
    GstStructure* control = nullptr;
    bool controlEnviado = false;
    std::string formato, colorimetria;
    int ancho = 0, alto = 0;
    bool entrelazado = false;
    double fpsDeclarado = 0;
    std::string origen; ///< lo que llega, ANTES de normalizar (JSON: formato, tamaño, fps, color…)
};

void presentarVideo(Fuente* f, GstSample* s);
void presentarAudio(Fuente* f, GstSample* s);

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
    obs_data_set_string(
        o, "pixel",
        pn == pd ? "cuadrado" : (std::to_string(pn) + "/" + std::to_string(pd) + " (no cuadrado)").c_str());
    const GstVideoColorimetry& k = GST_VIDEO_INFO_COLORIMETRY(&info);
    const char* matriz = k.matrix == GST_VIDEO_COLOR_MATRIX_BT601    ? "BT.601"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT709  ? "BT.709"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT2020 ? "BT.2020"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_RGB    ? "RGB"
                                                                     : "sin declarar";
    obs_data_set_string(o, "matriz", matriz);
    obs_data_set_string(o, "rango",
                        k.range == GST_VIDEO_COLOR_RANGE_0_255    ? "completo"
                        : k.range == GST_VIDEO_COLOR_RANGE_16_235 ? "limitado"
                                                                  : "sin declarar");
    obs_data_set_bool(o, "hdr",
                      k.transfer == GST_VIDEO_TRANSFER_SMPTE2084 ||
                          k.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67);
    const std::string r = obs_data_get_json(o);
    obs_data_release(o);
    return r;
}

constexpr double kMaxColchonSeg = 10; ///< más de 10 s esperando es un error de reloj, no un colchón

void vaciar(Fuente* f) {
    std::lock_guard<std::mutex> l(f->colaLock);
    for (auto& p : f->video)
        gst_sample_unref(p.muestra);
    for (auto& p : f->audio)
        gst_sample_unref(p.muestra);
    f->video.clear();
    f->audio.clear();
}

/// Cuándo hay que presentar esta muestra: su hora de captura en el reloj del
/// pipeline, más la latencia y el desfase.
GstClockTime venceDe(Fuente* f, GstSample* s, double* retrasoMs = nullptr) {
    GstBuffer* b = gst_sample_get_buffer(s);
    const GstSegment* seg = gst_sample_get_segment(s);
    const GstClockTime base = f->pipeline != nullptr ? gst_element_get_base_time(f->pipeline) : 0;
    GstClockTime t = GST_CLOCK_TIME_NONE;
    if (b != nullptr && GST_BUFFER_PTS_IS_VALID(b) && seg != nullptr)
        t = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
    // Sin hora (algunas fuentes no la ponen): la de llegada.
    const GstClockTime ahora = gst_clock_get_time(f->clock);
    const GstClockTime captura = GST_CLOCK_TIME_IS_VALID(t) ? base + t : ahora;
    if (retrasoMs != nullptr)
        *retrasoMs = GST_CLOCK_TIME_IS_VALID(t)
                         ? (static_cast<double>(ahora) - static_cast<double>(captura)) / 1e6
                         : -1;
    const std::int64_t extra =
        (f->latenciaMs + f->desfaseMs + f->compMs) * static_cast<std::int64_t>(GST_MSECOND);
    const std::int64_t v = static_cast<std::int64_t>(captura) + extra;
    return v > 0 ? static_cast<GstClockTime>(v) : 0;
}

GstFlowReturn alVideo(GstAppSink* sink, gpointer datos) {
    auto* f = static_cast<Fuente*>(datos);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (s == nullptr) return GST_FLOW_OK;
    if (f->estandar) {
        // El sink ya esperó su hora (reloj, base_time y latencia del estudio): a libobs ya.
        presentarVideo(f, s);
        gst_sample_unref(s);
        std::lock_guard<std::mutex> l(f->colaLock);
        const std::uint64_t n = os_gettime_ns();
        if (f->primeroNs == 0) f->primeroNs = n;
        ++f->cuentaFps;
        if (n - f->primeroNs >= 2'000'000'000ULL) {
            f->fpsMedido = static_cast<double>(f->cuentaFps) * 1e9 / static_cast<double>(n - f->primeroNs);
            f->primeroNs = n;
            f->cuentaFps = 0;
        }
        if (f->ventanaNs == 0) f->ventanaNs = n;
        if (n - f->ventanaNs >= 10'000'000'000ULL) {
            f->tarde10 = f->tardeVentana;
            f->recibidos10 = f->recibidosVentana;
            f->tardeVentana = f->recibidosVentana = 0;
            f->ventanaNs = n;
        }
        return GST_FLOW_OK;
    }
    if (f->clock == nullptr) {
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    double retraso = -1;
    const GstClockTime vence = venceDe(f, s, &retraso);
    const GstClockTime ahora = gst_clock_get_time(f->clock);
    std::lock_guard<std::mutex> l(f->colaLock);
    if (retraso >= 0) f->retrasoV = retraso > f->retrasoV ? retraso : f->retrasoV - (f->retrasoV - retraso) * 0.01;
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
    // 10 s de cuadros a la cadencia que llega (60 fps: 600), nunca menos de 300.
    const size_t maxColchon = static_cast<size_t>(std::max(300.0, kMaxColchonSeg * f->fpsMedido));
    while (f->video.size() > maxColchon) {
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
    if (f->estandar) {
        // Esperó la misma hora que su video: sale con él (sincronía de labios).
        presentarAudio(f, s);
        {
            std::lock_guard<std::mutex> l(f->colaLock);
            ++f->bloquesA;
        }
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    if (f->clock == nullptr) {
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    double retraso = -1;
    const GstClockTime vence = venceDe(f, s, &retraso);
    std::lock_guard<std::mutex> l(f->colaLock);
    if (retraso >= 0) f->retrasoA = retraso > f->retrasoA ? retraso : f->retrasoA - (f->retrasoA - retraso) * 0.01;
    f->audio.push_back({s, vence});
    ++f->bloquesA;
    // Bloques de audio: 10 s de bloques chicos (~10 ms) como mucho.
    while (f->audio.size() > static_cast<size_t>(kMaxColchonSeg * 120)) {
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
        case GST_VIDEO_FORMAT_YVYU: return VIDEO_FORMAT_YVYU;
        case GST_VIDEO_FORMAT_GRAY8: return VIDEO_FORMAT_Y800;
        case GST_VIDEO_FORMAT_BGR: return VIDEO_FORMAT_BGR3;
        case GST_VIDEO_FORMAT_Y42B: return VIDEO_FORMAT_I422;
        case GST_VIDEO_FORMAT_Y444: return VIDEO_FORMAT_I444;
        case GST_VIDEO_FORMAT_A420: return VIDEO_FORMAT_I40A;
        // 10 y 12 bits (cámaras HDR, 4:2:2 de producción): libobs los toma tal cual.
        case GST_VIDEO_FORMAT_P010_10LE: return VIDEO_FORMAT_P010;
        case GST_VIDEO_FORMAT_I420_10LE: return VIDEO_FORMAT_I010;
        case GST_VIDEO_FORMAT_I422_10LE: return VIDEO_FORMAT_I210;
        case GST_VIDEO_FORMAT_Y444_12LE: return VIDEO_FORMAT_I412;
        case GST_VIDEO_FORMAT_v210: return VIDEO_FORMAT_V210;
        default: return VIDEO_FORMAT_NONE;
    }
}

/// Lo que libobs acepta sin conversión (las caps del appsink sin normalizar).
constexpr const char* kFormatosObs =
    "{NV12,I420,BGRA,BGRx,RGBA,UYVY,YUY2,YVYU,GRAY8,BGR,Y42B,Y444,A420,P010_10LE,I420_10LE,I422_10LE,Y444_12LE,v210}";

/// Las caps de color a las que se normaliza: las del lienzo del estudio
/// (Ajustes > Video: formato, espacio de color, rango). Salida de 10 bits o
/// HDR -> P010; si no, NV12.
std::string capsColorDelEstudio(const obs_video_info& ovi) {
    const bool diez = ovi.output_format == VIDEO_FORMAT_P010 || ovi.output_format == VIDEO_FORMAT_I010 ||
                      ovi.colorspace == VIDEO_CS_2100_PQ || ovi.colorspace == VIDEO_CS_2100_HLG;
    GstVideoColorimetry c = {};
    const char* base = ovi.colorspace == VIDEO_CS_601        ? "bt601"
                       : ovi.colorspace == VIDEO_CS_2100_PQ  ? "bt2100-pq"
                       : ovi.colorspace == VIDEO_CS_2100_HLG ? "bt2100-hlg"
                                                             : "bt709";
    gst_video_colorimetry_from_string(&c, base);
    c.range = ovi.range == VIDEO_RANGE_FULL ? GST_VIDEO_COLOR_RANGE_0_255 : GST_VIDEO_COLOR_RANGE_16_235;
    gchar* col = gst_video_colorimetry_to_string(&c);
    std::string out = std::string("video/x-raw,format=") + (diez ? "P010_10LE" : "NV12") +
                      ",colorimetry=" + (col != nullptr ? col : base);
    g_free(col);
    return out;
}

/// La hora en que esta muestra tiene que salir (captura + la demora de la
/// entrada), en el reloj de libobs (os_gettime_ns). Así libobs alinea audio y
/// video por la hora real y no por cuándo nuestro hilo se lo entregó (el temblor
/// del tic o del sink). Se leen los dos relojes juntos: no hace falta que tengan
/// la misma base. Sin hora (o una sin sentido), la de ahora.
std::uint64_t horaParaObs(Fuente* f, GstSample* s) {
    const std::uint64_t ahoraObs = os_gettime_ns();
    if (f->clock == nullptr || f->pipeline == nullptr) return ahoraObs;
    GstBuffer* b = gst_sample_get_buffer(s);
    const GstSegment* seg = gst_sample_get_segment(s);
    if (b == nullptr || seg == nullptr || !GST_BUFFER_PTS_IS_VALID(b)) return ahoraObs;
    const GstClockTime rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
    if (!GST_CLOCK_TIME_IS_VALID(rt)) return ahoraObs;
    const std::int64_t ms = static_cast<std::int64_t>(GST_MSECOND);
    // Lo que espera cada modo: el estándar, la latencia puesta en la cadena y el
    // ts-offset; el colchón, latencia + desfase + compensación (venceDe).
    const std::int64_t demora = f->estandar ? (f->medio ? 0 : f->latObjetivoNs) + f->desfaseMs * ms
                                            : (f->latenciaMs + f->desfaseMs + f->compMs) * ms;
    const std::int64_t sale = static_cast<std::int64_t>(gst_element_get_base_time(f->pipeline)) +
                              static_cast<std::int64_t>(rt) + demora;
    const std::int64_t ahoraGst = static_cast<std::int64_t>(gst_clock_get_time(f->clock));
    const std::int64_t ts = static_cast<std::int64_t>(ahoraObs) + (sale - ahoraGst);
    // Más de 1 s corrida (un salto, un reloj que cambió): mejor la de ahora.
    if (ts <= 0 || std::llabs(ts - static_cast<std::int64_t>(ahoraObs)) > 1'000'000'000LL) return ahoraObs;
    return static_cast<std::uint64_t>(ts);
}

void presentarVideo(Fuente* f, GstSample* s) {
    f->ultimoCuadroNs = os_gettime_ns();
    f->enPlaca = false;
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
    frame.timestamp = horaParaObs(f, s); // la hora en que tenía que salir (no la de entrega)
    const GstVideoColorimetry& c = GST_VIDEO_INFO_COLORIMETRY(&info);
    const bool rgb = GST_VIDEO_INFO_IS_RGB(&info);
    const bool completo = rgb || c.range == GST_VIDEO_COLOR_RANGE_0_255;
    // El espacio de color que trae (la matriz) y su transferencia (PQ/HLG: HDR).
    video_colorspace cs = VIDEO_CS_709;
    if (rgb)
        cs = VIDEO_CS_SRGB;
    else if (c.matrix == GST_VIDEO_COLOR_MATRIX_BT601 || c.matrix == GST_VIDEO_COLOR_MATRIX_SMPTE240M)
        cs = VIDEO_CS_601;
    else if (c.matrix == GST_VIDEO_COLOR_MATRIX_BT2020)
        cs = c.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67 ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
    frame.trc = c.transfer == GST_VIDEO_TRANSFER_SMPTE2084      ? VIDEO_TRC_PQ
                : c.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67 ? VIDEO_TRC_HLG
                : rgb                                           ? VIDEO_TRC_SRGB
                                                                : VIDEO_TRC_DEFAULT;
    frame.full_range = completo;
    video_format_get_parameters_for_format(cs, completo ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, fmt,
                                           frame.color_matrix, frame.color_range_min, frame.color_range_max);
    obs_source_output_video(f->ctx, &frame);
    gst_video_frame_unmap(&vf);
    // Lo que se informa (bajo colaLock: con la sincronía estándar esto corre en el
    // hilo del sink, que no puede tomar `lock`).
    gchar* col = gst_video_colorimetry_to_string(&c);
    std::lock_guard<std::mutex> lc(f->colaLock);
    f->formato = gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info));
    f->colorimetria =
        std::string(col != nullptr ? col : "") + (completo ? " (rango completo)" : " (rango limitado)");
    g_free(col);
    f->ancho = static_cast<int>(frame.width);
    f->alto = static_cast<int>(frame.height);
    f->entrelazado = GST_VIDEO_INFO_IS_INTERLACED(&info);
    f->fpsDeclarado = GST_VIDEO_INFO_FPS_D(&info) > 0
                          ? static_cast<double>(GST_VIDEO_INFO_FPS_N(&info)) / GST_VIDEO_INFO_FPS_D(&info)
                          : 0;
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
    a.timestamp = horaParaObs(f, s);
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
    if (g_object_class_find_property(k, "latency") != nullptr)
        gst_util_set_object_arg(G_OBJECT(origen), "latency", lat.c_str());
    if (g_object_class_find_property(k, "ntp-sync") != nullptr)
        gst_util_set_object_arg(G_OBJECT(origen), "ntp-sync",
                                f->ntpSync && !f->ntpApagadoSolo ? "true" : "false");
    // Con la hora de la cámara (ntp-sync) «synced» alinea varias cámaras; sin
    // ella, «synced» fija la hora con el PRIMER paquete y si ese llegó tarde
    // cada cuadro quedaba ~200 ms adelantado (esperando en el colchón, un PTZ
    // que respondía tarde). «slave» sigue la llegada real.
    if (g_object_class_find_property(k, "buffer-mode") != nullptr)
        gst_util_set_object_arg(G_OBJECT(origen), "buffer-mode",
                                f->ntpSync && !f->ntpApagadoSolo ? "synced" : "slave");
}

void enganchar(Fuente* f, GstElement* p, const char* nombre, GstFlowReturn (*cb)(GstAppSink*, gpointer)) {
    GstElement* sink = gst_bin_get_by_name(GST_BIN(p), nombre);
    if (sink == nullptr) return;
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = cb;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, f, nullptr);
    gst_object_unref(sink);
}

// --- Una sola compensación para todo el estudio ----------------------------------------
// Cada entrada calcula cuánto le falta al colchón por sus filtros; todas usan la
// MAYOR: las cámaras siguen alineadas entre sí (una con superresolución no queda
// atrasada respecto de otra sin ella) y prender o apagar una red no mueve nada.
std::mutex g_compLock;
std::map<const Fuente*, std::int64_t> g_compPropia;

std::int64_t compensacionEstudio(const Fuente* f, std::int64_t propia) {
    std::lock_guard<std::mutex> l(g_compLock);
    g_compPropia[f] = propia;
    std::int64_t m = 0;
    for (const auto& [x, v] : g_compPropia)
        m = std::max(m, v);
    return m;
}

void olvidarCompensacion(const Fuente* f) {
    std::lock_guard<std::mutex> l(g_compLock);
    g_compPropia.erase(f);
}


/// La latencia del estudio (ns): la mayor que pidan las cadenas (estándar).
std::mutex g_latLock;
std::map<const Fuente*, std::int64_t> g_latPropia;

std::int64_t latenciaDelEstudio(const Fuente* f, std::int64_t propia) {
    std::lock_guard<std::mutex> l(g_latLock);
    if (propia > 0) g_latPropia[f] = propia;
    std::int64_t m = 0;
    for (const auto& [x, v] : g_latPropia)
        m = std::max(m, v);
    return m;
}

void olvidarLatencia(const Fuente* f) {
    std::lock_guard<std::mutex> l(g_latLock);
    g_latPropia.erase(f);
}

/// Al llegar un cuadro (o un bloque de audio) a la cola de espera: cuán viejo
/// llega (lo que tardó la cadena) y cuánto le falta para su hora.
GstPadProbeReturn alLlegar(GstPad* pad, GstPadProbeInfo* info, gpointer d) {
    auto* par = static_cast<std::pair<Fuente*, bool>*>(d);
    Fuente* f = par->first;
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b == nullptr || !GST_BUFFER_PTS_IS_VALID(b) || f->clock == nullptr) return GST_PAD_PROBE_OK;
    GstEvent* ev = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
    if (ev == nullptr) return GST_PAD_PROBE_OK;
    const GstSegment* seg = nullptr;
    gst_event_parse_segment(ev, &seg);
    const GstClockTime rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
    gst_event_unref(ev);
    if (!GST_CLOCK_TIME_IS_VALID(rt)) return GST_PAD_PROBE_OK;
    const double ahora = static_cast<double>(gst_clock_get_time(f->clock));
    const GstClockTime base = f->pipeline != nullptr ? gst_element_get_base_time(f->pipeline) : 0;
    const double captura = static_cast<double>(base + rt);
    const double retraso = (ahora - captura) / 1e6;
    const double falta = (captura + static_cast<double>(f->latObjetivoNs + f->desfaseMs * static_cast<std::int64_t>(GST_MSECOND)) - ahora) / 1e6;
    std::lock_guard<std::mutex> l(f->colaLock);
    double& r = par->second ? f->retrasoV : f->retrasoA;
    r = retraso > r ? retraso : r - (r - retraso) * 0.01;
    if (par->second) {
        if (f->edades == nullptr) f->edades = new std::deque<double>();
        f->edades->push_back(retraso);
        while (f->edades->size() > 300) f->edades->pop_front();
        ++f->recibidos;
        ++f->recibidosVentana;
        if (falta < -40.0) {
            ++f->tarde;
            ++f->tardeVentana;
        }
        f->adelantoMs = f->recibidos == 1 ? falta : f->adelantoMs * 0.95 + falta * 0.05;
    }
    return GST_PAD_PROBE_OK;
}

// --- El canal de control ------------------------------------------------------------
// Un evento de GStreamer estándar: personalizado, hacia abajo y fuera de banda
// (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, no espera detrás de los cuadros), con una
// GstStructure «ba/control». Entra por la cola del origen y recorre toda la
// cadena: cada elemento que lo entiende lo usa (la superresolución: `superres`
// = activo | reposo | barato, `transicion_ms`) y todos lo dejan pasar. Sirve
// para cualquier efecto o evento futuro: se agregan campos.

/// JSON plano -> GstStructure «ba/control» (texto, números, sí/no).
GstStructure* estructuraDe(const char* json) {
    obs_data_t* d = obs_data_create_from_json(json != nullptr ? json : "{}");
    if (d == nullptr) return nullptr;
    GstStructure* st = gst_structure_new_empty("ba/control");
    for (obs_data_item_t* it = obs_data_first(d); it != nullptr; obs_data_item_next(&it)) {
        const char* k = obs_data_item_get_name(it);
        switch (obs_data_item_gettype(it)) {
            case OBS_DATA_STRING: gst_structure_set(st, k, G_TYPE_STRING, obs_data_item_get_string(it), nullptr); break;
            case OBS_DATA_BOOLEAN: gst_structure_set(st, k, G_TYPE_BOOLEAN, obs_data_item_get_bool(it), nullptr); break;
            case OBS_DATA_NUMBER:
                if (obs_data_item_numtype(it) == OBS_DATA_NUM_INT)
                    gst_structure_set(st, k, G_TYPE_INT, static_cast<int>(obs_data_item_get_int(it)), nullptr);
                else
                    gst_structure_set(st, k, G_TYPE_DOUBLE, obs_data_item_get_double(it), nullptr);
                break;
            default: break;
        }
    }
    obs_data_release(d);
    return st;
}

/// Manda lo último pedido por la cadena (entra por la cola del origen).
bool enviarControl(Fuente* f) {
    if (f->pipeline == nullptr || f->control == nullptr) return false;
    GstElement* q = gst_bin_get_by_name(GST_BIN(f->pipeline), "ba_origen");
    if (q == nullptr) return false;
    GstPad* pad = gst_element_get_static_pad(q, "sink");
    bool ok = false;
    if (pad != nullptr) {
        ok = gst_pad_send_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, gst_structure_copy(f->control)));
        gst_object_unref(pad);
    }
    gst_object_unref(q);
    return ok;
}

// --- Los filtros ---------------------------------------------------------------------

/// Un valor de obs_data como texto para gst_util_set_object_arg.
std::string comoTexto(obs_data_item_t* it) {
    switch (obs_data_item_gettype(it)) {
        case OBS_DATA_STRING: return obs_data_item_get_string(it);
        case OBS_DATA_BOOLEAN: return obs_data_item_get_bool(it) ? "true" : "false";
        case OBS_DATA_NUMBER: {
            if (obs_data_item_numtype(it) == OBS_DATA_NUM_INT) return std::to_string(obs_data_item_get_int(it));
            char b[64];
            std::snprintf(b, sizeof b, "%.6g", obs_data_item_get_double(it));
            for (char* c = b; *c; ++c)
                if (*c == ',') *c = '.'; // sin locale
            return b;
        }
        default: return {};
    }
}

std::vector<Filtro> leerFiltros(obs_data_t* d, const char* clave) {
    std::vector<Filtro> out;
    obs_data_array_t* a = d != nullptr ? obs_data_get_array(d, clave) : nullptr;
    for (size_t i = 0; a != nullptr && i < obs_data_array_count(a); ++i) {
        obs_data_t* x = obs_data_array_item(a, i);
        Filtro f;
        f.id = obs_data_get_string(x, "id");
        f.elemento = obs_data_get_string(x, "elemento");
        f.memoria = obs_data_get_string(x, "memoria");
        f.saleEnSistema = obs_data_has_user_value(x, "memoria_salida") &&
                          std::string(obs_data_get_string(x, "memoria_salida")).empty();
        if (f.id.empty()) f.id = std::to_string(i);
        f.nombre = f.id;
        for (char& c : f.nombre)
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        if (obs_data_t* pr = obs_data_get_obj(x, "propiedades")) {
            for (obs_data_item_t* it = obs_data_first(pr); it != nullptr; obs_data_item_next(&it)) {
                const std::string v = comoTexto(it);
                if (obs_data_item_gettype(it) != OBS_DATA_STRING || !v.empty())
                    f.props.emplace_back(obs_data_item_get_name(it), v);
            }
            obs_data_release(pr);
        }
        if (!f.elemento.empty()) out.push_back(std::move(f));
        obs_data_release(x);
    }
    obs_data_array_release(a);
    return out;
}

/// La especificación de una propiedad del elemento (sin instanciarlo).
GParamSpec* especDe(const std::string& elemento, const std::string& prop) {
    GstElementFactory* fac = gst_element_factory_find(elemento.c_str());
    if (fac == nullptr) return nullptr;
    GType t = gst_element_factory_get_element_type(fac);
    if (t == 0) {
        // Todavía no cargado: se carga el plugin.
        auto* cargada = GST_ELEMENT_FACTORY(gst_plugin_feature_load(GST_PLUGIN_FEATURE(fac)));
        if (cargada != nullptr) {
            t = gst_element_factory_get_element_type(cargada);
            gst_object_unref(cargada);
        }
    }
    gst_object_unref(fac);
    if (t == 0) return nullptr;
    auto* k = static_cast<GObjectClass*>(g_type_class_ref(t));
    GParamSpec* ps = g_object_class_find_property(k, prop.c_str());
    g_type_class_unref(k);
    return ps;
}

/// Lo que obliga a rearmar: qué elementos, en qué orden, y las propiedades que
/// no se pueden cambiar andando.
std::string firmaDe(const std::vector<Filtro>& v, const std::vector<Filtro>& a, bool alfa) {
    std::string r = alfa ? "A|" : "|";
    for (const auto* l : {&v, &a}) {
        for (const auto& f : *l) {
            r += f.id + "=" + f.elemento + "@" + f.memoria + "{";
            for (const auto& [k, val] : f.props) {
                GParamSpec* ps = especDe(f.elemento, k);
                if (ps != nullptr && (ps->flags & GST_PARAM_MUTABLE_PLAYING) != 0) continue;
                r += k + ":" + val + ";";
            }
            r += "}";
        }
        r += "#";
    }
    return r;
}

/// Las propiedades de un filtro sobre su elemento (las que tiene; las demás, aviso).
void ponerPropiedades(GstElement* e, const Filtro& f, std::vector<std::string>* avisos) {
    for (const auto& [k, v] : f.props) {
        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), k.c_str());
        if (ps == nullptr || (ps->flags & G_PARAM_WRITABLE) == 0) {
            if (avisos != nullptr) avisos->push_back(f.elemento + ": no tiene la propiedad «" + k + "»");
            continue;
        }
        gst_util_set_object_arg(G_OBJECT(e), k.c_str(), v.c_str());
    }
}

bool hayFabrica(const std::string& elemento) {
    GstElementFactory* fac = gst_element_factory_find(elemento.c_str());
    if (fac == nullptr) return false;
    gst_object_unref(fac);
    return true;
}

/// La cadena de un tipo de filtros, para gst_parse_launch ("" si no hay ninguno).
std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos) {
    std::string r;
    for (const auto& f : v) {
        if (!hayFabrica(f.elemento)) {
            avisos.push_back("no está el elemento «" + f.elemento + "» (filtro " + f.id + "): pasa sin él");
            continue;
        }
        // Un elemento que trabaja en la placa: subir antes y bajar después (si
        // ya viene en esa memoria —un decodificador de la placa—, la subida
        // pasa de largo: sin copias).
        static const std::map<std::string, std::pair<const char*, const char*>> kMemorias{
            {"CUDAMemory", {"cudaupload ! cudaconvertscale", "cudadownload"}},
            {"GLMemory", {"glupload ! glcolorconvert", "gldownload"}},
            {"D3D11Memory", {"d3d11upload ! d3d11convert", "d3d11download"}},
            {"D3D12Memory", {"d3d12upload ! d3d12convert", "d3d12download"}}};
        const auto m = kMemorias.find(f.memoria);
        bool hayPuente = m != kMemorias.end();
        if (hayPuente) {
            const std::string sube = m->second.first;
            const std::string primero = sube.substr(0, sube.find(' '));
            hayPuente = hayFabrica(primero) && hayFabrica(m->second.second);
            if (!hayPuente)
                avisos.push_back("no hay " + primero + " para darle " + f.memoria + " a «" + f.elemento +
                                 "»: va en memoria de sistema");
        }
        if (hayPuente)
            r += std::string(m->second.first) + " ! " + f.elemento + " name=" + prefijo + f.nombre + " ! " +
                 (f.saleEnSistema ? std::string() : std::string(m->second.second) + " ! ");
        else
            r += std::string(conv) + " ! " + f.elemento + " name=" + prefijo + f.nombre + " ! ";
    }
    return r;
}

/// Entre comillas para gst_parse_launch (los id de Windows traen \\ y #).
std::string entreComillas(const std::string& v) {
    std::string r = "\"";
    for (char c : v) {
        if (c == '\\' || c == '"') r += '\\';
        r += c;
    }
    return r + "\"";
}

#if defined(__linux__)
/// /dev/videoN cambia de número al reenchufar o reiniciar: el enlace estable de
/// /dev/v4l/by-id que apunta a él (si no hay, el mismo).
inline std::string idEstableV4l2(const std::string& dev) {
    std::error_code ec;
    const std::filesystem::path dir("/dev/v4l/by-id");
    if (dev.rfind("/dev/video", 0) != 0 || !std::filesystem::is_directory(dir, ec)) return dev;
    const auto real = std::filesystem::canonical(dev, ec);
    if (ec) return dev;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (std::filesystem::canonical(e.path(), ec) == real && e.path().string().find("index0") != std::string::npos)
            return e.path().string();
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (std::filesystem::canonical(e.path(), ec) == real) return e.path().string();
    return dev;
}
#endif

/// El id estable de un dispositivo de video (el que guarda `camara`).
std::string idDeCamara(GstDevice* dev, GstElement* e) {
    std::string id;
#if defined(__APPLE__)
    (void)e;
    if (GstStructure* p = gst_device_get_properties(dev)) {
        if (const gchar* u = gst_structure_get_string(p, "avf.unique_id")) id = u;
        gst_structure_free(p);
    }
#else
    (void)dev;
#if defined(_WIN32)
    const char* prop = "device-path";
#else
    const char* prop = "device";
#endif
    if (e != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) != nullptr) {
        gchar* v = nullptr;
        g_object_get(e, prop, &v, nullptr);
        if (v != nullptr) id = v;
        g_free(v);
    }
#if defined(__linux__)
    id = idEstableV4l2(id);
#endif
#endif
    return id;
}

/// Sin modo pedido: el de la cámara que mejor va con el lienzo (el tamaño del
/// lienzo si lo tiene; si no, el más grande con su proporción; si no, nada y
/// negocia GStreamer). Solo memoria de sistema.
std::string modoParaElLienzo(GstCaps* caps) {
    obs_video_info ovi = {};
    if (caps == nullptr || !obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) return {};
    const double prop = static_cast<double>(ovi.base_width) / ovi.base_height;
    int mejorW = 0, mejorH = 0;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        GstCapsFeatures* ft = gst_caps_get_features(caps, i);
        if (ft != nullptr && !gst_caps_features_is_equal(ft, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) continue;
        int w = 0, h = 0;
        const GstStructure* st = gst_caps_get_structure(caps, i);
        if (!gst_structure_get_int(st, "width", &w) || !gst_structure_get_int(st, "height", &h) || h == 0) continue;
        if (static_cast<uint32_t>(w) == ovi.base_width && static_cast<uint32_t>(h) == ovi.base_height)
            return std::to_string(w) + "x" + std::to_string(h);
        if (std::abs(static_cast<double>(w) / h - prop) < 0.02 && w * h > mejorW * mejorH) {
            mejorW = w;
            mejorH = h;
        }
    }
    return mejorW > 0 ? std::to_string(mejorW) + "x" + std::to_string(mejorH) : std::string();
}

/// La parte de origen de la cámara `id` como está enchufada AHORA ("" si no está).
std::string origenDeCamara(const std::string& id, std::string modo) {
    // El monitor de dispositivos: cuál es la cámara ahora (avfvideosrc solo sabe
    // de índices, que cambian al enchufar o desenchufar otra) y qué modos tiene.
    std::string o;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    if (gst_device_monitor_start(mon)) {
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr && o.empty(); l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            GstElement* e = gst_device_create_element(dev, nullptr);
            const bool es = (id.empty() || id == "default") || idDeCamara(dev, e) == id;
            if (es && e != nullptr) {
#if defined(__APPLE__)
                gint indice = -1;
                g_object_get(e, "device-index", &indice, nullptr);
                if (indice >= 0) o = "avfvideosrc device-index=" + std::to_string(indice);
#elif defined(_WIN32)
                o = "mfvideosrc device-path=" + entreComillas(idDeCamara(dev, e));
#else
                o = "v4l2src device=" + entreComillas(idDeCamara(dev, e));
#endif
                if (!o.empty() && modo.empty()) {
                    GstCaps* caps = gst_device_get_caps(dev);
                    modo = modoParaElLienzo(caps);
                    if (caps != nullptr) gst_caps_unref(caps);
                }
            }
            if (e != nullptr) gst_object_unref(e);
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
    if (o.empty()) return {};
    // El modo («1920x1080@30»): tamaño y cuadros; el formato lo negocia.
    int w = 0, h = 0, fps = 0;
    if (!modo.empty() && std::sscanf(modo.c_str(), "%dx%d@%d", &w, &h, &fps) >= 2 && w > 0 && h > 0)
        o += " ! video/x-raw,width=" + std::to_string(w) + ",height=" + std::to_string(h) +
             (fps > 0 ? ",framerate=" + std::to_string(fps) + "/1" : std::string());
    return o;
}

// --- El vigía de dispositivos ------------------------------------------------------
//
// Un GstDeviceMonitor de todo el proceso (micrófonos y cámaras): sabe qué
// dispositivos hay y avisa (DEVICE_ADDED / DEVICE_REMOVED, por su bus) cuando
// uno se conecta o se va. Las entradas que esperan uno no rearman cada 2 s:
// miran la generación del vigía y se arman apenas aparece el suyo; las que lo
// están usando se desarman si se va (algunas capturadoras no dan error al
// desenchufarlas: dejan de mandar y nada más).

/// El id estable de un micrófono (el que guarda `microfono`).
std::string idDeMicrofono(GstElement* e) {
    if (e == nullptr) return {};
#if defined(__APPLE__)
    const char* prop = "unique-id";
#else
    const char* prop = "device";
#endif
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) == nullptr) return {};
    gchar* v = nullptr;
    g_object_get(e, prop, &v, nullptr);
    std::string id = v != nullptr ? v : "";
    g_free(v);
    return id;
}

struct Vigia {
    std::mutex mu;
    GstDeviceMonitor* mon = nullptr;
    std::set<std::string> audio, video;
    std::atomic<std::uint64_t> generacion{1};

    /// Anota (o saca) un dispositivo. Con mu tomado.
    void anotar(GstDevice* dev, bool esta) {
        GstElement* e = gst_device_create_element(dev, nullptr);
        const bool esVideo = gst_device_has_classes(dev, "Video/Source");
        const std::string id = esVideo ? idDeCamara(dev, e) : idDeMicrofono(e);
        if (e != nullptr) gst_object_unref(e);
        if (id.empty()) return;
        auto& conjunto = esVideo ? video : audio;
        if (esta) conjunto.insert(id);
        else conjunto.erase(id);
    }

    void arrancar() {
        if (mon != nullptr) return;
        mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Audio/Source", nullptr);
        gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
        if (!gst_device_monitor_start(mon)) return;
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr; l = l->next) anotar(GST_DEVICE(l->data), true);
        g_list_free_full(lista, gst_object_unref);
    }

    /// Lee los avisos del monitor (desde el tic de cualquier entrada; no bloquea).
    void revisar() {
        std::unique_lock<std::mutex> l(mu, std::try_to_lock);
        if (!l.owns_lock()) return;
        arrancar();
        if (mon == nullptr) return;
        GstBus* bus = gst_device_monitor_get_bus(mon);
        bool cambio = false;
        while (GstMessage* m = gst_bus_pop(bus)) {
            GstDevice* dev = nullptr;
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_DEVICE_ADDED) {
                gst_message_parse_device_added(m, &dev);
                anotar(dev, true);
                cambio = true;
            } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_DEVICE_REMOVED) {
                gst_message_parse_device_removed(m, &dev);
                anotar(dev, false);
                cambio = true;
            }
            if (dev != nullptr) gst_object_unref(dev);
            gst_message_unref(m);
        }
        gst_object_unref(bus);
        if (cambio) ++generacion;
    }

    /// ¿Está? (sin saberlo todavía: sí, para no trabar el arranque).
    bool hay(bool esVideo, const std::string& id) {
        std::lock_guard<std::mutex> l(mu);
        arrancar();
        if (mon == nullptr) return true;
        return (esVideo ? video : audio).count(id) > 0;
    }
};

Vigia& vigia() {
    static Vigia* v = new Vigia(); // vive lo que el proceso
    return *v;
}

/// Qué tiene un archivo (video, audio), con GstDiscoverer. false + error si no se puede leer.
bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error) {
    video = audio = false;
    GError* e = nullptr;
    GstDiscoverer* d = gst_discoverer_new(5 * GST_SECOND, &e);
    if (d == nullptr) {
        error = e != nullptr ? e->message : "no se pudo mirar el archivo";
        if (e != nullptr) g_error_free(e);
        return false;
    }
    GstDiscovererInfo* info = gst_discoverer_discover_uri(d, uri.c_str(), &e);
    bool ok = info != nullptr && gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK;
    if (ok) {
        GList* v = gst_discoverer_info_get_video_streams(info);
        GList* a = gst_discoverer_info_get_audio_streams(info);
        // Una imagen sola cuenta como video (un cuadro).
        video = v != nullptr;
        audio = a != nullptr;
        gst_discoverer_stream_info_list_free(v);
        gst_discoverer_stream_info_list_free(a);
        ok = video || audio;
        if (!ok) error = "el archivo no tiene video ni audio";
    } else {
        error = e != nullptr ? e->message : "no se pudo leer el archivo";
    }
    if (e != nullptr) g_error_free(e);
    if (info != nullptr) gst_discoverer_info_unref(info);
    g_object_unref(d);
    return ok;
}

void armar(Fuente* f) {
    desarmar(f);
    f->segmentoListo = false;
    f->avisosFiltros.clear();
    f->controlEnviado = false; // la cadena nueva lo recibe apenas pasen cuadros
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
    if (f->uri.empty() && f->descripcion.empty() && f->descripcionAudio.empty() && f->camara.empty()) return;
    // La cámara del equipo: se busca cuál es ahora; si no está, se reintenta.
    std::string origenV = f->descripcion;
    // Un dispositivo del equipo que no está: a esperar que aparezca (el vigía avisa).
    const auto esperar = [f](const std::string& que) {
        if (!f->esperaDispositivo)
            blog(LOG_WARNING, "[ba_gst_fuente] %s: se espera a que se conecte", que.c_str());
        f->error = que + ": esperando que se conecte";
        f->esperaDispositivo = true;
        f->genVista = vigia().generacion.load();
        f->esperaDesdeNs = os_gettime_ns();
    };
    if (!f->microfono.empty() && f->microfono != "default" && !vigia().hay(false, f->microfono)) {
        esperar("el micrófono " + f->microfono);
        return;
    }
    if (!f->camara.empty()) {
        origenV = origenDeCamara(f->camara, f->camaraModo);
        if (origenV.empty()) {
            esperar("la cámara " + f->camara);
            return;
        }
    }
    if (f->esperaDispositivo) blog(LOG_INFO, "[ba_gst_fuente] %s: apareció, se conecta", f->camara.empty() ? f->microfono.c_str() : f->camara.c_str());
    f->esperaDispositivo = false;
    f->clock = relojDe(f->reloj, f->relojServidor, f->relojPuerto, f->relojDominio, f->error);
    if (f->clock == nullptr) return;
    // Estándar: los sinks esperan al reloj (sync=true) con una cola de espera
    // delante (los filtros trabajan al llegar el cuadro; la espera es acá).
    // Colchón (respaldo): no esperan (sync=false) y la hora la pone el colchón.
    // async=false: una rama sin datos (un stream sin audio) no traba el arranque.
    f->estandar = f->sincronia == "gstreamer";
    const std::string desf = std::to_string(f->desfaseMs * static_cast<std::int64_t>(GST_MSECOND));
    // Un archivo (ba_gst_medio) sí hace preroll (async=true): solo tiene las ramas
    // de lo que trae, así que ninguna se queda esperando, y con preroll los saltos
    // con flush (en pausa, al terminar, para repetir) funcionan como manda
    // GStreamer: RESET_TIME, ASYNC_DONE y el pipeline recalcula su base_time.
    const std::string preroll = f->medio ? "async=true" : "async=false";
    const std::string sinkVideoS =
        f->estandar ? "queue name=ba_espera_v max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000 ! "
                      "appsink name=ba_video sync=true " + preroll + " max-buffers=4 drop=false emit-signals=false ts-offset=" +
                          desf
                    : "appsink name=ba_video sync=false async=false max-buffers=8 drop=false emit-signals=false";
    const std::string sinkAudioS =
        f->estandar ? "queue name=ba_espera_a max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000 ! "
                      "appsink name=ba_audio sync=true " + preroll + " max-buffers=64 drop=false emit-signals=false ts-offset=" +
                          desf
                    : "appsink name=ba_audio sync=false async=false max-buffers=64 drop=false emit-signals=false";
    const char* sinkVideo = sinkVideoS.c_str();
    const char* sinkAudio = sinkAudioS.c_str();
    // NORMALIZAR (de fábrica): GStreamer deja todo igual antes de libobs
    // —desentrelaza solo si viene entrelazado (`deinterlace mode=auto`), lleva
    // a los cuadros del lienzo (`videorate`) y al color del estudio (el que
    // diga Ajustes > Video: formato, espacio de color y rango; de fábrica NV12,
    // BT.709, limitado)—. Así una cámara rara (JPEG de rango completo, 601 en
    // HD, 25i) entra como las demás. Sin normalizar, pasa tal cual (también
    // 10 bits y 4:2:2).
    std::string capsVideo = std::string("video/x-raw,format=") + kFormatosObs;
    std::string antesSink = "videoconvert ! ";
    if (f->normalizar) {
        obs_video_info ovi = {};
        const bool hay = obs_get_video_info(&ovi) && ovi.fps_num > 0;
        antesSink = "deinterlace mode=auto ! videorate ! videoconvertscale ! ";
        // pixel-aspect-ratio=1/1: libobs no sabe de píxeles no cuadrados (una
        // cámara PAL de 704x576 con píxel 12/11 se vería apretada): se escala.
        if (!hay) ovi = obs_video_info{};
        capsVideo = capsColorDelEstudio(ovi) + ",interlace-mode=progressive,pixel-aspect-ratio=1/1";
        if (hay) capsVideo += ",framerate=" + std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den);
    }
    // LOS FILTROS: las tarjetas del grafo, como elementos de GStreamer. La
    // superresolución vieja (`superres_motor`) es un filtro más.
    std::vector<Filtro> filtrosV = f->filtrosV;
    if (filtrosV.empty()) {
        const std::string motor =
            !f->superresMotor.empty() ? f->superresMotor : (f->superres ? "apple" : "");
        Filtro sr{"superres", "", "superres", {}};
        if (motor == "apple")
            sr.elemento = "bavtsuperres";
        else if (motor == "fsr")
            sr.elemento = "bafsr";
        else if (motor == "onnx" && !f->superresModelo.empty()) {
            sr.elemento = "baonnxsr";
            sr.props.emplace_back("modelo", f->superresModelo);
        }
        if (!sr.elemento.empty()) filtrosV.push_back(std::move(sr));
    }
    const std::string cadenaV = cadenaDe(filtrosV, "videoconvert", "ba_f_", f->avisosFiltros);
    if (!cadenaV.empty()) {
        // ANTES de `videorate`: si el lienzo va a más cuadros que la cámara, no
        // se procesa cada cuadro repetido. Con una cola que descarta lo viejo:
        // si un filtro tarda, no se acumula demora. Píxel cuadrado primero (las
        // redes y libobs no saben de píxeles no cuadrados).
        // Si el primer filtro trabaja en la placa, va PRIMERO (pegado al
        // decodificador, que entonces entrega en esa memoria: sin bajar a la
        // CPU) y lo que solo anda en la CPU (desentrelazar, píxel cuadrado)
        // después.
        const bool placaPrimero = !filtrosV.empty() && !filtrosV.front().memoria.empty();
        const std::string cola = "queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! ";
        if (placaPrimero) {
            antesSink = cola + cadenaV + "videoconvert ! " +
                        (f->normalizar ? "deinterlace mode=auto ! videorate ! videoconvertscale ! " : "");
        } else {
            const std::string cadena =
                std::string(f->normalizar ? "videoconvertscale ! video/x-raw,pixel-aspect-ratio=1/1 ! " : "") +
                cola + cadenaV + "videoconvert ! ";
            antesSink = f->normalizar ? "deinterlace mode=auto ! " + cadena + "videorate ! videoconvertscale ! "
                                      : cadena;
        }
    }
    if (f->alfa) {
        // Con transparencia (croma, recorte): BGRA, que libobs recibe con alfa.
        capsVideo = "video/x-raw,format=BGRA";
        if (f->normalizar) {
            obs_video_info ovi = {};
            capsVideo += ",interlace-mode=progressive,pixel-aspect-ratio=1/1";
            if (obs_get_video_info(&ovi) && ovi.fps_num > 0)
                capsVideo += ",framerate=" + std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den);
        }
    }
    std::string cadenaA = cadenaDe(f->filtrosA, "audioconvert", "ba_fa_", f->avisosFiltros);
    f->rackFaltaDesde = 0;
    if (!f->rack.empty()) {
        if (hayFabrica("barack")) {
            std::string nom;
            for (char c : f->rack) {
                if (c == '\\' || c == '"') nom += '\\';
                nom += c;
            }
            cadenaA = "audioconvert ! audioresample ! audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! "
                      "barack name=ba_rack fuente=\"" + nom + "\" ! " + cadenaA;
        } else {
            f->avisosFiltros.push_back("el rack de audio todavía no está (la app no registró barack): se agrega cuando esté");
            f->rackFaltaDesde = os_gettime_ns();
        }
    }
    std::string completo;
    if (!f->uri.empty()) {
        // Un archivo: solo las ramas de lo que tiene (una rama sin datos nunca
        // termina y el archivo no avisaría que terminó), con los pads por nombre
        // (si no, el audio de un mp3 se puede enganchar en la rama del video).
        bool hayV = true, hayA = f->conAudio;
        std::string padV = "ba_dec.", padA = "ba_dec.";
        if (f->medio) {
            if (!queTieneElArchivo(f->uri, hayV, hayA, f->error)) {
                f->estadoMedio = OBS_MEDIA_STATE_ERROR;
                blog(LOG_WARNING, "[ba_gst_medio] %s: %s", f->uri.c_str(), f->error.c_str());
                return;
            }
            hayA = hayA && f->conAudio;
            padV = "ba_dec.video_0";
            padA = "ba_dec.audio_0";
        }
        completo = "uridecodebin3 name=ba_dec uri=\"" + f->uri + "\"";
        if (hayV) completo += " " + padV + " ! queue name=ba_origen ! " + antesSink + capsVideo + " ! " + sinkVideo;
        if (hayA)
            completo += " " + padA + " ! queue ! " + cadenaA +
                        "audioconvert ! audioresample ! "
                        "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! " +
                        sinkAudio;
    } else {
        if (!origenV.empty())
            completo = origenV + " ! queue name=ba_origen ! " + antesSink + capsVideo + " ! " + sinkVideo;
        // El audio de un pipeline va aparte (`pipeline_audio`): p. ej. el de un
        // plugin, o un micrófono que acompaña a una cámara.
        if (!f->descripcionAudio.empty() && f->conAudio)
            completo += std::string(completo.empty() ? "" : " ") + f->descripcionAudio +
                        " ! queue ! " + cadenaA +
                        "audioconvert ! audioresample ! "
                        "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! " +
                        sinkAudio;
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
            gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
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
    // Las propiedades de cada filtro (las que no tiene, se avisan).
    for (const auto* l : {&filtrosV, &f->filtrosA})
        for (const auto& x : *l) {
            const std::string n = (l == &filtrosV ? "ba_f_" : "ba_fa_") + x.nombre;
            if (GstElement* e = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
                ponerPropiedades(e, x, &f->avisosFiltros);
                gst_object_unref(e);
            }
        }
    for (const auto& a : f->avisosFiltros)
        blog(LOG_WARNING, "[ba_gst_fuente] %s", a.c_str());
    enganchar(f, p, "ba_video", alVideo);
    enganchar(f, p, "ba_audio", alAudio);
    // EL RELOJ NUNCA LO PONE UN DISPOSITIVO: ningún elemento ofrece el suyo (un
    // micrófono que se desenchufa se llevaría el reloj del pipeline: CLOCK_LOST y
    // todo trabado). Siempre el del estudio; la deriva del dispositivo la corrige
    // él mismo remuestreando (slave-method).
    if (GstIterator* it = gst_bin_iterate_recurse(GST_BIN(p))) {
        GValue v = G_VALUE_INIT;
        while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
            auto* e = GST_ELEMENT(g_value_get_object(&v));
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "provide-clock") != nullptr)
                g_object_set(e, "provide-clock", FALSE, nullptr);
            g_value_reset(&v);
        }
        g_value_unset(&v);
        gst_iterator_free(it);
    }
    gst_pipeline_use_clock(GST_PIPELINE(p), f->clock);
    if (f->estandar) {
        // La misma latencia que las demás cadenas del estudio (con el mismo reloj, la
        // hora de cada cuadro de una fuente en vivo sale del reloj al capturar: el
        // base_time de cada cadena se cancela y quedan alineadas).
        f->latObjetivoNs = std::max<std::int64_t>(f->estudio ? latenciaDelEstudio(f, 0) : 0,
                                                  f->latenciaMs * static_cast<std::int64_t>(GST_MSECOND));
        // Un archivo no es en vivo: su latencia es la natural de GStreamer (sin
        // esperar de más; con una forzada, cada salto arrancaba tarde).
        if (!f->medio) gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(f->latObjetivoNs));
        // Lo que se mide al llegar a la cola de espera (antes de esperar al reloj):
        // cuán viejo llega cada cuadro y cuánto le falta para su hora.
        for (const char* q : {"ba_espera_v", "ba_espera_a"})
            if (GstElement* e = gst_bin_get_by_name(GST_BIN(p), q)) {
                if (GstPad* pad = gst_element_get_static_pad(e, "sink")) {
                    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, alLlegar,
                                      new std::pair<Fuente*, bool>(f, std::string(q) == "ba_espera_v"),
                                      [](gpointer d) { delete static_cast<std::pair<Fuente*, bool>*>(d); });
                    gst_object_unref(pad);
                }
                gst_object_unref(e);
            }
    }
    f->pipeline = p;
    {
        std::lock_guard<std::mutex> l(f->colaLock);
        f->recibidos = f->tarde = f->saltados = f->descartados = f->bloquesA = 0;
        f->ventanaNs = f->tardeVentana = f->recibidosVentana = f->tarde10 = f->recibidos10 = 0;
        f->adelantoMs = 0;
        f->retrasoV = f->retrasoA = 0;
        f->compMs = 0;
        if (f->edades != nullptr) f->edades->clear();
        f->edadP50 = f->edadP90 = 0;
    }
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        f->error = "el pipeline no arrancó";
        desarmar(f);
        if (f->medio) {
            f->estadoMedio = OBS_MEDIA_STATE_ERROR;
            blog(LOG_WARNING, "[ba_gst_medio] %s: el pipeline no arrancó", f->uri.c_str());
        }
        return;
    }
    if (f->medio) f->estadoMedio = OBS_MEDIA_STATE_PLAYING;
}

const char* nombre(void*) { return obs_module_text("Fuente GStreamer (BA)"); }

void actualizar(void* data, obs_data_t* s) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    std::string uri = obs_data_get_string(s, "uri"), d = obs_data_get_string(s, "pipeline"),
                da = obs_data_get_string(s, "pipeline_audio");
    if (f->medio) {
        // Un archivo, con los ajustes de ffmpeg_source.
        uri.clear();
        d.clear();
        da.clear();
        const char* lf = obs_data_get_string(s, "local_file");
        if (lf != nullptr && *lf != '\0') {
            GError* e = nullptr;
            if (gchar* u = gst_filename_to_uri(lf, &e)) {
                uri = u;
                g_free(u);
            }
            if (e != nullptr) g_error_free(e);
        }
        f->bucle = obs_data_get_bool(s, "looping");
        f->limpiarAlTerminar = obs_data_get_bool(s, "clear_on_media_end");
        f->reiniciarAlActivar = obs_data_get_bool(s, "restart_on_activate");
        f->cerrarInactiva = obs_data_get_bool(s, "close_when_inactive");
    }
    const std::string reloj = obs_data_get_string(s, "reloj"),
                      servidor = obs_data_get_string(s, "reloj_servidor");
    const int puerto = static_cast<int>(obs_data_get_int(s, "reloj_puerto"));
    const int dominio = static_cast<int>(obs_data_get_int(s, "reloj_dominio"));
    const bool audio = obs_data_get_bool(s, "audio");
    const std::string rack = obs_data_get_string(s, "rack");
    const std::string camara = obs_data_get_string(s, "camara"), camaraModo = obs_data_get_string(s, "camara_modo");
    const bool estudio = f->medio ? false : obs_data_get_bool(s, "estudio");
    const std::string microfono = obs_data_get_string(s, "microfono");
    const bool normalizar = obs_data_get_bool(s, "normalizar");
    const bool superres = obs_data_get_bool(s, "superres");
    const std::string superresMotor = obs_data_get_string(s, "superres_motor");
    const std::string superresModelo = obs_data_get_string(s, "superres_modelo");
    const bool ntpSync = obs_data_get_bool(s, "ntp_sync");
    // Un archivo va con la sincronía estándar (los sinks esperan al reloj: tiempo real).
    const std::string sincronia =
        f->medio || std::string(obs_data_get_string(s, "sincronia")) == "gstreamer" ? "gstreamer" : "colchon";
    // Los de audio también pueden venir aparte (`filtros_audio`: un arreglo):
    // los pone otra parte del programa que el grafo de video.
    const std::string filtrosJson =
        std::string(obs_data_get_string(s, "filtros")) + "|" + obs_data_get_string(s, "filtros_audio");
    std::vector<Filtro> filtrosV, filtrosA;
    bool alfa = false;
    if (const char* fv = obs_data_get_string(s, "filtros"); fv != nullptr && *fv != '\0') {
        if (obs_data_t* fj = obs_data_create_from_json(fv)) {
            filtrosV = leerFiltros(fj, "video");
            filtrosA = leerFiltros(fj, "audio");
            alfa = obs_data_get_bool(fj, "alfa");
            obs_data_release(fj);
        }
    }
    if (const char* fa = obs_data_get_string(s, "filtros_audio"); fa != nullptr && *fa != '\0') {
        const std::string envuelto = std::string("{\"audio\":") + fa + "}";
        if (obs_data_t* fj = obs_data_create_from_json(envuelto.c_str())) {
            for (auto& x : leerFiltros(fj, "audio"))
                filtrosA.push_back(std::move(x));
            obs_data_release(fj);
        }
    }
    const std::string firma = firmaDe(filtrosV, filtrosA, alfa);
    const std::int64_t latencia = obs_data_get_int(s, "latencia"), desfase = obs_data_get_int(s, "desfase");
    // La latencia y el desfase se cambian EN VIVO (solo mueven el colchón); lo
    // demás rearma el pipeline. (Con `uri`, la latencia también va al jitter
    // buffer de RTSP/SRT: se rearma.)
    const bool rearmar = uri != f->uri || d != f->descripcion || da != f->descripcionAudio ||
                         reloj != f->reloj || servidor != f->relojServidor || puerto != f->relojPuerto ||
                         dominio != f->relojDominio || audio != f->conAudio || normalizar != f->normalizar ||
                         superres != f->superres || superresMotor != f->superresMotor ||
                         superresModelo != f->superresModelo || ntpSync != f->ntpSync ||
                         firma != f->firmaFiltros || sincronia != f->sincronia || rack != f->rack ||
                         camara != f->camara || camaraModo != f->camaraModo || microfono != f->microfono ||
                         (latencia != f->latenciaMs && !uri.empty()) || f->pipeline == nullptr;
    const std::int64_t corrimiento =
        (latencia + desfase - f->latenciaMs - f->desfaseMs) * static_cast<std::int64_t>(GST_MSECOND);
    f->uri = uri;
    f->descripcion = d;
    f->descripcionAudio = da;
    f->reloj = reloj;
    f->relojServidor = servidor;
    f->relojPuerto = puerto;
    f->relojDominio = dominio;
    f->conAudio = audio;
    f->rack = rack;
    f->camara = camara;
    f->camaraModo = camaraModo;
    f->microfono = microfono;
    f->estudio = estudio;
    f->normalizar = normalizar;
    f->superres = superres;
    f->superresMotor = superresMotor;
    f->superresModelo = superresModelo;
    const bool filtrosCambiaron = filtrosJson != f->filtrosJson;
    f->filtrosJson = filtrosJson;
    f->filtrosV = std::move(filtrosV);
    f->filtrosA = std::move(filtrosA);
    f->alfa = alfa;
    f->firmaFiltros = firma;
    if (ntpSync != f->ntpSync) f->ntpApagadoSolo = false; // lo pidieron de nuevo: se vuelve a probar
    f->ntpSync = ntpSync;
    f->latenciaMs = latencia;
    f->desfaseMs = desfase;
    f->sincronia = sincronia;
    if (rearmar) {
        armar(f);
        return;
    }
    // Solo cambiaron propiedades que se pueden cambiar andando: en vivo.
    if (filtrosCambiaron && f->pipeline != nullptr)
        for (const auto* l : {&f->filtrosV, &f->filtrosA})
            for (const auto& x : *l) {
                const std::string n = (l == &f->filtrosV ? "ba_f_" : "ba_fa_") + x.nombre;
                if (GstElement* e = gst_bin_get_by_name(GST_BIN(f->pipeline), n.c_str())) {
                    ponerPropiedades(e, x, nullptr);
                    gst_object_unref(e);
                }
            }
    // Estándar: el desfase es el ts-offset de los sinks (en vivo); la latencia la
    // ajusta el tic (la del estudio).
    if (f->estandar && f->pipeline != nullptr) {
        for (const char* n : {"ba_video", "ba_audio"})
            if (GstElement* e = gst_bin_get_by_name(GST_BIN(f->pipeline), n)) {
                g_object_set(e, "ts-offset", static_cast<gint64>(f->desfaseMs * static_cast<std::int64_t>(GST_MSECOND)), nullptr);
                gst_object_unref(e);
            }
        return;
    }
    // Lo que ya está en el colchón se corre lo mismo.
    std::lock_guard<std::mutex> lc(f->colaLock);
    for (auto* q : {&f->video, &f->audio})
        for (auto& p : *q)
            p.vence = static_cast<GstClockTime>(
                std::max<std::int64_t>(0, static_cast<std::int64_t>(p.vence) + corrimiento));
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* f = new (std::nothrow) Fuente();
    if (f == nullptr) return nullptr;
    f->ctx = ctx;
    // ba_gst_medio: el mismo código, con un archivo y los controles de medios.
    f->medio = std::string(obs_source_get_unversioned_id(ctx)) == "ba_gst_medio";
    // El colchón ya decide cuándo va cada cuadro: libobs lo muestra al llegar.
    obs_source_set_async_unbuffered(ctx, true);
    actualizar(f, settings);
    proc_handler_add(
        obs_source_get_proc_handler(ctx), "void estado(out string error, out bool andando, out string info)",
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
            obs_data_set_bool(o, "superres", x->superres || !x->superresMotor.empty());
            obs_data_set_string(o, "superres_motor",
                                !x->superresMotor.empty() ? x->superresMotor.c_str()
                                                          : (x->superres ? "apple" : ""));
            obs_data_set_string(o, "superres_modelo", x->superresModelo.c_str());
            if (x->pipeline != nullptr) {
                if (GstElement* sr = gst_bin_get_by_name(GST_BIN(x->pipeline), "ba_f_superres")) {
                    gdouble ms = 0;
                    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sr), "ms-por-cuadro") != nullptr)
                        g_object_get(sr, "ms-por-cuadro", &ms, nullptr);
                    obs_data_set_double(o, "superres_ms", ms);
                    if (g_object_class_find_property(G_OBJECT_GET_CLASS(sr), "error") != nullptr) {
                        gchar* e = nullptr;
                        gchar* pr = nullptr;
                        g_object_get(sr, "error", &e, "proveedor-usado", &pr, nullptr);
                        if (e != nullptr) obs_data_set_string(o, "superres_error", e);
                        if (pr != nullptr) obs_data_set_string(o, "superres_proveedor", pr);
                        g_free(e);
                        g_free(pr);
                    }
                    gst_object_unref(sr);
                }
            }
            // Cada filtro: su elemento y lo que informe (lo que tarda, el motor, por qué no anda).
            obs_data_array_t* fs = obs_data_array_create();
            for (const auto* l : {&x->filtrosV, &x->filtrosA})
                for (const auto& fl : *l) {
                    obs_data_t* e = obs_data_create();
                    obs_data_set_string(e, "id", fl.id.c_str());
                    obs_data_set_string(e, "elemento", fl.elemento.c_str());
                    obs_data_set_bool(e, "audio", l == &x->filtrosA);
                    const std::string n = (l == &x->filtrosV ? "ba_f_" : "ba_fa_") + fl.nombre;
                    GstElement* el =
                        x->pipeline != nullptr ? gst_bin_get_by_name(GST_BIN(x->pipeline), n.c_str()) : nullptr;
                    obs_data_set_bool(e, "andando", el != nullptr);
                    if (el != nullptr) {
                        GObjectClass* k = G_OBJECT_GET_CLASS(el);
                        if (GParamSpec* ps = g_object_class_find_property(k, "ms-por-cuadro");
                            ps != nullptr && ps->value_type == G_TYPE_DOUBLE) {
                            gdouble ms = 0;
                            g_object_get(el, "ms-por-cuadro", &ms, nullptr);
                            obs_data_set_double(e, "ms", ms);
                        }
                        // Lo que informa (sus propiedades de solo lectura: lo que tarda,
                        // el motor, el factor, por qué no anda…), sea el elemento que sea.
                        guint n = 0;
                        GParamSpec** pss = g_object_class_list_properties(k, &n);
                        obs_data_t* info = obs_data_create();
                        for (guint i = 0; i < n; ++i) {
                            GParamSpec* ps = pss[i];
                            if ((ps->flags & G_PARAM_READABLE) == 0 || (ps->flags & G_PARAM_WRITABLE) != 0) continue;
                            if (ps->owner_type == GST_TYPE_ELEMENT || ps->owner_type == GST_TYPE_OBJECT ||
                                ps->owner_type == G_TYPE_OBJECT)
                                continue;
                            GValue v = G_VALUE_INIT;
                            g_value_init(&v, ps->value_type);
                            g_object_get_property(G_OBJECT(el), ps->name, &v);
                            if (G_VALUE_HOLDS_STRING(&v)) {
                                const char* t = g_value_get_string(&v);
                                if (t != nullptr && *t != '\0') obs_data_set_string(info, ps->name, t);
                            } else if (G_VALUE_HOLDS_DOUBLE(&v)) {
                                obs_data_set_double(info, ps->name, g_value_get_double(&v));
                            } else if (G_VALUE_HOLDS_INT(&v)) {
                                obs_data_set_int(info, ps->name, g_value_get_int(&v));
                            } else if (G_VALUE_HOLDS_BOOLEAN(&v)) {
                                obs_data_set_bool(info, ps->name, g_value_get_boolean(&v));
                            }
                            g_value_unset(&v);
                        }
                        g_free(pss);
                        // Lo de siempre, arriba (compatibilidad): error, motor-usado…
                        for (const char* prop : {"error", "motor-usado", "proveedor-usado", "unidades-usadas"})
                            if (obs_data_has_user_value(info, prop))
                                obs_data_set_string(e, prop, obs_data_get_string(info, prop));
                        obs_data_set_obj(e, "info", info);
                        obs_data_release(info);
                        // Y lo que negoció: el tamaño que sale.
                        if (GstPad* pad = gst_element_get_static_pad(el, "src")) {
                            if (GstCaps* c = gst_pad_get_current_caps(pad)) {
                                gchar* t = gst_caps_to_string(c);
                                obs_data_set_string(e, "caps", t);
                                g_free(t);
                                gst_caps_unref(c);
                            }
                            gst_object_unref(pad);
                        }
                        gst_object_unref(el);
                    }
                    obs_data_array_push_back(fs, e);
                    obs_data_release(e);
                }
            obs_data_set_array(o, "filtros", fs);
            obs_data_array_release(fs);
            obs_data_array_t* av = obs_data_array_create();
            for (const auto& a : x->avisosFiltros) {
                obs_data_t* e = obs_data_create();
                obs_data_set_string(e, "aviso", a.c_str());
                obs_data_array_push_back(av, e);
                obs_data_release(e);
            }
            obs_data_set_array(o, "avisos_filtros", av);
            obs_data_array_release(av);
            obs_data_set_bool(o, "alfa", x->alfa);
            obs_data_set_bool(o, "ntp_sync", x->ntpSync && !x->ntpApagadoSolo);
            obs_data_set_bool(o, "ntp_apagado_solo", x->ntpApagadoSolo);
            obs_data_set_int(o, "desfase_ms", x->desfaseMs);
            obs_data_set_string(o, "sincronia", x->estandar ? "gstreamer" : "colchon");
            if (x->estandar) {
                obs_data_set_int(o, "latencia_estudio_ms", x->latObjetivoNs / static_cast<std::int64_t>(GST_MSECOND));
                obs_data_set_int(o, "latencia_declarada_ms", std::lround(x->declaradoMs));
                std::lock_guard<std::mutex> lc(x->colaLock);
                obs_data_set_int(o, "llegada_p50_ms", std::lround(x->edadP50));
                obs_data_set_int(o, "llegada_p90_ms", std::lround(x->edadP90));
            }
            {
                std::lock_guard<std::mutex> lc(x->colaLock);
                obs_data_set_string(o, "formato", x->formato.c_str());
                obs_data_set_string(o, "colorimetria", x->colorimetria.c_str());
                obs_data_set_int(o, "ancho", x->ancho);
                obs_data_set_int(o, "alto", x->alto);
                obs_data_set_bool(o, "entrelazado", x->entrelazado);
                obs_data_set_double(o, "fps_declarados", x->fpsDeclarado);
                obs_data_set_double(o, "fps_recibidos", x->fpsMedido);
                obs_data_set_int(o, "recibidos", static_cast<long long>(x->recibidos));
                obs_data_set_int(o, "llegaron_tarde", static_cast<long long>(x->tarde));
                obs_data_set_int(o, "saltados", static_cast<long long>(x->saltados));
                obs_data_set_int(o, "descartados", static_cast<long long>(x->descartados));
                obs_data_set_int(o, "colchon_ms", x->colchonMs);
                obs_data_set_int(o, "adelanto_ms", static_cast<long long>(x->adelantoMs));
                // Lo de los últimos 10 s (o lo que va, si todavía no pasaron).
                obs_data_set_int(o, "tarde_10s",
                                 static_cast<long long>(x->recibidos10 ? x->tarde10 : x->tardeVentana));
                obs_data_set_int(
                    o, "recibidos_10s",
                    static_cast<long long>(x->recibidos10 ? x->recibidos10 : x->recibidosVentana));
                obs_data_set_int(o, "en_colchon", static_cast<long long>(x->video.size()));
                // La demora de los filtros (video respecto del audio, medida) y lo
                // que el colchón agregó para que sigan juntos.
                obs_data_set_int(o, "retraso_video_ms", std::lround(x->retrasoV));
                obs_data_set_int(o, "retraso_audio_ms", std::lround(x->retrasoA));
                obs_data_set_int(o, "compensacion_ms", x->compMs);
                if (!x->origen.empty()) {
                    obs_data_t* og = obs_data_create_from_json(x->origen.c_str());
                    obs_data_set_obj(o, "origen", og);
                    obs_data_release(og);
                }
            }
            if (x->control != nullptr) {
                gchar* c = gst_structure_to_string(x->control);
                obs_data_set_string(o, "control", c);
                g_free(c);
            }
            calldata_set_string(cd, "info", obs_data_get_json(o));
            obs_data_release(o);
        },
        f);
    // El canal de control: un JSON plano que se suma a lo último pedido y va por
    // la cadena como evento «ba/control» (ver arriba). Se recuerda: si la cadena
    // se rearma, lo vuelve a recibir.
    proc_handler_add(
        obs_source_get_proc_handler(ctx), "void control(in string json, out bool ok)",
        [](void* d, calldata_t* cd) {
            auto* x = static_cast<Fuente*>(d);
            GstStructure* nuevo = estructuraDe(calldata_string(cd, "json"));
            if (nuevo == nullptr) {
                calldata_set_bool(cd, "ok", false);
                return;
            }
            std::lock_guard<std::mutex> l(x->lock);
            if (x->control == nullptr) {
                x->control = nuevo;
            } else {
                gst_structure_foreach(
                    nuevo,
                    [](GQuark campo, const GValue* v, gpointer dst) -> gboolean {
                        gst_structure_id_set_value(static_cast<GstStructure*>(dst), campo, v);
                        return TRUE;
                    },
                    x->control);
                gst_structure_free(nuevo);
            }
            x->controlEnviado = enviarControl(x);
            calldata_set_bool(cd, "ok", true);
        },
        f);
    return f;
}

void destruir(void* data) {
    auto* f = static_cast<Fuente*>(data);
    olvidarCompensacion(f);
    olvidarLatencia(f);
    {
        std::lock_guard<std::mutex> l(f->lock);
        desarmar(f);
        if (f->control != nullptr) gst_structure_free(f->control);
    }
    delete f->edades;
    delete f->placa;
    delete f;
}

void irA(Fuente* f, std::int64_t ns);
void procesarMensaje(Fuente* f, GstMessage* m);

/// ¿Esta entrada da video? (no: los micrófonos, las voces, el audio de un invitado; los
/// archivos tienen su propio final).
bool daVideo(const Fuente* f) {
    if (f->medio) return false;
    return !f->camara.empty() || !f->uri.empty() || !f->descripcion.empty();
}

/// La placa «<nombre> — sin señal» (BGRA, del tamaño del lienzo), hecha con
/// GStreamer (videotestsrc + textoverlay) la primera vez que hace falta.
void armarPlaca(Fuente* f) {
    const char* n = obs_source_get_name(f->ctx);
    const std::string nombre = n != nullptr ? n : "Entrada";
    if (f->placa != nullptr && nombre == f->placaNombre) return;
    obs_video_info ovi = {};
    const int w = obs_get_video_info(&ovi) && ovi.base_width > 0 ? static_cast<int>(ovi.base_width) : 1280;
    const int h = ovi.base_height > 0 ? static_cast<int>(ovi.base_height) : 720;
    std::string texto;
    for (char c : nombre + " — sin señal") {
        if (c == '\\' || c == '"') texto += '\\';
        texto += c;
    }
    const std::string d = "videotestsrc num-buffers=1 pattern=solid-color foreground-color=0xff1c1c22 ! "
                          "video/x-raw,format=BGRA,width=" + std::to_string(w) + ",height=" + std::to_string(h) +
                          " ! textoverlay text=\"" + texto + "\" font-desc=\"Sans Bold " + std::to_string(std::max(18, h / 18)) +
                          "\" valignment=center halignment=center ! videoconvert ! video/x-raw,format=BGRA ! appsink name=p";
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4, 0x20);
    GError* e = nullptr;
    if (GstElement* p = gst_parse_launch(d.c_str(), &e)) {
        if (GstElement* sink = gst_bin_get_by_name(GST_BIN(p), "p")) {
            gst_element_set_state(p, GST_STATE_PLAYING);
            if (GstSample* smp = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 2 * GST_SECOND)) {
                GstMapInfo m;
                GstBuffer* b = gst_sample_get_buffer(smp);
                if (b != nullptr && gst_buffer_map(b, &m, GST_MAP_READ)) {
                    std::memcpy(px.data(), m.data, std::min<size_t>(m.size, px.size()));
                    gst_buffer_unmap(b, &m);
                }
                gst_sample_unref(smp);
            }
            gst_element_set_state(p, GST_STATE_NULL);
            gst_object_unref(sink);
        }
        gst_object_unref(p);
    }
    if (e != nullptr) g_error_free(e);
    if (f->placa == nullptr) f->placa = new std::vector<std::uint8_t>();
    *f->placa = std::move(px);
    f->placaAncho = w;
    f->placaAlto = h;
    f->placaNombre = nombre;
}

/// Cada cuadro de libobs: si la entrada dejó de dar video (se cayó, se desenchufó,
/// espera su dispositivo), a los 3 s pasa a la placa (dos veces por segundo).
void vigilarSenal(Fuente* f) {
    if (!daVideo(f)) return;
    const std::uint64_t ahora = os_gettime_ns();
    // Recién creada: le da 10 s (una red de superresolución tarda en cargar); si ya
    // dio video y se cortó, 3 s con el último cuadro.
    if (f->ultimoCuadroNs == 0) f->ultimoCuadroNs = ahora + 7'000'000'000ULL;
    if (ahora < f->ultimoCuadroNs || ahora - f->ultimoCuadroNs < 3'000'000'000ULL ||
        ahora - f->placaEnviadaNs < 500'000'000ULL)
        return;
    armarPlaca(f);
    obs_source_frame fr = {};
    fr.data[0] = f->placa->data();
    fr.linesize[0] = static_cast<uint32_t>(f->placaAncho) * 4;
    fr.width = static_cast<uint32_t>(f->placaAncho);
    fr.height = static_cast<uint32_t>(f->placaAlto);
    fr.format = VIDEO_FORMAT_BGRA;
    fr.full_range = true;
    fr.timestamp = ahora;
    obs_source_output_video(f->ctx, &fr);
    f->placaEnviadaNs = ahora;
    if (!f->enPlaca)
        blog(LOG_WARNING, "[ba_gst_fuente] %s: sin señal, se muestra la placa", f->placaNombre.c_str());
    f->enPlaca = true;
}

/// Cada cuadro de libobs: presenta lo que ya venció, y revisa el bus (un
/// error se anota y se reintenta a los 2 s).
void tic(void* data, float) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    vigilarSenal(f);
    const bool deDispositivo = !f->camara.empty() || (!f->microfono.empty() && f->microfono != "default");
    if (deDispositivo) vigia().revisar();
    if (f->pipeline == nullptr && f->esperaDispositivo) {
        // Esperando su dispositivo: se arma cuando el vigía avisa que cambió algo
        // (o, de respaldo, cada 15 s: hay sistemas que no avisan).
        const std::uint64_t gen = vigia().generacion.load();
        if (gen != f->genVista || os_gettime_ns() - f->esperaDesdeNs > 15'000'000'000ULL) armar(f);
        return;
    }
    // Lo está usando y se fue: a esperar que vuelva (sin error de por medio no se notaría).
    if (f->pipeline != nullptr && deDispositivo && vigia().generacion.load() != f->genVista) {
        f->genVista = vigia().generacion.load();
        const bool esta = !f->camara.empty() ? vigia().hay(true, f->camara) : vigia().hay(false, f->microfono);
        if (!esta) {
            desarmar(f);
            obs_source_output_video(f->ctx, nullptr);
            armar(f); // queda esperando (no está)
            return;
        }
    }
    if (f->pipeline == nullptr) {
        if ((!f->descripcion.empty() || !f->descripcionAudio.empty() || !f->uri.empty() || !f->camara.empty()) &&
            f->reintento != 0 && os_gettime_ns() >= f->reintento) {
            f->reintento = 0;
            armar(f);
        }
        return;
    }
    // Un salto pedido con la cadena quieta: apenas la rearmada pasa datos.
    if (f->medio && f->buscarNs >= 0 && (f->recibidos > 0 || f->bloquesA > 0)) {
        irA(f, f->buscarNs);
        f->buscarNs = -1;
    }
    // `looping`: vueltas sin cortes con saltos por segmentos. El primero (con
    // flush, apenas pasan datos) arma el segmento; al terminar cada vuelta llega
    // SEGMENT_DONE y se salta al principio SIN flush (no se corta nada).
    if (f->medio && f->bucle && !f->segmentoListo && (f->recibidos > 0 || f->bloquesA > 0)) {
        gint64 pos = 0;
        gst_element_query_position(f->pipeline, GST_FORMAT_TIME, &pos);
        f->segmentoListo = gst_element_seek(f->pipeline, 1.0, GST_FORMAT_TIME,
                                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT |
                                                                      GST_SEEK_FLAG_ACCURATE),
                                            GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
    }
    // Se armó sin el rack (la app no había registrado `barack`): cada segundo se
    // mira si ya está, y entonces se rearma con él.
    if (f->rackFaltaDesde != 0 && os_gettime_ns() - f->rackFaltaDesde > 1'000'000'000ULL) {
        if (hayFabrica("barack")) {
            armar(f);
            return;
        }
        f->rackFaltaDesde = os_gettime_ns();
    }
    // La hora de la cámara (RTCP) muy corrida: cada cuadro llega con mucho más
    // adelanto que el colchón y la entrada sale con SEGUNDOS de atraso (pasa
    // cuando la cámara se reinicia y pierde la hora). Sostenido 3 s: se deja de
    // usar su hora y se rearma (la sincronía entre cámaras se mide aparte).
    if (f->ntpSync && !f->ntpApagadoSolo && f->uri.rfind("rtsp", 0) == 0) {
        double adelanto = 0;
        std::uint64_t recibidos = 0;
        {
            std::lock_guard<std::mutex> lc(f->colaLock);
            adelanto = f->adelantoMs;
            recibidos = f->recibidos;
        }
        // (Lo que agregó el colchón por la demora de los filtros no es culpa del reloj.)
        double exceso = adelanto - static_cast<double>(f->latenciaMs + f->desfaseMs + f->compMs);
        if (f->estandar) {
            // Con la sincronía estándar: la hora de la cámara corrida hace que los
            // cuadros lleguen «del futuro» (lo viejo que llegan, negativo).
            std::lock_guard<std::mutex> lc(f->colaLock);
            exceso = f->edades != nullptr && f->edades->size() >= 30 ? -f->edadP50 : 0.0;
        }
        const std::uint64_t ahoraNs = os_gettime_ns();
        if (recibidos > 60 && exceso > 400) {
            if (f->ntpMalDesde == 0)
                f->ntpMalDesde = ahoraNs;
            else if (ahoraNs - f->ntpMalDesde > 3'000'000'000ULL) {
                blog(LOG_WARNING,
                     "[ba-sources] fuente GStreamer: la hora de la cámara está corrida %.0f ms; se deja de "
                     "usar (ntp-sync)",
                     exceso);
                f->ntpApagadoSolo = true;
                f->ntpMalDesde = 0;
                desarmar(f);
                f->reintento = ahoraNs;
                return;
            }
        } else {
            f->ntpMalDesde = 0;
        }
    }
    if (f->estandar && !f->medio) {
        // Sincronía estándar: la latencia del estudio. Cada cadena pide la suya
        // —la consulta LATENCY (jitter buffer de RTSP/SRT y lo que declaran sus
        // filtros: una red en reposo declara lo que tarda activa, así prenderla
        // no mueve nada), lo medido al llegar a la cola de espera (por si algún
        // elemento no declara) y el colchón configurado— y todas usan la mayor
        // (gst_pipeline_set_latency): salen alineadas y con su audio.
        const std::uint64_t ahoraNs = os_gettime_ns();
        if (ahoraNs - f->consultaNs > 1'000'000'000ULL) {
            f->consultaNs = ahoraNs;
            GstQuery* q = gst_query_new_latency();
            if (gst_element_query(f->pipeline, q)) {
                gboolean vivo = FALSE;
                GstClockTime min = 0, max = 0;
                gst_query_parse_latency(q, &vivo, &min, &max);
                f->declaradoMs = GST_CLOCK_TIME_IS_VALID(min) ? static_cast<double>(min) / 1e6 : 0;
            }
            gst_query_unref(q);
        }
        // Lo medido: el percentil 95 de lo viejo que llegaron los últimos ~10 s
        // de cuadros a la cola de espera (no el pico: la llegada por red tiene
        // ruido). Hace falta porque no todos los elementos declaran lo que
        // demoran (vtdec, por ejemplo, guarda cuadros para reordenar y no lo
        // dice): con la latencia declarada sola, entre un 15 y un 57 % de los
        // cuadros llegaban tarde al appsink.
        double p95 = 0;
        std::uint64_t recibidos = 0;
        {
            std::lock_guard<std::mutex> lc(f->colaLock);
            recibidos = f->recibidos;
            if (f->edades != nullptr && f->edades->size() >= 30) {
                std::vector<double> v(f->edades->begin(), f->edades->end());
                std::sort(v.begin(), v.end());
                f->edadP50 = v[v.size() / 2];
                f->edadP90 = v[v.size() * 9 / 10];
                p95 = std::clamp(v[v.size() * 95 / 100], 0.0, 2000.0);
            }
        }
        const std::int64_t piso = f->latenciaMs * static_cast<std::int64_t>(GST_MSECOND);
        // La latencia que pide esta cadena (y el estudio usa la mayor, con
        // gst_pipeline_set_latency): lo que declaran sus elementos (consulta
        // LATENCY) o lo medido, lo que sea más, con un margen; el colchón
        // configurado es el piso.
        const double necesitaMs = std::max(f->declaradoMs + 15.0, p95 + 10.0);
        const std::int64_t propia = std::max<std::int64_t>(piso, static_cast<std::int64_t>(necesitaMs * 1e6));
        const std::int64_t estudio = std::clamp<std::int64_t>(
            f->estudio ? latenciaDelEstudio(f, recibidos > 30 ? propia : piso) : (recibidos > 30 ? propia : piso), piso,
            static_cast<std::int64_t>(5 * GST_SECOND));
        // Sube enseguida (si no, los cuadros llegan tarde); baja solo si sobra
        // bastante durante 10 s (cada cambio corre la imagen y el audio).
        bool cambiar = false;
        if (estudio > f->latObjetivoNs + static_cast<std::int64_t>(15 * GST_MSECOND)) {
            cambiar = true;
        } else if (estudio < f->latObjetivoNs - static_cast<std::int64_t>(60 * GST_MSECOND)) {
            if (f->sobraDesdeNs == 0) f->sobraDesdeNs = ahoraNs;
            cambiar = ahoraNs - f->sobraDesdeNs > 10'000'000'000ULL;
        } else {
            f->sobraDesdeNs = 0;
        }
        if (cambiar) {
            f->sobraDesdeNs = 0;
            f->latObjetivoNs = estudio;
            gst_pipeline_set_latency(GST_PIPELINE(f->pipeline), static_cast<GstClockTime>(estudio));
            gst_bin_recalculate_latency(GST_BIN(f->pipeline));
        }
        // Para el diagnóstico (y la revisión de la hora de la cámara): lo que la
        // latencia del estudio agrega sobre el colchón configurado.
        f->compMs = f->latObjetivoNs / static_cast<std::int64_t>(GST_MSECOND) - f->latenciaMs;
        if (!f->controlEnviado && f->control != nullptr && recibidos > 0) f->controlEnviado = enviarControl(f);
    } else {
        // Los filtros demoran el video: el colchón crece lo que falta (para audio Y
        // video: siguen juntos). Lo que falta sale de lo medido (lo viejo que llega
        // cada cuadro) y, como piso, de la latencia que DECLARAN los elementos (una
        // red en reposo llega rápido pero declara lo que tarda activa: prenderla no
        // mueve el audio). Todas las entradas usan la mayor del estudio.
        const bool hayFiltros = !f->filtrosV.empty() || !f->filtrosA.empty() || !f->superresMotor.empty() || f->superres;
        const std::uint64_t ahoraNs = os_gettime_ns();
        if (hayFiltros && ahoraNs - f->consultaNs > 1'000'000'000ULL) {
            f->consultaNs = ahoraNs;
            if (GstElement* sink = gst_bin_get_by_name(GST_BIN(f->pipeline), "ba_video")) {
                if (GstPad* pad = gst_element_get_static_pad(sink, "sink")) {
                    GstQuery* q = gst_query_new_latency();
                    if (gst_pad_peer_query(pad, q)) {
                        gboolean vivo = FALSE;
                        GstClockTime min = 0, max = 0;
                        gst_query_parse_latency(q, &vivo, &min, &max);
                        f->declaradoMs = GST_CLOCK_TIME_IS_VALID(min) ? static_cast<double>(min) / 1e6 : 0;
                    }
                    gst_query_unref(q);
                    gst_object_unref(pad);
                }
                gst_object_unref(sink);
            }
        }
        {
            std::lock_guard<std::mutex> lc(f->colaLock);
            std::int64_t propia = 0;
            if (hayFiltros) {
                const double llega = std::max({f->retrasoV, f->retrasoA, f->declaradoMs});
                propia = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::lround(llega + 15.0)) - f->latenciaMs -
                                                      f->desfaseMs,
                                                  0, 3000);
            }
            // Una entrada solo de audio (un micrófono) también se alinea con el
            // estudio: si no, sale adelantada respecto de las cámaras que esperan.
            const bool anda = f->recibidos > 15 || (f->recibidos == 0 && f->bloquesA > 50);
            std::int64_t deseado = anda ? propia : 0;
            if (f->estudio) {
                deseado = compensacionEstudio(f, deseado);
            } else {
                olvidarCompensacion(f);
            }
            if (anda && std::llabs(deseado - f->compMs) >= 8) {
                const std::int64_t corr = (deseado - f->compMs) * static_cast<std::int64_t>(GST_MSECOND);
                for (auto* q : {&f->video, &f->audio})
                    for (auto& pe : *q)
                        pe.vence = static_cast<GstClockTime>(
                            std::max<std::int64_t>(0, static_cast<std::int64_t>(pe.vence) + corr));
                f->compMs = deseado;
            }
        }
        // El control pendiente, apenas la cadena (nueva) pasa cuadros.
        if (!f->controlEnviado && f->control != nullptr && f->recibidos > 0) f->controlEnviado = enviarControl(f);
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
            f->colchonMs =
                f->video.empty()
                    ? 0
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
    }
    // EL BUS: todos los mensajes que juntó, cada cuadro (antes se tomaba uno por
    // cuadro con gst_bus_pop_filtered, que además TIRA los demás: se perdían
    // LATENCY, CLOCK_LOST y los avisos).
    GstBus* bus = gst_element_get_bus(f->pipeline);
    if (bus == nullptr) return;
    GstElement* esta = f->pipeline;
    while (f->pipeline == esta) {
        GstMessage* m = gst_bus_pop(bus);
        if (m == nullptr) break;
        procesarMensaje(f, m);
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

/// Un mensaje del bus de la cadena (con f->lock tomado). Puede rearmar o
/// desarmar la cadena: el que llama deja de leer si cambió f->pipeline.
void procesarMensaje(Fuente* f, GstMessage* m) {
    switch (GST_MESSAGE_TYPE(m)) {
        case GST_MESSAGE_LATENCY:
            // Un elemento cambió lo que tarda (un filtro que se prende, un jitter
            // buffer que crece): se recalcula y se reparte. En modo estándar la
            // latencia fija (la del estudio) sigue mandando.
            gst_bin_recalculate_latency(GST_BIN(f->pipeline));
            return;
        case GST_MESSAGE_CLOCK_LOST:
            // El reloj se fue (un elemento que lo daba salió): pausa y play para
            // que el pipeline vuelva a poner el suyo.
            gst_element_set_state(f->pipeline, GST_STATE_PAUSED);
            gst_element_set_state(f->pipeline, GST_STATE_PLAYING);
            return;
        case GST_MESSAGE_WARNING: {
            GError* e = nullptr;
            gst_message_parse_warning(m, &e, nullptr);
            if (e != nullptr) {
                blog(LOG_INFO, "[ba-sources] fuente GStreamer (aviso de %s): %s", GST_OBJECT_NAME(GST_MESSAGE_SRC(m)),
                     e->message);
                g_error_free(e);
            }
            return;
        }
        case GST_MESSAGE_SEGMENT_DONE:
            // `looping`: otra vuelta, sin flush: sigue de corrido.
            if (f->medio && f->bucle)
                gst_element_seek(f->pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_SEGMENT, GST_SEEK_TYPE_SET, 0,
                                 GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
            return;
        case GST_MESSAGE_EOS:
            if (f->medio) {
                // Un archivo que terminó: otra vuelta, o quieto (en negro si se pidió).
                if (f->bucle) {
                    // No anduvo el salto por segmentos: al principio con flush (un corte chico).
                    irA(f, 0);
                    f->segmentoListo = false;
                    return;
                }
                f->estadoMedio = OBS_MEDIA_STATE_ENDED;
                gst_element_set_state(f->pipeline, GST_STATE_PAUSED);
                if (f->limpiarAlTerminar) obs_source_output_video(f->ctx, nullptr);
                return;
            }
            f->error = "terminó (EOS)";
            desarmar(f);
            f->reintento = os_gettime_ns() + 2'000'000'000ULL;
            return;
        case GST_MESSAGE_ERROR: {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            f->error = e != nullptr ? e->message : "error";
            if (e != nullptr) g_error_free(e);
            blog(LOG_WARNING, "[ba-sources] fuente GStreamer: %s (reintento en 2 s)", f->error.c_str());
            if (f->medio) f->estadoMedio = OBS_MEDIA_STATE_ERROR;
            desarmar(f);
            f->reintento = os_gettime_ns() + 2'000'000'000ULL;
            return;
        }
        default:
            return;
    }
}

// --- Medios (ba_gst_medio) ----------------------------------------------------------

/// Ir a `ns` del archivo (sin pipeline: nada).
void irA(Fuente* f, std::int64_t ns) {
    if (f->pipeline == nullptr) return;
    gst_element_seek_simple(f->pipeline, GST_FORMAT_TIME,
                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                            std::max<std::int64_t>(0, ns));
}

// Con preroll (async=true), los saltos con flush andan en cualquier estado
// (andando, en pausa, terminado): reiniciar es ir a 0 y play; parar es pausa,
// ir a 0 y negro. Solo sin cadena (nunca armada, o se cayó) se arma.

void medioReproducirPausar(void* data, bool pausar) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline == nullptr) {
        if (!pausar) armar(f);
        return;
    }
    if (!pausar && f->estadoMedio == OBS_MEDIA_STATE_ENDED) irA(f, 0);
    gst_element_set_state(f->pipeline, pausar ? GST_STATE_PAUSED : GST_STATE_PLAYING);
    f->estadoMedio = pausar ? OBS_MEDIA_STATE_PAUSED : OBS_MEDIA_STATE_PLAYING;
}

void medioReiniciar(void* data) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline == nullptr) {
        armar(f);
        return;
    }
    irA(f, 0);
    f->segmentoListo = false; // el salto con flush deshizo el segmento de `looping`
    gst_element_set_state(f->pipeline, GST_STATE_PLAYING);
    f->estadoMedio = OBS_MEDIA_STATE_PLAYING;
}

void medioParar(void* data) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline != nullptr) {
        gst_element_set_state(f->pipeline, GST_STATE_PAUSED);
        irA(f, 0);
    }
    obs_source_output_video(f->ctx, nullptr);
    f->estadoMedio = OBS_MEDIA_STATE_STOPPED;
}

int64_t medioTiempo(void* data) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->buscarNs >= 0) return f->buscarNs / static_cast<std::int64_t>(GST_MSECOND);
    gint64 pos = 0;
    if (f->pipeline == nullptr || !gst_element_query_position(f->pipeline, GST_FORMAT_TIME, &pos)) return 0;
    return pos / static_cast<gint64>(GST_MSECOND);
}

int64_t medioDuracion(void* data) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    gint64 dur = 0;
    if (f->pipeline == nullptr || !gst_element_query_duration(f->pipeline, GST_FORMAT_TIME, &dur)) return 0;
    return dur / static_cast<gint64>(GST_MSECOND);
}

void medioIrA(void* data, int64_t ms) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline == nullptr) {
        armar(f);
        f->buscarNs = std::max<std::int64_t>(0, ms) * static_cast<std::int64_t>(GST_MSECOND);
        return;
    }
    irA(f, std::max<std::int64_t>(0, ms) * static_cast<std::int64_t>(GST_MSECOND));
    f->segmentoListo = false;
    // Terminado o parado: ir a un lugar es para verlo andar desde ahí.
    if (f->estadoMedio == OBS_MEDIA_STATE_ENDED || f->estadoMedio == OBS_MEDIA_STATE_STOPPED) {
        gst_element_set_state(f->pipeline, GST_STATE_PLAYING);
        f->estadoMedio = OBS_MEDIA_STATE_PLAYING;
    }
}

obs_media_state medioEstado(void* data) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline == nullptr && !f->error.empty()) return OBS_MEDIA_STATE_ERROR;
    if (f->pipeline == nullptr && f->estadoMedio != OBS_MEDIA_STATE_STOPPED) return OBS_MEDIA_STATE_NONE;
    return static_cast<obs_media_state>(f->estadoMedio.load());
}

/// Se ve en algún lado (programa o preview).
void medioActivar(void* data) {
    auto* f = static_cast<Fuente*>(data);
    bool reiniciar;
    {
        std::lock_guard<std::mutex> l(f->lock);
        reiniciar = f->reiniciarAlActivar;
    }
    if (reiniciar) medioReiniciar(data);
}

void medioDesactivar(void* data) {
    auto* f = static_cast<Fuente*>(data);
    bool cerrar;
    {
        std::lock_guard<std::mutex> l(f->lock);
        cerrar = f->cerrarInactiva;
    }
    if (cerrar) medioParar(data);
}

const char* nombreMedio(void*) { return obs_module_text("Medio por GStreamer (BA)"); }

obs_properties_t* propiedadesMedio(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_path(p, "local_file", obs_module_text("Archivo"), OBS_PATH_FILE, nullptr, nullptr);
    obs_properties_add_bool(p, "looping", obs_module_text("Repetir"));
    obs_properties_add_bool(p, "restart_on_activate", obs_module_text("Empezar de nuevo al mostrarlo"));
    obs_properties_add_bool(p, "clear_on_media_end", obs_module_text("Al terminar, negro (y no el último cuadro)"));
    obs_properties_add_bool(p, "close_when_inactive", obs_module_text("Parar cuando no se ve"));
    return p;
}

void porDefectoMedio(obs_data_t* s) {
    obs_data_set_default_bool(s, "is_local_file", true);
    obs_data_set_default_bool(s, "looping", false);
    obs_data_set_default_bool(s, "restart_on_activate", true);
    obs_data_set_default_bool(s, "clear_on_media_end", true);
    obs_data_set_default_bool(s, "close_when_inactive", false);
    obs_data_set_default_bool(s, "audio", true);
    obs_data_set_default_bool(s, "normalizar", true);
}

obs_properties_t* propiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "uri",
                            obs_module_text("URI (srt://, rtmp://, rtsp://, udp://, http://, archivo)"),
                            OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "pipeline", obs_module_text("O un pipeline de GStreamer (origen)"),
                            OBS_TEXT_MULTILINE);
    obs_properties_add_text(p, "pipeline_audio", obs_module_text("Y el de su audio (opcional)"),
                            OBS_TEXT_MULTILINE);
    obs_properties_add_int(p, "latencia", obs_module_text("Latencia (ms)"), 0, 10000, 10);
    obs_properties_add_int(p, "desfase", obs_module_text("Desfase (ms)"), -5000, 5000, 1);
    obs_property_t* r = obs_properties_add_list(p, "reloj", obs_module_text("Reloj"), OBS_COMBO_TYPE_LIST,
                                                OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(r, "Sistema", "sistema");
    obs_property_list_add_string(r, "NTP", "ntp");
    obs_property_list_add_string(r, "PTP", "ptp");
    obs_property_list_add_string(r, "De otro aparato (red)", "red");
    obs_properties_add_text(p, "reloj_servidor", obs_module_text("Servidor del reloj"), OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "reloj_puerto", obs_module_text("Puerto del reloj"), 0, 65535, 1);
    obs_properties_add_int(p, "reloj_dominio", obs_module_text("Dominio PTP"), 0, 127, 1);
    obs_properties_add_bool(p, "audio", obs_module_text("Con audio"));
    obs_properties_add_bool(p, "normalizar",
                            obs_module_text("Normalizar (desentrelazar, cuadros y color del estudio)"));
    obs_properties_add_bool(p, "superres",
                            obs_module_text("Superresolución x2 (macOS 26, cámaras chicas)"));
    obs_property_t* sm =
        obs_properties_add_list(p, "superres_motor", obs_module_text("Motor de superresolución"),
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(sm, "Ninguno (o el interruptor de arriba)", "");
    obs_property_list_add_string(sm, "Apple (VideoToolbox, macOS 26)", "apple");
    obs_property_list_add_string(sm, "FSR 1 (AMD, cualquier GPU)", "fsr");
    obs_property_list_add_string(sm, "Red ONNX (el modelo de abajo)", "onnx");
    obs_property_t* sy = obs_properties_add_list(p, "sincronia", obs_module_text("Sincronía"), OBS_COMBO_TYPE_LIST,
                                                 OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(sy, "GStreamer (reloj, base y latencia del estudio)", "gstreamer");
    obs_property_list_add_string(sy, "Colchón propio (de fábrica)", "colchon");
    obs_properties_add_text(p, "filtros", obs_module_text("Filtros (JSON: video, audio, alfa)"),
                            OBS_TEXT_MULTILINE);
    obs_properties_add_path(p, "superres_modelo", obs_module_text("Modelo ONNX"), OBS_PATH_FILE,
                            "Modelo (*.onnx)", nullptr);
    return p;
}

void porDefecto(obs_data_t* s) {
    obs_data_set_default_string(s, "pipeline", "videotestsrc pattern=smpte is-live=true");
    obs_data_set_default_string(s, "reloj", "sistema");
    obs_data_set_default_bool(s, "audio", true);
    obs_data_set_default_bool(s, "estudio", true);
    obs_data_set_default_bool(s, "normalizar", true);
    obs_data_set_default_bool(s, "superres", false);
    obs_data_set_default_bool(s, "ntp_sync", true);
    obs_data_set_default_string(s, "sincronia", "colchon");
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
static struct obs_source_info ba_gst_medio_info = {};

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
    // Un archivo (video o audio) con los controles de medios de libobs.
    ba_gst_medio_info.id = "ba_gst_medio";
    ba_gst_medio_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_gst_medio_info.output_flags =
        OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CONTROLLABLE_MEDIA;
    ba_gst_medio_info.get_name = nombreMedio;
    ba_gst_medio_info.create = crear;
    ba_gst_medio_info.destroy = destruir;
    ba_gst_medio_info.update = actualizar;
    ba_gst_medio_info.video_tick = tic;
    ba_gst_medio_info.activate = medioActivar;
    ba_gst_medio_info.deactivate = medioDesactivar;
    ba_gst_medio_info.get_properties = propiedadesMedio;
    ba_gst_medio_info.get_defaults = porDefectoMedio;
    ba_gst_medio_info.media_play_pause = medioReproducirPausar;
    ba_gst_medio_info.media_restart = medioReiniciar;
    ba_gst_medio_info.media_stop = medioParar;
    ba_gst_medio_info.media_get_time = medioTiempo;
    ba_gst_medio_info.media_set_time = medioIrA;
    ba_gst_medio_info.media_get_duration = medioDuracion;
    ba_gst_medio_info.media_get_state = medioEstado;
    ba_gst_medio_info.icon_type = OBS_ICON_TYPE_MEDIA;
    obs_register_source(&ba_gst_medio_info);
    ba::gst::registrarFiltroAudioPlugin();
    // El reloj del estudio, para que otros aparatos se sincronicen con este:
    // proc global «ba_reloj_publicar» (puerto 0 = apagar).
    proc_handler_add(
        obs_get_proc_handler(),
        "void ba_reloj_publicar(in int puerto, out bool ok, out string error, out int puerto_publicado)",
        [](void*, calldata_t* cd) {
            std::string e;
            const bool ok = ba::gst::publicarReloj(static_cast<int>(calldata_int(cd, "puerto")), e);
            calldata_set_bool(cd, "ok", ok);
            calldata_set_string(cd, "error", e.c_str());
            calldata_set_int(cd, "puerto_publicado", ba::gst::relojPublicado());
        },
        nullptr);
    // El puerto de medios de un plugin de Python (lo anuncia al arrancar; 0 = se
    // cerró): lo usan `baplugin` y `bapluginsrc` (ba_plugin_gst.cpp).
    proc_handler_add(
        obs_get_proc_handler(), "void ba_plugin_puerto(in string plugin, in int puerto)",
        [](void*, calldata_t* cd) {
            const char* p = calldata_string(cd, "plugin");
            if (p != nullptr) ba::gst::puertoPlugin(p, static_cast<int>(calldata_int(cd, "puerto")));
        },
        nullptr);
}

} // extern "C"

#else

extern "C" void ba_register_gst_fuente(void) {}

#endif
