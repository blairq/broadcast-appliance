// ============================================================================
// src/core/platform/Huerfanos_win.cpp — los procesos, en Windows (ver el .hpp)
//
// Lo que lanza cada hijo lo ata su Job Object (KILL_ON_JOB_CLOSE, agent/HijosDeLaApp):
// acá no hay árbol que recorrer. Solo hace falta saber si un dueño vive (el barrido).
// ============================================================================
#include "platform/Huerfanos.hpp"

#include <windows.h>

#include <iterator>

namespace ba::platform::huerfanos {

namespace fs = std::filesystem;

bool vivo(std::int64_t pid) {
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return false;
    DWORD code = 0;
    const bool si = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return si;
}

fs::path ejecutableDe(std::int64_t pid) {
    if (pid <= 0) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return {};
    wchar_t ruta[32768];
    DWORD n = static_cast<DWORD>(std::size(ruta));
    const bool ok = QueryFullProcessImageNameW(h, 0, ruta, &n) != 0;
    CloseHandle(h);
    if (!ok) return {};
    std::error_code ec;
    const fs::path r(std::wstring(ruta, n));
    const fs::path c = fs::weakly_canonical(r, ec);
    return ec ? r : c;
}

std::vector<std::int64_t> descendientes(std::int64_t) { return {}; }

void terminar(const std::vector<std::int64_t>& pids, int) {
    for (const std::int64_t p : pids)
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(p))) {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
}

} // namespace ba::platform::huerfanos
