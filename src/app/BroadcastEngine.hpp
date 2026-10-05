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

#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "audio/LoudnessPrograma.hpp"
#include <utility>
#include <vector>

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

// Punteros opacos de libobs: el header NO incluye <obs.h> para que los
// consumidores (la UI, los tests) no arrastren toda la API de libobs ni sus
// macros. El .cpp es el unico que la ve.
struct obs_output;
struct obs_encoder;
struct obs_service;
struct obs_data;
struct obs_scene;
struct obs_source;
struct obs_scene_item;
struct obs_video_info;
struct video_data;
struct audio_data;
struct calldata;

namespace ba::app {

/// Resolucion y cadencia del pipeline de video (spec §7.1).
struct VideoConfig {
    int baseWidth = 1920;   ///< lienzo de composicion
    int baseHeight = 1080;  ///< lienzo de composicion
    int outputWidth = 1920; ///< resolucion de salida (escalada desde el lienzo)
    int outputHeight = 1080;
    int fpsNum = 30;
    int fpsDen = 1;              ///< 1001 para 23,976 / 29,97 / 59,94
    std::string formato = "nv12"; ///< lo que reciben los codificadores: nv12 | i420 | i444 | p010 | i010
    std::string color = "709";    ///< espacio de color: 601 | 709 | srgb | 2100pq | 2100hlg (HDR: 10 bits)
    std::string rango = "limitado"; ///< limitado (broadcast) | completo
};

/// "30", "29.97", "30000/1001" -> num/den. false si no se entiende.
bool fpsDeTexto(const std::string& texto, int& num, int& den);
/// num/den -> "30" o "30000/1001" (lo que se guarda y se muestra en Ajustes).
std::string textoDeFps(int num, int den);
/// num/den -> "29,97" para mostrar.
std::string fpsLegible(int num, int den);
/// Formato, espacio de color y rango de la salida en `ovi` (los de `v`).
void colorDeSalida(::obs_video_info& ovi, const VideoConfig& v);

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
    bool preferHardware = true;      ///< con "auto": probar hardware antes que x264
    /// Codificador de video: un alias o un id de libobs.
    ///   "auto"         -> hardware de la plataforma en orden (Windows: NVENC,
    ///                     QSV, AMF; Linux: NVENC, VAAPI; macOS: VideoToolbox)
    ///   "nvenc" | "qsv" | "amf" | "vaapi" | "videotoolbox" | "x264"
    ///   cualquier otro -> id literal de libobs (p. ej. "ffmpeg_vaapi_tex")
    /// Siempre termina en x264 como red de seguridad: si el elegido no esta o
    /// no arranca (driver roto, GPU ocupada), se cae al siguiente.
    std::string videoEncoder = "auto";
};

/// Rectangulo en FRACCIONES del lienzo (0..1): independiente de la resolucion.
struct CanvasRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
};

/// Fuente generica de libobs para la escena (camara, captura, imagen, texto,
/// media...). Es lo que hace a la escena configurable sin recompilar.
struct SourceSpec {
    std::string id;                  ///< id de libobs: "v4l2_input", "image_source", ...
    std::string name;                ///< nombre unico en la escena
    std::string settingsJson = "{}"; ///< settings de la fuente, tal cual los usa OBS
    CanvasRect rect;                 ///< ubicacion (ignorado en fuentes solo-audio)
    bool visible = true;
    float volume = 1.0f; ///< ganancia lineal (1 = 0 dB)
    bool muted = false;
    /// Recorte en la escena: izquierda, arriba, derecha, abajo (fracciones de su tamaño).
    float recorte[4] = {0, 0, 0, 0};
};

/// Tipo de fuente disponible en esta libobs (para "Agregar fuente").
struct SourceType {
    std::string id;          ///< id de libobs ("v4l2_input", "image_source"...)
    std::string displayName; ///< nombre legible que da el plugin
    bool video = false;
    bool audio = false;
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
    /// Con mp4/mov: se graba en MKV (sobrevive a un corte) y al terminar se
    /// pasa al formato pedido sin recodificar (el MKV se borra si salió bien).
    bool remuxAlTerminar = true;
    /// "como_stream": comparte los codificadores de la transmisión (no cuesta otro
    /// encode). "archivo": codificador propio a calidad constante (CRF/CQP), pensado
    /// para editar después; es un encode más.
    std::string calidad = "como_stream";
    int calidadCrf = 18; ///< con "archivo": 14 (casi sin pérdida) a 28 (chico)
    /// Pistas de audio EXTRA, numeradas como en la Matriz (2..6): cada una con lo que
    /// la Matriz le cruce. La 1 (el programa) va siempre.
    std::vector<int> pistas;
    /// Segunda copia simultánea en otro disco ("" = sin copia). Mismos codificadores.
    std::string copiaDir;
};

/// Rutas del layout portable que el motor necesita (spec §5.1/§5.3).
struct EnginePaths {
    std::string moduleBinDir;  ///< obs-plugins/64bit
    std::string moduleDataDir; ///< data/obs-plugins (cada modulo en <dir>/<nombre>)
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
    std::string videoEncoder; ///< id de libobs del codificador en uso ("" si ninguno)
    std::string lastError;
    /// Se corto la transmision y libobs esta reintentando. Mientras esto es
    /// true, `streaming` puede seguir en true: para libobs la salida sigue
    /// activa, pero no esta llegando nada.
    bool reconnecting = false;
    /// En castellano y con la CAUSA: "Se cortó la conexión con el servidor",
    /// "El servidor rechazó la clave"... Vacio si no hubo ningun problema.
    /// Es lo que el operador necesita para saber si esperar o hacer algo.
    std::string outputProblem;
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
    /// modulos desde `paths.moduleBinDir` (solo esos). Sin modulos no
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

    // --- Escenas ------------------------------------------------------------
    //
    // Varias escenas; la del aire es la "actual". El canal de salida 0 es una
    // TRANSICION de libobs (corte, fundido, barrido...) cuyo destino es la
    // escena actual: cambiar de escena es iniciar la transicion.

    /// Crea una escena vacia. false si el nombre ya existe.
    bool createScene(const std::string& name);

    /// Elimina una escena (no la actual si es la unica).
    bool removeScene(const std::string& name);

    /// Renombra una escena.
    bool renameScene(const std::string& from, const std::string& to);

    /// Nombres de las escenas en orden de creacion.
    std::vector<std::string> sceneNames() const;

    /// Pone `name` al aire con la transicion configurada (setTransition).
    bool setCurrentScene(const std::string& name);

    /// Escena al aire ("" si no hay).
    const std::string& currentScene() const { return currentScene_; }

    /// Tipo ("cut", "fade", "swipe", "slide", "fade_to_color") y duracion de
    /// la transicion entre escenas.
    bool setTransition(const std::string& type, int durationMs);
    /// Pone `name` al aire con OTRA transición solo esta vez (`tipo`: cut, fade,
    /// swipe, slide, fade_to_color; `ms` < 0 = la duración global). La global no cambia.
    bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms);
    /// T-bar: lleva la transición a mano hacia `name` (t de 0 a 1). Con `soltar`,
    /// termina (si pasó la mitad, queda al aire; si no, vuelve). Devuelve true si
    /// `name` quedó al aire.
    bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire);
    /// En medio de una transición manual (y hacia qué escena).
    std::string manualHacia() const { return manualHacia_; }

    // --- FTB y placa de seguridad (por encima de todo lo que sale) -------------
    /// Fundido a negro de la salida (canal 31, encima del programa). `ms` de
    /// fundido; con `bajarAudio`, el audio del programa baja con él.
    bool ftb(bool activar, int ms, bool bajarAudio);
    /// La placa ("Cuarto intermedio", "Volvemos enseguida"…): una escena interna
    /// con fondo, texto e imagen opcional (canal 30), con fundido.
    bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms);
    // --- DSK: capas globales encima del programa (canales 1..4) ------------------
    /// Arma el contenido de la capa `capa` (1..4): escenas o fuentes existentes,
    /// por nombre, apiladas en ese orden. `recuadro` (opcional) achica cada una
    /// a un recuadro: {posicion: abajo_derecha|abajo_izquierda|arriba_derecha|
    /// arriba_izquierda, tamano: 0..1 del ancho, margen: 0..0.2}.
    bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro);
    /// Sube o baja la capa con un fundido de `ms`.
    bool dskMostrar(int capa, bool visible, int ms);
    /// [{capa, visible, opacidad}] para las 4.
    nlohmann::json dskEstado() const;
    /// Las fuentes que muestran las capas que están arriba (para el tally y el interlock de PTZ).
    std::vector<std::string> fuentesDsk() const;
    /// Un rótulo de texto con nombre (fuera de las escenas, para una capa DSK):
    /// lo crea si no existe y le pone `texto`.
    bool textoGlobal(const std::string& nombre, const std::string& texto);
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen} (lo que se ve ahora o va hacia eso).
    nlohmann::json estadoPlaca() const;

    /// Subtítulos ocultos CEA-608 (canal 1) en la transmisión y en cada destino
    /// adicional activo. Devuelve a cuántas salidas se mandó.
    int enviarCaption(const std::string& texto, double segundos);

    /// El loudness de lo que sale (mezcla 0): M/S/I, true peak, LRA (EBU R128).
    audio::LoudnessPrograma& loudness() { return *loudness_; }

    /// El archivo que se está grabando (o el último) y en qué contenedor de verdad.
    std::string archivoGrabacion() const { return recordPath_; }
    /// Pasa `archivo` (mkv) a `formato` (mp4/mov) sin recodificar, en otro hilo.
    /// `borrarOrigen`: si salió bien. false si ya hay uno andando.
    bool remux(const std::string& archivo, const std::string& formato, bool borrarOrigen);
    /// {archivo, destino, pct, andando, ok, error}.
    nlohmann::json remuxEstado() const;

    /// Compatibilidad: crea la escena "programa" y la pone al aire.
    bool buildScene();

    // --- Fuentes -------------------------------------------------------------
    //
    // Los nombres de fuente son unicos en todo libobs. Agregar a una escena una
    // fuente cuyo nombre ya existe REUTILIZA esa fuente (la misma camara en dos
    // escenas es una sola captura).

    /// Agrega (o reutiliza) una fuente en la escena `scene`, arriba de todo.
    bool addSource(const std::string& scene, const SourceSpec& spec);

    /// Compatibilidad: agrega a la escena actual.
    bool addSource(const SourceSpec& spec);

    /// Saca la fuente de la escena (si no queda en ninguna, se libera).
    bool removeSource(const std::string& scene, const std::string& name);

    /// Sube (delta > 0) o baja (delta < 0) la fuente en el orden de la escena.
    bool moveSource(const std::string& scene, const std::string& name, int delta);

    /// Fuentes de la escena, de abajo hacia arriba.
    std::vector<std::string> sceneSources(const std::string& scene) const;

    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible);
    bool sourceVisible(const std::string& scene, const std::string& name) const;

    /// Ubica la fuente en `rect` (fracciones del lienzo) dentro de la escena.
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect);
    /// Recorta la fuente EN LA ESCENA (lo que se ve de ella): izquierda, arriba,
    /// derecha, abajo en fracciones de su tamaño (0..1). Junto con el layout,
    /// amplía una zona. Todo en 0 lo saca.
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    /// Los recortes que esperan que su fuente tenga tamaño (una cámara que recién
    /// arranca mide 0x0 y OBS recorta en píxeles): se aplican apenas lo tiene.
    void aplicarRecortesPendientes();

    /// Renombra una fuente (en todas las escenas).
    bool renameSource(const std::string& from, const std::string& to);

    /// id de libobs de la fuente ("" si no existe).
    std::string sourceTypeId(const std::string& name) const;

    /// Settings actuales de la fuente como JSON.
    std::string sourceSettingsJson(const std::string& name) const;
    /// Mantiene una fuente "mostrandose" (sus cuadros se suben a la GPU aunque
    /// no este al aire): la pagina Entradas lo usa para sus miniaturas.
    void holdShowing(const std::string& name, bool on);
    /// Tamaño actual del video de una fuente (0x0 si todavia no llego imagen).
    bool sourceSize(const std::string& name, int& width, int& height) const;

    /// Reemplaza los settings de la fuente (JSON como en app.json).
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);

    /// Puntero opaco a la fuente (obs_source_t*) para el editor de propiedades
    /// de la interfaz. Prestado: valido mientras la fuente exista.
    struct obs_source* sourceHandle(const std::string& name) const;

    /// Imagen actual de una fuente o escena (por nombre; "" = la escena al
    /// aire), renderizada por libobs fuera de pantalla, escalada para que el
    /// ancho no pase de `maxWidth` (0 = tamaño nativo). RGBA 8 bits.
    /// Lo mismo que ve el programa de esa fuente (filtros incluidos).
    bool captureSource(const std::string& name, int maxWidth, std::vector<std::uint8_t>& rgba, int& width,
                       int& height);

    /// Tipos de fuente de entrada disponibles (solo los utilizables).
    std::vector<SourceType> inputTypes() const;

    /// Invitados: la fuente de video va en escenas; la de AUDIO es global
    /// (siempre en la mezcla, como un microfono): canal de salida 1+slot.
    bool addGuestAudio(int slot);

    /// Entrada de audio del equipo (microfono, linea) como fuente GLOBAL: suena
    /// en todas las escenas, igual que el audio de los invitados (canales 5+).
    /// Si ya existe una con ese nombre, la reengancha.
    ///
    /// Crear la fuente abre el dispositivo, y en la Mac eso espera a que
    /// conteste el servicio de audio del sistema (coreaudiod). Si no contesta
    /// (pasa: queda colgado un rato después de cerrar mal una app que tenía el
    /// micrófono), la app entera quedaba trabada al arrancar. Ahora se crea en
    /// otro hilo y se espera hasta `kEsperaAudioMs`: si no llega, queda
    /// pendiente (devuelve true, con el aviso en lastError) y la engancha
    /// adoptPendingAudio() cuando llegue.
    bool addGlobalAudio(const SourceSpec& spec);
    /// Engancha las entradas de audio que tardaron en crearse (hilo principal,
    /// periódicamente). Devuelve cuántas enganchó.
    int adoptPendingAudio();
    /// Saca la entrada de audio global (libera su canal).
    bool removeGlobalAudio(const std::string& name);
    /// Nombres de las entradas de audio del equipo (sin los invitados).
    std::vector<std::string> globalAudioInputs() const { return micInputs_; }
    bool addGuestVideo(int slot);
    bool addGuestVideo(int slot, const CanvasRect& rect);

    // --- Audio por fuente (mezclador) ------------------------------------------

    /// Fuentes con audio que maneja el motor (escenas + globales), en orden.
    std::vector<std::string> audioSources() const;
    /// Todas las fuentes que maneja el motor (de todas las escenas), sin repetir.
    std::vector<std::string> sourceNames() const;
    bool setSourceVolume(const std::string& name, float mul);
    float sourceVolume(const std::string& name) const;
    bool setSourceMuted(const std::string& name, bool muted);
    bool sourceMuted(const std::string& name) const;
    /// Pico mas reciente de la fuente en dBFS (-100 = silencio).
    float sourcePeakDb(const std::string& name) const;

    /// Escuchar una fuente por el monitor del operador (PFL), SIN cambiar lo
    /// que sale al aire.
    ///
    /// OJO CON EL MODO. libobs tiene MONITOR_ONLY, que parece el indicado
    /// —«solo monitoreo»— y es justo el que no hay que usar: SACA la fuente de
    /// la mezcla, o sea que escuchar un microfono lo bajaria del aire. Se usa
    /// MONITOR_AND_OUTPUT: se escucha y lo que sale sigue saliendo. Escuchar
    /// no puede cambiar lo que se emite; esa es toda la idea de un PFL.
    ///
    /// Que se oiga depende de que haya monitor configurado en el sistema de
    /// audio; sin monitor, esto no falla pero tampoco suena.
    bool setSourceMonitoring(const std::string& name, bool on);
    bool sourceMonitoring(const std::string& name) const;

    /// Atrasa el audio de una fuente para que coincida con su video.
    ///
    /// Es lo que hace falta con una camara IP: el video llega 200 ms a 2 s
    /// tarde y el microfono por la placa llega casi al instante, asi que se
    /// escucha antes de ver los labios. `ms` positivo atrasa el audio; negativo
    /// lo adelanta (sirve cuando el audio llega DESPUES que el video).
    ///
    /// El valor lo puede medir el sistema solo con el metodo del aplauso (ver
    /// camaras/SincroLabios.hpp), o ponerlo el operador a ojo.
    bool setSourceSyncOffset(const std::string& name, int ms);
    int sourceSyncOffset(const std::string& name) const;

    // --- Medios (videos/musica que se reproducen) -------------------------------
    struct MediaInfo {
        std::string name;
        std::string state; ///< playing | paused | stopped | ended | opening | buffering | error | none
        std::int64_t durationMs = 0;
        std::int64_t timeMs = 0;
    };
    /// Fuentes reproducibles (con controles de medio) y su estado.
    std::vector<MediaInfo> mediaSources() const;

    /// Reproduce un archivo como fuente GLOBAL de audio: suena en todas las
    /// escenas, como un microfono. Es lo que hace falta para la musica de un
    /// evento (el himno, una cortina), que no tiene por que depender de que
    /// escena este al aire.
    ///
    /// Si ya habia algo sonando con ese `name`, lo reemplaza: dos temas
    /// encimados es lo peor que puede pasar en vivo.
    ///
    /// :param name: nombre de la fuente (se reutiliza entre llamadas).
    /// :param file: ruta absoluta del archivo.
    /// :param loop: repetir al terminar.
    /// :param volume: ganancia lineal (1 = 0 dB).
    bool playMedia(const std::string& name, const std::string& file, bool loop = false, float volume = 1.0f);
    /// Para lo que este sonando y libera la fuente.
    bool stopMedia(const std::string& name);
    /// Rebobina y arranca una fuente de medios que YA existe (en una escena o
    /// global). Hace falta aparte de `playMedia` porque una fuente de video
    /// vive en su escena —se ve— y cambiarle los ajustes no la rearranca sola:
    /// si el archivo es el mismo que ya habia, libobs no tiene motivo para
    /// volver a empezar, y el cue del guion no mostraria nada.
    bool restartMedia(const std::string& name);

    // --- Descubrir dispositivos y capacidades (sin crear fuentes) ------------------
    struct ListItem {
        std::string name;
        std::string value;
        bool disabled = false;
    };
    /// true si el tipo de fuente esta registrado (el plugin cargo).
    bool sourceTypeExists(const std::string& typeId) const;
    /// Opciones de una propiedad-lista de un TIPO de fuente (p. ej. los
    /// microfonos de "pulse_input_capture"/"device_id"), con `settingsJson`
    /// aplicado primero para las listas que dependen de otra (las resoluciones
    /// de una camara dependen de la camara elegida).
    std::vector<ListItem> sourceTypeListItems(const std::string& typeId, const std::string& prop,
                                              const std::string& settingsJson = {}) const;
    /// Nombre del dispositivo grafico (GPU) con el que dibuja libobs.
    std::string graphicsDeviceName() const;
    struct EncoderInfo {
        std::string id, name, codec;
        bool hardware = false;
    };
    /// Codificadores de video registrados (los de hardware marcados).
    std::vector<EncoderInfo> videoEncoders() const;

    // --- Cambios en caliente -------------------------------------------------

    /// true si hay grabacion o transmision en curso.
    bool outputsActive() const;

    /// Cambia resolucion/fps. Solo sin salidas activas (libobs no lo permite
    /// en caliente): reinicia video, retorno a B y vista previa.
    bool resetVideo(const VideoConfig& video);

    /// Suelta los codificadores para que la proxima salida use una
    /// configuracion nueva. Solo sin salidas activas.
    bool resetEncoders();

    const VideoConfig& videoConfig() const { return video_; }

    /// Ids de codificador de video registrados en esta libobs, en el orden en
    /// que se probarian para `enc` (el ultimo siempre es x264).
    std::vector<std::string> videoEncoderCandidates(const EncoderConfig& enc) const;

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
    /// Transmite. Si YA está transmitiendo: al mismo destino no hace nada
    /// (ultimoInicio = YaIgual); a OTRO destino falla (YaOtroDestino), salvo
    /// `cambiarEnCaliente`, que corta y arranca al nuevo.
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente = false);
    enum class Inicio { Iniciado, YaIgual, YaOtroDestino, Error };
    Inicio ultimoInicio() const { return ultimoInicio_; }
    /// El destino al aire, enmascarado (servidor y los últimos 4 de la clave).
    std::string destinoActivo() const;

    // --- Destinos adicionales (respaldo, otras plataformas) --------------------
    // Cada uno es otra salida RTMP que comparte los codificadores del principal.
    /// La salida de libobs para una dirección: rtmp_output o ffmpeg_mpegts_muxer (srt://, rist://).
    static const char* tipoSalida(const std::string& url);
    /// Los datos del servicio rtmp_custom (en SRT la clave va como passphrase).
    static obs_data* datosServicio(const std::string& server, const std::string& key);
    bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc);
    void stopExtra(const std::string& id);
    void stopExtras();
    /// {id: {activo, kbps, perdidos, destino}}.
    nlohmann::json extrasEstado() const;
    /// La salud de cada salida de transmisión: {principal: {...}, extras: {id: {...}},
    /// saltados_encoder, lag_render} con activo, reconectando, bytes, perdidos (por red),
    /// cuadros y congestión (0..1). Lo muestrea la app cada segundo (T27).
    nlohmann::json saludSalidas() const;

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
    /// La grabación en curso: {activa, archivo, copia, copia_activa, calidad, encoder, pistas}.
    nlohmann::json grabacionEstado() const;

    // --- Repetición (replay buffer de libobs) ---------------------------------
    /// Guarda en memoria los últimos `segundos` del programa (0 = apagar), con los
    /// codificadores de la transmisión. `dir` es donde quedan los clips.
    bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc);
    /// Pide guardar el clip; llega por `ultimoReplay()` cuando termina de escribir.
    bool guardarReplay();
    std::string ultimoReplay() const;
    nlohmann::json replayEstado() const;

    /// Si un codificador se cayó en el aire (OBS_OUTPUT_ENCODE_ERROR), lo reemplaza
    /// por el siguiente candidato (al final, x264) y rearranca lo que corría:
    /// la transmisión, los destinos, la grabación (a un archivo nuevo) y el replay.
    /// Lo llama la app en cada vuelta; no hace nada si no pasó nada.
    void atenderEncoderCaido(bool stream, bool record);
    bool encoderRespaldoActivo() const { return encoderRespaldo_; }
    /// Las pistas (índice de libobs 1..5) que usa la grabación del programa: la
    /// grabación por entrada no las toma.
    int pistasGrabacionMask() const { return pistasGrabacionMask_; }

    // --- Grabacion POR ENTRADA ------------------------------------------------
    // Grabar cada camara a su propio archivo, ademas del programa. Es lo que
    // pide un evento del que despues hay que editar: el programa sirve para
    // subirlo esa misma noche, y los archivos por entrada para armar la version
    // buena sin haber perdido nada.
    //
    // COMO. libobs no sabe «grabar una fuente»: lo que sabe es rendir una VISTA
    // (obs_view) con una fuente en el canal 0 y dar un `video_t` propio. De ahi
    // cuelga un codificador dedicado y una salida a archivo. O sea que cada
    // entrada que se graba es UN ENCODE MAS: ocho camaras son ocho
    // codificaciones simultaneas y eso no lo hace cualquier maquina. Por eso
    // hay tope y por eso se avisa.
    //
    // EL AUDIO es mas limitado: libobs mezcla en 6 pistas y la 0 es la del
    // programa, asi que quedan 5 para darle una propia a cada fuente
    // (obs_source_set_audio_mixers). A la sexta en adelante, y a las que no
    // tienen audio, se les graba la del PROGRAMA: sirve de referencia para
    // sincronizar al editar, y ademas `ffmpeg_muxer` no arranca sin audio.
    struct GrabacionIndividual {
        std::string source; ///< que fuente
        std::string path;   ///< a que archivo
        /// true = graba el audio PROPIO de la fuente; false = el del programa
        /// (que igual sirve de referencia para sincronizar al editar).
        bool audioPropio = false;
        bool activa = false;
    };
    /// Cuantas se pueden grabar a la vez. No es un limite de libobs: es que
    /// mas que esto no lo codifica ninguna maquina razonable en vivo.
    static constexpr int kMaxIndividuales = 8;

    /// Arranca la grabacion individual de esa fuente.
    ///
    /// :param source: nombre de la fuente (tiene que existir).
    /// :param dir: carpeta destino (normalmente Recordings/).
    /// :param enc: de donde salen bitrate y preset.
    /// :returns: false y `lastError()` explica (no existe, ya se graba, no hay
    ///           lugar, no se pudo crear el codificador).
    bool startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    /// La señal limpia (clean feed): el programa SIN las capas globales (DSK) ni la
    /// placa de seguridad, con su propio encoder, a un destino de red (rtmp/srt/rist:
    /// `destino` + `clave`) o a un archivo en `dir` si no hay destino. Una a la vez.
    bool startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                        const EncoderConfig& enc);
    void stopCleanFeed();
    nlohmann::json cleanFeedEstado() const;
    /// La para y cierra su archivo. false si no se estaba grabando.
    bool stopSourceRecording(const std::string& source);
    /// Para todas.
    void stopAllSourceRecordings();
    /// Que se esta grabando por entrada, con su archivo.
    std::vector<GrabacionIndividual> sourceRecordings() const;

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

    // --- Vista previa y medicion (para la UI) -------------------------------

    /// Activa la vista previa del programa a `width` x `height`.
    ///
    /// Registra un callback de video crudo de libobs PROPIO, con su tamaño y
    /// formato (BGRA): el escalado y la conversion los hace libobs en la GPU,
    /// asi que la vista previa no cuesta CPU de escalado. Tambien activa el
    /// medidor de picos de la mezcla de programa.
    bool enablePreview(int width, int height);

    /// Desactiva la vista previa y el medidor. Idempotente.
    void disablePreview();

    /// Vista previa CONTINUA de una escena (o fuente) que puede no estar al
    /// aire: es lo que mira el editor de escenas.
    ///
    /// A diferencia de `captureSource`, que es una foto, esta mantiene tomado
    /// el `inc_showing` mientras dura, asi que las fuentes asincronas (camaras,
    /// video de red) siguen subiendo cuadros y no hay que esperarlas en cada
    /// uno. Llamar de nuevo con otro nombre cambia de escena.
    ///
    /// HAY VARIOS CANALES, y no por lujo: el editor de escenas mira la que se
    /// esta editando y el monitor de ARMADA mira la que va a salir con el
    /// proximo take. Son dos escenas distintas al mismo tiempo, y con un solo
    /// canal se pisaban —abrir el editor apagaba el monitor del director, que
    /// es exactamente cuando mas lo necesita—.
    ///
    /// :param canal: quien pide la vista previa ("editor", "armada"...).
    /// :param name: escena o fuente ("" = apaga ese canal).
    /// :param maxWidth: ancho maximo del cuadro entregado.
    bool enableScenePreview(const std::string& canal, const std::string& name, int maxWidth);
    /// Igual, sobre el canal "editor" (el historico).
    bool enableScenePreview(const std::string& name, int maxWidth) {
        return enableScenePreview("editor", name, maxWidth);
    }

    /// Apaga un canal de vista previa y suelta su fuente. Idempotente.
    void disableScenePreview(const std::string& canal);
    void disableScenePreview() { disableScenePreview("editor"); }

    /// Renderiza AHORA la escena de `enableScenePreview` y la deja en `dst`.
    ///
    /// :param dst: recibe RGBA empaquetado (width*4 por fila).
    /// :returns: false si no hay vista previa activa o la fuente no da imagen.
    bool renderScenePreview(const std::string& canal, std::vector<std::uint8_t>& dst, int& width,
                            int& height);
    bool renderScenePreview(std::vector<std::uint8_t>& dst, int& width, int& height) {
        return renderScenePreview("editor", dst, width, height);
    }
    /// Que escena esta mirando ese canal ("" = ninguna).
    std::string scenePreviewName(const std::string& canal) const;

    /// Copia el ultimo frame de vista previa si es mas nuevo que `serial`.
    ///
    /// :param dst: recibe BGRA empaquetado (width*4 bytes por fila).
    /// :param serial: el ultimo visto; se actualiza al entregar uno nuevo.
    /// :returns: true si se copio un frame nuevo.
    bool latestPreview(std::vector<std::uint8_t>& dst, int& width, int& height, std::uint64_t& serial) const;

    /// Picos de la mezcla de programa desde la ultima llamada, en dBFS por
    /// canal (L, R). -100 = silencio.
    void programPeaksDb(float& left, float& right);

    /// Callbacks crudos de la vista previa y el medidor (los llama libobs).
    void onPreviewVideo(struct video_data* frame);
    void onMeterAudio(struct audio_data* data);

    /// Estado actual (para la UI y el HealthBlock).
    EngineStatus status() const;

    /// Ultimo error registrado ("" si no hubo).
    const std::string& lastError() const { return lastError_; }

    /// Indica si libobs quedo inicializada.
    bool ready() const { return started_; }

   private:
    struct RecortePendiente {
        std::string escena, fuente;
        float r[4];
    };
    std::vector<RecortePendiente> recortesPendientes_;
    // Crea los codificadores si todavia no existen (se comparten entre las dos
    // salidas: codificar una sola vez y muxar dos veces).
    bool ensureEncoders(const EncoderConfig& enc);
    // Arranca una salida con los codificadores actuales y, si falla con un
    // codificador de hardware (y ninguna otra salida lo esta usando), cae al
    // siguiente candidato y reintenta.
    bool startOutputWithFallback(struct obs_output* out, const EncoderConfig& enc);
    void releaseVideoEncoder();
    // Ubica un item de escena en un rectangulo (fracciones del lienzo).
    void placeItem(struct obs_scene_item* item, const CanvasRect& rect) const;
    void setError(std::string msg);
    // Linux: elige Wayland/X11 para libobs antes de obs_startup (ver .cpp).
    bool selectWindowingPlatform();
    void releaseWindowingPlatform();

    bool started_ = false;
    void* nixDisplay_ = nullptr; // wl_display* o Display* (Linux)
    bool nixWayland_ = false;
    std::string lastError_;

    EnginePaths paths_;
    VideoConfig video_;
    AudioConfig audio_;

    // Handles de libobs (punteros opacos; el .cpp los castea).
    // Escenas (referencias propias) en orden de creacion y la transicion del
    // canal 0. Fuentes creadas por el motor (referencias propias).
    std::vector<std::pair<std::string, obs_scene*>> scenes_;
    std::string currentScene_;
    std::string activeServer_, activeKey_;
    std::unique_ptr<audio::LoudnessPrograma> loudness_ = std::make_unique<audio::LoudnessPrograma>(48000);
    static void loudnessCallback(void* param, size_t mix, struct audio_data* data);
    std::string recordPath_, remuxA_;   ///< lo que se graba y a qué pasarlo al terminar
    bool remuxAlTerminar_ = true;
    struct EstadoRemux;
    std::shared_ptr<EstadoRemux> remux_;
    Inicio ultimoInicio_ = Inicio::Iniciado;
    struct Extra {
        std::string server, key;
        void* output = nullptr;  ///< obs_output_t*
        void* service = nullptr; ///< obs_service_t*
        std::uint64_t bytesAntes = 0;
        std::int64_t msAntes = 0;
    };
    std::map<std::string, Extra> extras_;
    bool algunExtraActivo(const void* menos = nullptr) const;
    // FTB y placa: la fuente (o escena), el filtro de opacidad y el fundido.
    struct Velo {
        void* fuente = nullptr;  ///< obs_source_t*
        void* filtro = nullptr;  ///< color_filter_v2 (opacidad)
        float actual = 0, objetivo = 0, porSeg = 1;
        std::uint32_t canal = 0;
    };
    Velo veloFtb_, veloPlaca_;
    Velo dsk_[4];
    std::vector<void*> rotulosGlobales_; ///< obs_source_t* de textoGlobal (se sueltan al apagar)
    std::vector<std::string> dskContenido_[4];
    void asegurarDsk(int i);
    bool ftbAudio_ = false;
    std::string placaTexto_, placaImagen_;
    float volumenAntesFtb_ = 1.0f;
    bool tickVelo_ = false;
    static void tickVelos(void* data, float segundos);
    void asegurarVelos();
    void soltarVelos();
    obs_source* transition_ = nullptr;
    obs_source* activa_ = nullptr;   ///< la que está en el canal del programa (la global o una de una vez)
    std::map<std::string, obs_source*> deUnaVez_; ///< transiciones de un solo take, por tipo
    std::string manualHacia_;        ///< T-bar: la escena a la que se está yendo
    float manualT_ = 0;
    obs_source* transicionDeTipo(const std::string& tipo);
    void ponerEnPrograma(obs_source* tr);
    std::string transitionType_ = "fade";
    int transitionMs_ = 300;
    std::vector<std::pair<std::string, obs_source*>> sources_;
    std::vector<std::string> globalAudio_; // fuentes en canales de salida 1..
    std::vector<std::string> micInputs_;   // entradas de audio del equipo (canales 5+)
    /// Entradas de audio que se están creando en otro hilo (ver addGlobalAudio).
    std::vector<std::pair<SourceSpec, std::shared_future<obs_source*>>> pendingAudio_;
    void registerGlobalAudio(const SourceSpec& spec, obs_source* src);
    // Vumetro por fuente: obs_volmeter (opaco) + pico mas reciente en dBFS.
    struct Meter {
        void* vm = nullptr;
        std::atomic<float> peakDb{-100.0f};
        ~Meter(); // BroadcastEngineScenes.cpp
    };
    std::vector<std::pair<std::string, std::unique_ptr<Meter>>> meters_;

    obs_scene* sceneByName(const std::string& name) const;
    obs_source* ownedSource(const std::string& name) const;
    void attachMeter(const std::string& name, obs_source* src);
    void releaseScenes();
    // Compatibilidad con el codigo de una sola escena (escena actual).
    obs_scene* scene_ = nullptr;
    obs_encoder* videoEncoder_ = nullptr;
    obs_encoder* audioEncoder_ = nullptr;
    obs_output* streamOutput_ = nullptr;
    obs_output* recordOutput_ = nullptr;

    /// Una por fuente que se graba aparte. Se guarda todo lo que hay que
    /// liberar: si algo queda vivo, libobs no suelta ni la GPU ni el archivo.
    struct SourceRec;
    std::map<std::string, std::unique_ptr<SourceRec>> sourceRecs_;
    std::unique_ptr<SourceRec> clean_;
    std::string cleanDestino_;
    // T29: grabación con codificador propio, pistas extra, copia y replay.
    obs_encoder* recVideoEncoder_ = nullptr;
    std::string recVideoEncoderId_;
    std::vector<obs_encoder*> recAudioEncoders_; ///< por índice de mezcla (0..5); nullptr = no
    obs_output* recordCopy_ = nullptr;
    std::string recordCopyPath_;
    RecordConfig lastRec_;
    EncoderConfig lastEnc_;
    bool lastRecValido_ = false;
    bool forzarX264Archivo_ = false;
    int pistasGrabacionMask_ = 0;
    obs_output* replay_ = nullptr;
    int replaySegundos_ = 0;
    mutable std::mutex replayMu_;
    std::string ultimoReplay_;
    std::atomic<bool> encoderCayo_{false};
    std::atomic<bool> encoderCayoArchivo_{false};
    bool encoderRespaldo_ = false;
    void soltarEncodersGrabacion();
    bool crearEncoderArchivo(const RecordConfig& rec, const EncoderConfig& enc, std::size_t desde);
    static void onReplaySaved(void* data, ::calldata* cd);
    obs_service* service_ = nullptr;

    // Candidatos de codificador de video (ver EncoderConfig::videoEncoder) y
    // el indice del que esta en uso.
    std::vector<std::string> encoderCandidates_;
    std::size_t encoderIndex_ = 0;
    std::string videoEncoderId_;
    /// Estado de la salida de transmision, escrito desde los callbacks de
    /// libobs (que llegan en CUALQUIER hilo) y leido desde el de control.
    mutable std::mutex outputStateLock_;
    bool reconnecting_ = false;
    std::string outputProblem_;
    /// Engancha "reconnect", "reconnect_success" y "stop" de una salida.
    void watchOutput(obs_output* out);
    static void onOutputReconnect(void* data, ::calldata* cd);
    static void onOutputReconnected(void* data, ::calldata* cd);
    static void onOutputStopped(void* data, ::calldata* cd);

    // Canales IPC hacia el Proceso B (el lado PRODUCTOR).
    ba::ipc::SharedAudioRing returnAudio_;
    // --- Mix-minus: cada invitado recibe el programa SIN su propia voz -------
    // Se capta lo que aporta cada invitado (su fuente, después de filtros) y se
    // resta del programa alineado por timestamp; sale por BA.Audio.Return.<n>.
    struct MixMinus;
    std::vector<std::unique_ptr<MixMinus>> mixMinus_; ///< [0] sin usar; 1..16
    static void capturaInvitado(void* param, struct obs_source* src, const struct audio_data* data, bool muted);
    void iniciarMixMinus(int slot, struct obs_source* src);
    void soltarMixMinus();
   public:
    /// Lo que oye el invitado del slot: "mix_minus" (de fábrica) o "programa".
    void setRetornoInvitado(int slot, const std::string& modo);
    std::string retornoInvitado(int slot) const;
   private:
    ba::ipc::SpoutSender programVideo_;
    bool returnFeedsActive_ = false;

    // Re-bloqueo del audio a los 480 frames que exige el ring (spec §4.3).
    // libobs entrega bloques de tamaño variable; el ring solo acepta bloques
    // exactos, asi que hay que acumular. Lo toca UN SOLO hilo (el de audio de
    // libobs), por eso no necesita lock.
    std::vector<float> audioAccum_;
    std::uint64_t programFrameIndex_ = 0;
    /// Para medir cuanto tarda libobs entre componer un cuadro y entregarlo
    /// (ver onRawVideo). Solo las toca el hilo del callback de video.
    std::uint64_t programEdadNs_ = 0;
    std::uint64_t programMedidos_ = 0;
    std::uint64_t programInformeNs_ = 0;
    // Frame de programa empaquetado cuando libobs entrega filas con relleno
    // (linesize > ancho*4). Solo lo toca el hilo de render.
    std::vector<std::uint8_t> programPacked_;

    // Vista previa: el hilo de render escribe, la UI lee (bajo previewLock_).
    mutable std::mutex previewLock_;
    std::vector<std::uint8_t> previewBuf_;
    int previewW_ = 0;
    int previewH_ = 0;
    std::uint64_t previewSerial_ = 0;
    bool previewActive_ = false;

    // Vista previa del editor de escenas: la fuente queda "mostrandose"
    // mientras dura (ver enableScenePreview).
    /// Un canal de vista previa: la fuente tomada y a que tamaño se entrega.
    struct VistaPrevia {
        struct obs_source* src = nullptr;
        std::string name;
        int maxW = 0;
    };
    std::map<std::string, VistaPrevia> scenePreviews_;
    // Picos de programa (lineales, 0..1) acumulados entre lecturas.
    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};
};

} // namespace ba::app
