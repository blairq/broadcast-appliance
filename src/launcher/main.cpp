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

#include "Datos.hpp"
#include "ajustes/Ajustes.hpp"
#include "Log.hpp"
#include "platform/Paths.hpp"
#include "security/CredentialStore.hpp"
#include "Supervisor.hpp"
#include "Updater.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <csignal>
#include <cstdio>
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
    // Portable: caches de librerias al bundle (lo heredan A y B).
    const std::filesystem::path root = ba::platform::bundleRoot();
    // Los datos: la misma carpeta en Windows y Linux; en macOS, al lado de la
    // .app (escribir adentro rompe su firma). La primera vez se mudan.
    std::vector<std::string> mudados;
    const std::filesystem::path datos = ba::launcher::prepararDatos(root, ba::platform::dataRoot(), mudados);
    // Cada configuración con lo suyo (Configuraciones/<nombre>/): la primera vez se muda la de ahora.
    ba::launcher::prepararConfiguraciones(root, datos, mudados);
    // Todo el launcher (y sus hijos) usa esta raíz: las caches, Runtime/, Logs/.
    {
        const std::u8string d = datos.u8string();
#ifdef _WIN32
        _putenv_s(ba::platform::kDataRootEnv, std::string(d.begin(), d.end()).c_str());
#else
        setenv(ba::platform::kDataRootEnv, std::string(d.begin(), d.end()).c_str(), 1);
#endif
    }
    ba::platform::redirectUserDirs();
    const std::filesystem::path bin = root / "bin" / "64bit";
    ba::launcher::Log::init((datos / "Logs" / "launcher.log").string());
    for (const std::string& m : mudados)
        ba::launcher::Log::write(ba::launcher::Log::Level::Info, "datos", 0, m);

    // --update <carpeta> / --update-rollback [--start]: reemplaza el programa sin
    // tocar los datos (ver Updater.hpp).
    {
        ba::launcher::UpdateOptions uo;
        uo.root = root;
        uo.datos = datos;
        bool wantUpdate = false;
        bool start = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--update" && i + 1 < args.size()) {
                wantUpdate = true;
                uo.source = std::filesystem::path(std::u8string(args[i + 1].begin(), args[i + 1].end()));
                ++i;
            } else if (args[i].rfind("--update=", 0) == 0) {
                wantUpdate = true;
                const std::string s = args[i].substr(9);
                uo.source = std::filesystem::path(std::u8string(s.begin(), s.end()));
            } else if (args[i] == "--update-rollback") {
                wantUpdate = true;
                uo.rollback = true;
            } else if (args[i] == "--start") {
                start = true;
            }
        }
        if (wantUpdate) {
            const ba::launcher::UpdateResult ur = ba::launcher::runUpdate(uo);
            std::fprintf(ur.ok ? stdout : stderr, "%s\n", ur.message.c_str());
            BA_LOG_INFO("actualizacion", ur.ok ? 0 : 1, ur.message);
            if (!ur.ok || !start) {
                ba::launcher::Log::close();
                return ur.ok ? 0 : 1;
            }
        }
    }
    // Credenciales en claro de versiones anteriores (Config/...) -> Credenciales/
    // cifradas, antes de que arranquen A y B.
    for (const std::string& name :
         ba::security::CredentialStore::bundle().importLegacy(ba::platform::dataRoot())) {
        ba::launcher::Log::write(ba::launcher::Log::Level::Info, "credenciales", 0,
                                 "importada " + name + " (cifrada en Credenciales/)");
    }

    ba::launcher::Supervisor::Config cfg;
    // --reiniciar: lo pide la app al cambiar de configuración (espera que se cierre el anterior).
    for (const std::string& a : args)
        if (a == "--reiniciar") cfg.esperarInstanciaMs = 60000;
    cfg.root = root;
    cfg.dataRoot = datos;
    cfg.appExe = bin / (std::string("ba-app") + kExeSuffix);
    cfg.gatewayExe = bin / (std::string("ba-gateway") + kExeSuffix);
    cfg.logsDir = datos / "Logs";

    // launcher.json (lo escribe la interfaz de A): copia sombra,
    // dialogo de recuperacion y argumentos extra de A y B. La linea de
    // comandos, mas abajo, lo pisa.
    {
        // Es de la máquina: <datos>/launcher.json (antes, Config/launcher.json).
        std::ifstream in(std::filesystem::exists(datos / "launcher.json") ? datos / "launcher.json"
                                                                          : datos / "Config" / "launcher.json");
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false, true);
        if (in && j.is_object()) {
            cfg.shadowCopy = j.value("shadowCopy", cfg.shadowCopy);
            cfg.recoveryUi = j.value("recoveryUi", cfg.recoveryUi);
            ba::ajustes::cargar(j.value("ajustes", nlohmann::json::object())); // Configuración → avanzados del arranque
            if (cfg.esperarInstanciaMs > 0) cfg.esperarInstanciaMs = ba::ajustes::sAMs("launcher.esperaCierreS", 60);
            // Configuración → Sistema y datos → Arranque y recuperación (con topes sanos).
            const auto entre = [&j](const char* k, double def, double lo, double hi) {
                const auto& v = j.value(k, nlohmann::json());
                return v.is_number() ? std::clamp(v.get<double>(), lo, hi) : def;
            };
            cfg.cuelgueMs = static_cast<std::uint64_t>(entre("cuelgueS", 5, 2, 120) * 1000);
            cfg.graciaArranqueMs = static_cast<std::uint64_t>(entre("graciaArranqueS", 20, 5, 600) * 1000);
            cfg.maxCaidas = static_cast<int>(entre("maxCaidas", 5, 1, 100));
            cfg.ventanaCaidasMs = static_cast<std::uint64_t>(entre("ventanaCaidasMin", 2, 1, 120) * 60000);
            cfg.reiniciosSinPreguntar = static_cast<int>(entre("reiniciosSinPreguntar", 2, 0, 100));
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
        } else if (a == "--test" || a == "--debug") {
            // Modo prueba: el MCP no pide aprobaciones (ver McpCore::setSinAprobaciones).
            cfg.appArgs.emplace_back("--test");
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

#include <sys/resource.h>

extern "C" void onStopSignal(int) { ba::launcher::Supervisor::requestStop(); }

int main(int argc, char** argv) {
    // Abierta desde el Finder, una app tiene un tope de 256 archivos abiertos, y
    // cada pipeline del motor usa dos (el bus de GStreamer): con ~100 pipelines
    // se pasaba y la app se caía al arrancar. Se sube el tope (los hijos lo heredan).
    if (struct rlimit rl{}; getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 8192) {
        rl.rlim_cur = rl.rlim_max == RLIM_INFINITY ? 8192 : std::min<rlim_t>(8192, rl.rlim_max);
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
    std::signal(SIGHUP, onStopSignal);
    std::signal(SIGPIPE, SIG_IGN);
    std::vector<std::string> args(argv + 1, argv + argc);
    return runLauncher(args);
}

#endif
