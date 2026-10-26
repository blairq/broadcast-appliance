// src/core/platform/Maquina_win.cpp — el id y el modelo de la PC en Windows

#include "platform/Maquina.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Los encabezados viejos de MinGW no lo traen (vale lo mismo en todos).
#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif

namespace ba::platform {

namespace {
/// Un valor de texto del registro (vista de 64 bits), en UTF-8.
std::string valor(HKEY raiz, const wchar_t* clave, const wchar_t* nombre) {
    wchar_t buf[256] = {};
    DWORD n = sizeof buf;
    if (RegGetValueW(raiz, clave, nombre, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &n) != ERROR_SUCCESS)
        return {};
    const int m = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string s(m > 0 ? static_cast<std::size_t>(m - 1) : 0, '\0');
    if (m > 1) WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), m, nullptr, nullptr);
    return s;
}
} // namespace

std::string idCrudoDeMaquina() {
    // El que pone Windows al instalarse (no cambia al reiniciar ni con el hardware).
    return valor(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid");
}

std::string modeloDeMaquina() {
    const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    const std::string fab = valor(HKEY_LOCAL_MACHINE, bios, L"SystemManufacturer");
    const std::string mod = valor(HKEY_LOCAL_MACHINE, bios, L"SystemProductName");
    return fab.empty() ? mod : mod.empty() ? fab : fab + " " + mod;
}

} // namespace ba::platform
