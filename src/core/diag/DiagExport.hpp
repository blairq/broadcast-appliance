// DiagExport.hpp
//
// Proposito:
//   Exportador de diagnostico (spec §11.4 "Boton 'Exportar diagnostico' en modo
//   ingenieria: genera un zip con logs y configuracion, sin secretos").
//   `exportBundle()` toma el directorio de logs rotativos (el que llena
//   LogRotator) y empaqueta en UN SOLO archivo (outPath) un encabezado de
//   metadatos (nlohmann ordered_json: host, timestamp, counts) + el contenido
//   de todos los logs rotados. Es la pieza de control que el launcher invoca
//   cuando el ingeniero pide el bundle; no toca secretos (los logs no los
//   contienen y la configuracion se agrega mas adelante fuera de este alcance).
//   No depende de Qt: solo std (filesystem + fstream) +
//   nlohmann/json (header-only, ya enlazado en ba_core).
//
// Modelo de threading:
//   Camino de CONTROL, NO de RT: exportBundle() lee varios archivos y escribe
//   uno, por lo que puede bloquear; debe llamarse desde un hilo de control (el
//   del launcher al pulsar el boton), nunca desde el bucle de audio. Es una
//   funcion static de estado puro sobre rutas: no hay estado compartido que
//   proteger, por lo que es segura de llamar desde cualquier hilo mientras no
//   se exporten dos bundles a la MISMA outPath.
//
// Estrategia de errores:
//   NUNCA lanza. exportBundle() devuelve un ExportResult {ok, path}: ok=false
//   ante cualquier fallo (logDir inexistible, outPath no escribible, disco
//   lleno) y deja `path` como la outPath solicitada (para que el caller sepa a
//   dónde iba a ir). writeMinidump() devuelve false ante cualquier fallo.
//
// Formato del bundle (UTF-8, texto plano):
//   Linea 1 (header):  #BA-DIAG v1
//   Linea 2:           el JSON de metadatos (un objeto, una linea) con
//                      {host, timestamp, logFiles, totalBytes}.
//   Despues, por cada log (ba.log, ba.log.1, ba.log.2, ...):
//     === <nombre> ===
//     <contenido del archivo, byte a byte>
//   El orden es del mas nuevo al mas antiguo (activo primero). El bundle es
//   autocontenido: quien lo reciba puede reconstruir cada log por su seccion.
//
// Minidumps (spec §11.4 "Minidumps propios (MiniDumpWriteDump) en
//   Logs/dumps/"): `writeMinidump()` es un STUB con gate por plataforma. En
//   Windows (_WIN32) documenta la implementacion real (MiniDumpWriteDump de
//   dbghelp.dll) pero todavia devuelve false (no cableado). En el resto de
//   plataformas devuelve false (no hay minidump nativo).

#pragma once

#include <string>

namespace ba::diag {

/// Resultado de una exportacion de diagnostico.
///
/// `ok` es false cuando la exportacion fallo; `path` es SIEMPRE la outPath
/// solicitada (aunque haya fallado), para que el caller sepa a donde iba a ir
/// el bundle. Pequeno y trivialmente copiable.
struct ExportResult {
    bool ok = false;
    std::string path;
};

/// Exportador de diagnostico (spec §11.4).
///
/// Todas las funciones son static y no lanzan: son operaciones de control sobre
/// rutas, sin estado persistente.
class DiagExport {
   public:
    /// Empaqueta los logs rotados de `logDir` + un encabezado de metadatos en
    /// un unico archivo en `outPath`.
    ///
    /// Crea el directorio padre de `outPath` si no existe. El bundle contiene
    /// un header JSON (host, timestamp, counts) y el contenido de cada log
    /// (ba.log, ba.log.1, ...) en secciones. No incluye secretos.
    ///
    /// No lanza. Ante cualquier fallo devuelve ExportResult{ok=false, path=
    /// outPath}; en exito, {ok=true, path=outPath}.
    ///
    /// :param logDir: directorio con los logs rotados (el de LogRotator).
    /// :param outPath: ruta completa del archivo de bundle a generar.
    /// :returns: ExportResult con ok=true y la outPath en exito.
    static ExportResult exportBundle(const std::string& logDir, const std::string& outPath);

    /// Escribe un minidump del proceso actual en `path` (spec §11.4).
    ///
    /// STUB con gate por plataforma (GATED:Win). En Windows la implementacion
    /// real usaria MiniDumpWriteDump de dbghelp.dll (ver el .cpp); por ahora
    /// devuelve false. En el resto de plataformas no hay minidump nativo y
    /// devuelve false. No lanza.
    ///
    /// :param path: ruta completa del archivo .dmp a escribir.
    /// :returns: true si se escribio el dump; false si fallo o no esta
    ///           disponible en esta plataforma.
    static bool writeMinidump(const std::string& path);
};

} // namespace ba::diag
