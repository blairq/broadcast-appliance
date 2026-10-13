// ============================================================================
// src/app/emision/BroadcastEngine.hpp — Motor de emision (Proceso A, spec §6)
//
// La cara del motor para el resto de la app: escenas, fuentes, audio,
// transiciones, capas (DSK, placa, negro), salidas (transmisión, destinos,
// grabación, señal limpia, grabación por entrada), vistas para la interfaz y
// el camino hacia el Proceso B (el programa en BA.Program y su audio).
//
// Por dentro es el motor de GStreamer (`src/motor`, ba::motor::Motor): ya no
// hay libobs. La API quedó la misma para que la interfaz, el MCP y la
// automatización no cambien; los tipos de fuente que guarda app.json (los
// nombres de siempre: «ffmpeg_source», «color_source_v3», «ba_gst_fuente»…)
// son tipos LÓGICOS que esta clase traduce a los ajustes del motor.
//
// Modelo de threading: todo desde el hilo de control (la UI o el arranque
// headless). El motor tiene sus hilos; lo que llega de ellos (cuadros, audio)
// solo toca estructuras con su propio lock.
//
// Estrategia de errores: no lanza. Cada operacion devuelve bool y deja el
// motivo en `lastError()`.
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <thread>
#include <deque>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "audio/LoudnessPrograma.hpp"
#include <utility>
#include <vector>

#include "ipc/CanalesEscena.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

namespace ba::motor {
class Motor;
struct Cuadro;
class Grabacion;
class Transmision;
} // namespace ba::motor

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
/// El tamaño del lienzo del motor andando (false si no hay motor).
bool lienzoActual(int& ancho, int& alto);
/// Los cuadros por segundo del motor andando (30 si no hay motor).
double fpsActual();

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
/// Efectos de un elemento EN LA ESCENA: transparencia, borde difuso y sombra (o
/// resplandor: distancia 0 y un color claro). Medidas en píxeles del lienzo; el
/// ángulo, hacia dónde cae la sombra (0 = derecha, 90 = abajo, 45 = abajo a la derecha).
/// En JSON (guardado y MCP):
///   {"opacidad": 1, "borde": 0, "esquinas": 0,
///    "sombra": {"activa": true, "color": "#000000", "alfa": 0.6, "difuminado": 20,
///               "distancia": 10, "angulo": 45, "tamano": 0}}
struct EfectosCapa {
    float opacidad = 1.0f; ///< de todo el elemento (y su sombra)
    float borde = 0;       ///< borde difuso: cuántos píxeles del borde se funden
    float esquinas = 0;    ///< radio de las esquinas redondeadas
    bool sombra = false;
    std::string color = "#000000"; ///< de la sombra: "#RRGGBB" o "#AARRGGBB"
    float alfa = 0.6f;             ///< opacidad de la sombra
    float difuminado = 20, distancia = 10, angulo = 45, tamano = 0;
    bool vacio() const { return opacidad >= 1.0f && borde <= 0 && esquinas <= 0 && !sombra; }
    /// Cambia lo que traiga `j` (lo demás queda); false + `error` si algo no sirve.
    bool aplicar(const nlohmann::json& j, std::string* error = nullptr);
    nlohmann::json aJson() const;
    /// Lo que entiende el motor en la capa (`sombra`, ver ba::motor::SombraCapa); null si no hay.
    nlohmann::json sombraMotor() const;
};

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
    EfectosCapa efectos; ///< en la escena
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
    /// Segundos de grabación que se juntan en memoria si el disco se traba
    /// (Configuración → Grabación: «Colchón si el disco se traba»).
    int colchonSeg = 60;
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

/// Motor de emision. Ver el header.
///
/// Uso tipico, todo desde el thread de control:
/// ```
/// BroadcastEngine eng;
/// eng.startup(paths, video, audio);
/// eng.buildScene();
/// eng.publishReturnFeeds();     // conecta el camino hacia el Proceso B
/// eng.startRecording(rec, enc); // respaldo local
/// eng.startStreaming(stream, enc);
/// ...
/// eng.shutdown();
/// ```
class BroadcastEngine {
   public:
    BroadcastEngine();
    ~BroadcastEngine();

    BroadcastEngine(const BroadcastEngine&) = delete;
    BroadcastEngine& operator=(const BroadcastEngine&) = delete;

    /// Arranca el motor (GStreamer, el reloj del estudio, el PROGRAMA y sus buses).
    bool startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio);
    /// Detiene salidas y el motor. Idempotente.
    void shutdown();
    /// El motor de GStreamer, para las piezas que trabajan sobre él (el grafo de
    /// video, los racks, la escucha). nullptr antes de startup.
    motor::Motor* motor() { return motor_.get(); }

    // --- Escenas ------------------------------------------------------------
    bool createScene(const std::string& name);
    /// Elimina una escena (no la actual si es la unica).
    bool removeScene(const std::string& name);
    bool renameScene(const std::string& from, const std::string& to);
    /// Nombres de las escenas en orden de creacion.
    std::vector<std::string> sceneNames() const;
    /// Pone `name` al aire con la transicion configurada (setTransition).
    bool setCurrentScene(const std::string& name);
    /// Escena al aire ("" si no hay).
    const std::string& currentScene() const { return currentScene_; }
    /// Tipo ("cut", "fade", "swipe", "slide", "fade_to_color", "wipe") y duracion.
    bool setTransition(const std::string& type, int durationMs);
    /// Pone `name` al aire con OTRA transición solo esta vez (`ms` < 0 = la global).
    bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms);
    /// T-bar: lleva la transición a mano hacia `name` (t de 0 a 1). Con `soltar`,
    /// termina (si pasó la mitad, queda al aire; si no, vuelve).
    bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire);
    /// En medio de una transición manual (y hacia qué escena).
    std::string manualHacia() const;

    // --- FTB y placa de seguridad (por encima de todo lo que sale) -------------
    /// Fundido a negro de la salida; con `bajarAudio`, el audio del programa baja con él.
    bool ftb(bool activar, int ms, bool bajarAudio);
    /// La placa ("Cuarto intermedio", "Volvemos enseguida"…): fondo, texto e imagen opcional.
    bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms);
    // --- DSK: capas globales encima del programa (1..4) ------------------------
    /// Arma el contenido de la capa `capa` (1..4): escenas o fuentes, por nombre,
    /// apiladas en ese orden. `recuadro` (opcional): {posicion, tamano, margen}.
    bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro);
    /// Sube o baja la capa con un fundido de `ms`.
    bool dskMostrar(int capa, bool visible, int ms);
    /// [{capa, visible, opacidad}] para las 4.
    nlohmann::json dskEstado() const;
    /// Las fuentes que muestran las capas que están arriba (tally, interlock de PTZ).
    std::vector<std::string> fuentesDsk() const;
    /// Un rótulo de texto con nombre (fuera de las escenas, para una capa DSK).
    bool textoGlobal(const std::string& nombre, const std::string& texto);
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    nlohmann::json estadoPlaca() const;

    /// Subtítulos CEA-608 en la transmisión (todavía no: M7). Devuelve a cuántas salidas se mandó.
    int enviarCaption(const std::string& texto, double segundos);

    /// El loudness de lo que sale: M/S/I, true peak, LRA (EBU R128).
    audio::LoudnessPrograma& loudness() { return *loudness_; }
    /// El programa en imagen, para las alarmas: brillo medio (0–255) y cuántos
    /// segundos lleva sin cambiar. `valido` false si todavía no llegó ningún cuadro.
    struct ProgramaVideo {
        double luma = 0, quietoSeg = 0;
        bool valido = false;
    };
    ProgramaVideo programaVideo() const;
    /// Cuántos cuadros salieron del programa (el latido del aire: si no crece, algo se trabó).
    std::uint64_t cuadrosPrograma() const { return progCuadros_.load(); }
    /// El tope de la latencia del estudio (ms; -1 = automático).
    void ponerTopeEstudio(long long ms);
    /// {latencia_ms, tope_ms, automatico, tarde: [entradas que pasan el tope]}.
    nlohmann::json latenciaEstudio() const;

    /// El archivo que se está grabando (o el último).
    std::string archivoGrabacion() const;
    /// La copia en el otro disco (vacío si no hay).
    std::string archivoCopiaGrabacion() const;
    /// Pasa `archivo` (mkv) a `formato` (mp4/mov) sin recodificar, en otro hilo.
    /// Varios archivos seguidos (los tramos de una grabación partida), en un solo hilo.
    bool remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen);
    bool remux(const std::string& archivo, const std::string& formato, bool borrarOrigen);
    /// {archivo, destino, pct, andando, ok, error}.
    nlohmann::json remuxEstado() const;

    /// Compatibilidad: crea la escena "programa" y la pone al aire.
    bool buildScene();

    // --- Fuentes -------------------------------------------------------------
    // Los nombres de fuente son unicos. Agregar a una escena una fuente cuyo
    // nombre ya existe REUTILIZA esa fuente.
    bool addSource(const std::string& scene, const SourceSpec& spec);
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
    /// Recorta la fuente EN LA ESCENA: izquierda, arriba, derecha, abajo (fracciones, 0..1).
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    /// Transparencia, borde difuso y sombra de la fuente EN LA ESCENA.
    bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos);
    EfectosCapa sourceEfectos(const std::string& scene, const std::string& name) const;
    /// Los recortes que esperan que su fuente tenga tamaño: se aplican apenas lo tiene.
    void aplicarRecortesPendientes();
    /// Renombra una fuente (en todas las escenas).
    bool renameSource(const std::string& from, const std::string& to);
    /// Tipo de la fuente ("" si no existe).
    std::string sourceTypeId(const std::string& name) const;
    /// TRAER (la tarjeta del grafo): lo que se ve de la entrada `entrada` en las
    /// escenas viene de otra parte: un compositor (`compositor`, el nombre de su
    /// fuente) o lo que publica otra entrada (`productor`, el de su basalida o
    /// «<fuente>:video»). Ambos vacíos = lo suyo. Rearma las escenas que la usan.
    void setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor);
    /// ¿Existe la fuente?
    bool sourceExists(const std::string& name) const { return sources_.count(name) > 0; }
    /// Lo que mide el motor de la entrada (tamaño, latencia, filtros, error...); null si no existe.
    nlohmann::json estadoEntrada(const std::string& name) const;
    /// Lo visible de una escena: (fuente, rectángulo en fracciones del lienzo), de abajo hacia arriba.
    std::vector<std::pair<std::string, CanvasRect>> itemsVisibles(const std::string& scene) const;
    /// El reloj del estudio: sistema, ntp, ptp o red. Si cambia con el motor andando, se rearma.
    bool ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio);
    /// Publica el reloj del estudio en la red (0 = no). "" o el error.
    std::string publicarReloj(int puerto);
    /// El canal de control «ba/control» de una entrada. "" o el error.
    std::string controlEntrada(const std::string& name, const nlohmann::json& campos);
    /// Settings actuales de la fuente como JSON.
    std::string sourceSettingsJson(const std::string& name) const;
    /// Compatibilidad: en el motor todas las fuentes andan siempre (no hace nada).
    void holdShowing(const std::string& name, bool on);
    /// Tamaño actual del video de una fuente (0x0 si todavia no llego imagen).
    bool sourceSize(const std::string& name, int& width, int& height) const;
    /// Reemplaza los settings de la fuente.
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);
    /// La captura sin que espere quien llama (la interfaz nunca espera): prepara
    /// acá —lee escenas y fuentes, que son del hilo de la interfaz—, espera el
    /// cuadro en otro hilo y llama a `listo` DESDE ESE HILO (quien la usa en la
    /// interfaz la vuelve a su hilo). `error` vacío = hay imagen (RGBA).
    using Captura = std::function<void(const std::string& error, std::vector<std::uint8_t> rgba, int ancho, int alto)>;
    void capturarAparte(const std::string& name, int maxWidth, Captura listo);
    /// Imagen actual de una fuente o escena ("" = el programa, con sus capas),
    /// con ancho de a lo sumo `maxWidth` (0 = el del lienzo). RGBA 8 bits.
    /// ESPERA el cuadro (hasta 3 s): no desde el hilo de la interfaz (ver capturarAparte).
    bool captureSource(const std::string& name, int maxWidth, std::vector<std::uint8_t>& rgba, int& width,
                       int& height);
    /// captureSource en dos partes, para capturar desde otro hilo (las
    /// miniaturas de Entradas, que se cargan aparte para no trabar la interfaz):
    /// `prepararCaptura` lee las escenas, las fuentes y el estado del motor, que
    /// son del hilo de la interfaz (desde otro hilo era una carrera que tumbaba
    /// la app); `capturarVista` arma o reusa la vista y espera el cuadro, y se
    /// puede llamar desde cualquier hilo. "" o el error.
    std::string prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                bool& esEscena) const;
    std::string capturarVista(const std::string& clave, int w, int h, bool esEscena, std::vector<std::uint8_t>& rgba,
                              int& width, int& height);
    /// Tipos de fuente que el motor sabe armar.
    std::vector<SourceType> inputTypes() const;
    /// Invitados: la fuente de video va en escenas; la de AUDIO es global.
    bool addGuestAudio(int slot);
    /// Entrada de audio del equipo (micrófono, línea) como fuente GLOBAL.
    bool addGlobalAudio(const SourceSpec& spec);
    /// Compatibilidad (en el motor nada queda pendiente): devuelve 0.
    int adoptPendingAudio();
    bool removeGlobalAudio(const std::string& name);
    /// Nombres de las entradas de audio del equipo (sin los invitados).
    std::vector<std::string> globalAudioInputs() const { return micInputs_; }
    bool addGuestVideo(int slot);
    bool addGuestVideo(int slot, const CanvasRect& rect);

    // --- Audio por fuente (mezclador) ------------------------------------------
    /// Fuentes con audio (escenas + globales), en orden.
    std::vector<std::string> audioSources() const;
    /// Todas las fuentes que maneja el motor, sin repetir.
    std::vector<std::string> sourceNames() const;
    bool setSourceVolume(const std::string& name, float mul);
    float sourceVolume(const std::string& name) const;
    bool setSourceMuted(const std::string& name, bool muted);
    bool sourceMuted(const std::string& name) const;
    /// Pico mas reciente de la fuente en dBFS (-100 = silencio): lo que entrega la
    /// entrada, vaya o no al Programa (el mismo de los vúmetros del Mezclador).
    float sourcePeakDb(const std::string& name) const;
    /// El pico de lo que ENTREGA la entrada (después de sus filtros y su rack, antes de su
    /// volumen), vaya o no a alguna salida: el vúmetro de su fila en la matriz.
    float sourceOutputPeakDb(const std::string& name) const;
    /// Los auriculares del operador tienen dos cosas distintas:
    /// - la ESCUCHA FIJA: lo que la matriz manda ahí a propósito (la voz del
    ///   asistente, la del teleprompter). Varias juntas; se guarda (audio-matriz.json).
    /// - el PFL: escuchar UN canal un rato. Uno solo a la vez; se SUMA a la escucha
    ///   fija sin tocarla (apagarlo saca solo lo que sumó) y no se guarda.
    /// Lo que suena es la unión de las dos (sourceMonitoring).
    bool setSourceMonitoring(const std::string& name, bool on);
    /// Si suena en los auriculares (escucha fija o PFL).
    bool sourceMonitoring(const std::string& name) const;
    /// Solo la escucha fija (lo que muestra y guarda la matriz).
    bool sourceEscuchaFija(const std::string& name) const;
    /// El PFL: "" lo apaga. Falla si la fuente no existe.
    bool setPfl(const std::string& name);
    std::string pfl() const { return pfl_; }
    /// Atrasa (ms > 0) o adelanta la fuente para que su audio coincida con su video.
    bool setSourceSyncOffset(const std::string& name, int ms);
    int sourceSyncOffset(const std::string& name) const;
    /// A qué pistas va la fuente (1 = el programa; 2..6 = la grabación multipista).
    bool setSourcePistas(const std::string& name, const std::vector<int>& pistas);
    std::vector<int> sourcePistas(const std::string& name) const;

    // --- Medios ------------------------------------------------------------------
    struct MediaInfo {
        std::string name;
        std::string state; ///< playing | paused | stopped | ended | error | none
        std::int64_t durationMs = 0;
        std::int64_t timeMs = 0;
    };
    /// Fuentes reproducibles y su estado.
    std::vector<MediaInfo> mediaSources() const;
    /// Reproduce un archivo como fuente GLOBAL de audio (reemplaza lo que sonaba con ese nombre).
    bool playMedia(const std::string& name, const std::string& file, bool loop = false, float volume = 1.0f);
    /// Para lo que este sonando y libera la fuente.
    bool stopMedia(const std::string& name);
    /// Rebobina y arranca una fuente de medios que YA existe.
    bool restartMedia(const std::string& name);
    /// Lleva un medio a `ms`.
    bool seekMedia(const std::string& name, std::int64_t ms);
    /// Para un medio sin sacarlo.
    bool pararMedia(const std::string& name);
    /// Pausa o sigue un medio.
    bool pausarMedia(const std::string& name, bool pausa);

    // --- Descubrir capacidades -------------------------------------------------
    struct ListItem {
        std::string name;
        std::string value;
        bool disabled = false;
    };
    /// true si el tipo de fuente es uno que el motor sabe armar.
    bool sourceTypeExists(const std::string& typeId) const;
    /// Opciones de una lista de un tipo (dispositivos): micrófonos, cámaras, salidas de audio.
    std::vector<ListItem> sourceTypeListItems(const std::string& typeId, const std::string& prop,
                                              const std::string& settingsJson = {}) const;
    /// Nombre de la placa de video (para el diagnóstico).
    std::string graphicsDeviceName() const;
    struct EncoderInfo {
        std::string id, name, codec;
        bool hardware = false;
    };
    /// Codificadores de video disponibles (los de la placa marcados).
    std::vector<EncoderInfo> videoEncoders() const;

    // --- Cambios en caliente -------------------------------------------------
    /// true si hay grabacion o transmision en curso.
    bool outputsActive() const;
    /// Cambia resolucion/fps. Solo sin salidas activas: rearma el motor con todo lo que había.
    bool resetVideo(const VideoConfig& video);
    /// Compatibilidad: los codificadores se crean por salida. Solo sin salidas activas.
    bool resetEncoders();
    const VideoConfig& videoConfig() const { return video_; }
    /// Codificadores de video en el orden en que se probarían para `enc` (el último, x264enc).
    std::vector<std::string> videoEncoderCandidates(const EncoderConfig& enc) const;

    /// Publica el programa hacia el Proceso B (BA.Program; el audio va por BA.Audio.ContentForReturn).
    bool publishReturnFeeds();
    void stopReturnFeeds();
    /// ¿Alguien mira el retorno (invitados en la sala, integrantes remotos)? Sin
    /// nadie, la vista que lo baja a 1920×1080 en cada cuadro se suelta (el canal
    /// queda publicado); con alguien, vuelve.
    void retornoEnUso(bool si);

    /// Una escena que no es el programa, para las pantallas de la sala: la pone
    /// en el canal `k` (BA.Escena.<k>, 0..ipc::kCanalesEscena-1) y la manda al
    /// Proceso B mientras esté ahí; vacía, lo suelta. La escena se compone aparte
    /// y sigue andando aunque no esté al aire. Dos pantallas con la misma escena
    /// comparten canal: eso lo decide quien reparte los canales (Pantallas).
    /// Lo que se le encargó al motor y falló después: las órdenes que pueden
    /// tardar van en cola y vuelven enseguida (la interfaz no espera), así que
    /// su error llega por acá, desde el hilo del motor.
    void alFallarMotor(std::function<void(const std::string&)> fn);
    /// Avisa cuando cambia a dónde va el audio de una fuente: su escucha (PFL) o sus
    /// pistas. Lo pida quien lo pida (la matriz, el PFL, el asistente, una escena que
    /// se carga, volver como estaba): quien muestra el ruteo se entera siempre. Se
    /// llama en el hilo que hizo el cambio.
    void alCambiarRuteo(std::function<void(const std::string& fuente)> fn);
    bool escenaACanal(int k, const std::string& escena);
    std::string escenaDeCanal(int k) const;
    /// Los que no son Pantallas (las vistas de la sesión remota) toman un canal con
    /// dueño: el que ya tiene esa escena o uno libre (-1 si no hay). Pantallas no
    /// suelta un canal con dueños; el último dueño que lo suelta, lo apaga.
    int tomarCanalEscena(const std::string& escena, const std::string& dueno);
    void soltarCanalesEscena(const std::string& dueno);
    bool canalEscenaConDuenos(int k) const;

    /// Transmite. Si YA está transmitiendo: al mismo destino no hace nada
    /// (ultimoInicio = YaIgual); a OTRO destino falla (YaOtroDestino), salvo
    /// `cambiarEnCaliente`, que corta y arranca al nuevo.
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente = false);
    enum class Inicio { Iniciado, YaIgual, YaOtroDestino, Error };
    Inicio ultimoInicio() const { return ultimoInicio_; }
    /// El destino al aire, enmascarado (servidor y los últimos 4 de la clave).
    std::string destinoActivo() const;

    // --- Destinos adicionales (respaldo, otras plataformas) --------------------
    bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc);
    void stopExtra(const std::string& id);
    void stopExtras();
    /// {id: {activo, reconectando, perdidos, cuadros, bytes, destino}}.
    nlohmann::json extrasEstado() const;
    /// {principal: {...}, extras: {id: {...}}, saltados_encoder, lag_render}.
    nlohmann::json saludSalidas() const;
    /// Detiene la transmision (no toca la grabacion).
    void stopStreaming();

    /// Arranca la grabacion del programa (MKV en tramos; MP4/MOV se pasan al terminar).
    bool startRecording(const RecordConfig& rec, const EncoderConfig& enc);
    void stopRecording();
    /// {activa, archivo, copia, copia_activa, calidad, encoder, pistas, tramos}.
    nlohmann::json grabacionEstado() const;

    // --- Repetición ---------------------------------------------------------------
    /// Guarda los últimos `segundos` del programa (0 = apagar). `dir`: donde quedan los clips.
    bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc);
    /// Guarda el clip; llega por `ultimoReplay()` cuando termina de escribir.
    bool guardarReplay();
    std::string ultimoReplay() const;
    nlohmann::json replayEstado() const;

    // --- El disco en apuros: el disco, para la grabación del programa --------
    // Si el disco no da abasto (lo que espera en el colchón de una grabación, o
    // el registro trabado escribiendo), se avisa y se ofrece darle el disco a la
    // grabación del programa: se pausan las ISO, la repetición, la copia si va
    // al mismo disco y la señal limpia a disco, y el registro se junta en memoria.
    // Cuando el disco anda bien de nuevo (30 s), todo sigue solo.
    /// "preguntar" (avisa y ofrece), "auto" (lo hace solo) o "nunca" (solo avisa).
    std::string modoDisco() const;
    /// Configuración → Grabación: cuánto espera lo grabado para entrar al disco
    /// hasta «apuros», y cuánto todo al día hasta salir.
    static void ponerUmbralesDisco(double apurosS, int recuperadoS);
    std::string setModoDisco(const std::string& modo); ///< "" o el error (se guarda en Config/disco.json)
    /// Darle (o devolverle) el disco al programa ahora.
    void priorizarPrograma(bool si);
    /// {apuros, atraso_s, registro_trabado_ms, prioridad, automatica, modo, pausadas[], desde_s}
    nlohmann::json discoEstado() const;
    /// Para probar (modo --test): la grabación del programa, como con el disco trabado `ms`.
    void simularDiscoTrabado(int ms);

    /// Compatibilidad: las salidas del motor se rearman solas. No hace nada.
    void atenderEncoderCaido(bool stream, bool record);
    bool encoderRespaldoActivo() const { return false; }

    // --- Medir la sincronía labios/voz con un aplauso -----------------------------
    bool empezarMedicionSincro();
    /// {ok, retardo_ms, dispersion_ms, aplausos, confiable, problema, golpes_audio, golpes_video}.
    nlohmann::json terminarMedicionSincro();
    bool midiendoSincro() const { return medSincro_ != nullptr; }
    /// Las pistas (máscara: bit 1..5 = pistas 2..6) que usa la grabación del programa.
    int pistasGrabacionMask() const { return pistasGrabacionMask_; }

    // --- Grabacion POR ENTRADA ------------------------------------------------
    // Cada entrada a su propio archivo, además del programa: con su video y su
    // audio (o el del programa si no tiene). Cada una es un encode más.
    struct GrabacionIndividual {
        std::string source;
        std::string path;
        bool audioPropio = false;
        bool activa = false;
        std::uint64_t bytes = 0; ///< lo que ya fue al archivo (sin preguntarle al disco)
        bool pausada = false;    ///< el disco, para el programa
    };
    static constexpr int kMaxIndividuales = 8;
    bool startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    /// La señal limpia (el programa sin DSK, placa ni negro) a un destino de red o a un archivo.
    bool startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                        const EncoderConfig& enc);
    void stopCleanFeed();
    nlohmann::json cleanFeedEstado() const;
    bool stopSourceRecording(const std::string& source);
    void stopAllSourceRecordings();
    std::vector<GrabacionIndividual> sourceRecordings() const;

    // --- Vista previa y medicion (para la UI) -------------------------------
    /// La vista previa del programa a `width` x `height` (BGRA, la escala el motor).
    bool enablePreview(int width, int height);
    void disablePreview();
    /// Vista previa CONTINUA de una escena (o fuente) aunque no esté al aire, por canal
    /// ("editor", "armada"…): varios canales a la vez.
    bool enableScenePreview(const std::string& canal, const std::string& name, int maxWidth);
    bool enableScenePreview(const std::string& name, int maxWidth) { return enableScenePreview("editor", name, maxWidth); }
    void disableScenePreview(const std::string& canal);
    /// Diagnóstico de parpadeos (BA_MEDIR_PREVIEW=1): ver medirVista en el .cpp.
    void medirVista(const std::string& clave);
    void disableScenePreview() { disableScenePreview("editor"); }
    /// El último cuadro de ese canal (BGRA, filas de `stride` bytes) si es más
    /// nuevo que `serial`: `dst` se queda con el buffer (sin copias de más).
    bool renderScenePreview(const std::string& canal, std::vector<std::uint8_t>& dst, int& width, int& height,
                            int& stride, std::uint64_t& serial);
    std::string scenePreviewName(const std::string& canal) const;
    /// El ultimo frame de vista previa (BGRA, filas de `stride` bytes) si es mas
    /// nuevo que `serial`: `dst` se queda con el buffer.
    bool latestPreview(std::vector<std::uint8_t>& dst, int& width, int& height, int& stride, std::uint64_t& serial) const;
    /// Picos del programa desde la ultima llamada, en dBFS (L, R). -100 = silencio.
    void programPeaksDb(float& left, float& right);

    /// Estado actual (para la UI y el HealthBlock).
    EngineStatus status() const;
    const std::string& lastError() const { return lastError_; }
    bool ready() const { return started_; }

    /// Lo que oye el invitado del slot: "mix_minus" (de fábrica) o "programa".
    void setRetornoInvitado(int slot, const std::string& modo);
    std::string retornoInvitado(int slot) const;

   private:
    /// Las vistas de captura vivas (clave → tamaño y último uso): las miniaturas se
    /// piden seguido y armar una vista por pedido congelaba la interfaz. Se sueltan
    /// a los 10 s sin uso. Puede llamarse desde varios hilos (capturaMu_).
    struct VistaCaptura {
        int ancho = 0, alto = 0;
        std::chrono::steady_clock::time_point uso;
    };
    std::mutex capturaMu_;
    std::map<std::string, VistaCaptura> capturas_;
    // --- El modelo propio (lo que el motor no guarda) ---
    struct Fuente {
        SourceSpec spec;           ///< tipo lógico y ajustes tal como los pidieron
        bool global = false;       ///< suena siempre (micrófonos, invitados, música)
        int syncMs = 0;
        bool monitor = false;
        std::vector<int> pistas{1};
        /// Un compositor del grafo de video (ba_video_compositor): no es una
        /// entrada, es una escena interna del motor; esta es su composición.
        std::string compProductor;
    };
    struct Item {
        std::string fuente;
        CanvasRect rect;
        bool visible = true;
        float recorte[4] = {0, 0, 0, 0};
        EfectosCapa efectos;
    };
    struct EscenaApp {
        std::string nombre;
        std::vector<Item> items; ///< de abajo hacia arriba
    };
    EscenaApp* escena(const std::string& nombre);
    const EscenaApp* escena(const std::string& nombre) const;
    /// La escena tal como la entiende el motor (capas en píxeles).
    nlohmann::json escenaMotor(const EscenaApp& e) const;
    /// entrada -> {compositor, productor} (ver setTraer).
    std::map<std::string, std::pair<std::string, std::string>> traer_;
    /// Le manda la escena al motor (si está al aire o armada, cambia andando).
    void publicarEscena(const EscenaApp& e);
    /// Crea la fuente en el motor (si no existe). false + lastError si no se pudo.
    bool crearFuente(const SourceSpec& spec, bool global);
    /// Los ajustes del motor para un tipo lógico (y si se pudo).
    bool ajustesMotor(const SourceSpec& spec, nlohmann::json& ajustes) const;
    bool ajustesMotorTipo(const SourceSpec& spec, nlohmann::json& ajustes) const;
    /// La escena interna de un compositor (sus capas, de la config del grafo).
    nlohmann::json escenaCompositor(const std::string& nombre, const nlohmann::json& config) const;
    /// ¿Quedó en alguna escena o es global? Si no, se libera.
    void soltarSiHuerfana(const std::string& nombre);
    /// Vuelve a mandarle al motor todas las capas (DSK, placa, negro).
    void publicarCapas(int ms);
    std::string transicionMotor(const std::string& tipo) const;
    void setError(std::string msg);
    /// El estado del motor, cacheado (las consultas de la UI llegan muy seguido).
    const nlohmann::json& estadoMotor() const;

    bool started_ = false;
    std::string lastError_;
    EnginePaths paths_;
    VideoConfig video_;
    std::string relojTipo_ = "sistema", relojServidor_;
    int relojPuerto_ = 0, relojDominio_ = 0, relojPublicar_ = 0;
    AudioConfig audio_;
    std::unique_ptr<motor::Motor> motor_;

    std::vector<EscenaApp> scenes_;
    std::map<std::string, Fuente> sources_;
    std::vector<std::string> ordenFuentes_;  ///< en orden de creación
    std::string currentScene_;
    std::string transitionType_ = "fade";
    int transitionMs_ = 300;
    std::vector<std::string> micInputs_;     ///< entradas de audio del equipo
    std::map<int, std::string> retornos_;    ///< slot → modo
    struct RecortePendiente {
        std::string escena, fuente;
    };
    std::vector<RecortePendiente> recortesPendientes_;

    // Capas: DSK 1..4, la placa y el negro.
    struct Dsk {
        std::vector<std::string> contenido;
        nlohmann::json recuadro = nlohmann::json::object();
        bool visible = false;
    };
    Dsk dsk_[4];
    bool ftb_ = false, ftbAudio_ = false, placa_ = false;
    std::string placaTexto_, placaImagen_;

    // Salidas. stream_, extras_, rec_, recCopia_, replay_, sourceRecs_, clean_ y
    // recordPath_/recordCopyPath_ son SOLO del hilo de salidas (ver enSalidas):
    // arrancarlas, pararlas y atenderlas son cambios de estado de GStreamer que
    // tardan (parar una grabación espera el cierre del archivo; una transmisión
    // caída se rearma probando codificadores). La interfaz encola y lee la foto.
    std::thread hiloSalidas_;
    std::mutex salidasColaM_;
    std::condition_variable salidasCv_;
    std::deque<std::pair<std::string, std::function<std::string()>>> salidasCola_;
    bool salidasSeguir_ = false; ///< bajo salidasColaM_
    void bucleSalidas();
    void enSalidas(const std::string& que, std::function<std::string()> f);
    void detenerSalidas();
    void ticSalidas();
    void publicarFotoSalidas();
    nlohmann::json fotoSalidas() const;
    mutable std::mutex fotoSalidasM_;
    nlohmann::json fotoSalidas_ = nlohmann::json::object();
    void avisarFallo(const std::string& texto);
    // Lo pedido (lo escribe la interfaz al pedir; el hilo de salidas lo baja si
    // no arrancó): para contestar «ya está» o «ya hay tantas» sin esperar.
    mutable std::mutex pedidosM_;
    bool streamPedido_ = false, grabacionPedida_ = false, limpioPedido_ = false;
    std::set<std::string> extrasPedidos_, individualesPedidas_;
    std::string startStreamingYa(const StreamConfig& stream, const EncoderConfig& enc);
    std::string startExtraYa(const std::string& id, const std::string& server, const std::string& key,
                             const EncoderConfig& enc);
    std::string startRecordingYa(const RecordConfig& rec, const EncoderConfig& enc, const std::vector<int>& pistas,
                                 const std::string& nombre);
    void stopRecordingYa();
    std::string setReplayYa(int segundos, const std::string& dir, const EncoderConfig& enc);
    std::string startSourceRecordingYa(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    std::string startCleanFeedYa(const std::string& destino, const std::string& clave, const std::string& dir,
                                 const EncoderConfig& enc);

    std::unique_ptr<motor::Transmision> stream_;
    std::string activeServer_, activeKey_;
    Inicio ultimoInicio_ = Inicio::Iniciado;
    struct Extra {
        std::string server, key;
        std::unique_ptr<motor::Transmision> tx;
    };
    std::map<std::string, Extra> extras_;
    std::unique_ptr<motor::Grabacion> rec_, recCopia_;
    std::string recordPath_, recordCopyPath_, remuxA_;
    std::string remuxSal_; ///< del hilo de salidas: a qué pasar la grabación al pararla
    bool remuxAlTerminarSal_ = true;
    bool remuxAlTerminar_ = true;
    RecordConfig lastRec_;
    bool lastRecValido_ = false;
    int pistasGrabacionMask_ = 0;
    struct EstadoRemux;
    std::shared_ptr<EstadoRemux> remux_; ///< bajo remuxM_ (lo lanza el hilo de salidas al parar)
    mutable std::mutex remuxM_;
    struct SourceRec {
        std::string path;
        bool audioPropio = false;
        std::unique_ptr<motor::Grabacion> grab;
        std::unique_ptr<motor::Transmision> tx;
        std::string destino;
    };
    std::map<std::string, SourceRec> sourceRecs_;
    std::unique_ptr<SourceRec> clean_;
    // Repetición: un anillo de tramos chicos en una carpeta; guardar junta los últimos.
    struct Repeticion;
    std::unique_ptr<Repeticion> replay_;
    // El disco en apuros (todo del hilo de salidas, menos el modo).
    void vigilarDisco();
    void aplicarPrioridad(bool si, bool automatica);
    std::atomic<int> modoDisco_{0}; ///< 0 preguntar, 1 auto, 2 nunca
    bool discoApuros_ = false, prioridad_ = false, prioridadAuto_ = false;
    std::int64_t apurosDesdeMs_ = 0, discoBienDesdeMs_ = 0;
    double atrasoDisco_ = 0;
    std::int64_t registroTrabadoMs_ = 0;
    std::vector<std::string> pausadas_;

    // Lo que llega de los hilos del motor
    std::unique_ptr<audio::LoudnessPrograma> loudness_ = std::make_unique<audio::LoudnessPrograma>(48000);
    int oyenteAudio_ = 0;
    std::atomic<float> peakL_{0.0f}, peakR_{0.0f};
    std::atomic<double> progLuma_{0};
    std::atomic<std::uint64_t> progFirma_{0};
    std::atomic<std::int64_t> progCambioMs_{0};
    std::atomic<std::uint64_t> progCuadros_{0};
    struct MedicionSincro;
    std::shared_ptr<MedicionSincro> medSincro_;

    // Vistas para la interfaz
    bool previewActive_ = false;
    std::uint64_t previewSerie_ = 0;
    struct VistaPrevia {
        std::string clave, name;
        std::uint64_t serie = 0;
    };
    std::map<std::string, VistaPrevia> scenePreviews_;

    // Al gateway
    ba::ipc::SpoutSender programVideo_;
    bool returnFeedsActive_ = false;
    bool retornoPausado_ = false;
    void pedirVistaRetorno();
    void enviarRetorno(const motor::Cuadro& c);
    std::uint64_t programFrameIndex_ = 0;
    std::vector<std::uint8_t> programPacked_;
    struct CanalEscena {
        std::unique_ptr<ba::ipc::SpoutSender> sender; ///< se rehace si cambia el tamaño
        std::string escena; ///< vacía: libre
        std::string clave;  ///< la vista del motor ("escena:<n>@pantallas<k>")
        int ancho = 0, alto = 0;
        std::uint64_t cuadros = 0;
        std::vector<std::uint8_t> filas;
    };
    std::array<CanalEscena, ba::ipc::kCanalesEscena> canalesEscena_;
    std::array<std::set<std::string>, ba::ipc::kCanalesEscena> duenosEscena_;
    /// El hilo del motor manda mientras la interfaz cambia la escena de un canal.
    std::mutex canalesEscenaM_;
    /// Las entradas internas (fundido a negro, placa) que ya se le pidieron a este motor.
    std::set<std::string> internasPedidas_;
    std::atomic<int> capturasEnCurso_{0};
    std::mutex falloMotorM_;
    std::function<void(const std::string&)> falloMotor_;
    std::function<void(const std::string&)> cambioRuteo_; ///< alCambiarRuteo (se fija una vez, al armar)
    std::string pfl_; ///< el canal en PFL ("" = ninguno); no se guarda
    void enviarEscena(int k, const motor::Cuadro& c);

    mutable nlohmann::json estadoCache_ = nlohmann::json::object(); ///< objeto vacío hasta la primera foto: arrancando, `value()` sobre un null tiraba excepción (caída del 2026-10-11)
    mutable std::shared_ptr<const nlohmann::json> estadoFoto_;
};

} // namespace ba::app
