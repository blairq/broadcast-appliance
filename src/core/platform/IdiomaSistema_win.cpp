// ============================================================================
// src/core/platform/IdiomaSistema_win.cpp — ver IdiomaSistema.hpp
//
// Los idiomas de la interfaz de Windows (Configuración → Hora e idioma), en orden:
// GetUserPreferredUILanguages da una lista de nombres separados por \0 ("es-AR").
// ============================================================================
#include "platform/IdiomaSistema.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ba::platform {

std::vector<std::string> idiomasDelSistema() {
    std::vector<std::string> out;
    ULONG cuantos = 0, largo = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &cuantos, nullptr, &largo) || largo == 0) return out;
    std::wstring buf(largo, L'\0');
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &cuantos, buf.data(), &largo)) return out;
    for (const wchar_t* p = buf.c_str(); *p != L'\0'; p += wcslen(p) + 1) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) continue;
        std::string s(static_cast<std::size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, p, -1, s.data(), n, nullptr, nullptr);
        out.push_back(s);
    }
    return out;
}

} // namespace ba::platform
