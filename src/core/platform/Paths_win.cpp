// ============================================================================
// src/core/platform/Paths_win.cpp — Rutas en Windows
// ============================================================================

#include "platform/Paths.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <string>
#include <system_error>

namespace ba::platform {

std::filesystem::path executablePath() {
    // MAX_PATH no alcanza para rutas largas (\\?\...): se agranda hasta que
    // GetModuleFileNameW deje de truncar.
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        if (buf.size() >= 32768) {
            return {};
        }
        buf.resize(buf.size() * 2);
    }
}

std::filesystem::path runtimeDir() {
    // Portable: nada en %LOCALAPPDATA%. En Windows los canales locales son
    // pipes y mutex con nombre (sin disco); lo que queda va al bundle.
    return stateDir();
}

} // namespace ba::platform
