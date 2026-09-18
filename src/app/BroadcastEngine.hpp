// ============================================================================
// src/app/BroadcastEngine.hpp — Motor de emision (Proceso A, spec §6)
//
// Proposito:
//   La pieza que convierte al proyecto en un aparato de emision: levanta el
//   pipeline de video y audio de libobs, arma la escena, crea los codificadores
//   y abre las salidas (transmision RTMP y grabacion a disco). Es lo que antes
//   no existia: `ba-app` llamaba a `obs_startup()`, abria una ventana y
//   llamaba a `obs_shutdown()`, sin nada que codificara ni transmitiera.
//
//   Tambien cierra el camino de datos hacia el Proceso B (spec §4.3/§8.5): con
//   `publishReturnFeeds()` engancha los callbacks de video y audio crudos de
//   libobs y publica el programa en `BA.Program` y la mezcla de retorno en
//   `BA.Audio.ContentForReturn`. Sin eso el invitado remoto no ve ni oye la
//   sala; con eso, el gateway (que ya los consume) queda conectado.
//
// Portabilidad:
//   libobs es cross-platform y este motor tambien: NO hay `#ifdef _WIN32` en la
//   logica. Lo unico que cambia por plataforma es el nombre del modulo grafico
//   (`libobs-d3d11` en Windows, `libobs-opengl` en el resto), que se resuelve
//   en `defaultGraphicsModule()`. Las rutas de modulos y datos llegan por
//   configuracion, derivadas del layout portable §5.1.
//
// Modelo de threading:
//   Todas las operaciones publicas se invocan desde el THREAD DE CONTROL (la
//   UI o el arranque headless). libobs corre sus propios hilos de render y de
//   audio por dentro; los callbacks crudos que instala `publishReturnFeeds()`
//   se disparan EN ESOS hilos y solo tocan los rings IPC, que son SPSC y
//   lock-free. `status()` es de solo lectura y puede llamarse desde la UI.
//
// Estrategia de errores:
//   No lanza. Cada operacion devuelve bool y deja el motivo en `lastError()`.
//   Un fallo de una salida (por ejemplo, el servidor RTMP rechaza la clave) no
//   tumba el motor: se reporta y el resto sigue corriendo, porque durante una
//   emision en vivo degradar siempre es mejor que caerse.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

// Punteros opacos de libobs: el header NO incluye <obs.h> para que los
// consumidores (la UI, los tests) no arrastren toda la API de libobs ni sus
// macros. El .cpp es el unico que la ve.
struct obs_output;
struct obs_encoder;
struct obs_service;
struct obs_scene;
struct obs_source;
struct video_data;
struct audio_data;

namespace ba::app {

/// Resolucion y cadencia del pipeline de video (spec §7.1).
struct VideoConfig {
    int baseWidth = 1920;   ///< lienzo de composicion
    int baseHeight = 1080;  ///< lienzo de composicion
    int outputWidth = 1920; ///< resolucion de salida (escalada desde el lienzo)
    int outputHeight = 1080;
    int fpsNum = 30;
    int fpsDen = 1;
};

/// Formato del pipeline de audio (spec §7.4: 48 kHz es el reloj del proyecto).
struct AudioConfig {
    int sampleRate = 48000;
    int channels = 2;
};

/// Parametros de codificacion.
struct EncoderConfig {
    int videoBitrateKbps = 6000;
    int audioBitrateKbps = 160;
    int keyintSec = 2;               ///< intervalo de keyframe (2 s es el estandar de RTMP)
    std::string preset = "veryfast"; ///< preset de x264
    std::string rateControl = "CBR"; ///< CBR es lo que esperan los CDN
    bool preferHardware = true;      ///< usar NVENC/QSV si esta disponible
};

/// Destino de transmision (spec §8).
struct StreamConfig {
    std::string server; ///< p.ej. "rtmp://a.rtmp.youtube.com/live2"
    std::string key;    ///< clave de transmision (sale de la boveda de secretos)
};

/// Destino de grabacion de respaldo (layout §5.1: Recordings/).
struct RecordConfig {
    std::string directory;         ///< carpeta destino
    std::string container = "mkv"; ///< mkv sobrevive a un corte de energia; mp4 no
};

/// Rutas del layout portable que el motor necesita (spec §5.1/§5.3).
struct EnginePaths {
    std::string moduleBinDir;  ///< obs-plugins/64bit
    std::string moduleDataDir; ///< data de los plugins
    std::string obsDataDir;    ///< data/libobs (shaders .effect)
    std::string configDir;     ///< Config/
};

/// Estado observable del motor, para la UI y el bloque de salud.
struct EngineStatus {
    bool videoActive = false;
    bool audioActive = false;
    bool streaming = false;
    bool recording = false;
    int droppedFrames = 0;
    int totalFrames = 0;
    double fps = 0.0;
    std::uint64_t bytesSent = 0;
    std::string lastError;
};

/// Motor de emision sobre libobs. Ver el header.
///
/// Uso tipico, todo desde el thread de control:
/// ```
/// BroadcastEngine eng;
/// eng.startup(paths, video, audio);
/// eng.buildScene();
/// eng.publishReturnFeeds();     // conecta el camino hacia el Proceso B
/// eng.startRecording(rec);      // respaldo local
/// eng.startStreaming(stream);   // al aire
/// ...
/// eng.shutdown();
/// ```
class BroadcastEngine {
   public:
    BroadcastEngine();
    ~BroadcastEngine();

    BroadcastEngine(const BroadcastEngine&) = delete;
    BroadcastEngine& operator=(const BroadcastEngine&) = delete;

    /// Nombre del modulo grafico de libobs para esta plataforma.
    ///
    /// :returns: "libobs-d3d11" en Windows, "libobs-opengl" en el resto.
    static const char* defaultGraphicsModule();

    /// Arranca libobs: contexto grafico, pipeline de audio y modulos.
    ///
    /// Hace `obs_startup`, `obs_reset_video`, `obs_reset_audio` y carga los
    /// modulos desde `paths.moduleBinDir`. Sin `obs_load_all_modules()` no
    /// existen ni los codificadores ni las salidas (ni el modulo propio
    /// `ba-sources`), que es exactamente lo que le faltaba al arranque anterior.
    ///
    /// :param paths: rutas del layout portable.
    /// :param video: configuracion del pipeline de video.
    /// :param audio: configuracion del pipeline de audio.
    /// :returns: true si el motor quedo listo; false y `lastError()` si no.
    bool startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio);

    /// Detiene salidas, libera la escena y apaga libobs. Idempotente.
    void shutdown();

    /// Crea la escena de programa y la deja como fuente de salida del canal 0.
    ///
    /// :returns: true si la escena quedo activa.
    bool buildScene();

    /// Agrega a la escena una fuente de audio de invitado (`ba_guest_audio`),
    /// que lee el ring `BA.Audio.Guest.<slot>` publicado por el Proceso B.
    ///
    /// :param slot: numero de invitado (1..4).
    /// :returns: true si la fuente se creo y se agrego a la escena.
    bool addGuestAudio(int slot);

    /// Agrega a la escena una fuente de video de invitado (`ba_guest_video`),
    /// que lee el canal compartido `BA.Guest.<slot>`.
    ///
    /// :param slot: numero de invitado (1..4).
    /// :returns: true si la fuente se creo y se agrego a la escena.
    bool addGuestVideo(int slot);

    /// Publica el programa hacia el Proceso B (spec §8.5/§8.6).
    ///
    /// Engancha los callbacks de video y audio crudos de libobs y escribe el
    /// video de programa en `BA.Program` y la mezcla de retorno en
    /// `BA.Audio.ContentForReturn`. Es el PRODUCTOR que faltaba: el gateway ya
    /// consumia esos dos canales y no habia nadie del otro lado.
    ///
    /// :returns: true si los callbacks quedaron instalados.
    bool publishReturnFeeds();

    /// Deja de publicar el retorno y suelta los canales IPC.
    void stopReturnFeeds();

    /// Crea los codificadores y arranca la transmision RTMP.
    ///
    /// :param stream: servidor y clave.
    /// :param enc: parametros de codificacion.
    /// :returns: true si la salida arranco.
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc);

    /// Detiene la transmision (no toca la grabacion).
    void stopStreaming();

    /// Arranca la grabacion de respaldo a disco.
    ///
    /// Comparte los codificadores con la transmision cuando ambas corren, que
    /// es lo que evita codificar dos veces la misma señal.
    ///
    /// :param rec: carpeta y contenedor.
    /// :param enc: parametros de codificacion.
    /// :returns: true si la grabacion arranco.
    bool startRecording(const RecordConfig& rec, const EncoderConfig& enc);

    /// Detiene la grabacion (no toca la transmision).
    void stopRecording();

    /// Recibe un frame de programa ya compuesto (lo llama libobs).
    ///
    /// Corre en el HILO DE RENDER de libobs. Copia el frame al canal
    /// `BA.Program`; no toma locks ni asigna memoria.
    ///
    /// :param frame: frame BGRA entregado por libobs.
    void onRawVideo(struct video_data* frame);

    /// Recibe la mezcla final de audio (lo llama libobs).
    ///
    /// Corre en el HILO DE AUDIO de libobs. Entrelaza los planos y publica
    /// bloques de 480 frames en `BA.Audio.ContentForReturn`.
    ///
    /// :param data: bloque de audio planar float entregado por libobs.
    void onRawAudio(struct audio_data* data);

    /// Estado actual (para la UI y el HealthBlock).
    EngineStatus status() const;

    /// Ultimo error registrado ("" si no hubo).
    const std::string& lastError() const { return lastError_; }

    /// Indica si libobs quedo inicializada.
    bool ready() const { return started_; }

   private:
    // Crea los codificadores si todavia no existen (se comparten entre las dos
    // salidas: codificar una sola vez y muxar dos veces).
    bool ensureEncoders(const EncoderConfig& enc);
    // Elige el codificador de video disponible: NVENC/QSV si se pidio hardware
    // y esta presente; x264 si no.
    std::string pickVideoEncoder(bool preferHardware) const;
    void setError(std::string msg);

    bool started_ = false;
    std::string lastError_;

    EnginePaths paths_;
    VideoConfig video_;
    AudioConfig audio_;

    // Handles de libobs (punteros opacos; el .cpp los castea).
    obs_scene* scene_ = nullptr;
    obs_source* sceneSource_ = nullptr;
    obs_encoder* videoEncoder_ = nullptr;
    obs_encoder* audioEncoder_ = nullptr;
    obs_output* streamOutput_ = nullptr;
    obs_output* recordOutput_ = nullptr;
    obs_service* service_ = nullptr;

    // Canales IPC hacia el Proceso B (el lado PRODUCTOR).
    ba::ipc::SharedAudioRing returnAudio_;
    ba::ipc::SpoutSender programVideo_;
    bool returnFeedsActive_ = false;

    // Re-bloqueo del audio a los 480 frames que exige el ring (spec §4.3).
    // libobs entrega bloques de tamaño variable; el ring solo acepta bloques
    // exactos, asi que hay que acumular. Lo toca UN SOLO hilo (el de audio de
    // libobs), por eso no necesita lock.
    std::vector<float> audioAccum_;
    std::uint64_t programFrameIndex_ = 0;
};

} // namespace ba::app
