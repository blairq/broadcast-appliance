// main.cpp
//
// Punto de entrada del launcher (Proceso 0, spec 3.1), igual en todas las
// plataformas. Deriva las rutas de la ubicacion del ejecutable
// (ba::platform::bundleRoot), porque la unidad o el punto de montaje cambian
// entre equipos (spec 5.3).
//
// Diferencias de plataforma que quedan aca (solo el ENTRY POINT):
//   - Windows: subsistema GUI (wWinMain), sin consola.
//   - Linux/macOS: main() de consola; SIGINT/SIGTERM/SIGHUP piden el cierre
//     ordenado (Ctrl+C en la terminal, systemd, fin de sesion).
//
// Opciones (para desarrollo y operacion):
//   --shadow         ejecutar A y B desde una copia sombra en la cache del
//                    usuario (spec 3.1.5). Desactivado por defecto: se corre
//                    desde el bundle, donde esta.
//   --no-recovery-ui reiniciar solo, sin dialogo de recuperacion
//   --app-arg=<x>    argumento extra para A (repetible)
//   --gw-arg=<x>     argumento extra para B (repetible)

#include "Log.hpp"
#include "platform/Paths.hpp"
#include "Supervisor.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// shellapi.h necesita windows.h antes (bloque aparte: clang-format no lo reordena).
#include <shellapi.h>
#endif

namespace {

#ifdef _WIN32
constexpr const char* kExeSuffix = ".exe";
#else
constexpr const char* kExeSuffix = "";
#endif

int runLauncher(const std::vector<std::string>& args) {
    const std::filesystem::path root = ba::platform::bundleRoot();
    const std::filesystem::path bin = root / "bin" / "64bit";
    ba::launcher::Log::init((root / "Logs" / "launcher.log").string());

    ba::launcher::Supervisor::Config cfg;
    cfg.root = root;
    cfg.appExe = bin / (std::string("ba-app") + kExeSuffix);
    cfg.gatewayExe = bin / (std::string("ba-gateway") + kExeSuffix);
    cfg.logsDir = root / "Logs";

    // Config/launcher.json (lo escribe la interfaz de A): copia sombra,
    // dialogo de recuperacion y argumentos extra de A y B. La linea de
    // comandos, mas abajo, lo pisa.
    {
        std::ifstream in(root / "Config" / "launcher.json");
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false, true);
        if (in && j.is_object()) {
            cfg.shadowCopy = j.value("shadowCopy", cfg.shadowCopy);
            cfg.recoveryUi = j.value("recoveryUi", cfg.recoveryUi);
            for (const auto& a : j.value("appArgs", nlohmann::json::array())) {
                if (a.is_string()) cfg.appArgs.push_back(a.get<std::string>());
            }
            for (const auto& a : j.value("gatewayArgs", nlohmann::json::array())) {
                if (a.is_string()) cfg.gatewayArgs.push_back(a.get<std::string>());
            }
        }
    }
    for (const auto& a : args) {
        if (a == "--shadow") {
            cfg.shadowCopy = true;
        } else if (a == "--no-shadow") {
            cfg.shadowCopy = false; // ya es el default; se acepta por compatibilidad
        } else if (a == "--no-recovery-ui") {
            cfg.recoveryUi = false;
        } else if (a.rfind("--app-arg=", 0) == 0) {
            cfg.appArgs.push_back(a.substr(10));
        } else if (a.rfind("--gw-arg=", 0) == 0) {
            cfg.gatewayArgs.push_back(a.substr(9));
        }
    }
    BA_LOG_INFO("launcher", 0, "raiz del bundle: " + root.string());

    ba::launcher::Supervisor sup(std::move(cfg));
    const int rc = sup.run();
    ba::launcher::Log::close();
    return rc;
}

} // namespace

#ifdef _WIN32

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; argvW != nullptr && i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(s);
    }
    LocalFree(argvW);
    return runLauncher(args);
}

#if !defined(_MSC_VER)
// MinGW GUI CRT espera `WinMain` (narrow); se puentea a `wWinMain`.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
#endif

#else

extern "C" void onStopSignal(int) { ba::launcher::Supervisor::requestStop(); }

int main(int argc, char** argv) {
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
    std::signal(SIGHUP, onStopSignal);
    std::signal(SIGPIPE, SIG_IGN);
    std::vector<std::string> args(argv + 1, argv + argc);
    return runLauncher(args);
}

#endif
