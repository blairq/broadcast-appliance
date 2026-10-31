// ============================================================================
// src/core/ipc/CanalesSalida.hpp — los canales de cada salida hacia el gateway
//
// El aparato tiene N salidas (docs/planes/PLAN-SALIDAS.md), cada una un
// operador completo. La salida por omisión sigue bajando como siempre
// (BA.Program + BA.Audio.ContentForReturn); cada una de las otras, por sus
// propios canales: BA.Salida.<id> (video) y BA.Audio.Salida.<id> (su audio de
// retorno). Los nombres salen de acá para los dos procesos: la app los publica,
// el gateway los lee solo mientras alguien los mira.
//
// El mix-minus de cada invitado es POR SALIDA (04-plan-unificado.md, F2-02): lo que
// oye el invitado del lugar n que está en la salida X es la mezcla de X sin su voz.
// La por omisión sigue con el nombre de siempre, BA.Audio.Return.<n>; las demás,
// BA.Audio.Return.<id>.<n>.
//
// El id va adentro del nombre de una memoria compartida: por eso solo letras
// ASCII, números, «-» y «_», y corto. Uno que no cumple no nombra ningún canal.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ba::ipc {

constexpr std::size_t kLargoIdSalida = 32;

inline bool idDeSalidaValido(std::string_view id) {
    if (id.empty() || id.size() > kLargoIdSalida) return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}

inline std::wstring nombreCanalSalida(const std::string& id) { return L"BA.Salida." + std::wstring(id.begin(), id.end()); }
inline std::wstring nombreAudioSalida(const std::string& id) {
    return L"BA.Audio.Salida." + std::wstring(id.begin(), id.end());
}

/// El canal del mix-minus del lugar `n` en la salida `id` sin el «BA.Audio.» (el motor
/// le pone su prefijo: las pruebas usan otro). `id` "" = la por omisión.
inline std::string sufijoRetorno(const std::string& id, int n) {
    return id.empty() ? "Return." + std::to_string(n) : "Return." + id + "." + std::to_string(n);
}
inline std::wstring nombreRetorno(const std::string& id, int n) {
    const std::string s = "BA.Audio." + sufijoRetorno(id, n);
    return std::wstring(s.begin(), s.end());
}

} // namespace ba::ipc
