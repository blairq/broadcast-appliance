// ============================================================================
// src/core/ipc/LugaresSala.hpp — cuántos invitados puede tener la sala
//
// UN solo número para los dos procesos: el gateway arma con él las sesiones
// WebRTC del pool y el lugar para los canales de cada invitado (cámara, pantalla
// y audio: BA.Guest.<n>, BA.GuestShare.<n>, BA.Audio.Guest.<n>); la app, el tope
// del cupo (Configuración → Invitados, el cupo en caliente, la línea de
// comandos). Los canales y las sesiones se crean recién al subir el cupo
// (MediaPump::ampliar): subir este tope no cuesta nada hasta que se use. Lo que
// sí cuesta es cada invitado conectado (un video que decodificar): eso lo mide
// el panel «Uso de la sala».
// ============================================================================
#pragma once

namespace ba::ipc {

/// El tope de invitados en el programa (el cupo se elige de 1 a esto).
inline constexpr int kMaxInvitados = 64;

} // namespace ba::ipc
