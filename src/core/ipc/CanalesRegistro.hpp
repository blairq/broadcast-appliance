// ============================================================================
// src/core/ipc/CanalesRegistro.hpp — los canales del registro de personas por tótem
//
// docs/funciones/REGISTRO-TOTEM.md. Cada conversación de registro que está
// andando ocupa un canal k (1..kCanalesRegistro) entre los dos procesos: la voz de
// la persona (BA.Audio.Registro.<k>, B->A), su cámara (BA.Registro.<k>, B->A) y la
// voz del asistente que le contesta (BA.Audio.RegistroVoz.<k>, A->B). Cuántos hay
// es UN tope para los dos lados (el gateway reparte los canales y crea los
// anillos; la app los escucha, los mira y les habla): cada canal cuesta un oído
// (voz a texto) y una voz en la app, y una sesión WebRTC en el gateway.
// ============================================================================
#pragma once

#include <string>

namespace ba::ipc {

/// Las conversaciones de registro a la vez (varios tótems, en distintas puertas).
constexpr int kCanalesRegistro = 8;

inline std::wstring nombreAudioRegistro(int k) { return L"BA.Audio.Registro." + std::to_wstring(k); }
inline std::wstring nombreVideoRegistro(int k) { return L"BA.Registro." + std::to_wstring(k); }
inline std::wstring nombreVozRegistro(int k) { return L"BA.Audio.RegistroVoz." + std::to_wstring(k); }

} // namespace ba::ipc
