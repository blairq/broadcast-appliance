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
//               `talk` (conversacion privada); con `atiende`, la voz del
//               asistente que lo atiende (BA.Audio.Atendedor)
//   Program(n)  sube -> BA.Guest.<n> / BA.Audio.Guest.<n> (+ pantalla en
//               BA.GuestShare.<n>)
//               baja <- programa + retorno de audio
//   Backstage(k) (modo oradores, esperando turno)
//               sube -> camara a BA.Backstage.<k> si tiene lugar de preview
//               (chica, solo la vista del operador; nunca el programa)
//               baja <- programa + retorno de audio
//   Escena(k)   (una pantalla de la sala que muestra una escena que no es el
//               programa, docs/funciones/PANTALLAS.md) no sube nada
//               baja <- BA.Escena.<k> + retorno de audio
//   None        nada (desconectado / sin lugar)
//
// Asi un invitado del lobby solo ve al operador (nunca a los demas del lobby
// ni al programa, donde aparecen los de adentro), y admitirlo es instantaneo.
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::gateway {

/// El tope de invitados en el programa. El cupo se cambia en caliente hasta acá:
/// el gateway crea los canales de un lugar cuando el cupo lo alcanza
/// (MediaPump::ampliar) y las sesiones WebRTC del pool son livianas hasta que
/// alguien se conecta, así que no hace falta reiniciar la sala.
inline constexpr int kMaxInvitados = 16;

struct MediaRoute {
    /// Backstage: admitido en modo oradores, esperando su turno: RECIBE el
    /// programa pero lo que manda no va a ningun lado (ni al programa).
    /// Remoto: un integrante remoto con la palabra (docs/funciones/SESION-REMOTA.md R3): lo que
    /// manda va a BA.Audio.Remoto.<n> / BA.Remoto.<n>; no recibe nada por acá (el
    /// programa lo ve en su página).
    /// Escena: una pantalla que muestra la escena del canal `index` (BA.Escena.<k>,
    /// 0..) en vez del programa; no sube nada.
    enum class Kind : uint8_t { None, Program, Lobby, Backstage, Remoto, Escena };
    Kind kind = Kind::None;
    int index = 0;     ///< slot del programa (1..guests) o del lobby (1..lobbySlots)
    bool talk = false; ///< el operador le habla (le llega su voz; adentro, mezclada con el retorno: IFB)
    /// Lobby: lo atiende el asistente HABLANDO (docs/funciones/ATENCION.md). Su voz sube a
    /// BA.Audio.Lobby.<k> (la app la pasa a texto) y le baja la voz del que atiende
    /// (BA.Audio.Atendedor), no la del operador. Si el operador además le habla (`talk`),
    /// gana el operador: dos voces en el mismo track se pisan.
    bool atiende = false;

    static MediaRoute program(int slot, bool talk = false) { return {Kind::Program, slot, talk}; }
    static MediaRoute lobby(int slot, bool talk, bool atiende = false) { return {Kind::Lobby, slot, talk, atiende}; }
    /// `preview`: lugar de preview de la fila (1..backstageSlots) -> su camara
    /// sube a BA.Backstage.<k> (chica, solo para la vista del operador); 0 = no sube.
    static MediaRoute backstage(int preview = 0, bool talk = false) { return {Kind::Backstage, preview, talk}; }
    static MediaRoute remoto(int canal) { return {Kind::Remoto, canal, false}; }
    static MediaRoute escena(int canal) { return {Kind::Escena, canal, false}; }
    bool operator==(const MediaRoute& o) const {
        return kind == o.kind && index == o.index && talk == o.talk && atiende == o.atiende;
    }
    bool operator!=(const MediaRoute& o) const { return !(*this == o); }
};

} // namespace ba::gateway
