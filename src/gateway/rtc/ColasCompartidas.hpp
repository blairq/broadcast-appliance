// ============================================================================
// src/gateway/rtc/ColasCompartidas.hpp — las colas de tareas de libwebrtc sin un hilo por cola
//
// libwebrtc crea una TaskQueue por stream y la de fábrica es un hilo por cola: con 40
// invitados, 40 «AudioEncoder», 40 «EncoderQueue», 80 «DecodingQueue»… cada uno con su
// despertada por tarea. Medido en la .177 (2026-11-02, 40 invitados): 647 hilos y 45 mil
// cambios de contexto/s, más de la mitad de esas colas. docs/planes/PLAN-GATEWAY-FRAGMENTOS.md
//
// Acá las colas se MULTIPLEXAN sobre dos grupos chicos de hilos:
//   - «pesadas» (decodificar video, ms por tarea): `gateway.hilosColas` hilos (0 = los
//     núcleos físicos de un bloque de caché, entre 2 y 6);
//   - «livianas» (todo lo demás: Opus y video de paso del retorno, µs por tarea): 2 hilos.
//   Así lo pesado no frena al audio.
//
// Cada cola cumple el contrato de webrtc::TaskQueueBase:
//   - FIFO por cola y nunca dos tareas de la misma cola a la vez (una cola la toma un hilo
//     por turno; en otro turno puede ser otro hilo);
//   - PostDelayedTask: un hilo del grupo vigila el vencimiento más próximo (en Windows con
//     un temporizador de alta resolución; en macOS/Linux, el planificador ya es preciso);
//   - IsCurrent/Current mientras corre una tarea (CurrentTaskQueueSetter);
//   - Delete: al volver no corre ninguna tarea ni empieza otra; las pendientes se destruyen
//     ahí (si se llama desde una tarea de la misma cola, termina esa y no corre más).
//
// Despertar en lote: una cola que ya está programada (o corriendo) no despierta a nadie al
// recibir otra tarea; un hilo del grupo se despierta solo si hay más colas listas que
// hilos ya avisados. Un turno corre varias tareas seguidas de la misma cola (hasta 16 o
// 2 ms) antes de pasar a la siguiente.
//
// Contadores (gateway_estado → fronteras): «cola.<nombre>» por tipo de cola (tareas/s,
// trabajo y espera por tarea) y «colas.<grupo>» (turnos/s y despertadas/s).
// ============================================================================
#pragma once

#include <memory>
#include <string_view>

namespace webrtc {
class TaskQueueFactory;
}

namespace ba::gateway::colas {

struct Opciones {
    int hilosPesadas = 0;  ///< 0 = automático (nucleosDeUnBloque)
    int hilosLivianas = 0; ///< 0 = automático (2; 1 si pesadas es 1)
};

/// Los núcleos físicos que comparten el último nivel de caché con el núcleo 0 (un CCD en
/// Ryzen, un grupo de rendimiento en Apple M), sin limitar. 0 si el sistema no lo dice.
int nucleosDeUnBloque();

/// Los hilos de cada grupo que usaría `o` en esta máquina (después de lo automático).
Opciones resolver(const Opciones& o);

/// Si la cola `nombre` (el que le pone libwebrtc) va al grupo de las pesadas.
bool esPesada(std::string_view nombre);

/// La fábrica con sus dos grupos de hilos (arrancan ya). Los grupos viven mientras viva la
/// fábrica o alguna de sus colas; al destruir la fábrica, los hilos terminan lo que haya
/// listo y salen.
std::unique_ptr<webrtc::TaskQueueFactory> crear(const Opciones& o);

/// La de los ajustes (`gateway.hilosColas`).
std::unique_ptr<webrtc::TaskQueueFactory> crearDeLosAjustes();

} // namespace ba::gateway::colas
