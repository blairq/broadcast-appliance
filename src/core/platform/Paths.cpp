// ============================================================================
// src/core/platform/Paths.cpp — Logica comun de rutas (sin #ifdef)
// ============================================================================

#include "platform/Paths.hpp"

#include <cstdlib>
#ifndef _WIN32
#include <stdlib.h>
#endif
#include <string>
#include <system_error>

namespace ba::platform {

std::filesystem::path executableDir() {
    const std::filesystem::path exe = executablePath();
    return exe.empty() ? std::filesystem::path{} : exe.parent_path();
}

std::filesystem::path bundleRootFor(const std::filesystem::path& exeDir) {
    if (exeDir.empty()) {
        return exeDir;
    }
    // `lexically_normal` quita un separador final: sin eso `filename()` de
    // "/x/bin/64bit/" es vacio y no se reconoce el layout.
    const std::filesystem::path dir = exeDir.lexically_normal();
    const std::filesystem::path norm = dir.has_filename() ? dir : dir.parent_path();

    // <root>/bin/64bit/<exe>
    if (norm.filename() == "64bit" && norm.parent_path().filename() == "bin") {
        return norm.parent_path().parent_path();
    }
    // <X>.app/Contents/MacOS/<exe>  -> root = Contents
    if (norm.filename() == "MacOS" && norm.parent_path().filename() == "Contents") {
        return norm.parent_path();
    }
    // <root>/<exe> (launcher en la raiz del bundle)
    return norm;
}

std::filesystem::path bundleRoot() { return bundleRootFor(executableDir()); }

std::filesystem::path dataRoot() {
    const char* env = std::getenv(kDataRootEnv);
    if (env != nullptr && *env != '\0') {
        // El launcher la escribe en UTF-8 (C++20: path desde char8_t).
        const std::string s(env);
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    }
    return bundleRoot();
}

namespace {
std::filesystem::path makeDir(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) return {};
    std::filesystem::permissions(p, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 ec);
    return p; // sin permisos (FAT): igual sirve
}

void setEnv(const char* name, const std::filesystem::path& value) {
    const std::u8string u = value.u8string();
    const std::string s(u.begin(), u.end());
#ifdef _WIN32
    _putenv_s(name, s.c_str());
#else
    ::setenv(name, s.c_str(), 1);
#endif
}
} // namespace

std::filesystem::path stateDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Runtime");
}

std::filesystem::path cacheDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Cache");
}

std::filesystem::path credentialsDir() {
    const std::filesystem::path root = dataRoot();
    return root.empty() ? std::filesystem::path{} : makeDir(root / "Credenciales");
}

void redirectUserDirs() {
    const std::filesystem::path cache = cacheDir();
    if (cache.empty()) return;
    setEnv("BA_CACHE_DIR", cache);
    setEnv("XDG_CACHE_HOME", cache);                 // Qt, fontconfig, Mesa (Linux)
    setEnv("QML_DISK_CACHE_PATH", cache / "qml");    // QML compilado (todas las plataformas)
    setEnv("MESA_SHADER_CACHE_DIR", cache / "mesa"); // por si XDG no alcanza
    setEnv("__GL_SHADER_DISK_CACHE_PATH", cache / "nvidia");
}

} // namespace ba::platform
