// ============================================================================
// src/motor/Memoria.hpp — la memoria privada del proceso y devolverle al sistema lo libre
//
// Después de la escalera hasta 200 en la .177, con la sala vacía, ba-app quedó con 5,7 GB
// privados (había arrancado con 1,9). Lo que se suelta al irse cada invitado (los cuadros
// del lector común, los búferes de sus cadenas al pasar a READY, las entradas que se
// borran al bajar el cupo) vuelve al montón del proceso, pero el montón no siempre se lo
// devuelve al sistema (Windows: los segmentos del montón y el LFH quedan comprometidos).
//
// `compactar` le pide a cada sistema que devuelva lo libre (Windows: HeapCompact en cada
// montón y _heapmin; Linux: malloc_trim; macOS: malloc_zone_pressure_relief) y dice cuánto
// bajó la memoria privada: si baja, lo que «no volvía» era el montón; si no baja, alguien
// lo tiene (y la medición lo dice). El motor la llama cuando la sala se vacía bastante
// (Motor::ponerLugares), nunca en cada cambio: recorrer el montón lo toma un rato.
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::motor::memoria {

/// La memoria privada del proceso en bytes (Windows: PrivateUsage; macOS: phys_footprint;
/// Linux: RssAnon de /proc/self/status); -1 si no se sabe.
std::int64_t privada();

struct Compactada {
    std::int64_t antes = -1, despues = -1; ///< privada(), en bytes
    std::int64_t us = 0;                   ///< lo que tardó
};
/// Devuelve al sistema lo libre del montón (ver arriba). Desde cualquier hilo; tarda (ms).
Compactada compactar();

} // namespace ba::motor::memoria
