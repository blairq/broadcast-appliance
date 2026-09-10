// Restore.hpp
//
// Proposito:
//   RESTORACION (spec §11.3) y flujos de RECUPERACION del snapshot de estado.
//   Es la contraparte de LECTURA de SnapshotStore (§11.2): dado un archivo de
//   snapshot (o un directorio con las generaciones retenidas), valida su
//   integridad (checksum SHA-256 del cuerpo) y lo deserializa a un StateDoc
//   (§11.1). Es la pieza que el launcher usa al arrancar para volver a subir el
//   ultimo estado valido (crash de A, corte de energia) y el "plan B" cuando la
//   generacion mas nueva esta corrupta: se saltea y se cae a la anterior.
//
//   Reutiliza la capa de persistencia existente SIN modificarla:
//     - el checksum es SnapshotStore::sha256Hex (el MISMO SHA-256 que
//       SnapshotStore escribe en el header), de modo que un archivo que
//       SnapshotStore escribio siempre pasa la verificacion de Restore y un
//       archivo con bytes alterados siempre la falla;
//     - el JSON del cuerpo se interpreta con StateDoc::deserialize (esquema
//       fijo, nlohmann/json);
//     - el formato de archivo (header + body) es el de SnapshotStore (§11.2).
//   No se re-implanta JSON a mano: se delega en StateDoc (nlohmann).
//
// Modelo de threading / control-path:
//   Operacion de CONTROL, no del camino de audio en tiempo real: hace I/O de
//   archivo (lee el archivo completo a memoria) y un hash SHA-256 sobre el
//   cuerpo, ambos con asignaciones y costosos. Debe llamarse en el hilo de
//   control / en el arranque (antes de que A escriba), NUNCA en la callback de
//   audio RT. No es thread-safe por si mismo, pero es STATELESS (solo funciones
//   estaticas puras sobre rutas en disco): no hay estado compartido que
//   proteger; cada llamada es independiente.
//
// Estrategia de errores:
//   NUNCA lanza. restore() y recover() devuelven un RestoreResult {status,
//   detail}; `out` solo se reemplaza cuando status==Ok. Los fallos se
//   clasifican:
//     - Missing:          el archivo no existe / el dir no tiene snapshots
//                         buenos.
//     - ChecksumMismatch: el SHA-256 del cuerpo no coincide con el del header
//                         (corrupcion detectada).
//     - Corrupt:          header malformado o cuerpo que no es un StateDoc
//                         JSON valido (no se pudo interpretar).
//   Un archivo corrupto NUNCA es una excepcion: se reporta y (en recover) se
//   saltea, cayendo a la generacion anterior.
//
// Formato de archivo (debe coincidir con SnapshotStore, §11.2):
//   Linea 1 (header):  #BA-SNAP v1 seq=<uint64> sha256=<hex64>
//   Linea 2..N (body):  el JSON serializado de StateDoc.
//   El checksum es SHA-256 sobre los bytes del BODY (sin el header).

#pragma once

#include <cstdint>
#include <string>

#include "StateDoc.hpp"

namespace ba::persist {

/// Resultado de una operacion de restauracion/recuperacion.
///
/// `status` clasifica el resultado; `detail` es un mensaje humano-legible con
/// el motivo (ruta, checksum esperado vs. obtenido) para logueo estructurado.
/// Pequeno y trivialmente copiable. El `out` (el StateDoc) solo es valido
/// cuando `status == Status::Ok`; en cualquier otro status queda
/// indeterminado (no se toca).
struct RestoreResult {
    /// Clasificacion del resultado de una restauracion/recuperacion.
    enum class Status {
        Ok,               // se restauro un snapshot valido.
        ChecksumMismatch, // el SHA-256 del cuerpo no coincide con el del header.
        Corrupt,          // header malformado o cuerpo no-JSON/StateDoc.
        Missing,          // archivo inexistente o sin snapshots buenos en el dir.
    };

    Status status = Status::Missing;
    std::string detail;
};

/// RESTORACION (spec §11.3) y flujos de RECUPERACION del snapshot.
///
/// Stateless: solo funciones estaticas puras sobre rutas en disco. No posee
/// estado, no hay que inicializar nada (a diferencia de SnapshotStore, que
/// mantiene rutas y debounce). Reutiliza el checksum de SnapshotStore y el
/// JSON de StateDoc (no re-implanta nada). Ninguna funcion lanza.
class Restore {
public:
    /// Restaura UN archivo de snapshot a `out`.
    ///
    /// Lee el archivo, valida el header, verifica el checksum SHA-256 del
    /// cuerpo (reutilizando SnapshotStore::sha256Hex) y deserializa el cuerpo
    /// a un StateDoc (StateDoc::deserialize). NUNCA lanza; clasifica el fallo
    /// en RestoreResult::Status:
    ///   - Missing:          el archivo no existe o no se puede leer.
    ///   - ChecksumMismatch: el checksum del cuerpo no coincide con el header.
    ///   - Corrupt:          header malformado o cuerpo que no es un StateDoc
    ///                        JSON valido.
    ///   - Ok:               `out` recibe el documento restaurado.
    ///
    /// :param snapshotPath: ruta completa del archivo de snapshot a restaurar.
    /// :param out: recibe el StateDoc solo si status==Ok; en otro caso queda
    ///             indeterminado (no se toca).
    /// :returns: RestoreResult con el status y un detail descriptivo.
    static RestoreResult restore(const std::string& snapshotPath, StateDoc& out);

    /// Flujo de RECUPERACION: elige el snapshot BUENO mas nuevo entre las
    /// generaciones retenidas en `dir` y lo restaura a `out`.
    ///
    /// Politica de eleccion (documentada):
    ///   1. Se consideran SOLO los archivos de snapshot retenidos en `dir`:
    ///      los regulares cuyo nombre sigue el patron de generacion
    ///      `snapshot.*.json` (las generaciones A/B por defecto, y cualquier
    ///      nombre de generacion personalizado que siga la convencion). Los
    ///      temporales (`*.json.tmp`) y los no-snapshot se ignoran.
    ///   2. Cada candidato se valida con la MISMA logica que restore() (header
    ///      + checksum + deserializacion). Los que fallan (ChecksumMismatch /
    ///      Corrupt) se SALTAN: no son el resultado, solo se omiten.
    ///   3. Entre los validos se elige el de MAYOR `sequence` (el mas nuevo; el
    ///      mismo criterio que SnapshotStore::readLatest, spec §11.2 "se toma
    ///      la mas nueva que sea valida"). Empate de secuencia: gana el primero
    ///      en orden alfabetico de ruta (los candidatos se iteran ordenados,
    ///      por lo que la eleccion es determinista).
    ///   4. Si al menos uno es bueno, status==Ok y `out` recibe el mas nuevo
    ///      bueno. Si ninguno es bueno (o no hay candidatos), status==Missing.
    ///
    /// NUNCA lanza. Un snapshot corrupto nunca impide la recuperacion: se
    /// saltea y se cae a la generacion anterior (spec §11.3: recuperacion ante
    /// corrupcion de la generacion mas reciente, p. ej. tras un corte de
    /// energia a mitad de escritura).
    ///
    /// :param dir: directorio donde viven las generaciones retenidas.
    /// :param out: recibe el StateDoc del snapshot bueno mas nuevo si
    ///             status==Ok; en otro caso queda indeterminado.
    /// :returns: RestoreResult con Ok (y el doc) o Missing (y detail).
    static RestoreResult recover(const std::string& dir, StateDoc& out);
};

} // namespace ba::persist
