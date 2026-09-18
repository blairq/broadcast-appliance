// ============================================================================
// src/core/platform/Paths_mac.cpp — Ruta del ejecutable en macOS
// ============================================================================

#include "platform/Paths.hpp"

#include <mach-o/dyld.h>

#include <cstdint>
#include <string>
#include <system_error>

namespace ba::platform {

std::filesystem::path executablePath() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // devuelve el tamaño necesario
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return {};
    }
    buf.resize(std::char_traits<char>::length(buf.c_str()));
    // _NSGetExecutablePath puede devolver una ruta con symlinks o "..":
    // se canonicaliza para que bundleRoot() vea el layout real del .app.
    std::error_code ec;
    std::filesystem::path p = std::filesystem::canonical(buf, ec);
    return ec ? std::filesystem::path(buf) : p;
}

} // namespace ba::platform
