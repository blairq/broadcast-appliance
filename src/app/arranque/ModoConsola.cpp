// ============================================================================
// src/app/arranque/ModoConsola.cpp — ver ModoConsola.hpp
//
// El modo consola (sin interfaz): la línea de comandos y el bucle que hace de dueño del
// motor (AppRuntime::tick cada ~100 ms). Lo que hace ba-app al aire está en AppRuntime y
// sus piezas; acá solo lo que es de la consola.
// ============================================================================

#include "arranque/ModoConsola.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "arranque/AppRuntime.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18nTexto.hpp"
#include "ipc/LugaresSala.hpp"

namespace ba::app {

namespace {

namespace fs = std::filesystem;

constexpr int kTickMs = 100;

extern "C" void onStopSignal(int) { AppRuntime::requestStop(); }

bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

void printStatus(const BroadcastEngine& eng) {
    const EngineStatus s = eng.status();
    char fps[32];
    std::snprintf(fps, sizeof fps, "%.1f", s.fps);
    const std::string si = ba::i18n::traducir("arranque", "si"), no = ba::i18n::traducir("arranque", "no");
    std::printf("ba-app: %s\n",
                ba::i18n::llenar(ba::i18n::traducir("arranque", "fps=%1 enc=%2 grabando=%3 transmitiendo=%4 perdidos=%5/%6 enviado=%7KB"),
                                 {fps, s.videoEncoder.empty() ? "-" : s.videoEncoder, s.recording ? si : no,
                                  s.streaming ? si : no, std::to_string(s.droppedFrames), std::to_string(s.totalFrames),
                                  std::to_string(static_cast<unsigned long long>(s.bytesSent / 1024))})
                    .c_str());
    std::fflush(stdout);
}

} // namespace

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
    c.modoPrueba = hasFlag(argc, argv, "--test") || hasFlag(argc, argv, "--debug");
    if (const char* v = std::getenv("BA_PRUEBA_RETOMA_SALIDAS")) c.retomaEnPrueba = c.modoPrueba && std::string(v) == "1";
    // El modo seguro ESCALONADO (F1-10, lo arma el launcher: fault/ModoSeguro.hpp).
    // Los niveles 1 y 2 apagan piezas (cada una pregunta delProceso()) y las
    // escenas vuelven; solo el nivel 3 (o el viejo --safe-mode) es lo de abajo.
    ba::fault::delProceso() = ba::fault::leerArgumentos(argc, argv);
    c.safeMode = ba::fault::delProceso().escenasVacias();
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
        // armar las escenas no se crean las fuentes (ver RepositorioEscenas::crearEnElMotor).
        // Asi el modo seguro es lo que tiene que ser: una forma de arrancar, no una
        // forma de perder datos.
        c.encoder.videoEncoder = "x264";
    }
    if (c.guests < 0) {
        c.guests = 0;
    }
    if (c.guests > ba::ipc::kMaxInvitados) {
        c.guests = ba::ipc::kMaxInvitados;
    }
}

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
