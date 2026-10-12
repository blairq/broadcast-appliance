// ============================================================================
// src/app/emision/OperatorEngine.cpp — Implementación del motor de operación (Proceso A)
//
// Propósito:
//   Implementa `ba::app::OperatorEngine` (ver OperatorEngine.hpp): la capa que
//   integra `ba_core` (dsp + ipc + persist) para el Proceso A, desacoplada de
//   Qt y de libobs. Aquí viven las llamadas reales a `ba::dsp::ChannelStrip`,
//   `ba::dsp::Automixer`, los presets, el canal IPC (`SharedAudioRing`/
//   `HealthBlock`/`ControlEndpoint`) y la persistencia (`SnapshotStore`/
//   `StateDoc`).
//
// Modelo de threading:
//   Un solo thread de control (UI o headless) invoca init/applyPreset/tick/
//   shutdown y, en su caso, processAudio (que modela el callback de audio de
//   libobs). No hay locks internos: la thread-safety de cada primitiva la aporta
//   `ba_core` (Automixer lock-free, ring SPSC, HealthBlock seqlock).
//
// Estrategia de errores:
//   No lanza. `init()` devuelve bool y traduce los `Status {ok,code}` de
//   `ba_core` a `lastError()`. Las demás operaciones son no-op seguros si el
//   objeto no está inicializado o el índice está fuera de rango.
// ============================================================================

#include "emision/OperatorEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

#include "persist/Restore.hpp"

#if defined(_WIN32)
#include <windows.h> // GetCurrentProcessId() en el camino portable de currentPid()
#else
#include <unistd.h> // getpid() en el resto de plataformas
#endif

namespace ba::app {

namespace {

// Frecuencia de muestreo del reloj de libobs (spec 7.4): 48 kHz.
constexpr double kFs = 48000.0;

// Convierte un ChannelPreset a su nombre para el StateDoc (persistencia).
const char* presetName(ChannelPreset p) {
    switch (p) {
        case ChannelPreset::kPodium: return "podium";
        case ChannelPreset::kLavalier: return "lavalier";
        case ChannelPreset::kHandheld: return "handheld";
    }
    return "podium";
}

// Nombre de sección del StateDoc (spec §11.1: blobs JSON opacos por nombre).
constexpr const char* kSectionDsp = "dsp";
constexpr const char* kSectionIpc = "ipc";

// PID del proceso, portable: GetCurrentProcessId() en _WIN32, getpid() en el
// resto. El motor es portable (cross-MinGW y Linux), así que no se asume
// un solo backend.
std::uint32_t currentPid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

} // namespace

// ============================================================================
// LoopbackDuplex
// ============================================================================

bool LoopbackDuplex::write(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) {
        return true; // escribir 0 bytes no falla
    }
    outBuf_.insert(outBuf_.end(), data, data + len);
    return true;
}

int LoopbackDuplex::read(std::uint8_t* buf, std::size_t cap) {
    if (buf == nullptr || cap == 0) {
        return 0;
    }
    if (outPos_ >= outBuf_.size()) {
        // No hay bytes pendientes: compacta (el loopback nunca "se cierra").
        outBuf_.clear();
        outPos_ = 0;
        return 0;
    }
    const std::size_t avail = outBuf_.size() - outPos_;
    const std::size_t n = std::min(cap, avail);
    std::copy_n(outBuf_.begin() + static_cast<std::ptrdiff_t>(outPos_), static_cast<std::ptrdiff_t>(n), buf);
    outPos_ += n;
    return static_cast<int>(n);
}

bool LoopbackDuplex::hasPending() const { return outPos_ < outBuf_.size(); }

// ============================================================================
// Construcción / destrucción
// ============================================================================

OperatorEngine::OperatorEngine() : automixer_(kNumChannels) {
    strips_.resize(kNumChannels);
    presets_.assign(kNumChannels, ChannelPreset::kPodium);
    lastLevel_.assign(kNumChannels, 0.0f);
}

OperatorEngine::~OperatorEngine() = default;

// ============================================================================
// init
// ============================================================================

bool OperatorEngine::init(const std::string& configDir, bool restore) {
    lastError_ = 0;
    if (initialized_) {
        return true; // idempotente
    }

    // --- DSP por canal (spec 7.2/7.3) --------------------------------------
    // Cada canal arranca con el preset de fábrica del atril (kPodium), con
    // suavizado 0 (carga inicial, sin click). El Automixer es compartido y
    // arranca con parámetros de broadcast razonables (fs=48k).
    for (int ch = 0; ch < kNumChannels; ++ch) {
        strips_[ch].setParams(stripParamsFor(ChannelPreset::kPodium), 0.0);
    }
    ba::dsp::AutomixerParams am;
    am.fs = kFs;
    automixer_.setParams(am, 0.0);

    // --- Canal IPC hacia invitados (spec §4) --------------------------------
    // HealthBlock: A abre el bloque si el launcher ya lo creó; si no (headless
    // / autotest) lo crea con dos slots (0 = A, 1 = observado). Publica su
    // identidad y su primer latido.
    ba::ipc::HealthBlock hb;
    ba::ipc::Status hs = ba::ipc::HealthBlock::open(ba::ipc::kHealthBlockName, hb);
    if (!hs.ok) {
        hs = ba::ipc::HealthBlock::create(ba::ipc::kHealthBlockName, 2, hb);
    }
    if (hs.ok) {
        health_ = std::move(hb);
        const std::uint32_t pid = currentPid();
        health_.setIdentity(kAppHealthSlot, pid, kAppProtocolVersion);
        health_.beat(kAppHealthSlot);
    } else {
        lastError_ = hs.code;
    }

    // SharedAudioRing: A es LECTOR del audio de invitados. En la app real (B
    // publica el ring) A lo abre con openReader. En headless no hay B, así que
    // A lo crea con init como respaldo (simula a B) para que el nombre exista.
    // Un solo objeto guestRing_: openReader (real) o init (headless). pop() no
    // depende de la bandera isWriter_, así que el objeto creado con init puede
    // usarse para leer igualmente. Evita un writer temporal que, al destruirse,
    // desmapearía la memoria compartida y dejaría el lector colgando.
    {
        ba::ipc::Status rs = ba::ipc::SharedAudioRing::openReader(kGuestAudioRingName, guestRing_);
        if (!rs.ok) {
            rs = ba::ipc::SharedAudioRing::init(kGuestAudioRingName, ba::ipc::audio_ring::kDefaultChannels,
                                                ba::ipc::audio_ring::kDefaultCapacityBlocks, currentPid(), 1u,
                                                guestRing_);
        }
        if (!rs.ok) {
            lastError_ = rs.code;
        }
    }

    // ControlEndpoint sobre el loopback (rol launcher/cliente, spec §4.1).
    // En la app real (Qt/libobs) se respaldaría sobre el transporte real
    // (named pipe/socket hacia el gateway); aquí el loopback lo hace testeable.
    control_ = std::make_unique<ba::ipc::ControlEndpoint>(loopback_, ba::ipc::ControlEndpoint::kLauncher);
    control_->onDisconnect([]() {
        // Reconexión (spec §4.1): en headless el loopback no se corta, así que
        // solo se registra el evento.
    });

    // --- Persistencia (spec §11) --------------------------------------------
    // Los snapshots viven en <configDir>/Config/session (spec §5.1). Si
    // configDir es vacío se usa el directorio actual.
    std::string dir = configDir;
    if (dir.empty()) {
        dir = ".";
    }
    const std::string sessionDir = dir + "/Config/session";
    ba::persist::Status ss = store_.init(sessionDir);
    if (!ss.ok) {
        lastError_ = ss.code;
    }

    // --- Restauración (spec §3.1.8/§11.3) -----------------------------------
    // Arranque con `--restore` (A relanzado tras crash o tras un corte de
    // energía): recupera el último snapshot válido y re-aplica su estado DSP.
    // `Restore::recover` es stateless y no lanza: si no hay snapshots (primer
    // arranque) o todos están corruptos, devuelve Missing y se degrada al
    // estado de fábrica sin fallar el init.
    if (restore && ss.ok) {
        ba::persist::StateDoc restored;
        const ba::persist::RestoreResult rr = ba::persist::Restore::recover(sessionDir, restored);
        if (rr.status == ba::persist::RestoreResult::Status::Ok) {
            applyRestoredPresets(restored);
            // Continúa la secuencia monotónica desde la del snapshot: el
            // próximo buildStateDoc() escribe restored.sequence + 1, así
            // Restore::recover (que elige por mayor secuencia) siempre prefiere
            // la generación recién escrita sobre la restaurada.
            sequence_ = restored.sequence + 1;
            lastDoc_ = restored;
        }
        // Missing/Corrupt/ChecksumMismatch: se degrada (no hay nada bueno que
        // recuperar). No es un fallo del init: el motor arranca con fábrica.
    }

    initialized_ = (lastError_ == 0);
    return initialized_;
}

// ============================================================================
// applyPreset
// ============================================================================

ba::dsp::StripParams OperatorEngine::stripParamsFor(ChannelPreset preset) const {
    switch (preset) {
        case ChannelPreset::kPodium: return ba::dsp::presetPodium();
        case ChannelPreset::kLavalier: return ba::dsp::presetLavalier();
        case ChannelPreset::kHandheld: return ba::dsp::presetHandheld();
    }
    return ba::dsp::presetPodium();
}

bool OperatorEngine::applyPreset(int channel, ChannelPreset preset, double smoothMs) {
    if (channel < 0 || channel >= kNumChannels) {
        return false;
    }
    const int p = static_cast<int>(preset);
    if (p < static_cast<int>(ChannelPreset::kPodium) || p > static_cast<int>(ChannelPreset::kHandheld)) {
        return false;
    }
    // Cambio en caliente: setParams con suavizado de ganancia (evita clicks).
    // El contrato es que no se solape con process() del mismo strip: aquí ambos
    // corren en el thread de control, así que no se solapan.
    strips_[channel].setParams(stripParamsFor(preset), smoothMs);
    presets_[channel] = preset;
    // Persistencia no crítica (debounce): se escribe en la próxima oportunidad.
    // `sequence_` es la secuencia monotónica que SnapshotStore/Restore usan para
    // elegir la generación más nueva: se incrementa en cada cambio de estado
    // persistido, de modo que cada generación lleva un número creciente y
    // `Restore::recover` (mayor secuencia) siempre prefiere la recién escrita.
    if (initialized_) {
        ++sequence_;
        store_.scheduleWrite(buildStateDoc(), false);
    }
    return true;
}

// ============================================================================
// applyRestoredPresets
// ============================================================================

void OperatorEngine::applyRestoredPresets(const ba::persist::StateDoc& doc) {
    // Contraparte de lectura de buildStateDoc(): interpreta el blob JSON opaco
    // de la sección "dsp" ({"channels":[{channel,preset},...]}) y re-aplica el
    // preset guardado por canal. No lanza: ante un blob corrupto o ausente se
    // degrada al estado de fábrica (no hay nada bueno que restaurar).
    const auto it = doc.sections.find(kSectionDsp);
    if (it == doc.sections.end()) {
        return;
    }
    nlohmann::ordered_json dsp;
    try {
        dsp = nlohmann::ordered_json::parse(it->second);
    } catch (const nlohmann::json::exception&) {
        return; // blob no-JSON: se degrada a fábrica
    }
    const auto chIt = dsp.find("channels");
    if (chIt == dsp.end() || !chIt->is_array()) {
        return;
    }
    for (const auto& entry : *chIt) {
        if (!entry.is_object()) {
            continue;
        }
        const auto chIt2 = entry.find("channel");
        const auto prIt = entry.find("preset");
        if (chIt2 == entry.end() || !chIt2->is_number_integer()) {
            continue;
        }
        if (prIt == entry.end() || !prIt->is_string()) {
            continue;
        }
        const int ch = chIt2->get<int>();
        const std::string name = prIt->get<std::string>();
        ChannelPreset preset = ChannelPreset::kPodium;
        if (name == "lavalier") {
            preset = ChannelPreset::kLavalier;
        } else if (name == "handheld") {
            preset = ChannelPreset::kHandheld;
        }
        // applyPreset valida el rango de canal (fuera de rango es no-op) y, en
        // init() (initialized_ aún false), no persiste ni bump. Suavizado 0:
        // es una carga en frío, sin click.
        applyPreset(ch, preset, 0.0);
    }
}

// ============================================================================
// processAudio
// ============================================================================

void OperatorEngine::processAudio(float* interleaved, int frames, int channel) {
    if (!initialized_ || interleaved == nullptr || frames <= 0) {
        return;
    }
    if (channel < 0 || channel >= kNumChannels) {
        return;
    }

    // 1) Cadena del channel strip (HPF→EQ→de-esser→gate→comp→lim), in-place.
    strips_[channel].process(interleaved, frames);

    // 2) Nivel RMS del bloque (post-strip) -> Automixer (thread de audio).
    const int n = frames * 2; // estéreo interleaved
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        const float s = interleaved[i];
        acc += static_cast<double>(s) * static_cast<double>(s);
    }
    const float rms = static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
    lastLevel_[channel] = rms;
    automixer_.setLevel(channel, rms);

    // 3) Ganancia dinámica del automixer (Dugan) aplicada al bloque.
    const float gain = automixer_.gainFor(channel);
    for (int i = 0; i < n; ++i) {
        interleaved[i] *= gain;
    }
}

// ============================================================================
// processGuestBlock
// ============================================================================

bool OperatorEngine::processGuestBlock(float* out, bool& isSilence, bool& isFadeIn) {
    isSilence = false;
    isFadeIn = false;
    // header() != nullptr equivale a "region mapeada" (data() nulo -> header nulo).
    if (!initialized_ || out == nullptr || guestRing_.header() == nullptr) {
        return false;
    }
    // Lector: pop() copia el bloque en `out` y avanza readIndex; ante underrun
    // llena `out` de silencio+fade. El buffer es del llamador, asi que no hay
    // ventana en la que el escritor pueda pisar lo que acabamos de leer.
    ba::ipc::PoppedBlock b;
    if (!guestRing_.pop(out, b)) {
        std::fill_n(out, static_cast<std::ptrdiff_t>(guestRing_.blockFloats()), 0.0f);
        return false;
    }
    isSilence = b.isSilence;
    isFadeIn = b.isFadeIn;
    return true;
}

// ============================================================================
// tick
// ============================================================================

bool OperatorEngine::tick() {
    if (!initialized_) {
        return false;
    }

    // 1) Heartbeat de A en el HealthBlock (spec §4.4, cada tick de control).
    if (health_.valid()) {
        health_.beat(kAppHealthSlot);
    }

    // 2) Canal de control: poll() no bloqueante + despacho.
    // Solo se hace `poll()` cuando el loopback tiene bytes pendientes: el
    // loopback nunca "se cierra", así que un `read()` sobre un buffer vacío
    // devuelve 0 (sin datos por ahora), no un EOF real. Si `poll()` leyera ese
    // 0 con su buffer de recepción vacío, `ControlEndpoint` lo trataría como
    // fin de stream y marcaría el endpoint como desconectado (falso positivo
    // que cortaría el canal de control tras el primer tick). Consultar
    // hasPending() antes evita la desconexión fantasma y deja que el frame
    // pendiente (p. ej. un ack propio) se decodifique cuando llega.
    if (control_ != nullptr && control_->connected() && loopback_.hasPending()) {
        ba::ipc::Message m;
        if (control_->poll(m)) {
            dispatchMessage(m);
            return true;
        }
    }
    return false;
}

void OperatorEngine::dispatchMessage(const ba::ipc::Message& m) {
    // Respuesta mínima (request/response, spec §4.1): a cualquier mensaje se
    // contesta con un Response que conserva el id de la petición. Se usa
    // sendRequest() (que asigna id fresco) para un mensaje de control de
    // "ack"; el caso de respuesta con id idéntico se cubre en la app real
    // sobre el transporte real. Aquí se ejercita el camino sendRequest/poll.
    ba::ipc::Message ack;
    ack.protocolVersion = m.protocolVersion;
    ack.type = ba::ipc::Type::Response;
    ack.payload = nlohmann::json{{"ackFor", m.type}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    control_->sendRequest(ack);
    // El ack queda en el buffer del loopback. Lo despacha el próximo tick()
    // (su hasPending() será true y poll() lo decodificará): así el round-trip
    // sendRequest -> poll queda ejercitado sin que el loopback vacío marque un
    // EOF falso (ver tick()).
}

// ============================================================================
// watchedIsStale
// ============================================================================

bool OperatorEngine::watchedIsStale() const {
    if (!health_.valid()) {
        return false;
    }
    // isStale(slot, nowQpc, threshold): umbral por defecto 5 s (spec §3.1.7).
    return health_.isStale(kWatchedHealthSlot);
}

// ============================================================================
// buildStateDoc
// ============================================================================

ba::persist::StateDoc OperatorEngine::buildStateDoc() const {
    ba::persist::StateDoc doc;
    doc.schemaVersion = 1;
    doc.sequence = sequence_;
    doc.cleanShutdown = false;

    // Sección "dsp": presets por canal + parámetros del automixer.
    nlohmann::json dsp;
    nlohmann::json channels = nlohmann::json::array();
    for (int ch = 0; ch < kNumChannels; ++ch) {
        channels.push_back(nlohmann::json{
            {"channel", ch},
            {"preset", presetName(presets_[ch])},
        });
    }
    dsp["channels"] = std::move(channels);
    doc.sections[kSectionDsp] = dsp.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    // Sección "ipc": estado de los flujos IPC (ring abierto, control conectado,
    // watchdog).
    nlohmann::json ipc;
    ipc["guestRingOpen"] = (guestRing_.header() != nullptr);
    ipc["controlConnected"] = (control_ != nullptr && control_->connected());
    ipc["watchedStale"] = watchedIsStale();
    doc.sections[kSectionIpc] = ipc.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

    return doc;
}

// ============================================================================
// shutdown
// ============================================================================

bool OperatorEngine::shutdown() {
    if (shutDown_) {
        return initialized_;
    }
    shutDown_ = true;
    if (!initialized_) {
        return false;
    }

    // Cierre ordenado (spec §11.2): fija cleanShutdown=true, escribe de forma
    // inmediata y flushNow para que la última generación quede en disco.
    ba::persist::StateDoc doc = buildStateDoc();
    doc.cleanShutdown = true;
    store_.scheduleWrite(doc, /*immediate=*/true);
    store_.flushNow();

    // Publica Terminated en el HealthBlock (transición de cierre, spec §4.4).
    if (health_.valid()) {
        health_.setState(kAppHealthSlot, ba::ipc::ProcessState::Terminated);
    }
    return true;
}

// ============================================================================
// Consultas para la UI / tests
// ============================================================================

ChannelPreset OperatorEngine::presetFor(int channel) const {
    if (channel < 0 || channel >= kNumChannels) {
        return ChannelPreset::kPodium;
    }
    return presets_[channel];
}

float OperatorEngine::levelFor(int channel) const {
    if (channel < 0 || channel >= kNumChannels) {
        return 0.0f;
    }
    return lastLevel_[channel];
}

float OperatorEngine::automixerGainFor(int channel) const { return automixer_.gainFor(channel); }

bool OperatorEngine::guestRingOpen() const { return guestRing_.header() != nullptr; }

bool OperatorEngine::controlConnected() const { return control_ != nullptr && control_->connected(); }

} // namespace ba::app
