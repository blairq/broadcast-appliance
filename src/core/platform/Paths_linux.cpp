// ============================================================================
// src/core/platform/Paths_linux.cpp — Ruta del ejecutable en Linux
// ============================================================================

#include "platform/Paths.hpp"

#include <system_error>

namespace ba::platform {

std::filesystem::path executablePath() {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : p;
}

} // namespace ba::platform
