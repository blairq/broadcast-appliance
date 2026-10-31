// ============================================================================
// src/core/diag/Caidas.hpp — que una caida deje algo escrito
//
// EL PROBLEMA, visto en vivo: el Proceso A se murio con SIGSEGV y en el
// registro no quedo NADA. Ni una linea. El launcher lo anoto («A termino
// inesperadamente, code=139») y ahi se acabo la informacion: apport ignora los
// binarios que no vienen de un paquete y `ulimit -c` es 0, asi que tampoco hay
// volcado. Una caida que no deja rastro es una caida que no se puede arreglar.
//
// Esto instala un manejador para las señales que matan por un error de
// programa (SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE) y escribe la pila de
// llamadas ANTES de morir.
//
// ASYNC-SIGNAL-SAFETY, que es lo que hace que esto sea correcto y no un adorno
// que falla justo cuando hace falta: adentro de un manejador de señal no se
// puede asignar memoria ni tomar un mutex, asi que NO se usa el Registro (que
// hace las dos cosas). Se escribe con `write()` directo al descriptor 2, que
// es donde el launcher captura la salida: termina en el mismo app.log, entre
// las lineas normales, y el visor lo muestra como cualquier otra cosa.
//
// Despues de escribir, se vuelve a lanzar la señal con el manejador por
// defecto: el proceso muere con el codigo que corresponde (139 para SIGSEGV) y
// el launcher hace lo que ya hacia. Esto no cambia el comportamiento, solo
// deja la prueba.
//
// LAS PILAS DE TODOS LOS HILOS (F1-12). Además de la del que se cayó, al
// archivo que dejó el launcher (BA_CAIDA_CANAL = Logs/dumps/<A|B>.pila) van las
// de todos los hilos con su nombre y las imágenes cargadas con su UUID /
// build-id: en macOS congelando cada hilo con Mach y siguiendo sus punteros de
// marco; en Linux pidiéndole a cada uno su pila con una señal propia. SIGQUIT es
// el pedido del launcher para un proceso que dejó de latir. El launcher
// (Volcados) lo renombra, lo atribuye (fault::atribuir) y lo deja para
// tools/caidas/analizar.py. Sin launcher, todo va al registro.
// ============================================================================
#pragma once

namespace ba::diag {

/// Instala el manejador. Idempotente.
///
/// En Windows: excepción sin atrapar, corrupción del heap (vectorizada: no
/// llega al filtro), abort/terminate, llamada pura y parámetro inválido. Escribe
/// la línea al archivo del registro y le pide el volcado al launcher, con la
/// excepción (ver CaidaCompartida.hpp); sin launcher, se vuelca solo. Lo que
/// no pasa por ningún manejador (__fastfail) lo ve WER y, si se cuelga, el
/// launcher.
///
/// :param quien: nombre del proceso, para que la linea diga cual se cayo.
void instalarManejadorDeCaidas(const char* quien);

} // namespace ba::diag
