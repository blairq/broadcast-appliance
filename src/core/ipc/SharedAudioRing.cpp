// SharedAudioRing.cpp
//
// Implementacion del ring SPSC de audio (spec 4.3) sobre SharedMemoryRegion.
//
// Layout en memoria compartida:
//   [ RingHeader (offset 0, alignas 64) ]
//   [ area de datos: capacityBlocks slots, cada uno = { qpc(8), samples } ]
//
// Los indices writeIndex/readIndex cuentan BLOQUES y se acceden con
// std::atomic_ref<uint64_t> (lock-free, ver static_assert en el header).
// El camino de datos no toma locks: solo fenceas acquire/release.

#include "SharedAudioRing.hpp"

#include <algorithm>
#include <cstring>

namespace ba::ipc {

namespace {

// Canales maximo soportados para dimensionar el buffer de silencio.
inline constexpr uint32_t kMaxChannels = 8u;

// Buffer de silencio compartido (solo lectura). Se usa para los underruns y
// para el fade-in: el caller lee blockFloats() floats desde aqui.
const float* silenceBuffer() {
    static float buf[audio_ring::kBlockFrames * kMaxChannels] = {};
    return buf;
}

inline size_t alignUp(size_t v, size_t align) { return (v + align - 1) & ~(align - 1); }

} // namespace

size_t SharedAudioRing::dataOffset() {
    // El header ya termina en multiplo de 64 (alignas), pero alineamos por si
    // cambia el layout.
    return alignUp(sizeof(RingHeader), audio_ring::kCacheLine);
}

RingHeader* SharedAudioRing::hdr() const { return reinterpret_cast<RingHeader*>(region_.data()); }

size_t SharedAudioRing::blockFloats() const {
    return static_cast<size_t>(audio_ring::kBlockFrames) * hdr()->channels;
}

uint64_t SharedAudioRing::capacityBlocks() const { return hdr()->capacityFrames / audio_ring::kBlockFrames; }

const RingHeader* SharedAudioRing::header() const { return hdr(); }

SharedMemoryRegion* SharedAudioRing::region() { return &region_; }

// Cada slot de bloque: { uint64_t qpc; float samples[blockFloats]; }
// Alineamos el inicio de samples a 16 bytes para SIMD.
static size_t blockSlotBytes(uint32_t channels) {
    const size_t samplesBytes = static_cast<size_t>(audio_ring::kBlockFrames) * channels * sizeof(float);
    // qpc(8) + padding a 16 + samples
    return alignUp(8 + samplesBytes, 16);
}

float* SharedAudioRing::blockAt(uint64_t blockIndex) const {
    const auto* base = static_cast<const uint8_t*>(region_.data()) + dataOffset();
    const size_t slot = blockSlotBytes(hdr()->channels);
    const uint8_t* slotBase = base + (blockIndex % capacityBlocks()) * slot;
    // samples empieza a 16 bytes del inicio del slot (tras qpc + padding)
    return reinterpret_cast<float*>(const_cast<uint8_t*>(slotBase) + 16);
}

static uint64_t* qpcAt(const uint8_t* base, uint64_t blockIndex, uint64_t capBlocks, uint32_t channels) {
    const size_t slot = blockSlotBytes(channels);
    return reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(base + (blockIndex % capBlocks) * slot));
}

Status SharedAudioRing::init(const wchar_t* name, uint32_t channels, uint64_t capacityBlocks,
                             uint32_t writerPid, uint32_t generation, SharedAudioRing& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (channels == 0 || channels > kMaxChannels) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    // capacityBlocks debe ser potencia de 2 (>= 2).
    if (capacityBlocks < 2 || (capacityBlocks & (capacityBlocks - 1)) != 0) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }

    const size_t dataBytes = capacityBlocks * blockSlotBytes(channels);
    const size_t totalBytes = dataOffset() + dataBytes;

    SharedMemoryRegion region;
    Status s = SharedMemoryRegion::create(name, totalBytes, region);
    if (!s.ok) {
        return s;
    }

    auto* h = reinterpret_cast<RingHeader*>(region.data());
    h->magic = audio_ring::kMagic;
    h->version = audio_ring::kVersion;
    h->writerPid = writerPid;
    h->generation = generation;
    h->sampleRate = audio_ring::kSampleRate;
    h->channels = channels;
    h->capacityFrames = capacityBlocks * audio_ring::kBlockFrames;
    h->writeIndex = 0;
    h->readIndex = 0;
    h->lastWriteQpc = 0;
    h->underrunCount = 0;
    h->overrunCount = 0;

    // Cero el area de datos (silencio inicial).
    std::memset(static_cast<uint8_t*>(region.data()) + dataOffset(), 0, dataBytes);

    out.region_ = std::move(region);
    out.isWriter_ = true;
    out.hasCached_ = false;
    return Status::success();
}

Status SharedAudioRing::openReader(const wchar_t* name, SharedAudioRing& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    SharedMemoryRegion region;
    Status s = SharedMemoryRegion::open(name, region);
    if (!s.ok) {
        return s;
    }
    auto* h = reinterpret_cast<RingHeader*>(region.data());
    if (h->magic != audio_ring::kMagic || h->version != audio_ring::kVersion) {
        region.close();
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }
    out.region_ = std::move(region);
    out.isWriter_ = false;
    out.hasCached_ = false;
    return Status::success();
}

Status SharedAudioRing::openWriter(const wchar_t* name, uint32_t writerPid, uint32_t generation,
                                   SharedAudioRing& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    SharedMemoryRegion region;
    Status s = SharedMemoryRegion::open(name, region);
    if (!s.ok) {
        return s;
    }
    auto* h = reinterpret_cast<RingHeader*>(region.data());
    if (h->magic != audio_ring::kMagic || h->version != audio_ring::kVersion) {
        region.close();
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }
    // Publica la nueva identidad del escritor.
    h->writerPid = writerPid;
    h->generation = generation;
    out.region_ = std::move(region);
    out.isWriter_ = true;
    out.hasCached_ = false;
    return Status::success();
}

bool SharedAudioRing::push(const float* samples, uint64_t qpc) {
    if (!isWriter_ || samples == nullptr) {
        return false;
    }
    auto* h = hdr();
    const uint64_t cap = capacityBlocks();
    const uint64_t write = h->writeIndex; // solo el escritor lo escribe
    const uint64_t read = std::atomic_ref<uint64_t>(h->readIndex).load(std::memory_order_acquire);

    // Overrun: el lector no llego; el slot destino aun tiene datos no leidos.
    if (write - read >= cap) {
        std::atomic_ref<uint64_t>(h->overrunCount).fetch_add(1, std::memory_order_relaxed);
        return false; // descarta el bloque
    }

    // Escribe qpc y samples ANTES de avanzar writeIndex (release).
    *qpcAt(static_cast<const uint8_t*>(region_.data()) + dataOffset(), write, cap, h->channels) = qpc;
    std::memcpy(blockAt(write), samples, blockFloats() * sizeof(float));

    std::atomic_ref<uint64_t>(h->lastWriteQpc).store(qpc, std::memory_order_relaxed);
    std::atomic_ref<uint64_t>(h->writeIndex).store(write + 1, std::memory_order_release);
    return true;
}

bool SharedAudioRing::pop(float* out, PoppedBlock& meta) {
    meta = PoppedBlock{};
    if (out == nullptr || region_.data() == nullptr) {
        return false;
    }
    auto* h = hdr();
    const uint64_t cap = capacityBlocks();

    // 1) Detecta cambio de generacion del escritor (reinicio).
    const uint32_t pid = h->writerPid;
    const uint32_t gen = h->generation;
    if (!hasCached_) {
        cachedWriterPid_ = pid;
        cachedGeneration_ = gen;
        hasCached_ = true;
    } else if (pid != cachedWriterPid_ || gen != cachedGeneration_) {
        // Resincroniza: descarta lo bufferizado, readIndex <- writeIndex.
        const uint64_t w = std::atomic_ref<uint64_t>(h->writeIndex).load(std::memory_order_acquire);
        std::atomic_ref<uint64_t>(h->readIndex).store(w, std::memory_order_release);
        cachedWriterPid_ = pid;
        cachedGeneration_ = gen;
        pendingFadeIn_ = true;
    }

    const uint64_t write = std::atomic_ref<uint64_t>(h->writeIndex).load(std::memory_order_acquire);
    const uint64_t read = std::atomic_ref<uint64_t>(h->readIndex).load(std::memory_order_relaxed);

    const size_t floats = blockFloats();

    // 2) Underrun: sin datos. Se entrega silencio en el buffer del llamador.
    if (read == write) {
        std::atomic_ref<uint64_t>(h->underrunCount).fetch_add(1, std::memory_order_relaxed);
        std::memcpy(out, silenceBuffer(), floats * sizeof(float));
        meta.samples = out;
        meta.qpc = std::atomic_ref<uint64_t>(h->lastWriteQpc).load(std::memory_order_relaxed);
        meta.isSilence = true;
        meta.isFadeIn = pendingFadeIn_;
        pendingFadeIn_ = false;
        return true;
    }

    // 3) Hay datos: COPIA el bloque y solo entonces avanza readIndex.
    //
    // El orden importa: mientras readIndex no avance, el escritor considera el
    // slot ocupado (push() descarta por overrun antes que pisarlo), asi que la
    // copia ocurre sobre datos estables. Publicar readIndex antes de copiar
    // dejaria al escritor libre de reclamar el slot a mitad del memcpy.
    const uint64_t qpc =
        *qpcAt(static_cast<const uint8_t*>(region_.data()) + dataOffset(), read, cap, h->channels);
    std::memcpy(out, blockAt(read), floats * sizeof(float));
    std::atomic_ref<uint64_t>(h->readIndex).store(read + 1, std::memory_order_release);

    meta.samples = out;
    meta.qpc = qpc;
    meta.isSilence = false;
    meta.isFadeIn = pendingFadeIn_;
    pendingFadeIn_ = false;
    return true;
}

} // namespace ba::ipc
