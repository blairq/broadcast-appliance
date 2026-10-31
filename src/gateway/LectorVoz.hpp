// ============================================================================
// src/gateway/LectorVoz.hpp — el bucle de los hilos de voz de B (F2-14, F3-13)
//
// La voz del operador (BA.Audio.Operator), la del que atiende (BA.Audio.Atendedor) y la
// del asistente de cada canal de registro bajan igual: un anillo de A que se lee entero
// cada vez que hay algo y se reparte a quien corresponda. Antes cada uno miraba cada 2 ms
// con platform::Ritmo: 500 despertadas por segundo por hilo, hablara alguien o no (con
// cuatro tótems, seis hilos: 3000 por segundo para nada). Ahora duermen en el timbre del
// anillo (ipc/Timbre.hpp): el escritor toca con cada bloque y el lector se despierta una
// vez por bloque (100 por segundo mientras A escribe) y, quieto, solo por el vigía de 1 s.
// Con un escritor que no toca (uno viejo, un macOS anterior a 14.4), hasta la próxima
// mirada (ipc/Mirada.hpp): justo después del bloque que debería llegar.
//
// Sin anillo abierto (A todavía no lo creó, o el canal no conversa) se reintenta cada
// kReintento y se duerme entre medio, no de a 50 ms. Parar (`sigue` en false) se nota en
// a lo sumo kVigia (1 s) con el anillo abierto, como las bajadas de video.
//
// El retorno del programa NO usa esto: lee el mix-minus de cada lugar en la misma vuelta
// que el programa (RetornoLugar.hpp) y despertarse con el toque del programa lo leería
// antes de que el motor escriba el del lugar (otro hilo): un bloque de colchón más.
// ============================================================================
#pragma once

#include "ipc/SharedAudioRing.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace ba::gateway::voz {

/// Cada cuánto se reintenta abrir un anillo que todavía no está.
inline constexpr auto kReintento = std::chrono::milliseconds(500);
/// Sin timbre y con el escritor quieto más de 1 s: cada cuánto se mira (ipc/Mirada.hpp).
inline constexpr auto kPasoQuieto = std::chrono::milliseconds(10);

/// Lee el anillo `nombre` mientras `sigue()`: al abrirlo, `alAbrir()`; en cada vuelta con el
/// anillo abierto, `alLeer(SharedAudioRing&)` (que saca lo pendiente: leerPendientes) y
/// después `alVuelta()`; entre vuelta y vuelta, dormido en el timbre. `nombre` vacío: nunca
/// abre (duerme de a kReintento hasta que `sigue()` sea false).
template <typename Sigue, typename AlAbrir, typename AlLeer, typename AlVuelta>
void leer(const std::wstring& nombre, Sigue&& sigue, AlAbrir&& alAbrir, AlLeer&& alLeer, AlVuelta&& alVuelta) {
    using Reloj = std::chrono::steady_clock;
    ba::ipc::SharedAudioRing anillo;
    bool abierto = false;
    auto intento = Reloj::now() - kReintento;
    while (sigue()) {
        if (!abierto) {
            const auto ahora = Reloj::now();
            if (ahora - intento >= kReintento) {
                intento = ahora;
                if (!nombre.empty() && ba::ipc::SharedAudioRing::openReader(nombre.c_str(), anillo).ok) {
                    abierto = true;
                    alAbrir();
                    continue;
                }
            }
            ba::platform::dormirHasta(intento + kReintento);
            continue;
        }
        if (anillo.stale()) { // A se reinició o cerró el canal: a reabrir
            anillo = ba::ipc::SharedAudioRing{};
            abierto = false;
            continue;
        }
        const std::uint32_t visto = anillo.timbre(); // ANTES de mirar (ipc/Timbre.hpp)
        alLeer(anillo);
        alVuelta();
        if (anillo.pendientes() == 0) anillo.esperarNuevo(visto, kPasoQuieto);
    }
}

} // namespace ba::gateway::voz
