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
//
// EL EMISOR APARTE (04-plan-unificado.md, F3-01): `ba-emisor --salida=<id>` codifica,
// graba y transmite una salida en otro proceso, así una caída de la app no corta el
// aire. Lee el MISMO canal de video que el gateway (el de video admite varios
// lectores) y su propio anillo de audio, BA.Audio.Emisor.<id>: el anillo es de un
// solo lector y el del gateway ya tiene el suyo. Late en su slot de BA.Health y
// atiende el cierre ordenado en control.emisor.<id>.
// ============================================================================
#pragma once

#include "ipc/HealthBlock.hpp"

#include <cstddef>
#include <cstdint>
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

/// El id de la salida por omisión (el mismo que motor::Motor::kSalidaPorOmision y
/// app::salidas::kPorOmision): baja por los canales de siempre.
inline constexpr const char* kIdSalidaPorOmision = "principal";

inline std::wstring nombreCanalSalida(const std::string& id) { return L"BA.Salida." + std::wstring(id.begin(), id.end()); }
inline std::wstring nombreAudioSalida(const std::string& id) {
    return L"BA.Audio.Salida." + std::wstring(id.begin(), id.end());
}

/// El video de la salida `id` tal como lo publica la app: la por omisión en BA.Program.
inline std::string canalVideoDeSalida(const std::string& id) {
    return id == kIdSalidaPorOmision ? std::string("BA.Program") : "BA.Salida." + id;
}

/// El anillo del audio de la salida `id` para su emisor, sin el «BA.Audio.» (el motor
/// le pone su prefijo) y entero.
inline std::string sufijoAudioEmisor(const std::string& id) { return "Emisor." + id; }
inline std::string canalAudioEmisor(const std::string& id) { return "BA.Audio." + sufijoAudioEmisor(id); }

/// El canal de control del emisor de la salida `id` (el launcher pide el cierre por acá).
inline std::string canalControlEmisor(const std::string& id) { return "control.emisor." + id; }

/// Los emisores usan los slots de BA.Health que quedan después de B y A: el k-ésimo
/// (0, 1…) el kSlotSaludApp + 1 + k. Sobran kSlotsSalud − 2.
inline constexpr std::size_t kMaxEmisores = kSlotsSalud - kSlotSaludApp - 1;
inline constexpr std::uint32_t slotSaludEmisor(std::size_t k) {
    return kSlotSaludApp + 1 + static_cast<std::uint32_t>(k);
}
static_assert(kSlotSaludGateway < kSlotSaludApp && kMaxEmisores >= 1, "BA.Health sin lugar para el emisor");

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
