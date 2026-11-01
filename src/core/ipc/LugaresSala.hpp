// ============================================================================
// src/core/ipc/LugaresSala.hpp — cuántos invitados puede tener la sala
//
// UN solo número para los dos procesos: el gateway arma con él las sesiones
// WebRTC del pool y el lugar para los canales de cada invitado (cámara, pantalla
// y audio: BA.Guest.<n>, BA.GuestShare.<n>, BA.Audio.Guest.<n>); la app, el tope
// del cupo (Configuración → Invitados, el cupo en caliente, la línea de
// comandos, el esquema MCP de room_set) y el motor (los retornos). Los canales y
// las sesiones se crean recién al subir el cupo (MediaPump::ampliar): subir este
// tope no cuesta nada hasta que se use. Lo que sí cuesta es cada invitado
// conectado (un video que decodificar): eso lo mide el panel «Uso de la sala». El
// audio crece lineal (los retornos: una suma y una resta por invitado,
// baretornos) y el rango UDP de WebRTC se valida contra el cupo (PuertosRtc).
//
// La RESERVA (Configuración → «Lugares reservados») son los lugares con canales
// desde el arranque; el gateway arma los del cupo o los de la reserva, el mayor.
// ============================================================================
#pragma once

#include <algorithm>

namespace ba::ipc {

/// El tope de invitados en el programa (el cupo se elige de 1 a esto).
inline constexpr int kMaxInvitados = 64;

/// La reserva que se guarda: lo que pidió el operador, de 1 al tope (no se arrastra con el cupo).
inline int reservaValida(int reserva) { return std::clamp(reserva, 1, kMaxInvitados); }

/// Los lugares con canales desde el arranque: los del cupo o los de la reserva, el mayor.
inline int lugaresIniciales(int cupo, int reserva) { return std::max(std::clamp(cupo, 1, kMaxInvitados), reservaValida(reserva)); }

} // namespace ba::ipc
