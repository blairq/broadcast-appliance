// HealthBlock.cpp
//
// Implementacion del bloque de salud "BA.Health" (spec §4.4) sobre la primitiva
// SharedMemoryRegion (API congelada). Dos ideas de diseno:
//
//   1) Layout binario estable: [ HealthHeader ][ HealthEntry x slotCount ].
//      El header se escribe UNA sola vez en create() y queda inmutable; los
//      entries se escriben con un seqlock por entry para que read() devuelva un
//      snapshot consistente sin locks (thread-safe reads, lock-free).
//
//   2) Reloj QPC cruzado de plataforma (nowQpc()): QueryPerformanceCounter en
//      Windows (el mismo reloj que libobs via os_gettime_ns, spec §4.3) y
//      steady_clock en nanosegundos en el resto, para que los tests corran en
//      Linux sin dependencias.
//
// Ninguna funcion publica lanza: los fallos de create/open se expresan con
// Status; las operaciones sobre un bloque valido son no-fallibles (validan el
// slot y, si es invalido, no tocan memoria).

#include "HealthBlock.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

namespace ba::ipc {

namespace {

// --- Seqlock por entry ------------------------------------------------------
//
// El campo `seq` de cada HealthEntry es el contador del seqlock. Par = snapshot
// estable; impar = writer en medio de una escritura. Un solo writer por slot
// (el owner del proceso) lo serializa contra cualquier cantidad de lectores.

// Tope de reintentos del lector antes de devolver la mejor lectura disponible.
// En la practica el writer solo esta "impar" durante nanosegundos, asi que esto
// casi nunca se alcanza; existe para que read() sea siempre terminante.
constexpr int kMaxSeqlockSpins = 64;

// Escribe un entry completo bajo el seqlock (writer).
void writeEntry(HealthEntry* e, std::uint32_t state, std::uint32_t pid,
                std::uint32_t version, std::uint64_t lastBeatQpc) {
    std::atomic_ref<std::uint32_t> seq{e->seq};
    std::atomic_ref<std::uint32_t> st{e->state};
    std::atomic_ref<std::uint32_t> pidRef{e->pid};
    std::atomic_ref<std::uint32_t> verRef{e->version};
    std::atomic_ref<std::uint64_t> beat{e->lastBeatQpc};

    // Abre la ventana de escritura (seq impar).
    seq.fetch_add(1, std::memory_order_acq_rel);
    // Publica los campos (relaxed: el orden lo da el seqlock).
    st.store(state, std::memory_order_relaxed);
    pidRef.store(pid, std::memory_order_relaxed);
    verRef.store(version, std::memory_order_relaxed);
    beat.store(lastBeatQpc, std::memory_order_relaxed);
    // Cierra la ventana (seq par) y hace visibles los campos a los lectores.
    seq.fetch_add(1, std::memory_order_release);
}

// Lee un entry completo bajo el seqlock (reader) y devuelve un snapshot
// consistente. `ok` es false solo si el slot es invalido (e == nullptr).
bool readEntry(HealthEntry* e, Snapshot& out) {
    if (e == nullptr) {
        out = Snapshot{}; // state=Unknown, lastBeatQpc=0
        return false;
    }
    std::atomic_ref<std::uint32_t> seq{e->seq};
    std::atomic_ref<std::uint32_t> st{e->state};
    std::atomic_ref<std::uint32_t> pidRef{e->pid};
    std::atomic_ref<std::uint32_t> verRef{e->version};
    std::atomic_ref<std::uint64_t> beat{e->lastBeatQpc};

    for (int spin = 0;; ++spin) {
        const std::uint32_t s1 = seq.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0u) {
            // Writer en curso: reintenta (hasta el tope).
            if (spin < kMaxSeqlockSpins) {
                continue;
            }
        }
        const std::uint32_t state = st.load(std::memory_order_relaxed);
        const std::uint32_t pid = pidRef.load(std::memory_order_relaxed);
        const std::uint32_t version = verRef.load(std::memory_order_relaxed);
        const std::uint64_t lastBeat = beat.load(std::memory_order_acquire);
        const std::uint32_t s2 = seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            out = Snapshot{pid, static_cast<ProcessState>(state), lastBeat, version};
            return true;
        }
        // Lectura invalidada por un writer concurrente; reintenta o se rinde.
        if (spin >= kMaxSeqlockSpins) {
            out = Snapshot{pid, static_cast<ProcessState>(state), lastBeat, version};
            return true;
        }
    }
}

} // namespace

// --- nowQpc() ---------------------------------------------------------------

std::uint64_t HealthBlock::nowQpc() {
#if defined(_WIN32)
    LARGE_INTEGER freq, counter;
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart != 0 &&
        QueryPerformanceCounter(&counter)) {
        return static_cast<std::uint64_t>(counter.QuadPart);
    }
    return 0;
#else
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        steady_clock::now().time_since_epoch().count());
#endif
}

// --- RAII -------------------------------------------------------------------

HealthBlock::~HealthBlock() = default;

HealthBlock::HealthBlock(HealthBlock&& other) noexcept
    : region_(std::move(other.region_)),
      header_(other.header_),
      entries_(other.entries_),
      slotCount_(other.slotCount_) {
    other.header_ = nullptr;
    other.entries_ = nullptr;
    other.slotCount_ = 0;
}

HealthBlock& HealthBlock::operator=(HealthBlock&& other) noexcept {
    if (this != &other) {
        region_ = std::move(other.region_);
        header_ = other.header_;
        entries_ = other.entries_;
        slotCount_ = other.slotCount_;
        other.header_ = nullptr;
        other.entries_ = nullptr;
        other.slotCount_ = 0;
    }
    return *this;
}

// --- create / open ----------------------------------------------------------

Status HealthBlock::create(const wchar_t* name, std::size_t slotCount,
                           HealthBlock& out) {
    const wchar_t* n = (name != nullptr) ? name : kName;
    if (slotCount == 0) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.region_.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    const std::size_t bytes =
        sizeof(HealthHeader) + slotCount * sizeof(HealthEntry);

    SharedMemoryRegion region;
    Status st = SharedMemoryRegion::create(n, bytes, region);
    if (!st.ok) {
        return st;
    }

    void* base = region.data();
    if (base == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    auto* header = static_cast<HealthHeader*>(base);
    auto* entries = reinterpret_cast<HealthEntry*>(
        static_cast<std::uint8_t*>(base) + sizeof(HealthHeader));

    // Header inmutable: se escribe una sola vez, aqui.
    header->magic = kMagic;
    header->version = kLayoutVersion;
    header->slotCount = static_cast<std::uint32_t>(slotCount);
    header->reserved = 0;

    // Entries en estado inicial: seq=0 (estable), state=Unknown, pid=0,
    // lastBeatQpc=0. Ya estan a cero por el layout, pero lo fijamos explicito
    // para no depender de la inicializacion del backend.
    for (std::size_t i = 0; i < slotCount; ++i) {
        entries[i].seq = 0;
        entries[i].state = static_cast<std::uint32_t>(ProcessState::Unknown);
        entries[i].pid = 0;
        entries[i].version = 0;
        entries[i].lastBeatQpc = 0;
    }

    out.region_ = std::move(region);
    out.header_ = header;
    out.entries_ = entries;
    out.slotCount_ = slotCount;
    return Status::success();
}

Status HealthBlock::open(const wchar_t* name, HealthBlock& out) {
    const wchar_t* n = (name != nullptr) ? name : kName;
    if (out.region_.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    SharedMemoryRegion region;
    Status st = SharedMemoryRegion::open(n, region);
    if (!st.ok) {
        return st;
    }

    void* base = region.data();
    if (base == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    auto* header = static_cast<HealthHeader*>(base);
    // Validacion: el buffer debe ser un HealthBlock del layout esperado.
    if (header->magic != kMagic || header->version != kLayoutVersion ||
        header->slotCount == 0) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }

    auto* entries = reinterpret_cast<HealthEntry*>(
        static_cast<std::uint8_t*>(base) + sizeof(HealthHeader));

    out.region_ = std::move(region);
    out.header_ = header;
    out.entries_ = entries;
    out.slotCount_ = header->slotCount;
    return Status::success();
}

// --- API --------------------------------------------------------------------

std::size_t HealthBlock::slotCount() const { return slotCount_; }

bool HealthBlock::valid() const { return region_.valid(); }

HealthEntry* HealthBlock::entryAt(std::size_t slot) const {
    if (entries_ == nullptr || slot >= slotCount_) {
        return nullptr;
    }
    return &entries_[slot];
}

void HealthBlock::beat(std::size_t slot) {
    HealthEntry* e = entryAt(slot);
    if (e == nullptr) {
        return;
    }
    // Preserva la identidad (pid/version) publicada por setIdentity(); solo
    // refresca el latido y fija Running.
    Snapshot cur;
    readEntry(e, cur);
    writeEntry(e, static_cast<std::uint32_t>(ProcessState::Running), cur.pid,
               cur.version, nowQpc());
}

void HealthBlock::setState(std::size_t slot, ProcessState state) {
    HealthEntry* e = entryAt(slot);
    if (e == nullptr) {
        return;
    }
    Snapshot cur;
    readEntry(e, cur);
    writeEntry(e, static_cast<std::uint32_t>(state), cur.pid, cur.version,
               cur.lastBeatQpc);
}

void HealthBlock::setIdentity(std::size_t slot, std::uint32_t pid,
                              std::uint32_t version) {
    HealthEntry* e = entryAt(slot);
    if (e == nullptr) {
        return;
    }
    Snapshot cur;
    readEntry(e, cur);
    writeEntry(e, static_cast<std::uint32_t>(cur.state), pid, version,
               cur.lastBeatQpc);
}

Snapshot HealthBlock::read(std::size_t slot) const {
    Snapshot out;
    readEntry(entryAt(slot), out);
    return out;
}

bool HealthBlock::isStale(std::size_t slot, std::uint64_t nowQpc,
                          std::uint64_t thresholdNs) const {
    HealthEntry* e = entryAt(slot);
    if (e == nullptr) {
        return false;
    }
    const std::uint64_t beat =
        std::atomic_ref<std::uint64_t>{e->lastBeatQpc}.load(
            std::memory_order_acquire);

    // Slot sin latidos: se considera estancado si ya paso el umbral desde la
    // epoca (0). Asi un proceso que nunca llego a latir se detecta igual.
    if (beat == 0) {
        return nowQpc >= thresholdNs;
    }
    // Salvaguarda ante skew de reloj (no deberia pasar con QPC, pero no hace
    // dano): si "ahora" es anterior al ultimo latido, no esta estancado.
    if (nowQpc < beat) {
        return false;
    }
    return (nowQpc - beat) >= thresholdNs;
}

} // namespace ba::ipc
