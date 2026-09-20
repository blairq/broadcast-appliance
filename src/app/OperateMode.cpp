// ============================================================================
// src/app/OperateMode.cpp — ver OperateMode.hpp
// ============================================================================

#include "OperateMode.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/LocalSocket.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"

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
        c.video.fpsNum = std::atoi(fps.c_str());
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
    c.safeMode = hasFlag(argc, argv, "--safe-mode");
    if (c.safeMode) {
        // Modo seguro (circuit breaker del launcher): lo minimo que funciona.
        c.encoder.videoEncoder = "x264";
        for (auto& sc : c.scenes) {
            sc.sources.clear();
        }
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

    addGuests();
    engine_.setTransition(cfg_.transition.type, cfg_.transition.durationMs);
    for (const auto& sc : cfg_.scenes) {
        engine_.createScene(sc.name);
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

    if (cfg_.record) {
        startRecording();
    }
    if (cfg_.stream) {
        startStreaming();
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
        health_.beat(kHealthSlot);
    }
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
    if (!engine_.startRecording(cfg_.rec, cfg_.encoder)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: la grabacion no arranco: %s\n", lastError_.c_str());
        return false;
    }
    std::printf("ba-app: grabando en %s\n", cfg_.rec.directory.c_str());
    return true;
}

void AppRuntime::stopRecording() { engine_.stopRecording(); }

bool AppRuntime::startStreaming(const std::string& server, const std::string& key) {
    StreamConfig sc = cfg_.streamCfg;
    if (!server.empty()) {
        sc.server = server;
    }
    if (!key.empty()) {
        sc.key = key;
    } else {
        const char* envKey = std::getenv(cfg_.streamKeyEnv.c_str());
        sc.key = envKey != nullptr ? envKey : "";
    }
    if (sc.server.empty()) {
        lastError_ = "no hay servidor de transmision configurado";
        return false;
    }
    if (!engine_.startStreaming(sc, cfg_.encoder)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: la transmision no arranco: %s\n", lastError_.c_str());
        return false;
    }
    std::printf("ba-app: transmitiendo a %s\n", sc.server.c_str());
    return true;
}

void AppRuntime::stopStreaming() { engine_.stopStreaming(); }

// --- Escenas y fuentes ----------------------------------------------------------

bool AppRuntime::addScene(const std::string& name) {
    if (!engine_.createScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.scenes.push_back(SceneConfig{name, {}});
    return true;
}

bool AppRuntime::removeScene(const std::string& name) {
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

bool AppRuntime::setScene(const std::string& name) {
    if (!engine_.setCurrentScene(name)) {
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
