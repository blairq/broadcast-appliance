// ControlChannel.cpp
//
// Implementacion del cliente del canal de control (ver ControlChannel.hpp).
// Win32 real bajo _WIN32; fuera de Windows es un stub que loguea y devuelve
// true para que el flujo de cierre se pueda probar.

#include "ControlChannel.hpp"

#include "Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// MinGW con WIN32_LEAN_AND_MEAN no define WAIT_EVENT (constante Win32 = 0x9).
// Solo se usa como valor reservado (openMode queda (void)), pero debe compilar.
#ifndef WAIT_EVENT
#define WAIT_EVENT 0x00000009
#endif
#endif

namespace ba::launcher {

bool ControlChannel::requestShutdown(const std::wstring& pipeName, const std::wstring& target,
                                     int timeoutMs) {
    const std::string targetStr(target.begin(), target.end());

#ifndef _WIN32
    (void)pipeName;
    (void)timeoutMs;
    BA_LOG_INFO("control", 0, "stub: shutdown solicitado a '" + targetStr + "' (no-Win)");
    return true;
#else
    // Mensaje JSON con prefijo de longitud (spec 4.1).
    const std::string json = "{\"protocolVersion\":" + std::to_string(kProtocolVersion) +
                             ",\"type\":\"shutdown\",\"target\":\"" + targetStr + "\"}";

    const DWORD openMode = (timeoutMs > 0) ? (WAIT_EVENT | 0) : 0; // reservado; usamos CreateFile directo.
    (void)openMode;

    HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | PIPE_REJECT_REMOTE_CLIENTS, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        BA_LOG_WARN("control", static_cast<int>(err),
                    "no se pudo abrir el pipe para shutdown de '" + targetStr +
                        "' (el hijo probablemente ya no esta)");
        return false;
    }

    const DWORD len = static_cast<DWORD>(json.size());
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, &len, sizeof(len), &written, nullptr) &&
              WriteFile(pipe, json.data(), len, &written, nullptr);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);

    if (!ok) {
        BA_LOG_WARN("control", static_cast<int>(GetLastError()),
                    "falló la escritura del shutdown a '" + targetStr + "'");
        return false;
    }
    BA_LOG_INFO("control", 0, "shutdown enviado a '" + targetStr + "'");
    return true;
#endif
}

} // namespace ba::launcher
