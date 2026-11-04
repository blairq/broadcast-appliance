// ============================================================================
// src/core/platform/Paths_posix.cpp — Directorio de runtime (Linux y macOS)
//
// `executablePath()` difiere entre Linux (/proc/self/exe) y macOS
// (_NSGetExecutablePath): vive en Paths_linux.cpp / Paths_mac.cpp.
// ============================================================================

#include "platform/Paths.hpp"
#include "platform/Instancia.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <system_error>

namespace ba::platform {

namespace {

/// Crea `dir` con 0700 y verifica que sea un directorio propio y privado.
///
/// Si ya existe con otro dueño o con permisos de grupo/otros, se rechaza: en
/// un /tmp compartido otro usuario podria haberlo creado antes para
/// interceptar el canal de control.
bool ensurePrivateDir(const std::filesystem::path& dir) {
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat st {};
    if (::lstat(dir.c_str(), &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) {
        return false;
    }
    if ((st.st_mode & 077) != 0) {
        // Es nuestro: se corrigen los permisos en vez de fallar.
        if (::chmod(dir.c_str(), 0700) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace {

/// La base del usuario: $TMPDIR (macOS) o $XDG_RUNTIME_DIR (Linux); "" sin ella.
std::filesystem::path baseDeRuntime() {
#if defined(__APPLE__)
    // En macOS TMPDIR es por usuario (/var/folders/..), ya privado.
    const char* base = std::getenv("TMPDIR");
#else
    const char* base = std::getenv("XDG_RUNTIME_DIR");
#endif
    return base != nullptr && *base != '\0' ? std::filesystem::path(base) : std::filesystem::path{};
}

} // namespace

std::filesystem::path runtimeDirComun() {
    const std::filesystem::path base = baseDeRuntime();
    const std::filesystem::path dir =
        !base.empty() ? base / "broadcast-appliance"
                      : std::filesystem::path("/tmp") / ("broadcast-appliance-" + std::to_string(::getuid()));
    return ensurePrivateDir(dir) ? dir : std::filesystem::path{};
}

std::filesystem::path runtimeDir() {
    const std::string clave = claveInstancia();
    if (clave.empty()) return runtimeDirComun();
    // Corto a propósito: la ruta de un socket Unix no pasa de 104 bytes en macOS
    // ($TMPDIR ya ocupa ~49).
    const std::filesystem::path base = baseDeRuntime();
    const std::filesystem::path dir =
        !base.empty() ? base / ("ba-" + clave)
                      : std::filesystem::path("/tmp") / ("ba-" + std::to_string(::getuid()) + "-" + clave);
    return ensurePrivateDir(dir) ? dir : std::filesystem::path{};
}

} // namespace ba::platform
