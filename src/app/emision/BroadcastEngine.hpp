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
// automatización no cambien. Los tipos de fuente: los del motor (ba_gst_fuente,
// ba_gst_medio) y los registrados (FuentesApp: color, texto, imagen…); lo que
// llega con un tipo de antes lo pasa inputs::alMotor (InputSpecs.hpp).
//
// LAS SALIDAS (docs/planes/PLAN-SALIDAS.md, etapa 2). El aparato tiene N salidas
// y cada una es un operador completo (ver ba::motor::Motor, LAS SALIDAS): acá,
// cada una con su escena al aire, su FTB, su placa y sus DSK, su mezcla (el
// volumen y el mute de cada fuente EN ESA salida), su loudness, sus
// transmisiones (la principal y las extra), su grabación (y su copia), su
// repetición, su señal limpia y su retorno al gateway. Las escenas y las
// fuentes son de todas. Todo lo que no dice salida (`salida` "" en cada
// método) va a la por omisión («principal»), que existe siempre: con una sola
// salida todo anda como antes. La por omisión publica al gateway BA.Program y
// BA.Audio.ContentForReturn, como siempre (y nada más: el gateway la reconoce
// por su id); cada una de las demás, BA.Salida.<id> y BA.Audio.Salida.<id>.
// Las grabaciones por entrada (ISO) son de las entradas, no de una salida.
//
// EL EMISOR APARTE (04-plan-unificado.md, F3-01; BroadcastEngineEmisor.cpp). Con el
// launcher en `emisor.aparte`, una salida (la principal) la codifica, graba y
// transmite `ba-emisor` en otro proceso: si A se cae, la transmisión y el archivo
// siguen (con la placa). El launcher lo dice en BA_EMISOR. Para esa salida, TODO lo
// que sale (la transmisión principal y los destinos extra, la grabación con su copia
// y sus pistas 2-6, la repetición, la señal limpia) se PIDE al emisor (Runtime/emisor/
// <id>.json, emisor/Pedido.hpp; las claves, al CredentialStore) en vez de armarse acá,
// y lo que hace se lee de su estado (la foto de las salidas lo muestra como si fuera
// de acá). Su video le llega por un canal propio en NV12 del tamaño de la salida
// (BA.Emisor.<id>, y .limpio mientras se pide la señal limpia), publicado siempre; el
// del gateway sigue como el de cualquier salida. Apagar o rearmar el motor no le pide
// parar nada: lo para el operador, o el cierre ordenado del launcher.
//
// Modelo de threading (F3-03): EL TURNO (emision/TurnoMotor.hpp). El dueño es el plano de
// control (en la consola, su bucle): corre el reloj del motor, las órdenes de la interfaz
// (emision/VentanillaMotor) y las herramientas MCP del control. La interfaz que todavía
// entra directo (las áreas sin migrar) lo toma por evento y queda contada. Cada método de
// la API toma el turno al entrar, salvo los que dicen DESDE CUALQUIER HILO (fotos, medidas y
// vistas con su propio candado). El motor tiene sus hilos (el de salidas, el de los
// cuadros, los de GStreamer): no toman el turno; lo que llega de ellos solo toca
// estructuras con su propio candado. Una operación compuesta de afuera (AppRuntime,
// escenas::Conmutador) toma el turno por todo lo que dura (turno()).
//
// Estrategia de errores: no lanza. Cada operacion devuelve bool y deja el
// motivo en `lastError()`, que es DEL HILO que llamó (como errno): lo que falló en el
// control no pisa el error que la interfaz está por leer.
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
#include "emision/TurnoMotor.hpp"

namespace ba::motor {
class Motor;
struct Cuadro;
class Grabacion;
class Transmision;
} // namespace ba::motor

namespace ba::emisor {
struct Pedido;
struct PedidoLimpio;
} // namespace ba::emisor

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
    /// Codificador de video: un alias.
    ///   "auto"         -> hardware de la plataforma en orden (Windows: NVENC,
    ///                     QSV, AMF; Linux: NVENC, VAAPI; macOS: VideoToolbox)
    ///   "nvenc" | "qsv" | "amf" | "vaapi" | "videotoolbox" | "x264"
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
    std::string id;                  ///< tipo: "ba_gst_fuente", "ba_gst_medio", "image_source"...
    std::string name;                ///< nombre unico en la escena
    std::string settingsJson = "{}"; ///< ajustes de la fuente (los de su tipo)
    CanvasRect rect;                 ///< ubicacion (ignorado en fuentes solo-audio)
    bool visible = true;
    float volume = 1.0f; ///< ganancia lineal (1 = 0 dB)
    bool muted = false;
    /// Recorte en la escena: izquierda, arriba, derecha, abajo (fracciones de su tamaño).
    float recorte[4] = {0, 0, 0, 0};
    EfectosCapa efectos; ///< en la escena
};

/// Tipo de fuente disponible (para "Agregar fuente").
struct SourceType {
    std::string id;          ///< tipo ("ba_gst_fuente", "image_source"...)
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
    /// Segunda copia simultánea en otro disco ("" = sin copia). Mismos codificadores (el
    /// mismo encode; uno propio solo con «record.copiaCodificadorPropio»). Si el disco de la
    /// grabación falla, la parte que sigue va acá.
    std::string copiaDir;
    /// Segundos de grabación que se juntan en memoria si el disco se traba
    /// (Configuración → Grabación: «Colchón si el disco se traba»).
    int colchonSeg = 60;
};

/// Rutas del layout portable que el motor necesita (spec §5.1/§5.3).
struct EnginePaths {
    std::string configDir; ///< Config/
};

/// Estado observable del motor, para la UI y el bloque de salud.
struct EngineStatus {
    bool videoActive = false;
    bool audioActive = false;
    bool streaming = false;
    bool recording = false;
    /// La grabación de la por omisión está pedida pero cortada: el motor la rearma sola
    /// en otra parte (F1-05). Mientras tanto `recording` es false y NO hay que pararla.
    bool recordingDown = false;
    int droppedFrames = 0;
    int totalFrames = 0;
    double fps = 0.0;
    std::uint64_t bytesSent = 0;
    std::string videoEncoder; ///< el codificador en uso ("" si ninguno)
    std::string lastError;
    /// Se corto la transmision y se esta reintentando. Mientras esto es
    /// true, `streaming` puede seguir en true: la salida sigue activa, pero
    /// no esta llegando nada.
    bool reconnecting = false;
    /// En castellano y con la CAUSA: "Se cortó la conexión con el servidor",
    /// "El servidor rechazó la clave"... Vacio si no hubo ningun problema.
    /// Es lo que el operador necesita para saber si esperar o hacer algo.
    std::string outputProblem;
    /// Cada salida (PLAN-SALIDAS), la por omisión primera. Los campos de arriba
    /// son los de la por omisión, como siempre.
    struct Salida {
        std::string id, nombre;
        bool porOmision = false;
        std::string escena;      ///< al aire en esa salida
        bool streaming = false;  ///< su transmisión principal
        bool reconnecting = false;
        bool recording = false;
        bool recordingDown = false; ///< pedida pero cortada: el motor la está rearmando (alarma record_down)
        int destinos = 0;        ///< transmisiones extra andando
        bool limpia = false, repeticion = false;
    };
    std::vector<Salida> salidas;
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
    /// Por qué no anda la escena interna de un compositor o una Mezcla de GStreamer ("" si anda),
    /// de la foto del motor (no espera su candado).
    std::string errorDeCompositor(const std::string& nombre) const;
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
    /// La foto de las salidas (transmisiones, grabación, limpia) que publica su hilo:
    /// no espera a nadie (la página Latencias del pipeline la lee seguido).
    nlohmann::json salidasAhora() const { return fotoSalidas(); }

    // --- Las salidas (ver LAS SALIDAS arriba) -------------------------------
    static constexpr const char* kSalidaPorOmision = "principal";
    /// Una salida nueva, andando: su programa, su preview y su mezcla en el motor
    /// (con los volúmenes y mutes que tiene ahora la por omisión, sin nada al aire),
    /// su loudness y su retorno al gateway si está publicado. false + lastError.
    bool crearSalida(const std::string& id, const std::string& nombre);
    /// Para todo lo que sale de ella (transmisiones, grabación, repetición, limpia),
    /// suelta su retorno y la saca del motor. La por omisión no se borra.
    bool borrarSalida(const std::string& id);
    /// Los ids, la por omisión primera y las demás en el orden en que se crearon.
    std::vector<std::string> salidaIds() const;
    bool existeSalida(const std::string& id) const;
    /// El id de la salida ("" = la por omisión), o "" si no existe.
    std::string idDeSalida(const std::string& salida) const;

    // --- Escenas ------------------------------------------------------------
    bool createScene(const std::string& name);
    /// Elimina una escena (no la actual si es la unica). Las salidas que la tenían
    /// al aire pasan a otra con un corte.
    bool removeScene(const std::string& name);
    bool renameScene(const std::string& from, const std::string& to);
    /// Nombres de las escenas en orden de creacion.
    std::vector<std::string> sceneNames() const;
    /// Pone `name` al aire en la salida con la transicion configurada (setTransition).
    bool setCurrentScene(const std::string& name, const std::string& salida = "");
    /// Escena al aire en la salida ("" si no hay o no existe la salida). Una copia: la
    /// referencia de antes se la podía cambiar el otro que tiene el turno.
    std::string currentScene(const std::string& salida = "") const;
    /// Tipo ("cut", "fade", "swipe", "slide", "fade_to_color", "wipe") y duracion.
    bool setTransition(const std::string& type, int durationMs);
    /// Pone `name` al aire con OTRA transición solo esta vez (`ms` < 0 = la global).
    bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida = "");
    /// T-bar: lleva la transición a mano hacia `name` (t de 0 a 1). Con `soltar`,
    /// termina (si pasó la mitad, queda al aire; si no, vuelve).
    bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida = "");
    /// En medio de una transición manual (y hacia qué escena).
    std::string manualHacia(const std::string& salida = "") const;
    /// La escena en el PREVIEW (el bus de armada) de la salida ("" = nada armado).
    /// Lo pide el conmutador (escenas::Conmutador): las vistas solo lo miran.
    bool armar(const std::string& name, const std::string& salida = "");

    // --- FTB y placa de seguridad (por encima de todo lo que sale) -------------
    /// Fundido a negro de la salida; con `bajarAudio`, el audio del programa baja con él.
    bool ftb(bool activar, int ms, bool bajarAudio, const std::string& salida = "");
    /// La placa ("Cuarto intermedio", "Volvemos enseguida"…): fondo, texto e imagen opcional.
    bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms, const std::string& salida = "");
    // --- DSK: capas globales encima del programa (1..4) ------------------------
    /// Arma el contenido de la capa `capa` (1..4): escenas o fuentes, por nombre,
    /// apiladas en ese orden. `recuadro` (opcional): {posicion, tamano, margen}.
    bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro,
                  const std::string& salida = "");
    /// Sube o baja la capa con un fundido de `ms`.
    bool dskMostrar(int capa, bool visible, int ms, const std::string& salida = "");
    /// [{capa, visible, opacidad}] para las 4.
    nlohmann::json dskEstado(const std::string& salida = "") const;
    /// Las fuentes que muestran las capas que están arriba (tally, interlock de PTZ).
    std::vector<std::string> fuentesDsk(const std::string& salida = "") const;
    /// Sale algo hacia afuera desde ALGUNA salida: alguna transmisión (la principal o
    /// una extra), la grabación o la señal limpia. Sin esto, lo que pasa en el programa
    /// no lo ve nadie: ni la PTZ ni las alertas ni los candados tratan nada como «al aire».
    /// `contarGrabacion` en false: solo lo que se transmite (el modo en vivo
    /// puede no contar la grabación, Config/modo-vivo.json).
    bool haySalida(bool contarGrabacion = true) const;
    /// Lo mismo, de una salida sola ("" = la por omisión).
    bool haySalidaEn(const std::string& salida, bool contarGrabacion = true) const;

    /// LA FUENTE «BANCA CON LA PALABRA» (tipo ba_banca, ajuste `ranura` 1, 2…): en la
    /// escena muestra la cámara de la banca de esa ranura (1 = quien tiene la
    /// palabra; en un debate, 1 y 2). La app dice qué hay en cada ranura; sin nada,
    /// la capa no se dibuja. Con `entera`, la capa toma la toma ENTERA de la cámara
    /// (antes del PTZ digital) y la recorta ella: una sola cámara muestra varias
    /// bancas a la vez sin moverse, y el plano general sigue igual.
    struct RanuraBanca {
        std::string entrada;          ///< la cámara ("" = nadie en esta ranura)
        bool entera = false;          ///< recortar la toma entera (posición digital)
        float recorte[4] = {0, 0, 0, 0}; ///< izquierda, arriba, derecha, abajo (fracciones)
    };
    static constexpr const char* kTipoBanca = "ba_banca";
    /// Cuántas ranuras hay: 1 (quien habla, o el primero de un debate) y 2 (el segundo; el
    /// debate es de a dos). Si el debate admite más, se sube acá y las Propiedades lo siguen.
    static constexpr int kRanurasBanca = 2;
    /// Cambia lo que muestra cada ranura y vuelve a publicar las escenas que las usan.
    /// `forzar`: aunque no hayan cambiado (el tamaño de la cámara llegó después y el
    /// recorte, que va en píxeles, recién ahora se puede calcular).
    void setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar = false);
    /// Alguna escena tiene una fuente «Banca con la palabra» (así se sabe si dar la
    /// palabra tiene que mover el recorte o lo hace la fuente).
    bool hayFuenteBanca() const;
    /// La primera escena que muestra la ranura `ranura` ("" si ninguna).
    std::string escenaDeRanura(int ranura) const;
    /// Las fuentes que SE VEN en el programa de una salida: las visibles de su escena
    /// al aire y lo que muestran sus capas DSK que están arriba.
    std::vector<std::string> fuentesEnPrograma(const std::string& salida) const;
    /// Las que se ven en el programa de ALGUNA salida (el tally: al aire en alguna).
    std::vector<std::string> fuentesEnPrograma() const;
    /// `fuente` se ve en el programa de alguna salida y esa salida sale (haySalidaEn):
    /// lo que le pase se ve en una transmisión o queda en una grabación.
    bool fuenteAlAire(const std::string& fuente) const;
    // --- Video en vivo dentro de un gráfico (graficos/VideoEnGrafico.hpp) --------------
    /// Las capas de video que acompañan al gráfico de la fuente `grafico` (ba_grafico):
    /// en cada escena donde está ese gráfico, cada fuente nombrada se anima con sus claves
    /// (si ya está en la escena, esa; si no, se suma mientras dure), arriba o debajo del
    /// gráfico. Quedan quietas en su primera clave hasta empezarVideoDeGrafico. No se
    /// guardan con la escena. `capas`: VideoEnGrafico::aJson. "" o el error.
    std::string ponerVideoDeGrafico(const std::string& grafico, const nlohmann::json& capas);
    /// El ms 0 de sus claves (g_get_monotonic_time en ns): el primer cuadro del gráfico.
    void empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs);
    /// Las saca: cada fuente vuelve a como estaba (o se va, si se había sumado).
    void quitarVideoDeGrafico(const std::string& grafico);
    /// Un rótulo de texto con nombre (fuera de las escenas, para una capa DSK).
    bool textoGlobal(const std::string& nombre, const std::string& texto);
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    nlohmann::json estadoPlaca(const std::string& salida = "") const;
    /// DESDE CUALQUIER HILO (el vigía de las alarmas en el plano de control, F3-03): el
    /// pico del programa de la por omisión (dBFS, los dos canales) desde la última vez
    /// que se pidió, sin tocar el del vúmetro (programPeaksDb).
    float picoProgramaVigiaDb();
    /// DESDE CUALQUIER HILO: la foto de lo que sale que publica el hilo de salidas
    /// ({principal, grabacion, salidas: {id: {principal, grabacion, …}}}; ver status()).
    nlohmann::json fotoSalidas() const;
    /// DESDE CUALQUIER HILO: el loudness del programa de la por omisión (tiene su candado).
    const audio::LoudnessPrograma& loudnessPrograma() const { return medidasPrograma_->loudness; }

    /// Subtítulos CEA-608 en la transmisión (todavía no: M7). Devuelve a cuántas salidas se mandó.
    int enviarCaption(const std::string& texto, double segundos);

    /// El loudness de lo que sale por la salida: M/S/I, true peak, LRA (EBU R128).
    /// El de la por omisión vive lo que el BroadcastEngine (se puede guardar la referencia).
    audio::LoudnessPrograma& loudness(const std::string& salida = "");
    /// El mismo, o nullptr si no hay una salida con ese id ("" = la por omisión).
    audio::LoudnessPrograma* loudnessDe(const std::string& salida);
    /// Lleva el objetivo de loudness de cada salida (el suyo: Config/loudness.json; una
    /// nueva arranca con el de la por omisión) al motor: el AGC lento si está pedido y el
    /// techo del limitador (el del objetivo; −2 dBTP si esa salida transmite AAC por
    /// debajo de 128 kbps, AES TD1004). Al cambiar un objetivo, al crear una salida y al
    /// empezar a transmitir.
    void aplicarLoudness();
    /// El programa de la salida en imagen, para las alarmas: brillo medio (0–255) y cuántos
    /// segundos lleva sin cambiar. `valido` false si todavía no llegó ningún cuadro (o no
    /// hay esa salida). DESDE CUALQUIER HILO.
    struct ProgramaVideo {
        double luma = 0, quietoSeg = 0;
        bool valido = false;
    };
    ProgramaVideo programaVideo(const std::string& salida = "") const;
    /// Cuántos cuadros salieron del programa (el latido del aire: si no crece, algo se trabó).
    std::uint64_t cuadrosPrograma() const { return medidasPrograma_->cuadros.load(); }
    /// El tope de la latencia del estudio (ms; -1 = automático).
    void ponerTopeEstudio(long long ms);
    /// La política térmica (F4-05): dormir las vistas del motor que nadie mira (las vistas
    /// previas, las capturas que quedaron vivas) hasta que alguien las lea. Vale también
    /// para el motor que se rearme.
    void soltarVistasSinMirar(bool on);
    /// {latencia_ms, tope_ms, automatico, tarde: [entradas que pasan el tope]}.
    nlohmann::json latenciaEstudio() const;
    /// Bajar la latencia del estudio ahora a lo que piden las entradas sincronizadas.
    bool bajarLatenciaEstudio();

    /// El archivo que se está grabando (o el último) de la salida.
    std::string archivoGrabacion(const std::string& salida = "") const;
    /// La copia en el otro disco (vacío si no hay).
    std::string archivoCopiaGrabacion(const std::string& salida = "") const;
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
    bool sourceExists(const std::string& name) const {
        const TurnoMotor::Guarda turno(turno_);
        return sources_.count(name) > 0;
    }
    /// Lo que mide el motor de la entrada (tamaño, latencia, filtros, error...); null si no existe.
    nlohmann::json estadoEntrada(const std::string& name) const;
    /// Lo visible de una escena: (fuente, rectángulo en fracciones del lienzo), de abajo hacia arriba.
    std::vector<std::pair<std::string, CanvasRect>> itemsVisibles(const std::string& scene) const;
    /// El reloj del estudio: sistema, ntp, ptp o red. Si cambia con el motor andando, se rearma.
    bool ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio);
    /// El reloj pedido y, si no arrancó (PTP sin permisos, NTP que no sincroniza), por qué
    /// (la tarjeta de preparación, F4-07).
    std::pair<std::string, std::string> relojEstado() const;
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
    std::vector<std::string> globalAudioInputs() const {
        const TurnoMotor::Guarda turno(turno_);
        return micInputs_;
    }
    bool addGuestVideo(int slot);
    bool addGuestVideo(int slot, const CanvasRect& rect);

    // --- Audio por fuente (mezclador) ------------------------------------------
    /// Fuentes con audio (escenas + globales), en orden.
    std::vector<std::string> audioSources() const;
    /// Todas las fuentes que maneja el motor, sin repetir.
    std::vector<std::string> sourceNames() const;
    /// El volumen (el fader) de la fuente en la mezcla de una salida. El de la por
    /// omisión es el de siempre (se guarda con la fuente); el de las demás arranca
    /// como el de la por omisión al crearlas y después es suyo.
    bool setSourceVolume(const std::string& name, float mul, const std::string& salida = "");
    /// El volumen de la fuente (su fader), SIN la ganancia de su grupo.
    float sourceVolume(const std::string& name, const std::string& salida = "") const;
    /// El grupo de audio de una fuente: su nombre sin los números y con los espacios en orden
    /// («Invitado 3 (audio)» -> «Invitado (audio)»). El mismo que agrupa el mezclador.
    static std::string grupoDeAudio(const std::string& nombre);
    /// El volumen general de un grupo (un VCA): multiplica el volumen de cada fuente del grupo
    /// sin tocar su fader, en el mismo control de nivel (no agrega nada al camino del audio).
    /// En 1 (0 dB), como si no estuviera. Una fuente que se suma después al grupo ya lo trae.
    /// Es de la mezcla de cada salida ("" = la por omisión; una nueva arranca sin ninguno).
    void setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida = "");
    float gananciaGrupo(const std::string& grupo, const std::string& salida = "") const;
    bool setSourceMuted(const std::string& name, bool muted, const std::string& salida = "");
    bool sourceMuted(const std::string& name, const std::string& salida = "") const;
    /// Pico mas reciente de la fuente en dBFS (-100 = silencio): lo que entrega la
    /// entrada, vaya o no al Programa (el mismo de los vúmetros del Mezclador).
    float sourcePeakDb(const std::string& name) const;
    /// El pico de lo que ENTREGA la entrada (después de sus filtros y su rack, antes de su
    /// volumen), vaya o no a alguna salida: el vúmetro de su fila en la matriz.
    float sourceOutputPeakDb(const std::string& name) const;
    /// Los auriculares del operador tienen dos cosas distintas:
    /// - la ESCUCHA FIJA: lo que la matriz manda ahí a propósito (la voz del
    ///   asistente, la del teleprompter). Varias juntas; se guarda (audio-matriz.json).
    ///   Suena con el fader y el mute de la salida de la escucha (setSalidaEscucha).
    /// - el SOLO: escuchar UN canal un rato, solo él (la escucha del motor en «pfl» o
    ///   «afl», F2-03). PFL: lo que entrega la entrada, antes del fader y del bus; AFL: con
    ///   el fader y el mute de una salida (la que muestra el Mezclador). Uno solo a la vez;
    ///   mientras dura, la escucha fija espera sin tocarse (vuelve al soltarlo) y no se guarda.
    bool setSourceMonitoring(const std::string& name, bool on);
    /// Si suena en los auriculares (escucha fija o solo).
    bool sourceMonitoring(const std::string& name) const;
    /// Solo la escucha fija (lo que muestra y guarda la matriz).
    bool sourceEscuchaFija(const std::string& name) const;
    /// El solo: `modo` "pfl" o "afl" (`salida`: de cuál es el fader del AFL; "" = la por
    /// omisión). `name` "" lo suelta. Falla si la fuente o la salida no existen.
    bool setPfl(const std::string& name, const std::string& modo = "pfl", const std::string& salida = "");
    std::string pfl() const {
        const TurnoMotor::Guarda turno(turno_);
        return pfl_;
    }
    /// "pfl" o "afl" (lo del solo prendido; "" sin solo).
    std::string pflModo() const {
        const TurnoMotor::Guarda turno(turno_);
        return pfl_.empty() ? std::string() : pflModo_;
    }
    /// La salida de la escucha fija: de cuál son su fader y su mute (su id; "" al pedirla =
    /// la por omisión). No se guarda. Falla si no existe.
    bool setSalidaEscucha(const std::string& salida);
    std::string salidaEscucha() const {
        const TurnoMotor::Guarda turno(turno_);
        return salidaEscucha_;
    }
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
    /// Nombre de la placa de video (para el diagnóstico).
    /// Con qué dibuja (no depende del estado del motor: desde cualquier hilo).
    static std::string graphicsDeviceName();
    struct EncoderInfo {
        std::string id, name, codec;
        bool hardware = false;
    };
    /// Codificadores de video disponibles (los de la placa marcados). De las fábricas de
    /// GStreamer, no del estado del motor: desde cualquier hilo.
    static std::vector<EncoderInfo> videoEncoders();

    // --- Cambios en caliente -------------------------------------------------
    /// true si hay grabacion o transmision en curso (en alguna salida).
    bool outputsActive() const;
    /// Cambia resolucion/fps. Solo sin salidas activas: rearma el motor con todo lo que había.
    /// Con `conElMotorApagado` (F3-09, la GPU perdida) también con salidas: las para, corre
    /// eso con el motor viejo ya apagado (rearmar la GPU) y arma el nuevo con el lienzo que
    /// devuelve; las salidas las retoma quien llama.
    bool resetVideo(const VideoConfig& video, const std::function<VideoConfig()>& conElMotorApagado = {});
    /// Compatibilidad: los codificadores se crean por salida. Solo sin salidas activas.
    bool resetEncoders();
    /// Una copia (ver currentScene).
    VideoConfig videoConfig() const {
        const TurnoMotor::Guarda turno(turno_);
        return video_;
    }
    /// Codificadores de video en el orden en que se probarían para `enc` (el último, x264enc).
    std::vector<std::string> videoEncoderCandidates(const EncoderConfig& enc) const;

    /// Publica el programa de cada salida hacia el Proceso B (la por omisión en
    /// BA.Program, las demás en BA.Salida.<id>; el audio lo escribe el motor: ver
    /// LAS SALIDAS arriba). Una salida que se crea después se publica sola.
    bool publishReturnFeeds();
    void stopReturnFeeds();
    /// El tamaño más grande con que el gateway le manda el programa a una sesión
    /// (Configuración → Sala, gateway.programaW/H: 1280×720 de fábrica). El retorno se
    /// publica a ese tamaño (el de la salida si es más chica), escalado y en BGRA en la
    /// GPU (F2-13): bajarlo a 1920×1080 para que el gateway lo achicara en la CPU era
    /// copiar y recorrer 2,25 veces lo que se manda. Antes de publishReturnFeeds; si
    /// cambia andando, el canal se rehace (el gateway se vuelve a suscribir solo).
    void ponerTopeRetorno(int ancho, int alto);
    /// ¿Alguien mira el retorno de la salida (invitados en la sala, integrantes
    /// remotos, quien mire BA.Salida.<id> en el gateway)? Sin nadie, la vista que lo
    /// baja en cada cuadro se suelta (el canal queda publicado); con alguien, vuelve.
    /// La por omisión arranca en uso; las demás, sin nadie.
    void retornoEnUso(bool si, const std::string& salida = "");

    /// Una escena que no es el programa, para las pantallas de la sala: la pone
    /// en el canal `k` (BA.Escena.<k>, 0..ipc::kCanalesEscena-1) y la manda al
    /// Proceso B mientras esté ahí; vacía, lo suelta. La escena se compone aparte
    /// y sigue andando aunque no esté al aire. Dos pantallas con la misma escena
    /// comparten canal: eso lo decide quien reparte los canales (Pantallas).
    /// Lo que se le encargó al motor y falló después: las órdenes que pueden
    /// tardar van en cola y vuelven enseguida (la interfaz no espera), así que
    /// su error llega por acá, desde el hilo del motor.
    void alFallarMotor(std::function<void(const std::string&)> fn);
    /// El texto que llega por alFallarMotor es el de «el disco anda bien de nuevo» (un
    /// aviso bueno, no un fallo). Comparar con esto y no con el texto: sale traducido.
    static bool esAvisoDeDiscoBien(const std::string& texto);
    /// Avisa cuando cambia el audio de una fuente: `que` es "escucha" (fija o PFL),
    /// "pistas", "retardo", "volumen", "mute" o "alta" (se creó, se rehízo o se renombró). Lo pida quien lo pida (la matriz, el
    /// Mezclador, el asistente, el AFV, una escena que se carga, volver como estaba):
    /// quien lo muestra se entera siempre. Se llama en el hilo que hizo el cambio.
    void alCambiarAudioFuente(std::function<void(const std::string& fuente, const char* que)> fn);
    bool escenaACanal(int k, const std::string& escena);
    std::string escenaDeCanal(int k) const;
    /// Los que no son Pantallas (las vistas de la sesión remota) toman un canal con
    /// dueño: el que ya tiene esa escena o uno libre (-1 si no hay). Pantallas no
    /// suelta un canal con dueños; el último dueño que lo suelta, lo apaga.
    int tomarCanalEscena(const std::string& escena, const std::string& dueno);
    /// Suelta los canales de `dueno` salvo los de `salvo` (los que acaba de volver a tomar:
    /// así un reparto nuevo no apaga y prende lo que sigue igual).
    void soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo = {});
    bool canalEscenaConDuenos(int k) const;

    /// Transmite. Si YA está transmitiendo: al mismo destino no hace nada
    /// (ultimoInicio = YaIgual); a OTRO destino falla (YaOtroDestino), salvo
    /// `cambiarEnCaliente`, que corta y arranca al nuevo.
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente = false,
                        const std::string& salida = "");
    enum class Inicio { Iniciado, YaIgual, YaOtroDestino, Error };
    /// Lo que hizo el último startStreaming DE ESTE HILO (como lastError).
    Inicio ultimoInicio() const;
    /// El destino al aire, enmascarado (servidor y los últimos 4 de la clave).
    std::string destinoActivo(const std::string& salida = "") const;

    // --- Destinos adicionales (respaldo, otras plataformas) --------------------
    /// Un destino extra (su id es único entre todas las salidas) con el programa de `salida`.
    /// `serverRespaldo`: el punto de ingest de respaldo (misma clave); sale a los dos a la vez.
    bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                    const std::string& salida = "", const std::string& serverRespaldo = "");
    void stopExtra(const std::string& id); ///< esté en la salida que esté
    void stopExtras();                     ///< los de todas las salidas
    /// {id: {activo, reconectando, perdidos, cuadros, bytes, destino, salida}}, de todas las salidas.
    nlohmann::json extrasEstado() const;
    /// {principal: {...}, extras: {id: {...}}, saltados_encoder, lag_render}.
    nlohmann::json saludSalidas() const;
    /// Detiene la transmision (no toca la grabacion).
    void stopStreaming(const std::string& salida = "");

    /// Arranca la grabacion del programa (MKV en tramos; MP4/MOV se pasan al terminar).
    bool startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida = "");
    void stopRecording(const std::string& salida = "");
    /// {activa, archivo, copia, copia_activa, calidad, encoder, pistas, tramos}.
    nlohmann::json grabacionEstado(const std::string& salida = "") const;

    // --- Repetición ---------------------------------------------------------------
    /// Guarda los últimos `segundos` del programa (0 = apagar). `dir`: donde quedan los clips.
    bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida = "");
    /// Guarda el clip; llega por `ultimoReplay()` cuando termina de escribir.
    bool guardarReplay(const std::string& salida = "");
    std::string ultimoReplay(const std::string& salida = "") const;
    nlohmann::json replayEstado(const std::string& salida = "") const;

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
    /// {apuros, atraso_s, registro_trabado_ms, prioridad, automatica, modo, pausadas[], desde_s,
    ///  restante: {horas, libre_gb, kbps, volumenes[{volumen, libre_gb, kbps, horas, que[]}]} (lo que
    ///  escribe AHORA en todas las salidas: grabación, copia, señal limpia e ISO, sumado por disco),
    ///  mismo_volumen: [{salida, grabacion, copia}] (copias que van al mismo disco que su grabación)}
    nlohmann::json discoEstado() const;
    /// Lo que escribe en disco la grabación de una salida (video + audio de cada pista):
    /// lo usa al arrancarla y quien calcula de antemano para cuánto alcanza el disco.
    /// `conPistas` false: solo la del programa (el emisor aparte todavía no graba las 2-6).
    static int kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool conPistas = true);
    /// Para probar (modo --test): la grabación del programa, como con el disco trabado `ms`.
    void simularDiscoTrabado(int ms);

    /// Alguna salida perdió un codificador en el aire y sigue con otro (la lista negra
    /// del motor; las salidas se rearman solas en el hilo de salidas).
    bool encoderRespaldoActivo() const;

    // --- Medir la sincronía labios/voz con un aplauso -----------------------------
    /// En el programa de `salida` ("" = la por omisión): una cámara contra su micrófono.
    bool empezarMedicionSincro(const std::string& salida = "");
    /// {ok, retardo_ms, dispersion_ms, aplausos, confiable, problema, golpes_audio, golpes_video}.
    nlohmann::json terminarMedicionSincro();
    bool midiendoSincro() const {
        const TurnoMotor::Guarda turno(turno_);
        return medSincro_ != nullptr;
    }
    /// Las pistas (máscara: bit 1..5 = pistas 2..6) que usa la grabación del programa.
    int pistasGrabacionMask() const;

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
                        const EncoderConfig& enc, const std::string& salida = "");
    void stopCleanFeed(const std::string& salida = "");
    nlohmann::json cleanFeedEstado(const std::string& salida = "") const;
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
    /// Lo mismo para un bus de una salida (PLAN-SALIDAS, etapa 5): su programa (`bus`
    /// "programa") o su preview ("preview"), tal como salen, en el canal `canal` (se
    /// lee con renderScenePreview y se suelta con disableScenePreview, como las demás).
    bool enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida, int maxWidth);
    void disableScenePreview(const std::string& canal);
    /// Diagnóstico de parpadeos (BA_MEDIR_PREVIEW=1): ver medirVista en el .cpp.
    void medirVista(const std::string& clave);
    void disableScenePreview() { disableScenePreview("editor"); }
    /// El último cuadro de ese canal (BGRA) si es más nuevo que `serial`: comparte los
    /// bytes con la vista del motor (motor::Pixeles), no los copia (F3-11). DESDE CUALQUIER
    /// HILO (la interfaz los lee a 60 Hz sin el turno: F3-03); pedir y soltar una vista, no.
    bool renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial);
    /// DESDE CUALQUIER HILO.
    std::string scenePreviewName(const std::string& canal) const;
    /// El ultimo frame de vista previa (BGRA) si es mas nuevo que `serial`, sin copiarlo
    /// (como renderScenePreview). DESDE CUALQUIER HILO.
    bool latestPreview(motor::Cuadro& out, std::uint64_t& serial) const;
    /// Picos del programa desde la ultima llamada, en dBFS (L, R). -100 = silencio.
    void programPeaksDb(float& left, float& right, const std::string& salida = "");

    /// Estado actual (para la UI y el HealthBlock).
    EngineStatus status() const;
    /// El último error DE ESTE HILO (ver «Estrategia de errores»).
    const std::string& lastError() const;
    /// DESDE CUALQUIER HILO.
    bool ready() const { return started_; }
    /// El arranque falló porque este programa no tiene GStreamer (build sin motor, bundle
    /// incompleto). Una marca y no el texto de lastError(), que sale traducido.
    bool sinGStreamer() const { return sinGStreamer_; }

    /// EL TURNO (emision/TurnoMotor.hpp): lo toma cada método de la API; quien hace varias
    /// cosas seguidas que tienen que verse juntas (AppRuntime, los conmutadores) lo toma por
    /// todo lo que dura, y el plano de control lo pide con tope (TurnoMotor::tomarHasta).
    const TurnoMotor& turno() const { return turno_; }

    /// Lo que oye el invitado del slot: "mix_minus" (de fábrica) o "programa".
    void setRetornoInvitado(int slot, const std::string& modo);
    std::string retornoInvitado(int slot) const;

   private:
    struct Salida; // ver abajo (LAS SALIDAS)
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
        /// «Banca con la palabra» (ba_banca): no es una entrada; al publicar la escena
        /// muestra la ranura que dice (ver setRanurasBanca).
        bool banca = false;
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
    /// Gráfico (nombre de su fuente) → {capas (VideoEnGrafico::aJson), desde_ns}.
    std::map<std::string, nlohmann::json> videoDeGraficos_;
    /// Suma (o anima) en las capas de una escena el video de sus gráficos.
    void sumarVideoDeGraficos(const EscenaApp& e, nlohmann::json& capas) const;
    /// Vuelve a publicar las escenas que tienen esa fuente.
    void publicarEscenasCon(const std::string& fuente);
    /// entrada -> {compositor, productor} (ver setTraer).
    std::map<std::string, std::pair<std::string, std::string>> traer_;
    std::vector<RanuraBanca> ranurasBanca_; ///< ver setRanurasBanca
    /// La ranura de una fuente ba_banca (1 si no dice).
    int ranuraDe(const Fuente& f) const;
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
    /// Vuelve a mandarle al motor todas las capas de la salida (DSK, placa, negro).
    void publicarCapas(Salida& s, int ms);
    std::string transicionMotor(const std::string& tipo) const;
    void setError(std::string msg);
    /// El estado del motor, cacheado (las consultas de la UI llegan muy seguido).
    const nlohmann::json& estadoMotor() const;

    TurnoMotor turno_;
    std::atomic<bool> started_{false};
    std::atomic<bool> sinGStreamer_{false};
    /// El último error de cualquier hilo (el de status(); el de lastError() es de cada uno).
    mutable std::mutex errorM_;
    std::string ultimoError_;
    EnginePaths paths_;
    VideoConfig video_;
    std::string relojTipo_ = "sistema", relojServidor_;
    int relojPuerto_ = 0, relojDominio_ = 0, relojPublicar_ = 0;
    AudioConfig audio_;
    std::unique_ptr<motor::Motor> motor_;

    std::vector<EscenaApp> scenes_;
    std::map<std::string, Fuente> sources_;
    std::vector<std::string> ordenFuentes_;  ///< en orden de creación
    std::string transitionType_ = "fade";
    int transitionMs_ = 300;
    std::vector<std::string> micInputs_;     ///< entradas de audio del equipo
    std::map<int, std::string> retornos_;    ///< slot → modo
    struct RecortePendiente {
        std::string escena, fuente;
    };
    std::vector<RecortePendiente> recortesPendientes_;

    // Capas de cada salida: DSK 1..4, la placa y el negro.
    struct Dsk {
        std::vector<std::string> contenido;
        nlohmann::json recuadro = nlohmann::json::object();
        bool visible = false;
    };

    // Lo que sale: las transmisiones, grabaciones, repetición y señal limpia de cada
    // salida, y las grabaciones por entrada (sourceRecs_), son SOLO del hilo de
    // salidas (ver enSalidas): arrancarlas, pararlas y atenderlas son cambios de
    // estado de GStreamer que tardan (parar una grabación espera el cierre del
    // archivo; una transmisión caída se rearma probando codificadores). La interfaz
    // encola y lee la foto.
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
    std::atomic<bool> alAire_{false}; ///< transmitiendo o grabando (lo anota el hilo de salidas)
    mutable std::mutex fotoSalidasM_;
    nlohmann::json fotoSalidas_ = nlohmann::json::object();
    void avisarFallo(const std::string& texto);
    // Lo pedido (lo escribe la interfaz al pedir; el hilo de salidas lo baja si
    // no arrancó): para contestar «ya está» o «ya hay tantas» sin esperar. Lo de
    // cada salida está en su Salida (los campos «pedido»), también bajo pedidosM_.
    mutable std::mutex pedidosM_;
    std::set<std::string> individualesPedidas_;
    struct Extra {
        std::string server, key, serverRespaldo;
        std::unique_ptr<motor::Transmision> tx;
    };
    struct EstadoRemux;
    std::shared_ptr<EstadoRemux> remux_; ///< bajo remuxM_ (lo lanza el hilo de salidas al parar)
    mutable std::mutex remuxM_;
    struct SourceRec {
        std::string path;
        int kbps = 0; ///< lo que escribe a disco (video + audio), para las horas restantes
        bool audioPropio = false;
        std::unique_ptr<motor::Grabacion> grab;
        std::unique_ptr<motor::Transmision> tx;
        std::string destino;
    };
    std::map<std::string, SourceRec> sourceRecs_;
    // Repetición: un anillo de tramos chicos en una carpeta; guardar junta los últimos.
    struct Repeticion;
    /// Lo que se mide del audio de una salida (llega del hilo de datos del motor): aparte,
    /// compartido con quien lo escucha, para que una salida borrada no lo deje colgado.
    struct Medidas {
        audio::LoudnessPrograma loudness{48000};
        std::atomic<float> picoL{0.0f}, picoR{0.0f};
        /// El mismo pico para el vigía de las alarmas (picoProgramaVigiaDb): cada lector
        /// pone el suyo en cero, así ninguno le roba el pico al otro.
        std::atomic<float> picoVigia{0.0f};
        // Lo que llega de su programa en video (claveAlarmas): brillo medio, la firma del
        // último cuadro, desde cuándo no cambia y cuántos salieron.
        std::atomic<double> luma{0};
        std::atomic<std::uint64_t> firma{0};
        std::atomic<std::int64_t> cambioMs{0};
        std::atomic<std::uint64_t> cuadros{0};
    };
    /// Las medidas de la por omisión: existe desde el constructor y no se borra nunca, así
    /// que se pueden leer desde cualquier hilo sin buscar en salidas_.
    std::shared_ptr<Medidas> medidasPrograma_;
    struct CanalEmisor; ///< el canal de video de una salida a su emisor (BroadcastEngineEmisor.cpp)
    /// Una salida (ver LAS SALIDAS arriba). Cada campo es de uno: del que tiene el turno
    /// (lo del aire: la interfaz o el plano de control), del hilo de salidas (lo que sale)
    /// o, bajo pedidosM_, de los dos.
    struct Salida {
        std::string id, nombre;
        // --- Del que tiene el turno ---
        std::string alAire; ///< la escena al aire
        std::string armada; ///< la escena en su PREVIEW (ver armar)
        Dsk dsk[4];
        bool ftb = false, ftbAudio = false, placa = false;
        std::string placaTexto, placaImagen;
        /// El volumen y el mute de cada fuente en su mezcla (la por omisión usa los de
        /// la fuente, Fuente::spec: estos quedan vacíos).
        std::map<std::string, float> volumen;
        std::map<std::string, bool> mute;
        std::map<std::string, float> gananciaGrupo; ///< los VCA de su mezcla: grupo -> ganancia (solo los que no son 1)
        RecordConfig lastRec;
        bool lastRecValido = false;
        int pistasGrabacionMask = 0;
        std::shared_ptr<Medidas> medidas = std::make_shared<Medidas>();
        int oyenteAudio = 0;
        // --- Bajo pedidosM_ ---
        bool streamPedido = false, grabacionPedida = false, limpioPedido = false;
        std::set<std::string> extrasPedidos;
        std::string activeServer, activeKey;
        // --- Del hilo de salidas ---
        std::unique_ptr<motor::Transmision> stream;
        std::map<std::string, Extra> extras;
        std::unique_ptr<motor::Grabacion> rec, recCopia;
        std::string recordPath, recordCopyPath;
        std::string fuenteVideo; ///< el productor del video de su programa: la lista negra de codificadores es suya
        int kbpsRec = 0; ///< lo que escriben la grabación y (otro tanto) la copia (kbpsGrabacion)
        /// El AAC más chico con que se transmitió por acá (0: ninguno): debajo de 128 kbps el
        /// techo baja a −2 dBTP (aplicarLoudness). Lo escribe el hilo de salidas.
        std::atomic<int> kbpsAac{0};
        std::string remuxSal; ///< a qué pasar la grabación al pararla
        bool remuxAlTerminarSal = true;
        std::unique_ptr<SourceRec> clean;
        std::unique_ptr<Repeticion> replay;
        // --- Al gateway (lo publica la interfaz; manda el hilo del motor, bajo retornoM) ---
        std::mutex retornoM;
        ba::ipc::SpoutSender retorno;
        int retornoAncho = 0, retornoAlto = 0; ///< el tamaño con que se publicó
        std::uint64_t retornoCuadros = 0;
        std::vector<std::uint8_t> retornoFilas;
        bool retornoPedido = false; ///< del hilo de la interfaz: su vista «…@gateway» está pedida
        bool retornoEnUso = false;  ///< del hilo de la interfaz: alguien lo mira (ver retornoEnUso)
        // --- El emisor aparte (F3-01; BroadcastEngineEmisor.cpp) ---
        bool emisor = false; ///< la codifica un ba-emisor (BA_EMISOR): fijo desde que se crea
        // Del hilo de salidas: lo pedido (copia de Runtime/emisor/<id>.json) y lo último
        // que dijo su estado (con cuándo se leyó: se relee cada medio segundo).
        std::unique_ptr<emisor::Pedido> pedidoEmisor;
        nlohmann::json estadoEmisor;
        std::int64_t estadoEmisorMs = 0;
        /// Lo que su pedido ya tenía (destinos, señal limpia, pistas) está enganchado acá en
        /// esta corrida del motor (sus canales, sus puentes, lo pedido): ver leerEmisor.
        bool emisorEnganchado = false;
        /// Sus canales de video para el emisor (NV12): el programa y la señal limpia. Los
        /// usa el hilo del motor (los cuadros) con su propio candado.
        std::shared_ptr<CanalEmisor> canalEmisor, canalLimpioEmisor;

        Salida();
        ~Salida(); // en el .cpp: Repeticion se completa ahí
        bool porOmision() const { return id == kSalidaPorOmision; }
        /// La vista de su programa para el gateway y el nombre de su canal.
        std::string claveRetorno() const;
        std::wstring canalRetorno() const;
    };
    /// Todas, por id. Solo lo cambia el que tiene el turno (con salidasM_ tomado); el
    /// hilo de salidas lo recorre con salidasM_ (ver lasSalidas). La
    /// por omisión existe siempre (desde el constructor).
    std::map<std::string, std::shared_ptr<Salida>> salidas_;
    std::vector<std::string> ordenSalidas_; ///< la por omisión primera
    mutable std::mutex salidasM_;
    /// "" = la por omisión; nullptr (y lastError) si no hay una con ese id.
    Salida* laSalida(const std::string& id);
    const Salida* laSalida(const std::string& id) const;
    std::shared_ptr<Salida> laSalidaCompartida(const std::string& id);
    Salida& porOmision() { return *salidas_.at(kSalidaPorOmision); }
    const Salida& porOmision() const { return *salidas_.at(kSalidaPorOmision); }
    /// Una foto de todas (para el hilo de salidas), en orden.
    std::vector<std::shared_ptr<Salida>> lasSalidas() const;
    /// Empieza a medir su programa: el audio (loudness, picos) y el video (brillo y quietud,
    /// para las alarmas), y la sincronía si se mide en ella.
    void medirSalida(Salida& s);
    /// La vista chica de su programa para medirlo.
    static std::string claveAlarmas(const Salida& s);
    /// Una transmisión de `s` con AAC a `kbps`: si es la más chica y baja de 128, otro techo.
    void anotarAac(Salida& s, int kbps);
    void dejarDeMedir(Salida& s);
    /// Su canal al gateway publicado y (si alguien mira el retorno) su vista pedida.
    bool publicarRetorno(const std::shared_ptr<Salida>& s);
    bool pedirVistaRetorno(const std::shared_ptr<Salida>& s);
    void soltarVistaRetorno(Salida& s);
    /// Al borrarla: suelta su vista y su canal.
    void soltarRetorno(Salida& s);
    /// La foto de las salidas de una ("" = la por omisión): {principal, extras, grabacion, replay, limpio}.
    nlohmann::json fotoDeSalida(const std::string& salida) const;
    /// Lo que hay en el hilo de salidas de una salida, parado (al borrarla o al apagar).
    void pararTodoYa(Salida& s);
    /// {principal, extras, grabacion, replay, limpio} de una salida (en el hilo de salidas).
    nlohmann::json fotoDe(const Salida& s) const;
    // --- El emisor aparte (BroadcastEngineEmisor.cpp; del hilo de salidas salvo dicho) ---
    /// Las salidas que codifica un emisor aparte (BA_EMISOR, lo pone el launcher).
    static const std::set<std::string>& salidasConEmisor();
    /// Publica su canal de video para el emisor (o el de la señal limpia) en NV12 del
    /// tamaño de la salida y pide su vista. false + lastError. (Cualquier hilo.)
    bool publicarCanalEmisor(Salida& s, bool limpio);
    /// Suelta la vista del canal (el motor se apaga: el emisor pasa a la placa); con
    /// `cerrar`, también el canal.
    void soltarCanalEmisor(Salida& s, bool limpio, bool cerrar);
    /// Lo de cada salida con emisor, al apagar el motor (sus vistas sueltas, sin enganchar).
    void desengancharEmisores();
    /// El destino extra `id` como está en `desde` (nullptr: sacarlo) en el pedido. "" o el error.
    std::string extraAlEmisor(Salida& s, const std::string& id, const emisor::Pedido* desde);
    /// La repetición en el pedido (segundos 0: apagada) y guardar un clip (otro `guardarSeq`).
    std::string replayAlEmisor(Salida& s, int segundos, const std::string& dir, int kbps);
    std::string guardarReplayEnEmisor(Salida& s, const std::string& destino);
    /// La señal limpia en el pedido (nullptr: apagarla), con su canal publicado.
    std::string limpioAlEmisor(Salida& s, const emisor::PedidoLimpio* l);
    /// El pedido de la salida (lo carga del disco la primera vez), cambiado por `cambio`,
    /// con el cuadro de salida y la placa de ahora y el `seq` siguiente, escrito. "" o el error.
    std::string pedirAlEmisor(Salida& s, const std::function<void(emisor::Pedido&)>& cambio);
    /// Lo que hace el emisor ahora con la transmisión ("transmision") o la grabación
    /// ("grabacion"): si su estado (fresco o no) dice que la tiene andando, cayéndose o
    /// reconectando. Para adoptarla al volver en vez de pedirla de nuevo.
    bool emisorLaTiene(const Salida& s, const char* que) const;
    /// Relee su estado (cada medio segundo) y carga el pedido si todavía no está.
    void leerEmisor(Salida& s);
    /// Pone en la foto de la salida lo que hace el emisor: `principal` y `grabacion`.
    void fotoDelEmisor(const Salida& s, nlohmann::json& f) const;
    /// Algo de esa salida transmite o graba en el emisor.
    bool aireEnEmisor(const Salida& s) const;
    std::string startStreamingYa(Salida& s, const StreamConfig& stream, const EncoderConfig& enc);
    std::string startExtraYa(Salida& s, const std::string& id, const std::string& server, const std::string& key,
                             const EncoderConfig& enc, const std::string& serverRespaldo);
    std::string startRecordingYa(Salida& s, const RecordConfig& rec, const EncoderConfig& enc,
                                 const std::vector<int>& pistas, const std::string& nombre);
    void stopRecordingYa(Salida& s);
    std::string setReplayYa(Salida& s, int segundos, const std::string& dir, const EncoderConfig& enc);
    std::string startSourceRecordingYa(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    std::string startCleanFeedYa(Salida& s, const std::string& destino, const std::string& clave,
                                 const std::string& dir, const EncoderConfig& enc);
    // El disco en apuros (todo del hilo de salidas, menos el modo).
    void vigilarDisco();
    void aplicarPrioridad(bool si, bool automatica);
    std::atomic<int> modoDisco_{0}; ///< 0 preguntar, 1 auto, 2 nunca
    bool discoApuros_ = false, prioridad_ = false, prioridadAuto_ = false;
    std::int64_t apurosDesdeMs_ = 0, discoBienDesdeMs_ = 0;
    double atrasoDisco_ = 0;
    std::int64_t registroTrabadoMs_ = 0;
    std::vector<std::string> pausadas_;
    // Lo que queda de disco (discoEstado: restante, mismo_volumen): se recalcula cada
    // pocos segundos (stat y espacio libre de cada carpeta), no a cada vuelta del hilo.
    nlohmann::json discoRestante_ = nlohmann::json::object(), mismoVolumen_ = nlohmann::json::array();
    std::int64_t discoRestanteMs_ = 0;
    void medirDiscoRestante();

    // La sincronía labios/voz (una medición a la vez, en la salida que se pidió).
    struct MedicionSincro;
    std::shared_ptr<MedicionSincro> medSincro_;

    // Vistas para la interfaz
    /// Las vistas que lee la interfaz sin el turno (latestPreview, renderScenePreview,
    /// scenePreviewName): quien las cambia (con el turno) y quien cambia motor_ las toma.
    mutable std::mutex vistasMu_;
    bool previewActive_ = false;
    bool vistasSinMirar_ = false; ///< soltarVistasSinMirar
    std::uint64_t previewSerie_ = 0;
    struct VistaPrevia {
        std::string clave, name;
        std::uint64_t serie = 0;
    };
    std::map<std::string, VistaPrevia> scenePreviews_;

    // Al gateway (el programa de cada salida: ver Salida, «Al gateway»)
    bool returnFeedsActive_ = false;
    int topeRetornoAncho_ = 1280, topeRetornoAlto_ = 720; ///< ponerTopeRetorno
    /// El canal del retorno: la salida encajada en el tope, sin agrandar, con el período del lienzo.
    ba::ipc::SlotConfig canalRetorno() const;
    void enviarRetorno(Salida& s, const motor::Cuadro& c);
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
    std::function<void(const std::string&, const char*)> cambioAudio_; ///< alCambiarAudioFuente (se fija una vez, al armar)
    /// Lo que va al control de nivel del motor en la salida `s`: el fader de la fuente por la
    /// ganancia de su grupo en esa salida.
    float nivelAplicado(const std::string& name, float volumen, const Salida& s) const;
    std::string pfl_; ///< el canal en solo ("" = ninguno); no se guarda
    std::string pflModo_ = "pfl"; ///< el solo: "pfl" o "afl"
    std::string pflSalida_ = kSalidaPorOmision;     ///< la salida del fader del AFL
    std::string salidaEscucha_ = kSalidaPorOmision; ///< la salida del fader de la escucha fija
    /// Lo que pide la app a la escucha del motor: el solo, o la escucha fija con su salida.
    std::string aplicarEscucha();
    void enviarEscena(int k, const motor::Cuadro& c);

    mutable std::shared_ptr<const nlohmann::json> estadoFoto_;
};

} // namespace ba::app
