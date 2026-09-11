// ============================================================================
// src/app/OperatorEngine.hpp — Motor de operación del Proceso A (sin Qt, sin libobs)
//
// Propósito:
//   Núcleo de la aplicación del operador (Proceso A, spec §6) desacoplado de
//   Qt y de libobs: es la capa que realmente integra `ba_core` y que la UI
//   (MainWindow, Qt) o el modo headless (cross-compile sin Qt/libobs) invocan.
//   Posee y coordina:
//     - la cadena DSP por canal: un `ba::dsp::ChannelStrip` por canal de
//       micrófono + el `ba::dsp::Automixer` compartido de mesa redonda, con
//       los tres presets de fábrica (`presetPodium` / `presetLavalier` /
//       `presetHandheld`) aplicables en caliente por canal;
//     - el canal IPC hacia invitados (spec §4): el `ba::ipc::SharedAudioRing`
//       como LECTOR del audio de invitados, el `ba::ipc::HealthBlock` (beat +
//       lectura de estado) y el `ba::ipc::ControlEndpoint` (sendRequest/poll);
//     - la persistencia (spec §11): un `ba::persist::SnapshotStore` + el
//       `ba::persist::StateDoc` que se escribe en el cierre ordenado.
//
//   Es la pieza testeable y portable: NO depende de Qt ni de libobs, por lo
//   que compila en Windows (MSVC/MinGW) y en Linux y puede correr en headless
//   (sin ventana) cuando el entorno no trae Qt/libobs. La UI Qt y las llamadas
//   a libobs viven en `MainWindow`/`main`, no aquí.
//
// Modelo de threading:
//   El motor es de UN solo thread de control (el de la UI o el headless):
//   `init()`, `applyPreset()`, `tick()` y `shutdown()` se llaman desde ese
//   thread. `processAudio()` modela el callback de audio de libobs: en la
//   app real lo invocaría el thread de audio de libobs, pero aquí se llama
//   desde el thread de control (o desde un test) y NO hay locks internos —
//   la thread-safety de cada primitiva es la que aporta `ba_core` (el
//   Automixer es lock-free; el ring es SPSC; el HealthBlock usa seqlock).
//   No asigna en el camino de `processAudio()` salvo el buffer de salida del
//   automixer (de tamaño fijo, reservado en `init()`).
//
// Estrategia de errores:
//   No lanza. `init()` devuelve bool (false si algún recurso IPC no pudo
//   crearse; en ese caso el objeto queda en un estado coherente y las demás
//   operaciones se vuelven no-op seguros). `applyPreset()` con canal o preset
//   fuera de rango es no-op. `processAudio()` con `frames<=0` o puntero nulo
//   no hace nada. `shutdown()` es idempotente. Ninguna operación abre/cierra
//   excepciones: los fallos de `ba_core` (Status {ok,code}) se traducen a
//   bools y se reflejan en `lastError()`.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dsp/Automixer.hpp"
#include "dsp/ChannelStrip.hpp"
#include "dsp/Presets.hpp"
#include "ipc/ControlProtocol.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "persist/SnapshotStore.hpp"
#include "persist/StateDoc.hpp"

namespace ba::app {

/// Identificadores de los tres presets de fábrica de channel strip (spec 7.3).
///
/// Se exponen como opciones de canal en la UI (MainWindow). El índice es
/// estable y coincide con el orden de los `QComboBox` de la UI: 0=Podio,
/// 1=Lavalier, 2=De mano.
enum class ChannelPreset : int {
    kPodium = 0,     // presetPodium()    — atril / cuello de cisne.
    kLavalier = 1,   // presetLavalier()  — corbetero / lavalier.
    kHandheld = 2,   // presetHandheld()  — micrófono de mano.
};

/// Número de canales de micrófono que gestiona el operador (spec 7: tres
/// micrófonos de broadcast). Fijo: no se redimensiona en caliente.
inline constexpr int kNumChannels = 3;

/// Nombre lógico del ring de audio de invitados que A lee (spec §4.3).
///
/// En Windows se publica como `Local\BA.Audio.Guest.0`; en el backend anónimo
/// (Linux / cross) es la clave del registro de proceso.
inline constexpr const wchar_t* kGuestAudioRingName = L"BA.Audio.Guest.0";

/// Slot del HealthBlock que ocupa el Proceso A (spec §4.4). El launcher
/// (Proceso 0) es quien crea el bloque con `slotCount` slots; A abre el
/// bloque y escribe su slot. Aquí se fija el slot de A como 0 para el caso
/// de que A también lo cree (modo headless / autotest).
inline constexpr std::size_t kAppHealthSlot = 0;

/// Slot del HealthBlock que A LEe para el watchdog del invitado/gateway
/// (spec §3.1.7): si el heartbeat de ese slot se estanca > 5 s, A lo marca.
inline constexpr std::size_t kWatchedHealthSlot = 1;

/// Versión de build/protocolo que A publica en su slot de salud (spec §4.4).
inline constexpr std::uint32_t kAppProtocolVersion = 2;

/// Stream duplexo en memoria para el canal de control en modo headless.
///
/// Propósito:
///   En la app real (Qt/libobs) el `ControlEndpoint` se respalda sobre el
///   transporte real (named pipe / socket hacia el gateway, spec §4.1). Para
///   que el motor sea testeable y corra en headless (cross-compile sin
///   Qt/libobs) se usa este loopback en memoria: cada byte escrito por
///   `write()` queda disponible para el `read()` siguiente (y viceversa),
///   simulando un par conectado. Permite ejercitar `sendRequest()` y `poll()`
///   de forma determinista sin transporte real.
///
/// Modelo de threading:
///   No thread-safe: se usa desde el único thread de control del motor.
///
/// Estrategia de errores:
///   No lanza. `write()` siempre devuelve true (en memoria no falla); `read()`
///   devuelve los bytes pendientes o 0 si no hay (EOF lógico: el loopback
///   nunca "se cierra", así que <0 no se produce).
class LoopbackDuplex : public ba::ipc::DuplexStream {
public:
    /// Escribe `len` bytes a `data` en el buffer de salida (para el lector).
    ///
    /// :param data: bytes a escribir.
    /// :param len: cantidad de bytes.
    /// :returns: true siempre (escribir en memoria no falla).
    bool write(const std::uint8_t* data, std::size_t len) override;

    /// Lee hasta `cap` bytes del buffer de salida, avanzando el cursor.
    ///
    /// :param buf: buffer de destino.
    /// :param cap: capacidad de `buf` en bytes.
    /// :returns: bytes leídos (>0) o 0 si no hay bytes pendientes.
    int read(std::uint8_t* buf, std::size_t cap) override;

private:
    std::vector<std::uint8_t> outBuf_;
    std::size_t outPos_ = 0;
};

/// Motor de operación del Proceso A (sin Qt, sin libobs).
///
/// Posee la cadena DSP por canal, el automixer, el canal IPC hacia invitados
/// y la persistencia. La UI Qt (MainWindow) y el modo headless (main) lo
/// crean y lo operan desde su thread de control. Move-only por los objetos
/// IPC/persistencia de `ba_core` (son move-only).
class OperatorEngine {
public:
    /// Construye el motor en estado inicial (sin recursos IPC abiertos).
    /// Llamar a `init()` para abrirlos.
    OperatorEngine();

    /// Destructor: libera los recursos IPC/persistencia (RAII de ba_core).
    ~OperatorEngine();

    // No movible ni copiable: se crea una única vez (en main o MainWindow) y
    // vive toda la vida de la app. `ba::ipc::ControlEndpoint` no es movible
    // (guarda un `DuplexStream&`) y `ba::dsp::Automixer` reserva arrays de
    // capacidad fija en su constructor, así que el motor se queda como objeto
    // de duración de vida larga, no como valor transferible.
    OperatorEngine(OperatorEngine&&) = delete;
    OperatorEngine& operator=(OperatorEngine&&) = delete;
    OperatorEngine(const OperatorEngine&) = delete;
    OperatorEngine& operator=(const OperatorEngine&) = delete;

    /// Abre todos los recursos: crea los ChannelStrip (con presetPodium por
    /// defecto), el Automixer, el ring de audio de invitados (lector), el
    /// HealthBlock (beat + lectura) y el ControlEndpoint sobre el loopback, e
    /// inicializa el SnapshotStore en `configDir/Config/session`.
    ///
    /// :param configDir: directorio raíz del disco portable (spec §5.1); los
    ///                   snapshots viven en `<configDir>/Config/session`.
    ///                   Si es vacío se usa el directorio actual.
    /// :returns: true si todos los recursos se abrieron; false si alguno falló
    ///           (el objeto queda coherente y las operaciones se vuelven
    ///           no-op seguros). Ver `lastError()` para el motivo.
    bool init(const std::string& configDir = "");

    /// Aplica un preset de fábrica a un canal de micrófono, en caliente.
    ///
    /// Recalcula el ChannelStrip de ese canal con `setParams(preset, smoothMs)`
    /// (suavizado de ganancia para evitar clicks) y recuerda el preset elegido
    /// (lo persiste en el StateDoc). El Automixer no cambia: es un objeto
    /// compartido de niveles, no un preset.
    ///
    /// :param channel: índice de canal en [0, kNumChannels).
    /// :param preset: preset a aplicar (Podium/Lavalier/Handheld).
    /// :param smoothMs: duración del fade de ganancia en ms (default 5 ms).
    /// :returns: true si se aplicó; false si canal/preset fuera de rango.
    bool applyPreset(int channel, ChannelPreset preset, double smoothMs = 5.0);

    /// Procesa un bloque de audio de un canal de micrófono in-place y alimenta
    /// el Automixer con su nivel.
    ///
    /// Modela el callback de audio de libobs de ese canal:
    ///   1. `ChannelStrip::process()` (HPF→EQ→de-esser→gate→comp→lim) in-place;
    ///   2. calcula el nivel RMS del bloque (post-strip) y lo reporta al
    ///      `Automixer::setLevel(channel, level)` (thread de audio, un escritor);
    ///   3. aplica la ganancia dinámica del automixer
    ///      (`Automixer::gainFor(channel)`) multiplicando el bloque.
    /// No asigna en el camino (el buffer de nivel es de tamaño fijo).
    ///
    /// :param interleaved: buffer estéreo interleaved (2*frames floats), se
    ///                     modifica in-place.
    /// :param frames: número de FRAMES (no de muestras). <=0 o nullptr no hace
    ///                nada.
    /// :param channel: índice de canal en [0, kNumChannels).
    void processAudio(float* interleaved, int frames, int channel);

    /// Procesa un bloque de audio de invitados leído del ring IPC.
    ///
    /// Llama a `SharedAudioRing::pop()` (lector); si hay bloque (no underrun)
    /// lo procesa con el ChannelStrip del canal de invitados (0) y lo deja en
    /// `out` para que la UI / el motor de emisión lo consuma. Si fue underrun
    /// (silencio+fade) o fade-in, `out` recibe silencio y `isSilence`/
    /// `isFadeIn` se reflejan.
    ///
    /// :param out: buffer de destino (2*kBlockFrames floats, estéreo).
    /// :param isSilence: true si el pop fue underrun (silencio).
    /// :param isFadeIn: true si aplica fade-in de resincronización.
    /// :returns: true si se produjo un pop (con o sin datos); false si el ring
    ///           no está abierto.
    bool processGuestBlock(float* out, bool& isSilence, bool& isFadeIn);

    /// Un "tick" del thread de control: refresca el heartbeat de A en el
    /// HealthBlock, evalúa el watchdog del slot observado (isStale) y hace
    /// `poll()` del canal de control despachando un frame si hay.
    ///
    /// Es la operación periódica que la UI (un QTimer) o el headless (un
    /// bucle corto) invocan. No bloquea.
    ///
    /// :returns: true si se recibió y despachó un mensaje de control en este
    ///           tick; false si no había frame pendiente.
    bool tick();

    /// Indica si el slot observado del HealthBlock está estancado (posible
    /// cuelgue del invitado/gateway, spec §3.1.7: > 5 s sin heartbeat).
    ///
    /// :returns: true si `isStale(kWatchedHealthSlot)`; false si no hay bloque
    ///           o el slot no está estancado.
    bool watchedIsStale() const;

    /// Construye el `StateDoc` actual (fuente de verdad, spec §11.1) a partir
    /// del estado del motor: secuencia, presets por canal, parámetros del
    /// automixer y estado de los flujos IPC. `cleanShutdown` se fija solo en
    /// `shutdown()`.
    ///
    /// :returns: el documento serializable (se copia; el caller puede
    ///           reutilizarlo).
    ba::persist::StateDoc buildStateDoc() const;

    /// Cierre ordenado (spec §11.2): fija `cleanShutdown=true` en el
    /// StateDoc, `scheduleWrite(doc, immediate=true)` y `flushNow()` para que
    /// la última generación quede en disco. Idempotente.
    ///
    /// :returns: true si el store estaba inicializado y se programó la
    ///           escritura; false si no hay store (init falló).
    bool shutdown();

    // --- Consultas para la UI / tests --------------------------------------

    /// Número de canales gestionados.
    int numChannels() const { return kNumChannels; }

    /// Preset actualmente aplicado a `channel` (default kPodium).
    ///
    /// :param channel: índice de canal.
    ChannelPreset presetFor(int channel) const;

    /// Último nivel RMS reportado al automixer para `channel` (0..1).
    ///
    /// :param channel: índice de canal.
    float levelFor(int channel) const;

    /// Ganancia dinámica actual del automixer para `channel` (0..1).
    ///
    /// :param channel: índice de canal.
    float automixerGainFor(int channel) const;

    /// Indica si el ring de audio de invitados está abierto como lector.
    bool guestRingOpen() const;

    /// Indica si el ControlEndpoint sigue conectado.
    bool controlConnected() const;

    /// Último código de error (0 = ok; negativo = ErrorCode de ba_core).
    int lastError() const { return lastError_; }

private:
    /// Traduce un `ChannelPreset` a su `StripParams` de fábrica.
    ///
    /// :param preset: preset a resolver.
    ba::dsp::StripParams stripParamsFor(ChannelPreset preset) const;

    /// Despacha un mensaje de control recibido en `tick()`: responde con un
    /// `Response` (request/response, spec §4.1) sobre el `ControlEndpoint`.
    ///
    /// :param m: mensaje recibido.
    void dispatchMessage(const ba::ipc::Message& m);

    // --- DSP por canal (spec 7.2/7.3) --------------------------------------
    std::vector<ba::dsp::ChannelStrip> strips_;
    std::vector<ChannelPreset> presets_;
    ba::dsp::Automixer automixer_;

    // --- Canal IPC hacia invitados (spec §4) --------------------------------
    ba::ipc::SharedAudioRing guestRing_;   // lector del audio de invitados
    ba::ipc::HealthBlock health_;          // beat (slot de A) + lectura (watchdog)
    // ControlEndpoint sobre el loopback: en unique_ptr porque ControlEndpoint
    // NO es movible (guarda un `DuplexStream&`) y su vida se alinea con la del
    // motor. `loopback_` debe declararse ANTES para que siga vivo cuando el
    // endpoint lo referencia (orden de destrucción inverso).
    LoopbackDuplex loopback_;
    std::unique_ptr<ba::ipc::ControlEndpoint> control_;

    // --- Persistencia (spec §11) --------------------------------------------
    ba::persist::SnapshotStore store_;
    ba::persist::StateDoc lastDoc_;
    std::uint64_t sequence_ = 0;

    // --- Estado de diagnóstico ----------------------------------------------
    int lastError_ = 0;
    bool initialized_ = false;
    bool shutDown_ = false;
    std::vector<float> lastLevel_; // último nivel RMS por canal (para la UI)
};

} // namespace ba::app
