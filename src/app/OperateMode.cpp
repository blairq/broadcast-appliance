// ============================================================================
// src/app/OperateMode.cpp — ver OperateMode.hpp
// ============================================================================

#include "OperateMode.hpp"

#include <obs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "diag/Registro.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/LocalSocket.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"
#include "security/CredentialStore.hpp"

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kHealthSlot = 1; // A (spec §4.4; B es 0)
constexpr std::uint32_t kProcessVersion = 2;
constexpr int kTickMs = 100;

std::atomic<bool> g_stop{false};

extern "C" void onStopSignal(int) { g_stop.store(true); }

std::uint32_t currentPid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string argValue(int argc, char** argv, const char* key, const std::string& def) {
    const std::string prefix = std::string(key) + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind(prefix, 0) == 0) {
            return a.substr(prefix.size());
        }
    }
    return def;
}

bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

void applyCli(AppConfig& c, int argc, char** argv, const fs::path& root) {
    const std::string enc = argValue(argc, argv, "--encoder", "");
    if (!enc.empty()) {
        c.encoder.videoEncoder = enc;
    }
    const std::string br = argValue(argc, argv, "--bitrate", "");
    if (!br.empty()) {
        c.encoder.videoBitrateKbps = std::atoi(br.c_str());
    }
    const std::string res = argValue(argc, argv, "--resolution", "");
    const std::size_t x = res.find('x');
    if (x != std::string::npos) {
        c.video.baseWidth = c.video.outputWidth = std::atoi(res.substr(0, x).c_str());
        c.video.baseHeight = c.video.outputHeight = std::atoi(res.substr(x + 1).c_str());
    }
    const std::string fps = argValue(argc, argv, "--fps", "");
    if (!fps.empty()) {
        fpsDeTexto(fps, c.video.fpsNum, c.video.fpsDen);
    }
    if (hasFlag(argc, argv, "--record")) {
        c.record = true;
    }
    if (hasFlag(argc, argv, "--no-record")) {
        c.record = false;
    }
    const std::string recDir = argValue(argc, argv, "--rec-dir", "");
    if (!recDir.empty()) {
        const fs::path p = recDir;
        c.rec.directory = (p.is_absolute() ? p : root / p).string();
    }
    const std::string server = argValue(argc, argv, "--stream-server", "");
    if (!server.empty()) {
        c.stream = true;
        c.streamCfg.server = server;
    }
    const std::string guests = argValue(argc, argv, "--guests", "");
    if (!guests.empty()) {
        c.guests = std::atoi(guests.c_str());
    }
    c.durationSec = std::atoi(argValue(argc, argv, "--duration", "0").c_str());
    c.statusEverySec = std::atoi(argValue(argc, argv, "--status-every", "5").c_str());
    c.restore = hasFlag(argc, argv, "--restore");
    c.safeMode = hasFlag(argc, argv, "--safe-mode");
    if (c.safeMode) {
        // Modo seguro (circuit breaker del launcher): lo minimo que funciona.
        //
        // NO se le tocan las escenas a la configuracion. Antes se les vaciaban
        // las fuentes ACA, en memoria, para arrancar sin ellas; el problema es
        // que despues cualquier guardado escribia ESA configuracion vacia
        // encima del app.json del operador y le borraba el trabajo de verdad.
        // Paso: doce escenas con setenta y ocho fuentes quedaron en cero, y se
        // recuperaron de la copia .antes-del-modo-seguro de milagro.
        //
        // Ahora la configuracion queda INTACTA y lo unico que cambia es que al
        // armar las escenas no se crean las fuentes (ver `startEngine`). Asi el
        // modo seguro es lo que tiene que ser: una forma de arrancar, no una
        // forma de perder datos.
        c.encoder.videoEncoder = "x264";
    }
    if (c.guests < 0) {
        c.guests = 0;
    }
    if (c.guests > 4) {
        c.guests = 4;
    }
}

void printStatus(const BroadcastEngine& eng) {
    const EngineStatus s = eng.status();
    std::printf("ba-app: fps=%.1f enc=%s grabando=%s transmitiendo=%s perdidos=%d/%d enviado=%lluKB\n", s.fps,
                s.videoEncoder.empty() ? "-" : s.videoEncoder.c_str(), s.recording ? "si" : "no",
                s.streaming ? "si" : "no", s.droppedFrames, s.totalFrames,
                static_cast<unsigned long long>(s.bytesSent / 1024));
    std::fflush(stdout);
}

} // namespace

// ============================================================================
// AppRuntime
// ============================================================================

void AppRuntime::requestStop() { g_stop.store(true); }

bool AppRuntime::stopRequested() { return g_stop.load(); }

std::uint32_t AppRuntime::pid() const { return currentPid(); }

void AppRuntime::configure(int argc, char** argv) {
    // Binarios de la raiz del bundle; datos del disco real (BA_DATA_ROOT).
    bin_ = ba::platform::bundleRoot();
    root_ = ba::platform::dataRoot();
    if (bin_.empty()) {
        bin_ = fs::current_path();
    }
    if (root_.empty()) {
        root_ = bin_;
    }
    const fs::path cfgPath = argValue(argc, argv, "--config", appConfigPath(root_).string());
    cfg_ = loadAppConfig(cfgPath, root_);
    gateway_ = loadGatewaySettings(gatewayConfigPath(root_));
    launcher_ = loadLauncherSettings(launcherConfigPath(root_));
    applyCli(cfg_, argc, argv, root_);
}

SourceSpec* AppRuntime::findSpec(const std::string& scene, const std::string& name) {
    SceneConfig* sc = findScene(cfg_, scene);
    if (sc == nullptr) {
        return nullptr;
    }
    for (auto& s : sc->sources) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void AppRuntime::addAudioInputs() {
    // Microfonos y lineas del equipo: canal global, como el audio de los
    // invitados (suenan en todas las escenas, no son items de escena).
    for (const auto& spec : cfg_.audioInputs) {
        if (!engine_.addGlobalAudio(spec))
            std::printf("ba-app: entrada de audio '%s' omitida: %s\n", spec.name.c_str(),
                        engine_.lastError().c_str());
    }
}

bool AppRuntime::addAudioInput(const SourceSpec& spec) {
    if (!engine_.addGlobalAudio(spec)) {
        lastError_ = engine_.lastError();
        return false;
    }
    const auto it = std::find_if(cfg_.audioInputs.begin(), cfg_.audioInputs.end(),
                                 [&](const SourceSpec& s) { return s.name == spec.name; });
    if (it == cfg_.audioInputs.end())
        cfg_.audioInputs.push_back(spec);
    else
        *it = spec;
    return true;
}

bool AppRuntime::removeAudioInput(const std::string& name) {
    const bool ok = engine_.removeGlobalAudio(name);
    cfg_.audioInputs.erase(std::remove_if(cfg_.audioInputs.begin(), cfg_.audioInputs.end(),
                                          [&](const SourceSpec& s) { return s.name == name; }),
                           cfg_.audioInputs.end());
    return ok;
}

void AppRuntime::ajustarInvitados(int n) {
    const int antes = cfg_.guests;
    cfg_.guests = n;
    gateway_.guests = n;
    if (cfg_.guestAudio)
        for (int slot = antes + 1; slot <= n; ++slot)
            if (!engine_.addGuestAudio(slot))
                std::printf("ba-app: invitado %d (audio) omitido: %s\n", slot, engine_.lastError().c_str());
    save();
}

void AppRuntime::addGuests() {
    // Audio de invitados: global (canal propio), siempre en la mezcla.
    for (int slot = 1; slot <= cfg_.guests; ++slot) {
        if (cfg_.guestAudio && !engine_.addGuestAudio(slot)) {
            std::printf("ba-app: invitado %d (audio) omitido: %s\n", slot, engine_.lastError().c_str());
        }
    }
    // Video: si ninguna escena tiene invitados (config nueva o vieja), se
    // agregan a la primera escena en fila; despues el operador los mueve.
    bool any = false;
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            any = any || s.id == "ba_guest_video";
        }
    }
    if (!any && cfg_.guestVideo && !cfg_.scenes.empty()) {
        for (int slot = 1; slot <= cfg_.guests; ++slot) {
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot);
            spec.settingsJson = "{\"slot\":" + std::to_string(slot) + "}";
            spec.rect = guestRect(slot, cfg_.guests);
            cfg_.scenes.front().sources.push_back(spec);
        }
    }
    // R3 (docs/SESION-REMOTA.md): el legislador remoto con la palabra, como una
    // entrada más. El audio es global (suena en cualquier escena, con su rack en
    // Audio); el video va oculto a pantalla completa en la primera escena, como la
    // pantalla de un invitado: el operador lo muestra (o lo usa en otra escena).
    {
        SourceSpec a;
        a.id = "ba_guest_audio";
        a.name = "Remoto con la palabra (audio)";
        a.settingsJson = R"({"slot":1,"channel":"remoto"})";
        if (!engine_.addGlobalAudio(a))
            std::printf("ba-app: el audio del remoto con la palabra no entró: %s\n", engine_.lastError().c_str());
        bool hayVideo = false;
        for (const auto& sc : cfg_.scenes)
            for (const auto& s : sc.sources)
                hayVideo = hayVideo || (s.id == "ba_guest_video" && s.settingsJson.find("remoto") != std::string::npos);
        if (!hayVideo && !cfg_.scenes.empty()) {
            SourceSpec v;
            v.id = "ba_guest_video";
            v.name = "Remoto con la palabra";
            v.settingsJson = R"({"slot":1,"channel":"remoto"})";
            v.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            v.visible = false;
            cfg_.scenes.front().sources.push_back(v);
        }
    }
    // Pantalla o ventana que comparte cada invitado: a pantalla completa y
    // OCULTA; el operador la muestra cuando la quiere al aire.
    bool anyShare = false;
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            anyShare =
                anyShare || (s.id == "ba_guest_video" && s.settingsJson.find("share") != std::string::npos);
        }
    }
    if (!anyShare && cfg_.guestVideo && !cfg_.scenes.empty()) {
        for (int slot = 1; slot <= cfg_.guests; ++slot) {
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot) + " — pantalla";
            spec.settingsJson = "{\"slot\":" + std::to_string(slot) + ",\"channel\":\"share\"}";
            spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            spec.visible = false;
            cfg_.scenes.front().sources.push_back(spec);
        }
    }
}

int AppRuntime::start() {
    // --- Salud: A late en su slot para el watchdog del launcher. Sin launcher
    // (desarrollo) se crea el bloque para poder correr solo.
    if (!ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_).ok) {
        ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, kHealthSlot + 1, health_);
    }
    if (health_.valid()) {
        health_.setIdentity(kHealthSlot, currentPid(), kProcessVersion);
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Running);
        health_.beat(kHealthSlot);
    }

    EnginePaths paths;
    paths.moduleBinDir = (bin_ / "obs-plugins" / "64bit").string();
    paths.moduleDataDir = (bin_ / "data" / "obs-plugins").string();
    paths.obsDataDir = (bin_ / "data" / "libobs").string();
    paths.configDir = (root_ / "Config" / "plugin_config").string();

    std::printf("ba-app: arrancando motor %dx%d@%d, codificador=%s%s\n", cfg_.video.baseWidth,
                cfg_.video.baseHeight, cfg_.video.fpsNum, cfg_.encoder.videoEncoder.c_str(),
                cfg_.safeMode ? " (modo seguro)" : "");
    if (!engine_.startup(paths, cfg_.video, AudioConfig{})) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: el motor no arranco: %s\n", lastError_.c_str());
        if (health_.valid()) {
            health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
        }
        if (lastError_.find("obs_reset_video") != std::string::npos ||
            lastError_.find("sin sesion grafica") != std::string::npos) {
            std::printf("ba-app: sin contexto grafico (GL/D3D). En Linux hace falta una sesion grafica: "
                        "DISPLAY o WAYLAND_DISPLAY. Por SSH no hay.\n");
            return ba::platform::exit_code::kEnvironment;
        }
        return 2;
    }
    health_.beat(kHealthSlot);
    started_ = true;
    {
        std::ifstream f(root_ / "Config" / "loudness.json");
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_object()) engine_.loudness().ponerObjetivo(j);
    }
    // El latido que mira los cuadros del motor (hilo propio): una traba de la
    // interfaz ya no hace que el supervisor corte el aire.
    if (health_.valid())
        latido_.iniciar(
            root_,
            [this] {
                std::lock_guard<std::mutex> l(healthMu_);
                health_.beat(kHealthSlot);
            },
            []() -> std::uint64_t {
                return obs_get_video() != nullptr ? video_output_get_total_frames(obs_get_video()) : 0;
            });

    addGuests();
    addAudioInputs();
    // Lo que oye cada invitado (mix-minus de fábrica): Config/retornos.json.
    {
        std::ifstream f(root_ / "Config" / "retornos.json");
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_object())
            for (const auto& [slot, modo] : j.items())
                if (modo.is_string()) engine_.setRetornoInvitado(std::atoi(slot.c_str()), modo.get<std::string>());
    }
    engine_.setTransition(cfg_.transition.type, cfg_.transition.durationMs);
    for (const auto& sc : cfg_.scenes) {
        engine_.createScene(sc.name);
        // En modo seguro las escenas se crean VACIAS: si una fuente es la que
        // venia tumbando el arranque, asi no se la toca. La configuracion no
        // se modifica, o sea que al arrancar normal vuelven todas solas.
        if (cfg_.safeMode) continue;
        for (const auto& src : sc.sources) {
            // Una fuente rota (camara desenchufada, id mal escrito) no tumba
            // el proceso: se avisa y la emision sigue con el resto.
            if (!engine_.addSource(sc.name, src)) {
                std::printf("ba-app: fuente '%s' omitida: %s\n", src.name.c_str(),
                            engine_.lastError().c_str());
            }
        }
    }
    engine_.setCurrentScene(cfg_.currentScene);
    if (cfg_.returnFeeds && !engine_.publishReturnFeeds()) {
        std::printf("ba-app: sin retorno hacia B: %s\n", engine_.lastError().c_str());
    }
    // Lobby: la voz de quien espera (monitoreo) y lo que ve del operador.
    operator_.startLobbyMonitor(gateway_.lobbySlots);
    if (!operator_.configure(engine_, cfg_.operatorVideo, cfg_.operatorAudio)) {
        std::printf("ba-app: feed del operador incompleto (fuente inexistente)\n");
    }

    std::printf("ba-app: codificadores candidatos:");
    for (const auto& id : engine_.videoEncoderCandidates(cfg_.encoder)) {
        std::printf(" %s", id.c_str());
    }
    std::printf("\n");

    if (cfg_.replaySegundos > 0) aplicarReplay();
    if (cfg_.record) {
        startRecording();
    }
    if (cfg_.stream) {
        startStreaming();
    }
    if (cfg_.restore) {
        restoreOutputs();
    } else {
        writeOnAirState(); // arranque normal: no hereda el estado de otra corrida
    }

    std::string err;
    if (!control_.start("control.app", &err)) {
        std::printf("ba-app: sin canal de control (%s)\n", err.c_str());
    }
    std::printf("ba-app: en marcha (pid %u)\n", currentPid());
    return 0;
}

void AppRuntime::tick() {
    control_.poll([&](const ba::ipc::Message& m) {
        if (m.type == ba::ipc::Type::Shutdown) {
            std::printf("ba-app: cierre pedido por el launcher\n");
            requestStop();
        }
    });
    if (health_.valid()) {
        std::lock_guard<std::mutex> l(healthMu_);
        health_.beat(kHealthSlot);
    }
    if (started_) {
        const EngineStatus st = engine_.status();
        latido_.uiTick(st.streaming || st.recording);
        // Un codificador que se cayó en el aire se reemplaza y se rearranca lo que corría.
        engine_.atenderEncoderCaido(!streamStopWanted_, !recordStopWanted_);
    }
    engine_.aplicarRecortesPendientes(); // los de cámaras que recién tienen tamaño
}

bool AppRuntime::setOperatorFeed(const std::string& videoSource, const std::string& audioSource) {
    const bool ok = operator_.configure(engine_, videoSource, audioSource);
    cfg_.operatorVideo = videoSource;
    cfg_.operatorAudio = audioSource;
    if (!ok) lastError_ = "no existe la fuente elegida para el operador";
    return ok;
}

bool AppRuntime::gatewayAlive() const {
    return health_.valid() && health_.read(0).pid != 0 && !health_.isStale(0);
}

void AppRuntime::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    // Cierre ordenado: nada que retomar en el proximo arranque.
    streamStopWanted_ = recordStopWanted_ = true;
    writeOnAirState();
    // Las grabaciones por entrada, ANTES que el resto: cada una tiene su vista
    // de render y su archivo abierto, y si el cierre las deja vivas el archivo
    // queda sin cerrar (o sea, sin indice y a veces sin poder abrirse).
    engine_.stopAllSourceRecordings();
    engine_.stopCleanFeed();
    latido_.detener(); // antes de apagar libobs (lee sus cuadros)
    std::printf("ba-app: cerrando (salidas, escena, libobs)\n");
    control_.stop();
    operator_.shutdown(); // antes de apagar libobs: suelta vista y fuentes
    engine_.shutdown();
    if (health_.valid()) {
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
    }
    std::printf("ba-app: cierre ordenado\n");
}

bool AppRuntime::startRecording() {
    std::error_code ec;
    fs::create_directories(cfg_.rec.directory, ec);
    const EngineStatus antes = engine_.status();
    if (!engine_.startRecording(cfg_.rec, cfg_.encoder)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: la grabacion no arranco: %s\n", lastError_.c_str());
        return false;
    }
    std::printf("ba-app: grabando en %s\n", cfg_.rec.directory.c_str());
    // El integrado del loudness arranca con la sesión (si no había nada saliendo).
    if (!antes.streaming && !antes.recording) engine_.loudness().reiniciar();
    recordStopWanted_ = false;
    writeOnAirState();
    return true;
}

bool AppRuntime::startSourceRecording(const std::string& source) {
    std::error_code ec;
    // Las individuales van a su propia carpeta: en Recordings/ ya estan las del
    // programa, y mezclar veinte archivos de entradas con las del evento hace
    // que despues nadie encuentre nada.
    const std::string dir = cfg_.rec.directory + "/entradas";
    fs::create_directories(dir, ec);
    if (!engine_.startSourceRecording(source, dir, cfg_.encoder)) {
        lastError_ = engine_.lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               "grabando la entrada \"" + source + "\" aparte");
    return true;
}

bool AppRuntime::stopSourceRecording(const std::string& source) {
    if (!engine_.stopSourceRecording(source)) {
        lastError_ = engine_.lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               "para la grabacion aparte de \"" + source + "\"");
    return true;
}

void AppRuntime::stopAllSourceRecordings() { engine_.stopAllSourceRecordings(); }

void AppRuntime::stopRecording() {
    recordStopWanted_ = true;
    writeOnAirState();
    engine_.stopRecording();
}

namespace {
// Ultima salida al aire (servidor + clave), cifrada en Credenciales/: evita
// tipear la clave cada vez y permite retomar la emision tras una caida.
constexpr const char* kStreamCredential = "stream-last";

bool readLastStream(std::string& server, std::string& key) {
    std::string raw;
    if (!ba::security::CredentialStore::bundle().read(kStreamCredential, raw)) {
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (!j.is_object()) {
        return false;
    }
    server = j.value("server", "");
    key = j.value("key", "");
    return !key.empty();
}
} // namespace

bool AppRuntime::streamKeySaved() const {
    if (keySaved_ < 0) { // descifrar cuesta: se consulta una vez
        std::string server;
        std::string key;
        keySaved_ = readLastStream(server, key) ? 1 : 0;
    }
    return keySaved_ == 1;
}

void AppRuntime::writeOnAirState() const {
    const nlohmann::json j{{"streaming", !streamStopWanted_}, {"recording", !recordStopWanted_}};
    std::ofstream out(ba::platform::stateDir() / "al-aire.json", std::ios::trunc);
    out << j.dump();
}

void AppRuntime::restoreOutputs() {
    std::ifstream in(ba::platform::stateDir() / "al-aire.json");
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) {
        return;
    }
    if (j.value("recording", false) && !engine_.status().recording) {
        std::printf("ba-app: --restore: se retoma la grabacion\n");
        startRecording();
    }
    if (j.value("streaming", false) && !engine_.status().streaming) {
        std::printf("ba-app: --restore: se retoma la transmision\n");
        if (!startStreaming()) {
            std::printf("ba-app: --restore: la transmision no se pudo retomar: %s\n", lastError_.c_str());
        }
    }
}

bool AppRuntime::startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente) {
    StreamConfig sc = cfg_.streamCfg;
    if (!server.empty()) {
        sc.server = server;
    }
    if (!key.empty()) {
        sc.key = key;
    } else {
        const char* envKey = std::getenv(cfg_.streamKeyEnv.c_str());
        sc.key = envKey != nullptr ? envKey : "";
        if (sc.key.empty()) {
            // Sin clave tipeada ni en el entorno: la de la ultima salida al aire.
            std::string lastServer;
            if (readLastStream(lastServer, sc.key) && server.empty() && !lastServer.empty()) {
                sc.server = lastServer;
            }
        }
    }
    if (sc.server.empty()) {
        lastError_ = "no hay servidor de transmision configurado";
        return false;
    }
    const EngineStatus antes = engine_.status();
    if (!engine_.startStreaming(sc, cfg_.encoder, cambiarEnCaliente)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: la transmision no arranco: %s\n", lastError_.c_str());
        return false;
    }
    std::printf("ba-app: transmitiendo a %s\n", sc.server.c_str());
    if (!antes.streaming && !antes.recording) engine_.loudness().reiniciar();
    streamStopWanted_ = false;
    if (!sc.key.empty()) {
        ba::security::CredentialStore::bundle().write(
            kStreamCredential, nlohmann::json{{"server", sc.server}, {"key", sc.key}}.dump());
        keySaved_ = 1;
    }
    writeOnAirState();
    return true;
}

void AppRuntime::stopStreaming() {
    streamStopWanted_ = true;
    writeOnAirState();
    engine_.stopStreaming();
}

namespace {
nlohmann::json leerDestinos(const std::filesystem::path& root) {
    std::ifstream f(root / "Config" / "destinos.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    return j.is_array() ? j : nlohmann::json::array();
}
void escribirDestinos(const std::filesystem::path& root, const nlohmann::json& d) {
    std::error_code ec;
    std::filesystem::create_directories(root / "Config", ec);
    std::ofstream o(root / "Config" / "destinos.json");
    o << d.dump(1);
}
} // namespace

nlohmann::json AppRuntime::destinos() const {
    const nlohmann::json est = engine_.extrasEstado();
    nlohmann::json out = nlohmann::json::array();
    for (nlohmann::json d : leerDestinos(root_)) {
        const std::string id = d.value("id", std::string());
        d["tiene_clave"] = ba::security::CredentialStore::bundle().exists("stream-" + id);
        const nlohmann::json e = est.value(id, nlohmann::json::object());
        d["activo"] = e.value("activo", false);
        d["reconectando"] = e.value("reconectando", false);
        d["perdidos"] = e.value("perdidos", 0);
        d["cuadros"] = e.value("cuadros", 0);
        d["bytes"] = e.value("bytes", 0);
        out.push_back(d);
    }
    return out;
}

std::string AppRuntime::guardarDestino(const nlohmann::json& d, std::string* idOut) {
    const std::string url = d.value("url", std::string());
    nlohmann::json lista = leerDestinos(root_);
    std::string id = d.value("id", std::string());
    nlohmann::json* existente = nullptr;
    for (auto& x : lista)
        if (!id.empty() && x.value("id", std::string()) == id) existente = &x;
    if (existente == nullptr) {
        if (url.rfind("rtmp://", 0) != 0 && url.rfind("rtmps://", 0) != 0 && url.rfind("srt://", 0) != 0 &&
            url.rfind("rist://", 0) != 0)
            return "`url`: rtmp://, rtmps://, srt:// o rist://";
        if (id.empty()) {
            int n = 1;
            const auto usado = [&](const std::string& c) {
                for (const auto& x : lista)
                    if (x.value("id", std::string()) == c) return true;
                return false;
            };
            while (usado("destino-" + std::to_string(n))) ++n;
            id = "destino-" + std::to_string(n);
        }
        lista.push_back({{"id", id}, {"nombre", d.value("nombre", id)}, {"url", url}, {"respaldo", d.value("respaldo", false)}});
        existente = &lista.back();
    } else {
        for (const char* k : {"nombre", "url", "respaldo"})
            if (d.contains(k)) (*existente)[k] = d[k];
    }
    // SRT: el modo (caller/listener/rendezvous) y la latencia van en la URL, que es
    // como los lee el muxer (ffmpeg: latency en microsegundos). La passphrase es la clave.
    if (d.contains("modo") || d.contains("latencia_ms")) {
        const std::string actual = (*existente).value("url", std::string());
        if (actual.rfind("srt://", 0) != 0) return "`modo` y `latencia_ms` son solo para srt://";
        const auto q = actual.find('?');
        std::string u = actual.substr(0, q);
        std::vector<std::pair<std::string, std::string>> params;
        if (q != std::string::npos) {
            std::stringstream ss(actual.substr(q + 1));
            for (std::string kv; std::getline(ss, kv, '&');)
                if (!kv.empty()) {
                    const auto e = kv.find('=');
                    params.emplace_back(kv.substr(0, e), e == std::string::npos ? "" : kv.substr(e + 1));
                }
        }
        const auto poner = [&params](const std::string& k, const std::string& v) {
            for (auto& [pk, pv] : params)
                if (pk == k) return void(pv = v);
            params.emplace_back(k, v);
        };
        if (d.contains("modo")) {
            const std::string m = d.value("modo", std::string());
            if (m != "caller" && m != "listener" && m != "rendezvous") return "`modo`: caller, listener o rendezvous";
            poner("mode", m);
        }
        if (d.contains("latencia_ms")) {
            const int ms = d.value("latencia_ms", 0);
            if (ms < 20 || ms > 8000) return "`latencia_ms`: entre 20 y 8000";
            poner("latency", std::to_string(ms * 1000));
        }
        for (std::size_t i = 0; i < params.size(); ++i)
            u += (i == 0 ? "?" : "&") + params[i].first + (params[i].second.empty() ? "" : "=" + params[i].second);
        (*existente)["url"] = u;
    }
    if (d.contains("clave") && d["clave"].is_string() && !d["clave"].get<std::string>().empty())
        ba::security::CredentialStore::bundle().write("stream-" + id, d["clave"].get<std::string>());
    escribirDestinos(root_, lista);
    if (idOut != nullptr) *idOut = id;
    return {};
}

std::string AppRuntime::aplicarReplay() {
    const int s = cfg_.replaySegundos;
    if (!engine_.setReplay(s, cfg_.rec.directory + "/repeticiones", cfg_.encoder))
        return "Repetición: " + engine_.lastError();
    return s > 0 ? "Repetición: guarda los últimos " + std::to_string(s) + " s." : "Repetición apagada.";
}

std::string AppRuntime::iniciarSenalLimpia(const std::string& destino) {
    std::string url, clave, dir;
    if (!destino.empty()) {
        for (const auto& d : leerDestinos(root_))
            if (d.value("id", std::string()) == destino) url = d.value("url", std::string());
        if (url.empty()) return "no hay un destino «" + destino + "» (stream_destinos los lista)";
        ba::security::CredentialStore::bundle().read("stream-" + destino, clave);
    } else {
        std::error_code ec;
        dir = cfg_.rec.directory + "/limpia";
        fs::create_directories(dir, ec);
    }
    if (!engine_.startCleanFeed(url, clave, dir, cfg_.encoder)) return engine_.lastError();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               "señal limpia " + (destino.empty() ? std::string("grabando en ") + dir : "a " + destino));
    return {};
}

std::string AppRuntime::borrarDestino(const std::string& id) {
    nlohmann::json lista = leerDestinos(root_), nueva = nlohmann::json::array();
    for (const auto& x : lista)
        if (x.value("id", std::string()) != id) nueva.push_back(x);
    if (nueva.size() == lista.size()) return "no hay un destino «" + id + "»";
    engine_.stopExtra(id);
    ba::security::CredentialStore::bundle().remove("stream-" + id);
    escribirDestinos(root_, nueva);
    return {};
}

std::string AppRuntime::iniciarDestino(const std::string& id) {
    std::string errores;
    bool alguno = false;
    for (const auto& d : leerDestinos(root_)) {
        const std::string did = d.value("id", std::string());
        if (id != "todos" && did != id) continue;
        alguno = true;
        std::string clave;
        ba::security::CredentialStore::bundle().read("stream-" + did, clave);
        if (!engine_.startExtra(did, d.value("url", std::string()), clave, cfg_.encoder))
            errores += (errores.empty() ? "" : "; ") + did + ": " + engine_.lastError();
    }
    if (!alguno) return "no hay un destino «" + id + "» (stream_destinos los lista)";
    return errores;
}

nlohmann::json AppRuntime::discoGrabacion() const {
    std::error_code ec;
    fs::create_directories(cfg_.rec.directory, ec);
    const auto info = fs::space(cfg_.rec.directory, ec);
    const double gb = ec ? -1.0 : static_cast<double>(info.available) / 1e9;
    const int kbps = cfg_.encoder.videoBitrateKbps + cfg_.encoder.audioBitrateKbps;
    const double horas = gb >= 0 && kbps > 0 ? gb / (kbps * 1.05 * 3600.0 / 8.0 / 1e6) : -1.0;
    return {{"libre_gb", gb}, {"horas", horas}, {"kbps", kbps}, {"carpeta", cfg_.rec.directory}};
}

void AppRuntime::detenerDestino(const std::string& id) {
    if (id == "todos")
        engine_.stopExtras();
    else
        engine_.stopExtra(id);
}

// --- Escenas y fuentes ----------------------------------------------------------

bool AppRuntime::addScene(const std::string& name) {
    if (!engine_.createScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.scenes.push_back(SceneConfig{name, {}});
    return true;
}

bool AppRuntime::copyScene(const std::string& from, const std::string& to) {
    // `lastError_` es pegajoso: guarda lo ultimo que fallo, de cuando sea. Como
    // aca se lo usa DESPUES de haber salido bien —para contar que una fuente no
    // se pudo recrear— hay que limpiarlo al entrar, o la copia informa como
    // propio un error de otra operacion anterior.
    lastError_.clear();
    const SceneConfig* origen = nullptr;
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == from) origen = &s;
    if (origen == nullptr) {
        lastError_ = "no existe la escena \"" + from + "\"";
        return false;
    }
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == to) {
            lastError_ = "ya hay una escena llamada \"" + to + "\"";
            return false;
        }
    if (!engine_.createScene(to)) {
        lastError_ = engine_.lastError();
        return false;
    }
    // Se copia la CONFIGURACION, no las fuentes del motor: una fuente de
    // libobs pertenece a su escena, y compartirlas haria que mover algo en la
    // copia moviera el original. Recrearlas desde la especificacion da dos
    // escenas de verdad independientes, que es lo que uno espera al copiar.
    SceneConfig copia = *origen;
    copia.name = to;
    copia.protegida = false; // la copia se puede tocar: para eso se copio
    cfg_.scenes.push_back(copia);
    for (const SourceSpec& spec : origen->sources) {
        SourceSpec s = spec;
        if (!addSource(to, s)) {
            // Lo que no se pudo recrear se dice, pero no se deshace todo: una
            // escena copiada a medias se arregla a mano; una que desaparecio
            // sin explicacion, no.
            lastError_ =
                "la escena se copio, pero la fuente \"" + spec.name + "\" no se pudo recrear: " + lastError_;
        }
    }
    return true;
}

bool AppRuntime::setSceneProtected(const std::string& name, bool protegida) {
    for (SceneConfig& s : cfg_.scenes)
        if (s.name == name) {
            s.protegida = protegida;
            return true;
        }
    lastError_ = "no existe la escena \"" + name + "\"";
    return false;
}

bool AppRuntime::sceneProtected(const std::string& name) const {
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == name) return s.protegida;
    return false;
}

bool AppRuntime::removeScene(const std::string& name) {
    if (sceneProtected(name)) {
        lastError_ = "la escena \"" + name + "\" esta protegida: sacale la proteccion primero";
        return false;
    }
    if (!engine_.removeScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.scenes.erase(std::remove_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                                     [&](const SceneConfig& s) { return s.name == name; }),
                      cfg_.scenes.end());
    cfg_.currentScene = engine_.currentScene();
    return true;
}

bool AppRuntime::renameScene(const std::string& from, const std::string& to) {
    if (sceneProtected(from)) {
        lastError_ = "la escena \"" + from + "\" esta protegida: sacale la proteccion primero";
        return false;
    }
    if (!engine_.renameScene(from, to)) {
        lastError_ = engine_.lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, from)) {
        sc->name = to;
    }
    cfg_.currentScene = engine_.currentScene();
    return true;
}

std::string AppRuntime::sceneFolder(const std::string& name) const {
    const SceneConfig* sc = findScene(const_cast<AppConfig&>(cfg_), name);
    return sc != nullptr ? sc->folder : std::string();
}

bool AppRuntime::setSceneFolder(const std::string& name, const std::string& folder) {
    SceneConfig* sc = findScene(cfg_, name);
    if (sc == nullptr) {
        lastError_ = "no existe la escena '" + name + "'";
        return false;
    }
    sc->folder = folder;
    return true;
}

std::vector<std::string> AppRuntime::sceneFolders() const {
    std::vector<std::string> out;
    for (const auto& sc : cfg_.scenes)
        if (!sc.folder.empty() && std::find(out.begin(), out.end(), sc.folder) == out.end())
            out.push_back(sc.folder);
    return out;
}

std::vector<std::string> AppRuntime::orderedSceneNames() const {
    const std::vector<std::string> live = const_cast<AppRuntime*>(this)->engine_.sceneNames();
    std::vector<std::string> out;
    for (const auto& sc : cfg_.scenes)
        if (std::find(live.begin(), live.end(), sc.name) != live.end()) out.push_back(sc.name);
    for (const auto& n : live)
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    return out;
}

bool AppRuntime::moveScene(const std::string& name, const std::string& before) {
    if (name == before) return true;
    const auto it = std::find_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                                 [&](const SceneConfig& s) { return s.name == name; });
    if (it == cfg_.scenes.end()) {
        lastError_ = "no existe la escena '" + name + "'";
        return false;
    }
    SceneConfig moved = *it;
    cfg_.scenes.erase(it);
    auto dst = cfg_.scenes.end();
    if (!before.empty()) {
        dst = std::find_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                           [&](const SceneConfig& s) { return s.name == before; });
        if (dst != cfg_.scenes.end()) moved.folder = dst->folder; // cae en su misma carpeta
    }
    cfg_.scenes.insert(dst, std::move(moved));
    return true;
}

bool AppRuntime::setScene(const std::string& name) {
    if (!engine_.setCurrentScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.currentScene = name;
    return true;
}

bool AppRuntime::setSceneCon(const std::string& name, const std::string& tipo, int ms) {
    if (!engine_.setCurrentSceneCon(name, tipo, ms)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.currentScene = name;
    return true;
}

bool AppRuntime::setTransition(const std::string& type, int durationMs) {
    if (!engine_.setTransition(type, durationMs)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.transition = TransitionConfig{type, durationMs};
    return true;
}

bool AppRuntime::addSource(const std::string& scene, const SourceSpec& spec) {
    if (!engine_.addSource(scene, spec)) {
        lastError_ = engine_.lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        SourceSpec stored = spec;
        stored.settingsJson = engine_.sourceSettingsJson(spec.name);
        sc->sources.push_back(stored);
    }
    return true;
}

bool AppRuntime::removeSource(const std::string& scene, const std::string& name) {
    if (!engine_.removeSource(scene, name)) {
        lastError_ = "no existe la fuente '" + name + "' en '" + scene + "'";
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        sc->sources.erase(std::remove_if(sc->sources.begin(), sc->sources.end(),
                                         [&](const SourceSpec& s) { return s.name == name; }),
                          sc->sources.end());
    }
    return true;
}

bool AppRuntime::setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba,
                               float derecha, float abajo) {
    if (!engine_.setSourceCrop(scene, name, izquierda, arriba, derecha, abajo)) {
        lastError_ = "no se pudo recortar '" + name + "' en '" + scene + "' (¿recorte más grande que la imagen?)";
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->recorte[0] = izquierda;
        s->recorte[1] = arriba;
        s->recorte[2] = derecha;
        s->recorte[3] = abajo;
    }
    return true;
}

bool AppRuntime::moveSource(const std::string& scene, const std::string& name, int delta) {
    if (!engine_.moveSource(scene, name, delta)) {
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        auto& v = sc->sources;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].name == name) {
                const std::size_t j = delta > 0 ? i + 1 : (i == 0 ? 0 : i - 1);
                if (j < v.size() && j != i) {
                    std::swap(v[i], v[j]);
                }
                break;
            }
        }
    }
    return true;
}

bool AppRuntime::setSourceVisible(const std::string& scene, const std::string& name, bool visible) {
    if (!engine_.setSourceVisible(scene, name, visible)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->visible = visible;
    }
    return true;
}

bool AppRuntime::setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) {
    if (!engine_.setSourceRect(scene, name, rect)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->rect = rect;
    }
    return true;
}

bool AppRuntime::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    if (!engine_.updateSourceSettings(name, settingsJson)) {
        lastError_ = engine_.lastError();
        return false;
    }
    // La misma fuente puede estar en varias escenas: se sincronizan todas.
    const std::string current = engine_.sourceSettingsJson(name);
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.settingsJson = current;
            }
        }
    }
    return true;
}

bool AppRuntime::setVolume(const std::string& name, float mul) {
    if (!engine_.setSourceVolume(name, mul)) {
        return false;
    }
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.volume = mul;
            }
        }
    }
    return true;
}

bool AppRuntime::setMuted(const std::string& name, bool muted) {
    if (!engine_.setSourceMuted(name, muted)) {
        return false;
    }
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.muted = muted;
            }
        }
    }
    return true;
}

// --- Configuracion ----------------------------------------------------------------

bool AppRuntime::applyVideo(const VideoConfig& v) {
    if (!engine_.resetVideo(v)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.video = v;
    // Las ubicaciones son fracciones del lienzo: se recalculan en pixeles.
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            engine_.setSourceRect(sc.name, s.name, s.rect);
        }
    }
    return true;
}

bool AppRuntime::applyEncoder(const EncoderConfig& e) {
    if (!engine_.resetEncoders()) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.encoder = e;
    return true;
}

bool AppRuntime::save() {
    std::string err;
    // EN MODO SEGURO NO SE GUARDA. Es un estado degradado y transitorio: lo
    // que haya en memoria vale para esta corrida y nada mas. Escribirlo seria
    // hacer permanente una situacion de emergencia, y ya paso una vez
    // (escenas vaciadas). La copia de seguridad se deja igual, por si alguien
    // llega hasta aca con el app.json ya dañado de antes.
    if (cfg_.safeMode) {
        if (!safeBackupDone_) {
            safeBackupDone_ = true;
            std::error_code ec;
            std::filesystem::path backup = cfg_.configPath;
            backup += ".antes-del-modo-seguro";
            if (std::filesystem::exists(cfg_.configPath, ec) && !std::filesystem::exists(backup, ec)) {
                std::filesystem::copy_file(cfg_.configPath, backup, ec);
                std::printf("ba-app: modo seguro: copia de la configuracion en %s\n",
                            backup.string().c_str());
            }
        }
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "config", 0,
                                   "modo seguro: NO se guarda la configuracion. Lo que cambies "
                                   "ahora vale solo hasta que se cierre.");
        return true;
    }
    gateway_.guests = cfg_.guests; // B lee la cantidad de invitados de A
    if (!saveAppConfig(cfg_.configPath, cfg_, root_, &err) ||
        !saveGatewaySettings(gatewayConfigPath(root_), gateway_, &err) ||
        !saveLauncherSettings(launcherConfigPath(root_), launcher_, &err)) {
        lastError_ = err;
        return false;
    }
    return true;
}

bool AppRuntime::restartGateway() {
    // Cierre ordenado de B por su canal de control: el launcher lo relanza y
    // B relee gateway.json. Sin launcher, B simplemente se cierra.
    ba::platform::LocalStream s;
    std::string err;
    if (!ba::platform::LocalStream::connect("control.gateway", 1000, s, &err)) {
        lastError_ = "sin canal con el gateway: " + err;
        return false;
    }
    ba::ipc::ControlEndpoint ep(s, ba::ipc::ControlEndpoint::kLauncher);
    ba::ipc::Message m;
    m.type = ba::ipc::Type::Shutdown;
    m.payload = "{}";
    return ep.sendRequest(m);
}

CanvasRect guestRect(int slot, int count) {
    const float w = 1.0f / static_cast<float>(count > 0 ? count : 1);
    return CanvasRect{w * static_cast<float>(slot - 1), 0.62f, w, 0.36f};
}

// ============================================================================
// Modo consola
// ============================================================================

int runOperate(int argc, char** argv) {
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    AppRuntime rt;
    rt.configure(argc, argv);
    const int rc = rt.start();
    if (rc != 0) {
        return rc;
    }
    printStatus(rt.engine());

    const auto begin = std::chrono::steady_clock::now();
    auto lastStatus = begin;
    const AppConfig& cfg = rt.config();
    while (!AppRuntime::stopRequested()) {
        rt.tick();
        const auto now = std::chrono::steady_clock::now();
        if (cfg.durationSec > 0 && now - begin >= std::chrono::seconds(cfg.durationSec)) {
            break;
        }
        if (cfg.statusEverySec > 0 && now - lastStatus >= std::chrono::seconds(cfg.statusEverySec)) {
            printStatus(rt.engine());
            lastStatus = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }
    rt.stop();
    return 0;
}

} // namespace ba::app
