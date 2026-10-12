// ChildProcess.cpp
//
// Implementacion del lanzamiento de procesos hijos (ver ChildProcess.hpp).
// Toda la mecanica de SO vive en ba::platform::Process; aca solo queda la
// politica del launcher (entorno, logs, readopt).

#include "ChildProcess.hpp"

#include "ChildOutput.hpp"
#include "Log.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // GetEnvironmentStringsA
#endif

namespace ba::launcher {

namespace {

/// Variables que un hijo nunca debe heredar: apuntan a bibliotecas o plugins
/// de fuera del bundle y rompen la carga de Qt/libobs de formas dificiles de
/// diagnosticar (spec 3.1.3).
const char* const kDropAlways[] = {
    "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "PYTHONHOME", "PYTHONPATH",
};

#if !defined(_WIN32)
const char* const kDropPosix[] = {
    "LD_PRELOAD",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",
    "DYLD_FALLBACK_LIBRARY_PATH",
    "DYLD_INSERT_LIBRARIES",
};
#endif

#if defined(__linux__)
/// Mayor version GLIBCXX_3.4.N que exporta un libstdc++ (-1 si no se pudo leer).
int glibcxxLevel(const std::filesystem::path& lib) {
    std::ifstream in(lib, std::ios::binary);
    if (!in) return -1;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int best = -1;
    static const std::string kTag = "GLIBCXX_3.4.";
    for (std::size_t p = data.find(kTag); p != std::string::npos; p = data.find(kTag, p + 1)) {
        int n = 0;
        std::size_t i = p + kTag.size();
        bool any = false;
        while (i < data.size() && data[i] >= '0' && data[i] <= '9') {
            n = n * 10 + (data[i++] - '0');
            any = true;
        }
        if (any && n > best) best = n;
    }
    return best;
}

/// bin/64bit/cxx si el runtime de C++ que trae el bundle es MAS NUEVO que el
/// del sistema (o el sistema no tiene): los binarios se compilaron con ese
/// runtime y con uno viejo no arrancan ("GLIBCXX_3.4.xx not found"). Si el del
/// sistema es igual o mas nuevo se usa ese (drivers de video como Mesa cargan
/// el suyo y exigen el mas nuevo). Es lo que hace el "checkrt" de AppImage.
std::string cxxRuntimeDir(const std::filesystem::path& ownBinDir) {
    const std::filesystem::path bundled = ownBinDir / "cxx" / "libstdc++.so.6";
    const int mine = glibcxxLevel(bundled);
    if (mine < 0) return {};
    int system = -1;
    for (const char* d : {"/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
                          "/lib/aarch64-linux-gnu", "/usr/lib64", "/lib64", "/usr/lib", "/lib"}) {
        const std::filesystem::path p = std::filesystem::path(d) / "libstdc++.so.6";
        if (std::filesystem::exists(p)) {
            system = glibcxxLevel(p);
            break;
        }
    }
    const bool useBundled = mine > system;
    static bool logged = false;
    if (!logged) {
        logged = true;
        BA_LOG_INFO("child", 0,
                    "runtime C++: bundle GLIBCXX_3.4." + std::to_string(mine) + ", sistema " +
                        (system < 0 ? std::string("ausente") : "GLIBCXX_3.4." + std::to_string(system)) +
                        " -> " + (useBundled ? "se usa el del bundle" : "se usa el del sistema"));
    }
    return useBundled ? (ownBinDir / "cxx").string() : std::string();
}
#endif

#if defined(_WIN32)
std::string envOr(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : def;
}
#endif

void applyEnvPolicy(ba::platform::ProcessOptions& o, const std::filesystem::path& ownBinDir) {
    for (const char* n : kDropAlways) {
        o.envRemove.emplace_back(n);
    }
    // Python no escribe .pyc: el Python del bundle (plugins, GStreamer, traductor)
    // reescribía los de adentro del programa (una escritura que el antivirus mira,
    // y en el arranque siguiente un «hash distinto» en la integridad). Los .pyc ya
    // vienen en el bundle.
    o.envSet["PYTHONDONTWRITEBYTECODE"] = "1";
#if defined(_WIN32)
    // Entorno minimo (spec 3.1.3): nada heredado salvo lo imprescindible.
    o.inheritEnv = false;
    const std::string systemRoot = envOr("SystemRoot", "C:\\Windows");
    o.envSet["SystemRoot"] = systemRoot;
    o.envSet["SystemDrive"] = systemRoot.substr(0, 2);
    o.envSet["PATH"] = systemRoot + "\\system32;" + ownBinDir.string();
    const std::string temp = envOr("TEMP", "");
    if (!temp.empty()) {
        o.envSet["TEMP"] = temp;
        o.envSet["TMP"] = temp;
    }
    o.envSet["LOCALAPPDATA"] = envOr("LOCALAPPDATA", "");
    o.envSet["USERPROFILE"] = envOr("USERPROFILE", "");
    // Laptops hibridas: GPU de alto rendimiento (spec 4.2).
    o.envSet["NvOptimusEnablement"] = "1";
    o.envSet["AmdPowerXpressRequestHighPerformance"] = "1";
    // Las variables del propio producto (BA_*) SI pasan: por ahi viajan los
    // secretos que no deben ir en la linea de comandos (BA_GATEWAY_PIN,
    // BA_TUNNEL_TOKEN, BA_STREAM_KEY, BA_ADMIN_TOKEN...). Con el entorno
    // minimo de arriba no le llegaban a A ni a B.
    if (LPCH env = GetEnvironmentStringsA()) {
        for (const char* e = env; *e != '\0'; e += std::strlen(e) + 1) {
            const char* eq = std::strchr(e, '=');
            // También el diagnóstico de GStreamer (GST_DEBUG, GST_DEBUG_FILE,
            // GST_DEBUG_DUMP_DOT_DIR...): sin eso no había forma de ver por qué
            // una cadena no negocia en una máquina del cliente.
            if (eq != nullptr && (std::strncmp(e, "BA_", 3) == 0 || std::strncmp(e, "GST_DEBUG", 9) == 0)) {
                o.envSet[std::string(e, eq)] = std::string(eq + 1);
            }
        }
        FreeEnvironmentStringsA(env);
    }
#else
    for (const char* n : kDropPosix) {
        o.envRemove.emplace_back(n);
    }
#if defined(__linux__)
    // --- La interfaz corre sobre X11 (XWayland si la sesion es Wayland) -----
    //
    // NO ES CAPRICHO, Y TAMPOCO ES GRATIS. En Wayland un programa NO PUEDE
    // decir donde va su ventana: ubicarlas es del compositor. Lo peor es que
    // pedirlo no falla —Qt hasta informa la pantalla nueva— y la ventana se
    // queda donde estaba, asi que el operador ve botones que no hacen nada.
    //
    // Este aparato lo necesita: un puesto de television se arma repartiendo
    // ventanas entre dos o tres monitores (el programa aca, el guion alla, el
    // registro abajo), y se arma UNA VEZ y se repite igual antes de cada
    // evento. Sobre X11 eso funciona —medido: dos ventanas quedan en mitades
    // exactas, cuatro en cuadrantes, sin huecos ni superposicion—.
    //
    // LO QUE SE PAGA: sobre XWayland el escalado fraccionario se ve mas
    // blando y no hay DPI por monitor. Si algun dia eso pesa mas que poder
    // acomodar el puesto, se saca esta linea y todo lo demas sigue andando:
    // las herramientas de ventanas detectan solas que no se puede ubicar y lo
    // dicen en vez de mentir.
    //
    // Se respeta lo que ya venga puesto: quien exporte QT_QPA_PLATFORM antes
    // de arrancar manda (`QT_QPA_PLATFORM=wayland ./BroadcastAppliance`).
    if (std::getenv("QT_QPA_PLATFORM") == nullptr) {
        o.envSet["QT_QPA_PLATFORM"] = "xcb";
    }
    // Y SIN ESPERAR AL BARRIDO DE PANTALLA, que sobre XWayland es lo que
    // vuelve inusable cualquier SEGUNDA ventana.
    //
    // Medido en una app Qt de veinte lineas, sin nada nuestro adentro: con dos
    // ventanas abiertas sobre `xcb`, la segunda dibuja a UN cuadro por segundo.
    // No depende del tamaño, ni de lo que dibuje, ni del bucle de render
    // (`threaded` y `basic` dan lo mismo). Con Wayland nativo da 60, y con
    // `vblank_mode=0` sobre xcb tambien da 60.
    //
    // Se siente como «la interfaz esta lentisima» y aparece justo cuando el
    // operador hace lo que este aparato promete: sacar un panel a la otra
    // pantalla y agrandarlo. Con la ventana principal tapada, TODO el programa
    // se arrastraba.
    //
    // LO QUE SE PAGA: la interfaz puede mostrar algun corte horizontal al
    // moverse. No toca la salida: el programa se arma adentro de libobs y va
    // al codificador, no por esta pantalla.
    //
    // Son dos variables porque son dos controladores: `vblank_mode` es de Mesa
    // (Intel y AMD) y `__GL_SYNC_TO_VBLANK` es del de NVIDIA. Poner la que no
    // corresponde no hace nada. Igual que arriba, quien las exporte manda.
    if (std::getenv("vblank_mode") == nullptr) {
        o.envSet["vblank_mode"] = "0";
    }
    if (std::getenv("__GL_SYNC_TO_VBLANK") == nullptr) {
        o.envSet["__GL_SYNC_TO_VBLANK"] = "0";
    }
    if (const std::string cxx = cxxRuntimeDir(ownBinDir); !cxx.empty()) {
        o.envSet["LD_LIBRARY_PATH"] = cxx;
        o.envRemove.erase(std::remove(o.envRemove.begin(), o.envRemove.end(), "LD_LIBRARY_PATH"),
                          o.envRemove.end());
    }
#else
    (void)ownBinDir;
#endif
#endif
}

} // namespace

bool ChildProcess::start(const Config& cfg) {
    name_ = cfg.name;
    ba::platform::ProcessOptions o;
    o.exe = cfg.exePath;
    o.args = cfg.args;
    o.workDir = cfg.workDir;
    o.killWithParent = false; // A y B sobreviven a un crash del launcher (3.1.9)
    o.hideWindow = true;
    applyEnvPolicy(o, cfg.exePath.parent_path());
    for (const auto& [k, v] : cfg.env) o.envSet[k] = v;
    // Los datos (Config/, Logs/, Recordings/, Web/) viven en la raiz REAL
    // aunque el binario corra desde la copia sombra.
    const std::u8string root = cfg.workDir.u8string();
    o.envSet[ba::platform::kDataRootEnv] = std::string(root.begin(), root.end());
    // La configuración activa, leída AHORA (cambiar de configuración = reiniciar los hijos).
    const std::u8string conf = ba::platform::configRootFor(cfg.workDir).u8string();
    o.envSet[ba::platform::kConfigRootEnv] = std::string(conf.begin(), conf.end());

    // Salida del hijo (stdout+stderr) a su archivo de Logs/. Se prepara aca,
    // con el hijo todavia sin lanzar: es el unico momento en que se puede
    // rotar. Si el disco no deja, el hijo arranca igual y sin captura: un log
    // nunca deja al programa sin salir al aire.
    if (!cfg.outputFile.empty()) {
        if (prepareChildOutput(cfg.outputFile, name_, cfg.exePath, cfg.args)) {
            o.outputFile = cfg.outputFile;
            // Qt, si stderr no es una consola, en Windows manda sus avisos
            // (qWarning, errores de QML) solo al depurador (OutputDebugString):
            // con esto los escribe en stderr, o sea en el archivo.
            o.envSet["QT_FORCE_STDERR_LOGGING"] = "1";
        } else {
            const std::u8string out = cfg.outputFile.u8string();
            BA_LOG_WARN("child", 0,
                        "no se pudo preparar " + std::string(out.begin(), out.end()) + "; '" + name_ +
                            "' se lanza sin capturar su salida");
        }
    }

    std::string err;
    if (!ba::platform::Process::spawn(o, proc_, &err)) {
        BA_LOG_ERROR("child", 0, "no se pudo lanzar '" + name_ + "' (" + cfg.exePath.string() + "): " + err);
        return false;
    }
    BA_LOG_INFO("child", 0, "lanzado '" + name_ + "' pid=" + std::to_string(proc_.pid()));
    return true;
}

bool ChildProcess::attach(std::uint32_t pid, const std::string& name) {
    name_ = name;
    return ba::platform::Process::attach(pid, proc_);
}

bool ChildProcess::alive() { return proc_.running(); }

std::optional<int> ChildProcess::exitCode() {
    if (proc_.pid() == 0) {
        return std::nullopt;
    }
    return proc_.wait(0);
}

} // namespace ba::launcher
