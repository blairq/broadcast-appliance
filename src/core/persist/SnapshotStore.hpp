// SnapshotStore.hpp
//
// Proposito:
//   Writer/reader del snapshot de estado (spec §11.2) con DOBLE GENERACION
//   alternada (snapshot.A.json / snapshot.B.json). Es la pieza que persiste el
//   StateDoc (la fuente de verdad, spec §11.1) en el disco de forma ATOMICA y
//   con DEBOUNCE, de modo que mover un fader no genere cientos de escrituras
//   (maximo una escritura fisica cada 500 ms; los eventos criticos se escriben
//   de inmediato). No depende de Qt ni de libobs: solo std + llamadas de
//   sistema de archivo (temp + fsync + rename en Linux; temp +
//   FlushFileBuffers + MoveFileExW en Windows).
//
// Modelo de threading:
//   Un solo writer por store (el hilo de control de A) llama
//   scheduleWrite()/flushNow(); readLatest() lo puede llamar el launcher u otro
//   proceso al arrancar (antes de que A escriba). El objeto NO es thread-safe
//   por si mismo: no hay locks internos, la seguridad la aporta el contrato de
//   un solo writer + el rename atomico (un lector nunca ve un archivo a
//   medio escribir: o es la generacion anterior completa o la nueva completa).
//   El debounce usa steady_clock (no wall clock) para que no se vea afectado
//   por cambios de hora del sistema.
//
// Estrategia de errores:
//   init() y readLatest() devuelven un Status {ok, code} y NUNCA lanzan.
//   scheduleWrite()/flushNow() son no-fallibles sobre un store inicializado:
//   ante un fallo de disco (o disco "ausente" por fault-injection) NO pierden
//   el estado: lo guardan en memoria (pendingDoc_) y marcan un reintento
//   pendiente; el caller lo resuelve con flushNow() cuando el disco vuelve.
//   Un archivo con checksum invalido se trata como INVALIDO y se cae a la otra
//   generacion (readLatest nunca lanza por un archivo corrupto).
//
// Formato de archivo (UTF-8):
//   Linea 1 (header):  #BA-SNAP v1 seq=<uint64> sha256=<hex64>
//   Linea 2..N (body): el JSON serializado de StateDoc (StateDoc::serialize()).
//   El checksum es SHA-256 (FIPS 180-4) sobre los bytes del BODY (sin el
//   header). Al leer se recalcula y se compara con el hex del header; si no
//   coincide, la generacion es INVALIDA.
//
// Secuencia de escritura (spec §11.2):
//   serialize -> escribir en un TEMP file en el MISMO directorio ->
//   fsync(fd) -> rename atomico sobre la generacion elegida (alterna A/B en
//   cada escritura). El temp en el mismo fs garantiza que el rename sea
//   atomico (no hay ventana de "archivo a medio escribir").

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "StateDoc.hpp"

namespace ba::persist {

/// Resultado de una operacion de SnapshotStore.
///
/// `ok` es false cuando la operacion fallo; `code` es 0 en exito y un valor
/// negativo de :type:`ErrorCode` en fallo. Pequeno y trivialmente copiable.
struct Status {
    bool ok = false;
    int code = 0;

    static Status success() { return Status{true, 0}; }
    static Status fail(int code) { return Status{false, code}; }
};

/// Codigos de error estables para logueo estructurado (componente, codigo,
/// contexto). Son negativos para que `code == 0` signifique siempre "ok".
enum class ErrorCode : int {
    kOk = 0,
    kInvalidArgument = -1, // dir vacio, fileA == fileB, etc.
    kNotInitialized = -2,  // operacion sobre un store sin init()
    kIo = -3,              // fallo de sistema al escribir/leer/rename
    kNotFound = -4,        // readLatest: ninguna generacion valida
    kCorrupt = -5,         // (interno) checksum invalido
};

/// Writer/reader del snapshot con doble generacion alternada (spec §11.2).
///
/// Move-only, no copiable: posee rutas de archivo y estado de debounce.
///
/// Uso tipico:
///   - A (app): `init(dir)` al arrancar; `scheduleWrite(doc, critico)` en cada
///     cambio de estado (critico=true para iniciar/detener vivo o cambiar
///     escena); `flushNow()` en el cierre ordenado (tras fijar
///     doc.cleanShutdown=true).
///   - Launcher / arranque: `readLatest(doc)` para restaurar el ultimo estado
///     valido (el de mayor secuencia con checksum correcto).
class SnapshotStore {
   public:
    /// Paso de la escritura donde se puede inyectar un fallo (para tests).
    ///
    /// `None` no inyecta nada. `Temp` falla al escribir el archivo temporal
    /// (antes del fsync). `Flush` falla en el fsync/FlushFileBuffers. `Rename`
    /// falla en el rename atomico (el temp queda huérfano; la generacion
    /// anterior sigue intacta).
    enum class Step { None, Temp, Flush, Rename };

    SnapshotStore() = default;
    ~SnapshotStore();

    // Move-only.
    SnapshotStore(SnapshotStore&& other) noexcept;
    SnapshotStore& operator=(SnapshotStore&& other) noexcept;
    SnapshotStore(const SnapshotStore&) = delete;
    SnapshotStore& operator=(const SnapshotStore&) = delete;

    /// Prepara el store para leer/escribir en `dir`.
    ///
    /// Crea `dir` si no existe. No crea los archivos A/B: aparecen con la
    /// primera escritura. `fileA` y `fileB` son los nombres de las dos
    /// generaciones (por defecto snapshot.A.json / snapshot.B.json, spec §5.1
    /// Config/session/).
    ///
    /// :param dir: directorio donde viven los snapshots (mismo fs, para que el
    ///             rename sea atomico). No debe estar vacio.
    /// :param fileA: nombre de la generacion A.
    /// :param fileB: nombre de la generacion B. Debe ser distinto de fileA.
    /// :returns: Status con ok=true y code=0 en exito; si no, code es un
    ///           ErrorCode negativo (kInvalidArgument si dir vacio o fileA==fileB,
    ///           kIo si no se pudo crear el directorio).
    Status init(const std::string& dir, const std::string& fileA = "snapshot.A.json",
                const std::string& fileB = "snapshot.B.json");

    /// Programa una escritura del `doc` con debounce (spec §11.2).
    ///
    /// Actualiza SIEMPRE el documento pendiente en memoria (asi no se pierde
    /// estado). Si `immediate` es true (evento critico: iniciar/detener vivo,
    /// cambiar escena), fuerza la escritura fisica AHORA, sin importar el
    /// debounce. Si es false, solo escribe si desde la ultima escritura fisica
    /// pasaron al menos 500 ms (kDebounceMs); si no, queda pendiente y se
    /// escribe en la proxima oportunidad (otro scheduleWrite no debounced, o
    /// un flushNow()).
    ///
    /// No-fail: ante un fallo de disco (o disco ausente) el doc queda en
    /// memoria y se marca un reintento pendiente; no se pierde estado.
    ///
    /// :param doc: estado a persistir (se copia; el caller puede reutilizarlo).
    /// :param immediate: true para eventos criticos (bypasea el debounce).
    void scheduleWrite(const StateDoc& doc, bool immediate);

    /// Fuerza la escritura fisica del documento pendiente AHORA.
    ///
    /// Es el camino de cierre ordenado (tras fijar doc.cleanShutdown=true) y
    /// de los tests. Si no hay documento pendiente, no hace nada. No-fail:
    /// ante fallo de disco el doc sigue pendiente en memoria.
    void flushNow();

    /// Lee la generacion mas nueva que sea VALIDA (checksum correcto).
    ///
    /// Lee ambos archivos A y B, valida el checksum de cada uno y devuelve la
    /// de MAYOR secuencia entre las validas. Si ambas estan corruptas (o no
    /// existen), falla con kNotFound y deja `out` indeterminado. NUNCA lanza
    /// por un archivo corrupto: lo trata como invalido y cae a la otra.
    ///
    /// :param out: recibe el documento de la generacion mas nueva valida.
    /// :returns: Status con ok=true y code=0 si hay al menos una generacion
    ///           valida; si no, code=kNotFound.
    Status readLatest(StateDoc& out);

    /// Calcula SHA-256 (hex, 64 chars) sobre `body`.
    ///
    /// Publico para que los demas componentes de persistencia (Restore, spec
    /// §11.3) reutilicen el MISMO checksum que SnapshotStore escribe en el
    /// header, de modo que un archivo escrito por SnapshotStore siempre pasa la
    /// verificacion y uno con bytes alterados siempre la falla. Es una funcion
    /// pura y thread-safe (no toca estado del store).
    ///
    /// :param body: bytes a hashear (el cuerpo del snapshot, sin el header).
    /// :returns: representacion hexadecimal en minuscula del digest (64 chars).
    static std::string sha256Hex(const std::string& body);

    /// Contador de escrituras FISICAS realizadas (para el test de debounce).
    ///
    /// Solo sube cuando realmente se hace un rename a una generacion. Un
    /// scheduleWrite debounced (que no escribe) NO lo incrementa.
    ///
    /// :returns: cantidad de escrituras fisicas desde init().
    std::uint64_t physicalWrites() const;

    /// Indica si el disco esta "presente" (fault-injection para tests).
    ///
    /// Con false, las escrituras no tocan el disco: el doc queda en memoria y
    /// se marca un reintento pendiente (spec §11.2: "Si el disco no esta, el
    /// estado se mantiene en memoria y la escritura se reintenta").
    ///
    /// :param present: true (default) para disco presente.
    void setDiskPresent(bool present);

    /// Inyecta un fallo en un paso especifico de la escritura (para tests).
    ///
    /// `Step::None` (default) no inyecta nada. Ver :type:`Step` para el
    /// significado de cada paso. El fallo se aplica a la proxima escritura
    /// fisica; el caller lo limpia con setFaultAtStep(None).
    ///
    /// :param step: paso donde inyectar el fallo.
    void setFaultAtStep(Step step);

    /// Ruta completa de la generacion A (para inspeccion en tests).
    const std::string& pathA() const { return pathA_; }

    /// Ruta completa de la generacion B (para inspeccion en tests).
    const std::string& pathB() const { return pathB_; }

    /// Indica si hay un documento pendiente sin escribir (por debounce o por
    /// disco ausente). Utiles para tests y para el reintento en el cierre.
    bool hasPending() const { return hasPending_; }

    /// Devuelve el documento pendiente en memoria (el que se reintentara).
    ///
    /// :returns: copia del doc pendiente; si no hay, un StateDoc por defecto.
    StateDoc pendingDoc() const { return pendingDoc_; }

    /// Umbral de debounce: maximo una escritura fisica cada 500 ms (spec
    /// §11.2: "como maximo una escritura cada 500 ms").
    static constexpr std::chrono::milliseconds kDebounceMs{500};

   private:
    // --- estado -------------------------------------------------------------
    std::string dir_;
    std::string pathA_;
    std::string pathB_;
    bool initialized_ = false;

    // Doble generacion: la proxima escritura va a la generacion que NO se uso
    // en la ultima escritura fisica (alterna A/B en cada write).
    bool nextIsA_ = true;

    // Debounce: steady_clock (inmune a cambios de hora) de la ultima escritura
    // fisica. `lastWriteTime_` es epoch hasta la primera escritura.
    std::chrono::steady_clock::time_point lastWriteTime_{};

    // Documento pendiente en memoria (no se pierde si el disco falla).
    StateDoc pendingDoc_{};
    bool hasPending_ = false;

    // Fault-injection.
    bool diskPresent_ = true;
    Step faultStep_ = Step::None;

    // Contador de escrituras fisicas.
    std::uint64_t physicalWrites_ = 0;

    // --- helpers (privados) -------------------------------------------------

    /// Devuelve el punto en steady_clock "ahora" (inyectable para tests).
    std::chrono::steady_clock::time_point now() const;

    /// Decide si una escritura no-immediata debe escribir ya (debounce).
    bool debounceAllowsWrite() const;

    /// Hace la escritura fisica del `doc` (temp + fsync + rename).
    ///
    /// :param doc: documento a escribir.
    /// :param targetIsA: true si la generacion destino es A, false si es B.
    /// :returns: Status; ok=false con kIo si fallo (o si el disco esta
    ///           ausente / hay fault-injection en el paso indicado).
    Status physicalWrite(const StateDoc& doc, bool targetIsA);

    /// Escribe el `doc` en `targetPath` de forma atomica (temp + fsync +
    /// rename). El temp vive en el MISMO directorio para que el rename sea
    /// atomico (mismo fs).
    ///
    /// :param doc: documento a escribir.
    /// :param targetPath: ruta final de la generacion.
    /// :returns: Status; ok=false con kIo si fallo en Temp/Flush/Rename.
    Status atomicWrite(const StateDoc& doc, const std::string& targetPath);

    /// Lee y valida UNA generacion.
    ///
    /// :param path: ruta del archivo de la generacion.
    /// :param out: recibe el doc si es valido.
    /// :returns: true si el archivo existe, su checksum es correcto y se
    ///           deserializo; false si no existe, esta corrupto o es invalido.
    bool readGeneration(const std::string& path, StateDoc& out) const;
};

} // namespace ba::persist
