// ============================================================================
// src/gateway/MediaRoute.hpp — A donde van (y de donde vienen) los medios de
//                              un participante
//
// Cada participante que publica medios tiene UNA sesion WebRTC durante toda su
// estadia. Lo que cambia al admitirlo es la RUTA, no la conexion:
//
//   Lobby(k)    sube -> BA.Lobby.<k> / BA.Audio.Lobby.<k> (solo lo ve el
//               operador, nunca el programa)
//               baja <- feed del OPERADOR (BA.Operator); su voz solo si
//               `talk` (conversacion privada)
//   Program(n)  sube -> BA.Guest.<n> / BA.Audio.Guest.<n> (+ pantalla en
//               BA.GuestShare.<n>)
//               baja <- programa + retorno de audio
//   None        nada (desconectado / sin lugar)
//
// Asi un invitado del lobby solo ve al operador (nunca a los demas del lobby
// ni al programa, donde aparecen los de adentro), y admitirlo es instantaneo.
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::gateway {

struct MediaRoute {
    enum class Kind : uint8_t { None, Program, Lobby };
    Kind kind = Kind::None;
    int index = 0;     ///< slot del programa (1..guests) o del lobby (1..lobbySlots)
    bool talk = false; ///< lobby: el operador le habla (le llega su voz)

    static MediaRoute program(int slot) { return {Kind::Program, slot, false}; }
    static MediaRoute lobby(int slot, bool talk) { return {Kind::Lobby, slot, talk}; }
    bool operator==(const MediaRoute& o) const {
        return kind == o.kind && index == o.index && talk == o.talk;
    }
    bool operator!=(const MediaRoute& o) const { return !(*this == o); }
};

} // namespace ba::gateway
