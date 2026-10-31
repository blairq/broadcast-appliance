// ============================================================================
// src/core/platform/Volumen_win.cpp — ver Volumen.hpp (Windows: número de serie)
//
// La raíz del volumen que contiene la ruta (GetVolumePathNameW: «D:\», o la
// carpeta donde está montado un disco sin letra) y su número de serie. Comparar
// letras no alcanza: dos letras pueden ser el mismo disco (subst) y un disco
// montado en C:\Grabaciones es otro aunque empiece con «C:».
// ============================================================================
#include "platform/Volumen.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace ba::platform {

std::optional<std::uint64_t> volumenDeExistente(const std::filesystem::path& ruta) {
    const std::wstring w = ruta.wstring();
    std::vector<wchar_t> raiz(w.size() + MAX_PATH + 2, L'\0');
    if (!GetVolumePathNameW(w.c_str(), raiz.data(), static_cast<DWORD>(raiz.size()))) return std::nullopt;
    DWORD serie = 0;
    if (!GetVolumeInformationW(raiz.data(), nullptr, 0, &serie, nullptr, nullptr, nullptr, 0)) return std::nullopt;
    return static_cast<std::uint64_t>(serie);
}

} // namespace ba::platform
