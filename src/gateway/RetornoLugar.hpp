// ============================================================================
// src/gateway/RetornoLugar.hpp — qué oye cada invitado al aire: su mix-minus o nada
//
// El retorno de la salida por omisión baja al ritmo del programa (BA.Audio.ContentForReturn):
// por cada bloque del programa, a cada invitado del programa se le manda un bloque de SU
// mix-minus (BA.Audio.Return.<n>, el programa sin su voz). Las reglas
// (docs/arquitectura/04-plan-unificado.md, F0-01 y F0-04):
//
//   - Un invitado al aire NUNCA recibe el programa entero: tiene su propia voz y la oiría
//     con medio segundo de demora (eco). Si su mix-minus tiene un hueco, ese bloque va en
//     silencio; si su mix-minus no está abierto (la app todavía no lo armó, se cayó, o el
//     operador le apagó el retorno), «retorno apagado»: silencio, y la sala lo publica
//     para que el operador lo vea. Si el operador eligió que oiga el programa entero, la
//     app igual arma su anillo (con su voz adentro): el gateway no adivina el modo.
//   - Los demás (la fila, las pantallas de escena) oyen el programa.
//
// Por qué un bloque del mix-minus por bloque del programa, y no alineados por sello QPC:
// los dos anillos los escribe el mismo motor con el mismo reloj (baanillosink, sync=true),
// así que llegan en la misma vuelta de 2 ms, sin deriva. El del lugar puede ir hasta un
// bloque adelante o atrás (cada mezcla corta sus bloques de 480 cuadros donde le toca):
// un hueco del lugar arma un bloque de colchón y desde ahí no hay más huecos, y si el
// lugar se adelantó más de dos bloques se pone al día tirando los viejos (latencia
// acotada a 20 ms). Alinear por sello contra el PROGRAMA sería además el objetivo
// equivocado: lo que oye un invitado que conversa es su mix-minus, no el programa, y
// cuando los retornos sean buses de oído con latencia propia más corta (F2-01) alinear
// contra el programa le devolvería la demora que se le sacó. El programa acá es solo el
// metrónomo. Los huecos y la ocupación de cada lugar se miden (MedidaTransporte) para
// ver si esto alcanza en la máquina real.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "MediaRoute.hpp"
#include "ipc/MedidaTransporte.hpp"
#include "ipc/SharedAudioRing.hpp"

namespace ba::gateway::retorno {

/// Hasta cuántos bloques del lugar se dejan esperando (el resto se tira: es demora).
inline constexpr std::uint64_t kColchonLugar = 2;

/// El bloque del mix-minus del lugar que va con el bloque del programa que se reparte ahora:
/// el más viejo que quede después de ponerse al día, o `silencio` si no llegó (un hueco).
/// `buf` es del lugar; `n`, los floats del bloque del programa (un lugar con otro formato
/// no sirve: silencio).
inline const float* bloqueDelLugar(ipc::SharedAudioRing& lugar, std::vector<float>& buf, const float* silencio,
                                   std::size_t n, ipc::MedidaTransporte& medida, std::uint64_t ahoraQpc) {
    if (lugar.blockFloats() != n) return silencio;
    if (buf.size() < n) buf.resize(n);
    medida.mirar(lugar.pendientes());
    medida.descartesDelEscritor(lugar.descartados());
    ipc::PoppedBlock b;
    // Se atrasó respecto del programa: se pone al día (sin sumar demora).
    while (lugar.pendientes() > kColchonLugar) lugar.pop(buf.data(), b);
    if (lugar.pendientes() == 0 || !lugar.pop(buf.data(), b)) {
        medida.hueco();
        return silencio;
    }
    medida.bloque(b.qpc, ahoraQpc);
    return b.samples;
}

/// Lo que se le manda a la sesión con ruta `r` junto con el bloque `programa`. `deLugar[n]`:
/// el bloque del mix-minus del lugar n en esta vuelta, nullptr si su anillo no está abierto
/// (vacío si este retorno no tiene mix-minus: el de otra salida, que todavía baja entero).
inline const float* fuente(const MediaRoute& r, const float* programa, const std::vector<const float*>& deLugar,
                           const float* silencio) {
    if (deLugar.empty() || r.kind != MediaRoute::Kind::Program || r.index <= 0) return programa;
    const auto i = static_cast<std::size_t>(r.index);
    if (i >= deLugar.size() || deLugar[i] == nullptr) return silencio; // retorno apagado
    return deLugar[i];
}

} // namespace ba::gateway::retorno
