// ChildProcess.cpp
//
// Implementacion del lanzamiento de procesos hijos (ver ChildProcess.hpp).
// Toda la mecanica de SO vive en ba::platform::Process; aca solo queda la
// politica del launcher (entorno, logs, readopt).

#include "ChildProcess.hpp"

#include "Log.hpp"
#include "platform/Paths.hpp"

#include <cstdlib>
#include <cstring>

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
    (void)ownBinDir;
    for (const char* n : kDropPosix) {
        o.envRemove.emplace_back(n);
    }
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
