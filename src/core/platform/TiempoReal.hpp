// ============================================================================
// src/core/platform/TiempoReal.hpp — tiempo real para los POCOS hilos de audio con plazo
//
// Un hilo que tiene que entregar un bloque de audio cada 10 ms (la salida de un
// mezclador de bus, la matriz, el retorno a los invitados, el que tira del
// audio de libwebrtc) no puede esperar a que el sistema termine con un modelo
// local, la superresolución o ONNX: se oye como un corte. Con esto el sistema
// lo atiende a tiempo:
//
//   - Windows: MMCSS «Pro Audio» con prioridad alta (avrt.dll, que se carga al
//     llamar: es parte de Windows desde Vista, sin enlazarla).
//   - macOS: THREAD_TIME_CONSTRAINT_POLICY: período = el bloque (10 ms),
//     cómputo 2 ms, plazo 5 ms. Lo mismo que pide Core Audio para sus hilos.
//   - Linux: SCHED_FIFO si RLIMIT_RTPRIO lo permite (limits.conf o el grupo
//     «audio»); si no, un nice de −10 (si RLIMIT_NICE deja). Nada de
//     RealtimeKit ni servicios del sistema: portable.
//
// SOLO para hilos que no toman candados largos ni hacen E/S (el registro ya no
// traba: diag/Registro.hpp). Subir TODOS los hilos de streaming empeora (más
// cambios de contexto: ver motor/Prioridad.cpp) y con un candado compartido es
// inversión de prioridad: el hilo de tiempo real esperando a uno normal.
//
// Los hilos de GStreamer se reusan: el que hoy mueve audio mañana puede mover
// video. Por eso `soltarTiempoReal()` al dejar la tarea (STREAM_STATUS LEAVE).
// ============================================================================
#pragma once

#include <string>

namespace ba::platform {

/// El hilo que llama mueve audio en bloques de `periodoMs`. true si quedó en
/// tiempo real (o con la prioridad alta de respaldo, en Linux sin permisos).
bool tiempoRealAudio(int periodoMs = 10);

/// Vuelve el hilo que llama a lo normal, si `tiempoRealAudio()` lo había subido.
void soltarTiempoReal();

struct EstadoTiempoReal {
    int hilos = 0;     ///< cuántos hilos están en tiempo real ahora
    int fallidos = 0;  ///< cuántas veces no se pudo (sin permisos, sin avrt.dll)
    std::string modo;  ///< "mmcss", "time-constraint", "sched-fifo", "nice" o "" (ninguno todavía)
};
EstadoTiempoReal estadoTiempoReal();

} // namespace ba::platform
