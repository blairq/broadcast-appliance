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
            if (eq != nullptr && std::strncmp(e, "BA_", 3) == 0) {
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
    // Los datos (Config/, Logs/, Recordings/, Web/) viven en la raiz REAL
    // aunque el binario corra desde la copia sombra.
    const std::u8string root = cfg.workDir.u8string();
    o.envSet[ba::platform::kDataRootEnv] = std::string(root.begin(), root.end());

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
