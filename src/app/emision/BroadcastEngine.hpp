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
// Es una FACHADA (2026-11-01): la API de siempre, que toma el turno y delega en piezas con
// su estado y su dueño de hilo (en su cabecera; el cuadro, en
// docs/arquitectura/05-motor-del-plano.md): EscenasYFuentes (con CapasPrograma.cpp),
// MezclaApp, MediosApp, VistasYMedicion, RetornoGateway, EmisionSalidas (el hilo de
// salidas), VigiaDisco y EmisorAparte. Cada salida tiene dos mitades (emision/SalidaMotor.hpp):
// SalidaAire, del dueño, y SalidaEmision, del hilo de salidas, que se hablan por mensajes.
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

#include <utility>
#include <vector>

#include "ipc/CanalesEscena.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "emision/ApiMotor.hpp"
#include "emision/ConfigAire.hpp"
#include "emision/MotorFwd.hpp"
#include "emision/NivelesMotor.hpp"
#include "emision/TurnoMotor.hpp"
#include "escenas/RefEscena.hpp"

namespace ba::motor {
class Motor;
struct Cuadro;
class Grabacion;
class Transmision;
class Repeticion;
} // namespace ba::motor

namespace ba::emisor {
struct Pedido;
struct PedidoLimpio;
} // namespace ba::emisor

namespace ba::app {

struct SalidaAire;
struct MedidasSalida;
class EscenasYFuentes;
class MezclaApp;
class MediosApp;
class VistasYMedicion;
class RetornoGateway;
class EmisionSalidas;
class VigiaDisco;
class EmisorAparte;

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
///
/// La API que usa la app (la interfaz, el MCP, las áreas) está declarada y comentada en
/// emision/ApiMotor.hpp: acá cada uno de esos métodos va con `override`, con la misma firma
/// y los mismos argumentos por omisión. Lo que queda declarado solo acá (el ciclo de vida,
/// lo que usa el dueño por su puntero y las piezas de emision/) no lo ve quien llama por
/// ApiMotor (AppRuntime::engine()).
class BroadcastEngine final : public ApiMotor {
   public:
    std::string errorDeCompositor(const std::string& nombre) const override;
    BroadcastEngine();
    ~BroadcastEngine();

    BroadcastEngine(const BroadcastEngine&) = delete;
    BroadcastEngine& operator=(const BroadcastEngine&) = delete;

    /// Arranca el motor (GStreamer, el reloj del estudio, el PROGRAMA y sus buses).
    bool startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio);
    /// Detiene salidas y el motor. Idempotente.
    void shutdown();
    motor::Motor* motor() override { return motor_.get(); }
    nlohmann::json salidasAhora() const override { return fotoSalidas(); }
    // --- Las salidas (ver LAS SALIDAS arriba) -------------------------------
    bool crearSalida(const std::string& id, const std::string& nombre) override;
    bool borrarSalida(const std::string& id) override;
    std::vector<std::string> salidaIds() const override;
    bool existeSalida(const std::string& id) const override;
    std::string idDeSalida(const std::string& salida) const override;
    // --- Escenas ------------------------------------------------------------
    bool createScene(const std::string& name, const std::string& id = {}) override;
    bool removeScene(const std::string& name) override;
    bool renameScene(const std::string& from, const std::string& to) override;
    std::vector<std::string> sceneNames() const override;
    std::string sceneId(const std::string& escena) const override;
    std::string sceneName(const std::string& escena) const override;
    escenas::TablaEscenas tablaEscenas() const override;
    bool setCurrentScene(const std::string& name, const std::string& salida = "") override;
    std::string currentScene(const std::string& salida = "") const override;
    std::string currentSceneId(const std::string& salida = "") const override;
    /// El id de la escena armada en el preview de la salida ("" si nada).
    std::string armadaId(const std::string& salida = "") const;
    bool setTransition(const std::string& type, int durationMs) override;
    bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida = "") override;
    bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida = "") override;
    std::string manualHacia(const std::string& salida = "") const override;
    bool armar(const std::string& name, const std::string& salida = "") override;
    // --- FTB y placa de seguridad (por encima de todo lo que sale) -------------
    bool ftb(bool activar, int ms, bool bajarAudio, const std::string& salida = "") override;
    bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms, const std::string& salida = "") override;
    // --- DSK: capas globales encima del programa (1..4) ------------------------
    bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro,
                  const std::string& salida = "") override;
    bool dskMostrar(int capa, bool visible, int ms, const std::string& salida = "") override;
    nlohmann::json dskEstado(const std::string& salida = "") const override;
    std::vector<std::string> fuentesDsk(const std::string& salida = "") const override;
    bool haySalida(bool contarGrabacion = true) const override;
    bool haySalidaEn(const std::string& salida, bool contarGrabacion = true) const override;
    void setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar = false) override;
    bool hayFuenteBanca() const override;
    std::string escenaDeRanura(int ranura) const override;
    std::vector<std::string> fuentesEnPrograma(const std::string& salida) const override;
    std::vector<std::string> fuentesEnPrograma() const override;
    bool fuenteAlAire(const std::string& fuente) const override;
    // --- Video en vivo dentro de un gráfico (graficos/VideoEnGrafico.hpp) --------------
    std::string ponerVideoDeGrafico(const std::string& grafico, const nlohmann::json& capas) override;
    void empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs) override;
    void quitarVideoDeGrafico(const std::string& grafico) override;
    bool textoGlobal(const std::string& nombre, const std::string& texto) override;
    nlohmann::json estadoPlaca(const std::string& salida = "") const override;
    float picoProgramaVigiaDb() override;
    nlohmann::json fotoSalidas() const override;
    /// DESDE CUALQUIER HILO: el canal de niveles (los vúmetros de cada entrada, bus y
    /// programa; emision/NivelesMotor.hpp). Vive lo que el BroadcastEngine.
    NivelesMotor& nivelesCanal() { return nivelesCanal_; }
    /// Con el turno: lo que mide cada salida (el pico de su programa, su loudness), para
    /// leerlo SIN el turno desde el reloj del motor (NivelesMotor::medir). Cada medidor
    /// guarda las medidas compartidas de su salida: una salida borrada no lo deja colgado.
    std::vector<NivelesMotor::Medidor> medidores();
    const audio::LoudnessPrograma& loudnessPrograma() const override;
    int enviarCaption(const std::string& texto, double segundos) override;
    audio::LoudnessPrograma& loudness(const std::string& salida = "") override;
    audio::LoudnessPrograma* loudnessDe(const std::string& salida) override;
    void aplicarLoudness() override;
    ProgramaVideo programaVideo(const std::string& salida = "") const override;
    /// Cuántos cuadros salieron del programa (el latido del aire: si no crece, algo se trabó).
    std::uint64_t cuadrosPrograma() const;
    void ponerTopeEstudio(long long ms) override;
    void soltarVistasSinMirar(bool on) override;
    nlohmann::json latenciaEstudio() const override;
    bool bajarLatenciaEstudio() override;

    std::string archivoGrabacion(const std::string& salida = "") const override;
    /// Pasa `archivo` (mkv) a `formato` (mp4/mov) sin recodificar, en otro hilo.
    /// Varios archivos seguidos (los tramos de una grabación partida), en un solo hilo.
    bool remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen);
    bool remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) override;
    nlohmann::json remuxEstado() const override;

    /// Compatibilidad: crea la escena "programa" y la pone al aire.
    bool buildScene();
    // --- Fuentes -------------------------------------------------------------
    bool addSource(const std::string& scene, const SourceSpec& spec) override;
    bool addSource(const SourceSpec& spec) override;
    bool removeSource(const std::string& scene, const std::string& name) override;
    bool moveSource(const std::string& scene, const std::string& name, int delta) override;
    std::vector<std::string> sceneSources(const std::string& scene) const override;
    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible) override;
    bool sourceVisible(const std::string& scene, const std::string& name) const override;
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) override;
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo) override;
    bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) override;
    EfectosCapa sourceEfectos(const std::string& scene, const std::string& name) const override;
    /// Los recortes que esperan que su fuente tenga tamaño: se aplican apenas lo tiene.
    void aplicarRecortesPendientes();
    bool renameSource(const std::string& from, const std::string& to) override;
    std::string sourceTypeId(const std::string& name) const override;
    void setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor) override;
    bool sourceExists(const std::string& name) const override;
    nlohmann::json estadoEntrada(const std::string& name) const override;
    std::vector<std::pair<std::string, CanvasRect>> itemsVisibles(const std::string& scene) const override;
    bool ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) override;
    std::pair<std::string, std::string> relojEstado() const override;
    std::string publicarReloj(int puerto) override;
    std::string controlEntrada(const std::string& name, const nlohmann::json& campos) override;
    std::string sourceSettingsJson(const std::string& name) const override;
    void holdShowing(const std::string& name, bool on) override;
    bool sourceSize(const std::string& name, int& width, int& height) const override;
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson) override;
    void capturarAparte(const std::string& name, int maxWidth, Captura listo) override;
    std::string prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                bool& esEscena) const override;
    std::string capturarVista(const std::string& clave, int w, int h, bool esEscena, std::vector<std::uint8_t>& rgba,
                              int& width, int& height) override;
    bool addGuestAudio(int slot) override;
    bool addGlobalAudio(const SourceSpec& spec) override;
    /// Compatibilidad (en el motor nada queda pendiente): devuelve 0.
    int adoptPendingAudio();
    bool removeGlobalAudio(const std::string& name) override;
    std::vector<std::string> globalAudioInputs() const override;
    bool addGuestVideo(int slot);
    bool addGuestVideo(int slot, const CanvasRect& rect);
    // --- Audio por fuente (mezclador) ------------------------------------------
    std::vector<std::string> audioSources() const override;
    std::vector<std::string> sourceNames() const override;
    bool setSourceVolume(const std::string& name, float mul, const std::string& salida = "") override;
    float sourceVolume(const std::string& name, const std::string& salida = "") const override;
    void setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida = "") override;
    float gananciaGrupo(const std::string& grupo, const std::string& salida = "") const override;
    bool setSourceMuted(const std::string& name, bool muted, const std::string& salida = "") override;
    bool sourceMuted(const std::string& name, const std::string& salida = "") const override;
    float sourcePeakDb(const std::string& name) const override;
    bool setSourceMonitoring(const std::string& name, bool on) override;
    bool sourceMonitoring(const std::string& name) const override;
    bool sourceEscuchaFija(const std::string& name) const override;
    bool setPfl(const std::string& name, const std::string& modo = "pfl", const std::string& salida = "") override;
    std::string pfl() const override;
    std::string pflModo() const override;
    bool setSalidaEscucha(const std::string& salida) override;
    std::string salidaEscucha() const override;
    bool setSourceSyncOffset(const std::string& name, int ms) override;
    int sourceSyncOffset(const std::string& name) const override;
    bool setSourcePistas(const std::string& name, const std::vector<int>& pistas) override;
    std::vector<int> sourcePistas(const std::string& name) const override;
    // --- Medios ------------------------------------------------------------------
    std::vector<MediaInfo> mediaSources() const override;
    bool playMedia(const std::string& name, const std::string& file, bool loop = false, float volume = 1.0f) override;
    bool stopMedia(const std::string& name) override;
    bool restartMedia(const std::string& name) override;
    bool seekMedia(const std::string& name, std::int64_t ms) override;
    bool pararMedia(const std::string& name) override;
    // --- Descubrir capacidades -------------------------------------------------
    bool sourceTypeExists(const std::string& typeId) const override;

    // --- Cambios en caliente -------------------------------------------------
    /// true si hay grabacion o transmision en curso (en alguna salida).
    bool outputsActive() const;
    bool resetVideo(const VideoConfig& video, const std::function<VideoConfig()>& conElMotorApagado = {}) override;
    /// Compatibilidad: los codificadores se crean por salida. Solo sin salidas activas.
    bool resetEncoders();
    VideoConfig videoConfig() const override {
        const TurnoMotor::Guarda turno(turno_);
        return video_;
    }
    /// Codificadores de video en el orden en que se probarían para `enc` (el último, x264enc).
    /// No lee el estado del motor (solo `enc` y las fábricas de GStreamer): desde cualquier
    /// hilo, sin el turno (lo usa también el hilo de salidas al arrancar una grabación).
    static std::vector<std::string> videoEncoderCandidates(const EncoderConfig& enc);

    /// Publica el programa de cada salida hacia el Proceso B (la por omisión en
    /// BA.Program, las demás en BA.Salida.<id>; el audio lo escribe el motor: ver
    /// LAS SALIDAS arriba). Una salida que se crea después se publica sola.
    bool publishReturnFeeds();
    void stopReturnFeeds();
    void ponerTopeRetorno(int ancho, int alto) override;
    void retornoEnUso(bool si, const std::string& salida = "") override;
    void alFallarMotor(std::function<void(const AvisoMotor&)> fn) override;
    /// Avisa cuando cambia el audio de una fuente: `que` es "escucha" (fija o PFL),
    /// "pistas", "retardo", "volumen", "mute" o "alta" (se creó, se rehízo o se renombró). Lo pida quien lo pida (la matriz, el
    /// Mezclador, el asistente, el AFV, una escena que se carga, volver como estaba):
    /// quien lo muestra se entera siempre. Se llama en el hilo que hizo el cambio.
    void alCambiarAudioFuente(std::function<void(const std::string& fuente, const char* que)> fn);
    bool escenaACanal(int k, const std::string& escena) override;
    std::string escenaDeCanal(int k) const override;
    EscenasMostradas escenasMostradas() const override;
    int tomarCanalEscena(const std::string& escena, const std::string& dueno) override;
    void soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo = {}) override;
    bool canalEscenaConDuenos(int k) const override;
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente = false,
                        const std::string& salida = "", AlResultado alTerminar = {}) override;
    Inicio ultimoInicio() const override;
    std::string destinoActivo(const std::string& salida = "") const override;
    // --- Destinos adicionales (respaldo, otras plataformas) --------------------
    bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                    const std::string& salida = "", const std::string& serverRespaldo = "") override;
    void stopExtra(const std::string& id) override; ///< esté en la salida que esté
    void stopExtras() override;                     ///< los de todas las salidas
    nlohmann::json extrasEstado() const override;
    nlohmann::json saludSalidas() const override;
    void stopStreaming(const std::string& salida = "") override;
    bool startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida = "",
                        AlResultado alTerminar = {}) override;
    void stopRecording(const std::string& salida = "") override;
    nlohmann::json grabacionEstado(const std::string& salida = "") const override;
    // --- Repetición ---------------------------------------------------------------
    bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida = "") override;
    bool guardarReplay(const std::string& salida = "") override;
    std::string ultimoReplay(const std::string& salida = "") const override;
    nlohmann::json replayEstado(const std::string& salida = "") const override;

    // --- El disco en apuros: el disco, para la grabación del programa --------
    // Si el disco no da abasto (lo que espera en el colchón de una grabación, o
    // el registro trabado escribiendo), se avisa y se ofrece darle el disco a la
    // grabación del programa: se pausan las ISO, la repetición, la copia si va
    // al mismo disco y la señal limpia a disco, y el registro se junta en memoria.
    // Cuando el disco anda bien de nuevo (30 s), todo sigue solo.
    /// "preguntar" (avisa y ofrece), "auto" (lo hace solo) o "nunca" (solo avisa).
    std::string modoDisco() const;
    void ponerUmbralesDisco(double apurosS, int recuperadoS) override;
    std::string setModoDisco(const std::string& modo) override; ///< "" o el error (se guarda en Config/disco.json)
    void priorizarPrograma(bool si) override;
    nlohmann::json discoEstado() const override;
    void simularDiscoTrabado(int ms) override;

    bool encoderRespaldoActivo() const override;
    // --- Medir la sincronía labios/voz con un aplauso -----------------------------
    bool empezarMedicionSincro(const std::string& salida = "") override;
    nlohmann::json terminarMedicionSincro() override;
    bool midiendoSincro() const override;
    int pistasGrabacionMask() const override;
    // --- Grabacion POR ENTRADA ------------------------------------------------
    bool startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc) override;
    bool startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                        const EncoderConfig& enc, const std::string& salida = "") override;
    void stopCleanFeed(const std::string& salida = "") override;
    nlohmann::json cleanFeedEstado(const std::string& salida = "") const override;
    bool stopSourceRecording(const std::string& source) override;
    void stopAllSourceRecordings() override;
    std::vector<GrabacionIndividual> sourceRecordings() const override;
    // --- Vista previa y medicion (para la UI) -------------------------------
    bool enablePreview(int width, int height) override;
    void disablePreview();
    bool enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) override;
    bool enableScenePreview(const std::string& name, int maxWidth) override { return enableScenePreview("editor", name, maxWidth); }
    bool enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida, int maxWidth) override;
    void disableScenePreview(const std::string& canal) override;
    /// Diagnóstico de parpadeos (BA_MEDIR_PREVIEW=1): ver medirVista en el .cpp.
    void medirVista(const std::string& clave);
    void disableScenePreview() override { disableScenePreview("editor"); }
    bool renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial) override;
    std::string scenePreviewName(const std::string& canal) const override;
    bool latestPreview(motor::Cuadro& out, std::uint64_t& serial) const override;
    /// Picos del programa desde la ultima llamada, en dBFS (L, R). -100 = silencio.
    void programPeaksDb(float& left, float& right, const std::string& salida = "");

    /// Lo que contó el hilo de salidas (lo que no arrancó, lo que adoptó del emisor aparte, el
    /// AAC con que transmite): lo atiende el dueño en cada vuelta del motor (también antes de
    /// leer lo pedido). Ver emision/SalidaMotor.hpp.
    void atenderSalidas();
    EngineStatus status() const override;
    const std::string& lastError() const override;
    /// DESDE CUALQUIER HILO.
    bool ready() const { return started_; }
    /// El arranque falló porque este programa no tiene GStreamer (build sin motor, bundle
    /// incompleto). Una marca y no el texto de lastError(), que sale traducido.
    bool sinGStreamer() const { return sinGStreamer_; }
    const TurnoMotor& turno() const override { return turno_; }
    void setRetornoInvitado(int slot, const std::string& modo) override;
    std::string retornoInvitado(int slot) const override;

   private:
    // Las piezas (cada una con su dueño de hilo en su cabecera; el resumen, arriba en
    // «Modelo de threading» y en docs/arquitectura/05-motor-del-plano.md). Esta clase es la
    // FACHADA: la API de siempre, que toma el turno y delega.
    friend class EscenasYFuentes;
    friend class MezclaApp;
    friend class MediosApp;
    friend class VistasYMedicion;
    friend class RetornoGateway;
    friend class EmisionSalidas;
    friend class VigiaDisco;
    friend class EmisorAparte;

    // --- Las salidas: lo del aire de cada una (SalidaAire, del dueño; emision/SalidaMotor.hpp)
    /// Todas, por id. Solo lo cambia el que tiene el turno (con salidasM_ tomado); desde otro
    /// hilo se lee con salidasM_ (programaVideo). La por omisión existe siempre.
    std::map<std::string, std::shared_ptr<SalidaAire>> salidas_;
    std::vector<std::string> ordenSalidas_; ///< la por omisión primera
    mutable std::mutex salidasM_;
    /// "" = la por omisión; nullptr (y lastError) si no hay una con ese id.
    SalidaAire* laSalida(const std::string& id);
    const SalidaAire* laSalida(const std::string& id) const;
    std::shared_ptr<SalidaAire> laSalidaCompartida(const std::string& id);
    SalidaAire& porOmision();
    const SalidaAire& porOmision() const;
    /// Una copia de todas, en orden (con salidasM_).
    std::vector<std::shared_ptr<SalidaAire>> lasSalidas() const;
    /// Las medidas de la por omisión: existe desde el constructor y no se borra nunca, así
    /// que se pueden leer desde cualquier hilo sin buscar en salidas_.
    std::shared_ptr<MedidasSalida> medidasPrograma_;

    void setError(std::string msg);
    /// El estado del motor, cacheado (las consultas de la UI llegan muy seguido).
    const nlohmann::json& estadoMotor() const;
    /// Los avisos tipados a quien los escucha (alFallarMotor). Desde cualquier hilo.
    void avisar(const AvisoMotor& a);
    void avisarFallo(const std::string& texto) { avisar({"orden_fallida", AvisoMotor::Nivel::Error, texto}); }

    TurnoMotor turno_;
    std::atomic<bool> started_{false};
    std::atomic<bool> sinGStreamer_{false};
    /// El último error de cualquier hilo (el de status(); el de lastError() es de cada uno).
    mutable std::mutex errorM_;
    std::string ultimoError_;
    // Del dueño. Mientras anda el hilo de salidas no cambian (cambiar el lienzo o el reloj
    // rearma el motor: apagarlo para ese hilo antes), así que ese hilo los lee sin candado.
    EnginePaths paths_;
    VideoConfig video_;
    AudioConfig audio_;
    std::string relojTipo_ = "sistema", relojServidor_;
    int relojPuerto_ = 0, relojDominio_ = 0, relojPublicar_ = 0;
    /// Lo cambian startup y shutdown con el candado de las vistas tomado (las vistas lo
    /// leen desde cualquier hilo: VistasYMedicion).
    std::unique_ptr<motor::Motor> motor_;
    std::mutex falloMotorM_;
    std::function<void(const AvisoMotor&)> falloMotor_;
    NivelesMotor nivelesCanal_; ///< nivelesCanal
    std::function<void(const std::string&, const char*)> cambioAudio_; ///< alCambiarAudioFuente (se fija una vez, al armar)
    mutable std::shared_ptr<const nlohmann::json> estadoFoto_; ///< estadoMotor (del dueño)

    std::unique_ptr<EscenasYFuentes> escenas_;
    std::unique_ptr<MezclaApp> mezcla_;
    std::unique_ptr<MediosApp> medios_;
    std::unique_ptr<VistasYMedicion> vistas_;
    std::unique_ptr<RetornoGateway> retorno_;
    std::unique_ptr<EmisorAparte> emisor_;
    std::unique_ptr<VigiaDisco> disco_;
    std::unique_ptr<EmisionSalidas> emision_;
};

} // namespace ba::app
