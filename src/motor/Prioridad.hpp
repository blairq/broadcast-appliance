// ============================================================================
// src/motor/Prioridad.hpp — que el video y el audio no salten a núcleos lentos
//
// Los procesadores de hoy tienen núcleos de dos clases (Apple M: rendimiento y
// eficiencia; Intel desde la 12.ª: P y E) o varios grupos de caché (AMD Ryzen de
// 12 núcleos o más: un CCD por L3). El sistema mueve cada hilo según lo que cree
// que necesita, y un hilo de streaming que cae en un núcleo de eficiencia tarda
// más: los cuadros llegan tarde a la mezcla. Medido en la M1 Pro (2026-10-20): el
// mismo trabajo ocupaba 70 % o 98 % de un núcleo según dónde lo pusiera macOS (para
// comparar versiones: instrucciones y ciclos, no el porcentaje).
//
//   - macOS: el proceso sin App Nap ni temporizadores juntados. Los hilos, como los
//     pone el sistema: con QoS interactiva en los de streaming empeoraba (más cambios
//     de contexto, ver prioridadDelHilo); lo que pesaba era la cola de GL.
//   - Windows: el proceso y esos hilos sobre lo normal y sin EcoQoS (el «ahorro de
//     energía» que en un Intel híbrido los manda a los núcleos E).
//   - Linux: se intenta subir la prioridad de esos hilos (sin permisos no se puede:
//     se avisa una vez).
//   - Los POCOS hilos de audio con plazo (ClaseHilo::Audio), en tiempo real: MMCSS
//     «Pro Audio», THREAD_TIME_CONSTRAINT_POLICY o SCHED_FIFO (platform/TiempoReal).
//   - Los hilos de medios (entradas, el cruce a cada capa, los mezcladores, el bucle
//     del motor) en UN grupo de núcleos que comparte la L3: un CCD en AMD (y lo demás
//     del proceso al otro) o los de rendimiento en Intel híbrido; Windows con CPU Sets
//     (blandos), Linux con afinidad. Lo que se pasan (bloques de audio de 10 ms,
//     encargos a la GPU) queda en la misma caché y no salta de CCD.
//
// Ajustes: `procesador.prioridad` y `procesador.mediosJuntos` (de fábrica, los dos sí).
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>

namespace ba::motor {

enum class ClaseHilo {
    Audio,   ///< audio con plazo de 10 ms: la salida de cada audiomixer de bus, baanillo/baanillosink, la
             ///< matriz. Tiempo real (platform/TiempoReal); si el sistema no deja, como Critico
    Critico, ///< los que marcan el ritmo del video: el compositor (aggregator)
    Medios,  ///< cualquier otro hilo de streaming de GStreamer
    Motor,   ///< el bucle del motor (órdenes, vigilancia: cada 20 ms)
};

/// Una vez, al arrancar el motor: el proceso entero (ver arriba) y la topología al registro.
void prioridadDelProceso();
/// El hilo que llama (si `procesador.prioridad`).
void prioridadDelHilo(ClaseHilo c);
/// Que los hilos de streaming de `pipeline` tomen su nombre («<pipeline>/<elemento>»,
/// platform/Hilo.hpp) y su clase al arrancar: GStreamer avisa STREAM_STATUS ENTER desde el
/// propio hilo nuevo (manejador síncrono del bus); el de un mezclador de audio o el que
/// lleva/trae audio del gateway es Audio, el de otro aggregator Critico, el resto Medios. Al
/// dejar la tarea (LEAVE) el hilo vuelve a lo normal: GStreamer lo reusa para otra cosa. Lo
/// llama Reloj::adoptar, por donde pasan todos los pipelines del motor. Subir TODOS empeoraba (compiten entre ellos: ver Prioridad.cpp); la guía de GStreamer y la de
/// Intel dicen lo mismo: solo los que hacen falta.
void prioridadEnPipeline(GstElement* pipeline);
/// El procesador: núcleos por clase, grupos de L3, lo aplicado. Para el diagnóstico.
nlohmann::json topologiaCpu();

} // namespace ba::motor
