// LogRotator.hpp
//
// Proposito:
//   Rotador de logs por proceso (spec §11.4 "Logs rotativos por proceso").
//   Escribe lineas con timestamp a <dir>/ba.log y, cuando el archivo activo
//   supera maxBytes, lo rota a ba.log.1, ba.log.2, ... manteniendo a lo sumo
//   keepFiles archivos rotados (el mas antiguo se borra). Es la pieza de
//   diagnostico que deja en Logs/ un historial acotado del proceso sin que
//   crezca sin limite. No depende de Qt ni de libobs: solo std (filesystem +
//   fstream) y std::chrono para el timestamp.
//
// Modelo de threading:
//   Camino de CONTROL, NO de RT: no hay locks internos, la seguridad la aporta
//   el contrato de un solo writer por proceso (el hilo de log de A, B o del
//   launcher). No esta pensado para ser llamado desde el bucle de audio (RT):
//   write() toca el disco (open + write + rename) y puede bloquear, por lo que
//   debe usarse desde un hilo de control o de un worker, nunca desde el hilo
//   de render. Cada proceso tiene su propio LogRotator y su propio <dir>.
//
// Estrategia de errores:
//   NUNCA lanza. write() es no-fallible: ante un fallo de disco (directorio no
//   escribible, disco lleno, rename fallido) descarta la linea en lugar de
//   tirar una excepcion hacia el caller; el diagnostico no debe tumbar al
//   proceso. files() nunca lanza: si no puede listar el directorio devuelve un
//   vector vacio. El constructor tampoco lanza (un directorio que no se pudo
//   crear deja el rotador en estado "inactivo" y write() se vuelve no-op).
//
// Formato de linea (UTF-8):
//   [AAAA-MM-DDTHH:MM:SS.mmmZ] <linea>
//   El timestamp es UTC (system_clock) con milisegundos, para que los logs de
//   varios procesos se puedan entrelazar por hora. La linea original se
//   conserva tal cual despues del prefijo.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ba::diag {

/// Rotador de logs por proceso (spec §11.4).
///
/// Move-only, no copiable: posee rutas de archivo y estado del archivo activo.
///
/// Uso tipico:
///   - Uno por proceso (A, B, launcher): `LogRotator rot(Logs/<proc>, maxBytes,
///     keepFiles)` al arrancar; `rot.write("mensaje")` en cada evento de
///     diagnostico relevante (cambios de estado, errores, reinicios).
///   - En el cierre ordenado o al "Exportar diagnostico" (DiagExport), el
///     caller consume los archivos via `files()`.
///
/// Semantica de rotacion: antes de escribir, si el archivo activo ya tiene
/// maxBytes o mas (y no esta vacio), se rota: ba.log -> ba.log.1, ba.log.1 ->
/// ba.log.2, ... y el archivo rotado mas antiguo (indice keepFiles) se borra.
/// Asi el activo nunca supera maxBytes en mas de una linea y el total de
/// archivos rotados se mantiene acotado a keepFiles.
class LogRotator {
   public:
    /// Prepara el rotador para escribir en `dir`.
    ///
    /// Crea `dir` si no existe (best-effort). No crea el archivo activo:
    /// aparece con la primera linea. `maxBytes` es el umbral del archivo
    /// activo que dispara la rotacion (debe ser > 0; si llega 0 se normaliza
    /// a 1 para evitar una rotacion en cada linea). `keepFiles` es el maximo de
    /// archivos rotados a conservar (ba.log.1 .. ba.log.keepFiles); si llega 0
    /// se normaliza a 1 (siempre se conserva al menos el ultimo rotado).
    ///
    /// No lanza. Si no se pudo crear el directorio, el rotador queda inactivo
    /// y write() se vuelve no-op (el diagnostico no tumba al proceso).
    ///
    /// :param dir: directorio donde viven los logs (p. ej. Logs/).
    /// :param maxBytes: umbral en bytes del archivo activo que fuerza rotacion.
    /// :param keepFiles: maximo de archivos rotados a conservar.
    LogRotator(std::string dir, std::size_t maxBytes, std::size_t keepFiles);

    /// Escribe una linea con timestamp al archivo activo, rotando si hace falta.
    ///
    /// Ante un fallo de disco descarta la linea (no lanza). Ver la cabecera del
    /// modulo para el formato de linea y la semantica de rotacion.
    ///
    /// :param line: texto a loguear (se conserva tal cual tras el prefijo).
    void write(const std::string& line);

    /// Lista los archivos de log presentes en el directorio, ordenados de mas
    /// antiguo a mas nuevo.
    ///
    /// Incluye el archivo activo (ba.log) y los rotados (ba.log.1, ...). El
    /// orden es por "edad": el rotado de mayor indice es el mas antiguo, el
    /// activo es el mas nuevo. No lanza: si no puede listar, devuelve vacio.
    ///
    /// :returns: rutas completas de los archivos de log, de mas antiguo a mas
    ///           nuevo; vacio si no hay ninguno o no se pudo listar.
    std::vector<std::string> files() const;

   private:
    // --- estado -------------------------------------------------------------
    std::string dir_;
    std::string activePath_;
    std::size_t maxBytes_;
    std::size_t keepFiles_;
    bool active_ = false; // false si el directorio no se pudo crear

    // --- helpers (privados) -------------------------------------------------

    /// Tamano en bytes del archivo activo (0 si no existe). No lanza.
    std::size_t activeSize() const;

    /// Rota el archivo activo a la cadena de rotados, borrando el mas antiguo
    /// si excede keepFiles. No lanza: ante un fallo de disco deja el estado
    /// como pueda y sigue (la proxima write reintenta).
    void rotate();

    /// Ruta del archivo rotado de indice `n` (1..keepFiles): <dir>/ba.log.<n>.
    std::string rotatedPath(std::size_t n) const;
};

} // namespace ba::diag
