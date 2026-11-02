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

#include <cstdint>
#include <filesystem>
#include <string>

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

/// El id del hilo que llama, como lo escriben las pilas: el `thread_id` de Mach
/// (pthread_threadid_np) en macOS, el tid en Linux, GetCurrentThreadId en Windows.
std::uint64_t idDelHilo();

/// Lo que dejó una captura de pilas en vivo (capturarPilas).
struct CapturaPilas {
    std::filesystem::path archivo; ///< el .pila (POSIX) o el .dmp (Windows); vacío si no se pudo
    std::filesystem::path json;    ///< el .json de al lado
    std::string error;             ///< por qué no (vacío si salió)
    bool porLauncher = false;      ///< Windows: lo escribió el launcher (desde afuera)
};

/// LAS PILAS DE TODOS LOS HILOS SIN CAERSE (la traba de la interfaz). Se llama
/// desde un hilo vigía, nunca desde el que está trabado. El proceso sigue: nada
/// se mata ni se relanza.
///
///   - macOS: cada hilo se congela un instante con Mach y se le sigue la cadena
///     de punteros de marco (lo mismo que en una caída); Linux: una señal propia
///     a cada hilo. Va a `<dir>/<base>.pila` (el formato de siempre, con
///     «motivo <motivo>» y «interfaz <id>» en la cabecera) y su .json.
///   - Windows: se le pide al launcher por el canal de caídas (CaidaCompartida)
///     un minidump chico (las pilas y los hilos con su nombre, sin excepción)
///     que escribe DESDE AFUERA; sin launcher, se vuelca solo. `<dir>/<base>.dmp`.
///
/// :param base: el nombre sin extensión («A-20261102-101500-traba-2000»).
/// :param hiloInterfaz: idDelHilo() del de la interfaz (queda marcado).
/// :param extra: lo que va además al .json (lo que corría, el umbral…), en JSON.
CapturaPilas capturarPilas(const std::filesystem::path& dir, const std::string& base, const std::string& motivo,
                           std::uint64_t hiloInterfaz, const std::string& extraJson = "{}");

/// De las pilas de una captura (el texto del .pila), qué hacía el hilo `id`, en
/// una línea: «esperando al hilo de render de Qt (sincronización del scene
/// graph)», «en un bucle nativo del sistema (…)», «en código propio: …» o el
/// módulo y la función de arriba. Vacío si ese hilo no está.
std::string resumirPila(const std::string& texto, std::uint64_t id);


} // namespace ba::diag
