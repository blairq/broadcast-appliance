// ============================================================================
// src/core/platform/Volumen_posix.cpp — ver Volumen.hpp (macOS y Linux: st_dev)
// ============================================================================
#include "platform/Volumen.hpp"

#include <sys/stat.h>

namespace ba::platform {

std::optional<std::uint64_t> volumenDeExistente(const std::filesystem::path& ruta) {
    struct stat st {};
    if (::stat(ruta.c_str(), &st) != 0) return std::nullopt;
    return static_cast<std::uint64_t>(st.st_dev);
}

} // namespace ba::platform
