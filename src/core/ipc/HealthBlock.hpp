// HealthBlock.hpp
//
// Proposito:
//   Define el bloque de memoria compartida "BA.Health" (spec §4.4) que publica,
//   por proceso, su PID, estado, el QPC del ultimo latido y su version. Es la
//   primitiva que el launcher (Proceso 0) y los demas procesos leen para el
//   watchdog: un heartbeat sin actualizar por mas de 5 s cuenta como cuelgue
//   (spec §3.1.7). No depende de Qt ni de libobs.
//
// Layout (binario, estable entre procesos y entre versiones):
//   [ HealthHeader ] [ HealthEntry x slotCount ]
//   HealthHeader: magic, version (layout), slotCount, reserved.
//   HealthEntry:  seq (seqlock), state, pid, version (del proceso),
//                 lastBeatQpc.
//   El header se escribe UNA sola vez en create() y queda inmutable; los
//   entries se escriben con un seqlock por entry para que read() devuelva un
//   snapshot consistente sin locks.
//
// Modelo de threading:
//   - Escritura: cada proceso "owner" llama beat(slot)/setState(slot) desde su
//     propio hilo de control (p. ej. el timer de 250 ms del watchdog del
//     proceso). Un solo writer por slot; el seqlock de cada entry serializa
//     ese writer contra los lectores.
//   - Lectura: read()/isStale() pueden invocarlos desde cualquier thread del
//     lector (launcher, u otro proceso) sin locks: el seqlock hace la lectura
//     lock-free y el snapshot devuelto es consistente (los 4 campos corresponden
//     a una misma generacion de escritura).
//   - El objeto HealthBlock es move-only y no es thread-safe por si mismo: es
//     una vista (map) del buffer. La seguridad entre threads/procesos la aporta
//     el layout (seqlock + atomic_ref), no esta clase.
//   - Los tiempos son QPC (ver nowQpc()): el mismo reloj que libobs usa via
//     os_gettime_ns, para que no haya deriva entre A y B (spec §4.3 "Reloj").
//
// Estrategia de errores:
//   - create()/open() devuelven un Status {ok, code} y NUNCA lanzan. code == 0
//     es exito; en fallo, code es un valor negativo de ba::ipc::ErrorCode.
//   - beat()/setState()/read()/isStale() son no-fallibles sobre un bloque
//     valido: validan el slot y, si es invalido, no tocan memoria (no-fail,
//     no lanzan). read() sobre un slot invalido devuelve un Snapshot con
//     state=Unknown y lastBeatQpc=0; isStale() sobre slot invalido devuelve
//     false.
//   - El destructor/assign no lanzan.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ipc/SharedMemoryRegion.hpp" // ba::ipc::SharedMemoryRegion, Status, ErrorCode

namespace ba::ipc {

/// Nombre logico del bloque de salud en memoria compartida (spec §4.4).
/// En Windows se publica como `Local\BA.Health` (SharedMemoryRegion antepone
/// el namespace `Local\`); en el backend anonimo es la clave del registro.
inline constexpr const wchar_t* kHealthBlockName = L"BA.Health";

/// Estado de un proceso tal como lo publica en su slot de salud.
///
/// El launcher lo usa para decidir que hacer: `Running` con heartbeat fresco
/// es normal; `Hung` (o `Running` con heartbeat estancado) dispara minidump +
/// TerminateProcess + reinicio (spec §3.1.7). `Unknown` es el estado inicial
/// de un slot sin writer.
enum class ProcessState : std::uint32_t {
    Unknown = 0,    // Slot sin inicializar / writer ausente.
    Starting = 1,   // Proceso lanzado, todavia inicializando.
    Running = 2,    // Operativo; beat() lo fija y refresca lastBeatQpc.
    Hung = 3,       // Colgado (detectado por el launcher o auto-declarado).
    Terminated = 4, // Cierre ordenado publicado por el proceso.
};

/// Header inmutable del bloque. Se escribe una sola vez en create().
///
/// `magic` permite a un lector detectar que el buffer es (y es legible como)
/// un HealthBlock y no basura. `version` es la version del LAYOUT binario, no
/// la version del proceso (esa va por entry). `slotCount` es la cantidad de
/// procesos que el launcher supervisara.
struct HealthHeader {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t slotCount = 0;
    std::uint32_t reserved = 0;
};

/// Una entrada de salud por proceso.
///
/// `seq` es el contador del seqlock (par = snapshot estable). `state`, `pid`,
/// `version` y `lastBeatQpc` son los campos publicados. El orden de los campos
/// garantiza que `lastBeatQpc` quede alineado a 8 bytes para que
/// `std::atomic_ref<std::uint64_t>` sea lock-free (ver static_asserts del .cpp).
struct HealthEntry {
    std::uint32_t seq = 0;
    std::uint32_t state = 0;
    std::uint32_t pid = 0;
    std::uint32_t version = 0;
    std::uint64_t lastBeatQpc = 0;
};

/// Snapshot consistente de un slot, devuelto por read().
///
/// Es una copia plana (POD) de los campos del entry, tomada bajo el seqlock
/// para que los cuatro campos correspondan a una misma generacion de
/// escritura. El caller lo usa para decidir (p. ej. isStale, o mostrar estado
/// en la UI de recuperacion del launcher).
struct Snapshot {
    std::uint32_t pid = 0;
    ProcessState state = ProcessState::Unknown;
    std::uint64_t lastBeatQpc = 0;
    std::uint32_t version = 0;
};

/// RAII sobre el bloque de salud "BA.Health".
///
/// Move-only: transferir la vista del buffer es barato; copiarla esta
/// prohibida. El destructor (o una asignacion move) desmapa el buffer.
///
/// Uso tipico:
///   - Launcher: `HealthBlock::create(name, slotCount)` al arrancar; a partir
///     de alli lee `read(slot)`/`isStale(slot, nowQpc())` en su watchdog.
///   - Proceso hijo: `HealthBlock::open(name)` y escribe su slot con
///     `beat(slot)` (cada 250 ms) y `setState(slot, ...)` en transiciones.
class HealthBlock {
   public:
    /// Magic del layout binario. Un lector lo compara contra el header para
    /// validar que el buffer es un HealthBlock (y no basura de otro layout).
    static constexpr std::uint32_t kMagic = 0x4241484C; // "BAHL"
    /// Version del LAYOUT binario (no la del proceso). Cambia solo si cambia
    /// el orden/tamano de HealthHeader/HealthEntry.
    static constexpr std::uint32_t kLayoutVersion = 1;
    /// Nombre logico del bloque (alias de kHealthBlockName para el API).
    static constexpr const wchar_t* kName = kHealthBlockName;

    HealthBlock() = default;
    ~HealthBlock();

    // Move-only.
    HealthBlock(HealthBlock&& other) noexcept;
    HealthBlock& operator=(HealthBlock&& other) noexcept;
    HealthBlock(const HealthBlock&) = delete;
    HealthBlock& operator=(const HealthBlock&) = delete;

    /// Crea el bloque con `slotCount` slots y publica el nombre.
    ///
    /// Reserva `header + slotCount * entry` bytes, escribe el header (magic,
    /// version, slotCount) y deja todos los entries en estado inicial
    /// (seq=0, state=Unknown, pid=0, lastBeatQpc=0). El header queda inmutable
    /// a partir de aqui.
    ///
    /// :param name: nombre logico del bloque (UTF-16). Por defecto
    ///              kHealthBlockName ("BA.Health").
    /// :param slotCount: cantidad de procesos supervisados. Debe ser > 0.
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito; si no, code es un
    ///           ErrorCode negativo (kInvalidArgument si slotCount==0, etc.).
    static Status create(const wchar_t* name, std::size_t slotCount, HealthBlock& out);

    /// Reencuentra un bloque ya publicado con `name` y lo mapea.
    ///
    /// Es el camino que usa el launcher para leer los heartbeats de sus hijos
    /// (y el camino que usan los hijos para escribir el suyo). No crea nada:
    /// si el nombre no existe, falla con kNotFound.
    ///
    /// :param name: nombre logico publicado previamente por `create`.
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito.
    static Status open(const wchar_t* name, HealthBlock& out);

    /// Cantidad de slots publicados en el header.
    std::size_t slotCount() const;

    /// Indica si el objeto posee un mapa vigente del bloque.
    bool valid() const;

    /// Marca un latido en `slot`: fija lastBeatQpc = nowQpc() y state =
    /// Running. El owner del slot lo invoca cada 250 ms (spec §4.4).
    ///
    /// No-fail: si `slot` es invalido no toca memoria.
    ///
    /// :param slot: indice del slot (0 .. slotCount-1).
    void beat(std::size_t slot);

    /// Cambia solo el estado de `slot` (no toca lastBeatQpc). Para las
    /// transiciones que no son latidos: Starting, Hung, Terminated.
    ///
    /// :param slot: indice del slot.
    /// :param state: nuevo estado a publicar.
    void setState(std::size_t slot, ProcessState state);

    /// Publica el PID y la version del proceso que ocupa `slot`.
    ///
    /// Se usa en el arranque (antes del primer beat) para que el launcher
    /// pueda re-adoptar procesos vivos si muere y relanzarse (spec §3.1.9).
    ///
    /// :param slot: indice del slot.
    /// :param pid: PID del proceso owner.
    /// :param version: version de build/protocolo del proceso.
    void setIdentity(std::size_t slot, std::uint32_t pid, std::uint32_t version);

    /// Devuelve un snapshot consistente de `slot`.
    ///
    /// :param slot: indice del slot.
    /// :returns: Snapshot con los 4 campos de una misma generacion de
    ///           escritura; si `slot` es invalido, state=Unknown y
    ///           lastBeatQpc=0.
    Snapshot read(std::size_t slot) const;

    /// Indica si el heartbeat de `slot` esta estancado (posible cuelgue).
    ///
    /// Compara `nowQpc - lastBeatQpc` contra `thresholdNs`. Es la deteccion de
    /// cuelgue del launcher (spec §3.1.7: "un heartbeat sin actualizar por mas
    /// de 5 s cuenta como cuelgue"). Un slot sin latidos (lastBeatQpc==0) se
    /// considera estancado si `nowQpc` ya supera el umbral desde la epoca.
    ///
    /// :param slot: indice del slot.
    /// :param nowQpc: "ahora" en nanosegundos (mismo reloj/unidad que
    ///                lastBeatQpc y nowQpc()); por defecto nowQpc(). Se
    ///                inyecta para pruebas deterministas (fake now).
    /// :param thresholdNs: umbral de estancamiento en nanosegundos; por
    ///                     defecto 5 s (kDefaultHangThresholdNs).
    /// :returns: true si el heartbeat es estancado (posible cuelgue).
    bool isStale(std::size_t slot, std::uint64_t nowQpc = HealthBlock::nowQpc(),
                 std::uint64_t thresholdNs = kDefaultHangThresholdNs) const;

    /// Umbral por defecto de deteccion de cuelgue: 5 segundos (spec §3.1.7).
    static constexpr std::uint64_t kDefaultHangThresholdNs = 5'000'000'000ULL;

    /// Reloj QPC cruzado de plataforma, normalizado a NANOSEGUNDOS.
    ///
    /// En Windows usa QueryPerformanceCounter (el mismo reloj que libobs via
    /// os_gettime_ns, spec §4.3) y convierte los ticks a nanosegundos
    /// (dividiendo por QueryPerformanceFrequency). En otras plataformas
    /// (Linux/macOS, y las pruebas) usa steady_clock, que ya nace en
    /// nanosegundos. La unidad unica (ns) es la que exige isStale(): resta
    /// (nowQpc - lastBeatQpc) y la compara contra thresholdNs (ns), asi que
    /// cualquier aritmetica/comparacion de timestamps queda en una sola unidad.
    /// Devuelve 0 si el reloj no esta disponible (no lanza).
    ///
    /// :returns: nanosegundos actuales (QPC normalizado en Windows; steady en
    ///           no-Windows).
    static std::uint64_t nowQpc();

   private:
    SharedMemoryRegion region_; // vista (map) del buffer; move-only

    // Punteros cacheados sobre el buffer (validos solo cuando region_.valid()).
    HealthHeader* header_ = nullptr;
    HealthEntry* entries_ = nullptr;
    std::size_t slotCount_ = 0;

    // Devuelve el entry de `slot` si es valido, si no nullptr.
    HealthEntry* entryAt(std::size_t slot) const;
};

} // namespace ba::ipc
