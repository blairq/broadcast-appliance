// SpoutTransport.cpp
//
// Implementacion del canal de video entre procesos (spec §4.2) sobre
// SharedMemoryRegion: el mismo en Windows, macOS y Linux. Una region con nombre
// por slot con cabecera + triple buffer (layout abajo); send() copia los pixeles
// al buffer que no es el ultimo publicado y lo anuncia; receive() copia afuera
// el ultimo completo si es nuevo desde el ultimo receive (lastSeenIndex). No hay
// texturas compartidas ni Spout2: todo pasa por la CPU (ver SpoutTransport.hpp).
//
// Los sellos (frameIndex y QPC) van en la cabecera de cada buffer, con el cuadro.
// La cabecera del slot lleva además cuándo publicó el productor el último y cada cuánto
// publica: con eso el lector duerme hasta el próximo (ipc/Mirada.hpp, F2-14).
//
// Ninguna funcion publica lanza: publish()/subscribe() y send()/receive()
// devuelven bool; los fallos de create/open del meta se expresan con Status.

#include "SpoutTransport.hpp"

#include "ipc/AtomicRef.hpp"
#include "ipc/Mirada.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::ipc {

// --- Backend CPU: layout del slot en memoria compartida --------------------
//
// Un slot es UNA region compartida con nombre (`BA.Guest.1`, `BA.Program`):
//
//   [ VideoSlotHeader | buffer 0 | buffer 1 | buffer 2 ]
//
// Triple buffer + seqlock por buffer:
//   - El escritor escribe siempre en el buffer (pubCount+1) % 3, nunca en el
//     ultimo publicado. Cada buffer tiene un contador de secuencia: impar
//     mientras se escribe, par cuando esta completo.
//   - `pubCount` (release) anuncia el frame nuevo. El lector lee pubCount
//     (acquire), copia el buffer pubCount % 3 y verifica que la secuencia no
//     cambio durante la copia; si cambio (el escritor dio toda la vuelta a
//     los tres buffers mientras copiabamos, ~66 ms a 30 fps) reintenta con el
//     frame mas nuevo.
//   Resultado: el escritor nunca espera, puede haber varios lectores y ninguno
//   ve un frame a medio escribir.
//
// `instanceId` identifica a ESTE productor: si el proceso se reinicia y vuelve
// a publicar el nombre, el id cambia y los lectores lo detectan (stale()).

inline constexpr std::uint32_t kVideoMagic = 0x31564142u; // "BAV1"
inline constexpr std::uint32_t kVideoBuffers = 3;
inline constexpr std::size_t kBufferAlign = 4096; // alineado a pagina: memcpy rapido

struct alignas(kSpoutCacheLine) VideoBufferHeader {
    std::uint64_t seq = 0; // impar = escribiendose (atomic_ref)
    std::uint64_t frameIndex = 0;
    std::uint64_t qpc = 0;
    std::uint64_t contenidoQpc = 0; // la hora de lo que muestra (FrameMeta)
    std::uint32_t ancho = 0;        // el tamaño real del cuadro; 0 = el del slot
    std::uint32_t alto = 0;
};

// Versión 2: el período y la hora del último envío (en el hueco antes de pubCount: el
// tamaño de la cabecera no cambia) y, por buffer, la hora del contenido y el tamaño real.
struct VideoSlotHeader {
    std::uint32_t magic = 0;
    std::uint32_t version = 2;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frameBytes = 0;
    std::uint64_t instanceId = 0;
    std::uint64_t periodoNs = 0; // cada cuánto publica el productor (atomic_ref; 0 = no se sabe)
    std::uint64_t envioNs = 0;   // cuándo publicó el último (steady_clock en ns; atomic_ref)
    alignas(kSpoutCacheLine) std::uint64_t pubCount = 0; // frames publicados (atomic_ref)
    VideoBufferHeader buffers[kVideoBuffers];
};
static_assert(offsetof(VideoSlotHeader, pubCount) == kSpoutCacheLine, "el período y el envío caben antes de pubCount");
static_assert(sizeof(VideoBufferHeader) == kSpoutCacheLine, "la cabecera de un buffer es una línea de caché");

namespace {

std::size_t headerBytes() {
    return (sizeof(VideoSlotHeader) + kBufferAlign - 1) / kBufferAlign * kBufferAlign;
}

std::size_t alignedFrame(std::size_t bytes) {
    return (bytes + kBufferAlign - 1) / kBufferAlign * kBufferAlign;
}

// BGRA 8888 (4 bytes/pixel), lo que A y B se pasan por el canal.
std::size_t slotBytes(const SlotConfig& cfg) {
    return static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height) * 4u;
}

std::uint8_t* bufferAt(VideoSlotHeader* h, std::uint32_t i) {
    return reinterpret_cast<std::uint8_t*>(h) + headerBytes() + i * alignedFrame(h->frameBytes);
}

std::uint64_t monoNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

std::uint64_t newInstanceId() {
    std::random_device rd;
    const std::uint64_t r = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    return r ^ monoNs();
}

/// Valida que una region mapeada sea un slot bien formado.
VideoSlotHeader* asSlot(SharedMemoryRegion& region) {
    if (!region.valid() || region.size() < sizeof(VideoSlotHeader)) {
        return nullptr;
    }
    auto* h = static_cast<VideoSlotHeader*>(region.data());
    if (h->magic != kVideoMagic || h->width == 0 || h->height == 0) {
        return nullptr;
    }
    const std::size_t need = headerBytes() + kVideoBuffers * alignedFrame(h->frameBytes);
    return region.size() >= need ? h : nullptr;
}

static_assert(AtomicRef<std::uint64_t>::is_always_lock_free, "atomic_ref<uint64_t> debe ser lock-free");

} // namespace

// --- SpoutMetaBlock: RAII ---------------------------------------------------

SpoutMetaBlock::~SpoutMetaBlock() { region_.close(); }

SpoutMetaBlock::SpoutMetaBlock(SpoutMetaBlock&& other) noexcept
    : region_(std::move(other.region_)), layout_(other.layout_) {
    other.layout_ = nullptr;
}

SpoutMetaBlock& SpoutMetaBlock::operator=(SpoutMetaBlock&& other) noexcept {
    if (this != &other) {
        region_.close();
        region_ = std::move(other.region_);
        layout_ = other.layout_;
        other.layout_ = nullptr;
    }
    return *this;
}

Status SpoutMetaBlock::create(const wchar_t* name, SpoutMetaBlock& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.region_.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    SharedMemoryRegion region;
    Status st = SharedMemoryRegion::create(name, sizeof(SpoutMetaLayout), region);
    if (!st.ok) {
        return st;
    }
    void* base = region.data();
    if (base == nullptr) {
        region.close();
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    // Layout en estado inicial: frameIndex=0, qpc=0.
    auto* layout = static_cast<SpoutMetaLayout*>(base);
    layout->frameIndex = 0;
    layout->qpc = 0;

    out.region_ = std::move(region);
    out.layout_ = layout;
    return Status::success();
}

Status SpoutMetaBlock::open(const wchar_t* name, SpoutMetaBlock& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.region_.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    SharedMemoryRegion region;
    Status st = SharedMemoryRegion::open(name, region);
    if (!st.ok) {
        return st;
    }
    void* base = region.data();
    if (base == nullptr) {
        region.close();
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    out.region_ = std::move(region);
    out.layout_ = static_cast<SpoutMetaLayout*>(base);
    return Status::success();
}

void SpoutMetaBlock::publish(std::uint64_t frameIndex, std::uint64_t qpc) {
    if (layout_ == nullptr) {
        return;
    }
    // frameIndex primero (relaxed), qpc despues (release): el reader que vea el
    // qpc nuevo (acquire) ve tambien el frameIndex correspondiente.
    AtomicRef<std::uint64_t> fi{layout_->frameIndex};
    AtomicRef<std::uint64_t> q{layout_->qpc};
    fi.store(frameIndex, std::memory_order_relaxed);
    q.store(qpc, std::memory_order_release);
}

FrameMeta SpoutMetaBlock::read() const {
    if (layout_ == nullptr) {
        return FrameMeta{}; // {0, 0}
    }
    // qpc primero (acquire), frameIndex despues (relaxed): par consistente.
    AtomicRef<std::uint64_t> q{layout_->qpc};
    AtomicRef<std::uint64_t> fi{layout_->frameIndex};
    const std::uint64_t qpc = q.load(std::memory_order_acquire);
    const std::uint64_t frameIndex = fi.load(std::memory_order_relaxed);
    return FrameMeta{frameIndex, qpc};
}

bool SpoutMetaBlock::valid() const { return region_.valid() && layout_ != nullptr; }

// --- SpoutSender ------------------------------------------------------------

SpoutSender::~SpoutSender() = default;

SpoutSender::SpoutSender(SpoutSender&& other) noexcept
    : region_(std::move(other.region_)), header_(other.header_), published_(other.published_),
      periodoDicho_(other.periodoDicho_), anteriorNs_(other.anteriorNs_) {
    other.header_ = nullptr;
    other.published_ = false;
}

SpoutSender& SpoutSender::operator=(SpoutSender&& other) noexcept {
    if (this != &other) {
        region_ = std::move(other.region_);
        header_ = other.header_;
        published_ = other.published_;
        periodoDicho_ = other.periodoDicho_;
        anteriorNs_ = other.anteriorNs_;
        other.header_ = nullptr;
        other.published_ = false;
    }
    return *this;
}

bool SpoutSender::publish(const wchar_t* name, const SlotConfig& cfg) {
    if (name == nullptr || cfg.width <= 0 || cfg.height <= 0) {
        return false;
    }
    if (published_) {
        return false; // la resolucion es fija por slot: no se re-publica
    }
    const std::size_t frame = slotBytes(cfg);
    SharedMemoryRegion region;
    if (!SharedMemoryRegion::create(name, headerBytes() + kVideoBuffers * alignedFrame(frame), region).ok) {
        return false;
    }
    auto* h = static_cast<VideoSlotHeader*>(region.data());
    if (h == nullptr) {
        return false;
    }
    // Cabecera completa ANTES del magic (release): un lector que vea el magic
    // ve el resto de los campos.
    h->version = 2;
    h->width = static_cast<std::uint32_t>(cfg.width);
    h->height = static_cast<std::uint32_t>(cfg.height);
    h->frameBytes = frame;
    h->instanceId = newInstanceId();
    h->periodoNs = cfg.periodoNs;
    h->envioNs = 0;
    h->pubCount = 0;
    for (auto& b : h->buffers) {
        b = VideoBufferHeader{};
    }
    AtomicRef<std::uint32_t>(h->magic).store(kVideoMagic, std::memory_order_release);

    region_ = std::move(region);
    header_ = h;
    published_ = true;
    periodoDicho_ = cfg.periodoNs != 0;
    anteriorNs_ = 0;
    return true;
}

bool SpoutSender::send(const void* pixels, std::size_t bytes, const FrameMeta& meta) {
    if (!published_ || header_ == nullptr || pixels == nullptr || bytes > header_->frameBytes) {
        return false;
    }
    // Un tamaño real tiene que caber en el slot y venir entero.
    const bool real = meta.ancho != 0 || meta.alto != 0;
    if (real && (meta.ancho == 0 || meta.alto == 0 || meta.ancho > header_->width || meta.alto > header_->height ||
                 bytes < static_cast<std::size_t>(meta.ancho) * meta.alto * 4u)) {
        return false;
    }
    AtomicRef<std::uint64_t> pub(header_->pubCount);
    const std::uint64_t next = pub.load(std::memory_order_relaxed) + 1;
    const std::uint32_t idx = static_cast<std::uint32_t>(next % kVideoBuffers);
    VideoBufferHeader& bh = header_->buffers[idx];
    AtomicRef<std::uint64_t> seq(bh.seq);

    // Seqlock: impar mientras se escribe.
    const std::uint64_t s0 = seq.load(std::memory_order_relaxed);
    seq.store(s0 + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(bufferAt(header_, idx), pixels, bytes);
    bh.frameIndex = meta.frameIndex;
    bh.qpc = meta.qpc;
    bh.contenidoQpc = meta.contenidoQpc;
    bh.ancho = real ? meta.ancho : 0;
    bh.alto = real ? meta.alto : 0;
    seq.store(s0 + 2, std::memory_order_release);

    // Recien ahora se anuncia el frame.
    pub.store(next, std::memory_order_release);
    // Cuándo y cada cuánto (para que el lector duerma hasta el próximo: ipc/Mirada.hpp).
    const std::uint64_t ahora = mirada::ahoraNs();
    if (!periodoDicho_)
        AtomicRef<std::uint64_t>(header_->periodoNs)
            .store(mirada::periodoMedido(AtomicRef<std::uint64_t>(header_->periodoNs).load(std::memory_order_relaxed),
                                         anteriorNs_, ahora),
                   std::memory_order_relaxed);
    anteriorNs_ = ahora;
    AtomicRef<std::uint64_t>(header_->envioNs).store(ahora, std::memory_order_relaxed);
    return true;
}

bool SpoutSender::published() const { return published_; }

// --- SpoutReceiver ----------------------------------------------------------

SpoutReceiver::~SpoutReceiver() = default;

SpoutReceiver::SpoutReceiver(SpoutReceiver&& other) noexcept
    : region_(std::move(other.region_)), header_(other.header_), name_(std::move(other.name_)),
      instanceId_(other.instanceId_), lastSeenPub_(other.lastSeenPub_), lastFrameNs_(other.lastFrameNs_),
      lastCheckNs_(other.lastCheckNs_), subscribed_(other.subscribed_), temblor_(other.temblor_) {
    other.header_ = nullptr;
    other.subscribed_ = false;
}

SpoutReceiver& SpoutReceiver::operator=(SpoutReceiver&& other) noexcept {
    if (this != &other) {
        region_ = std::move(other.region_);
        header_ = other.header_;
        name_ = std::move(other.name_);
        instanceId_ = other.instanceId_;
        lastSeenPub_ = other.lastSeenPub_;
        lastFrameNs_ = other.lastFrameNs_;
        lastCheckNs_ = other.lastCheckNs_;
        subscribed_ = other.subscribed_;
        temblor_ = other.temblor_;
        other.header_ = nullptr;
        other.subscribed_ = false;
    }
    return *this;
}

bool SpoutReceiver::subscribe(const wchar_t* name) {
    if (name == nullptr || subscribed_) {
        return false;
    }
    SharedMemoryRegion region;
    if (!SharedMemoryRegion::open(name, region).ok) {
        return false; // el sender todavia no publico
    }
    VideoSlotHeader* h = asSlot(region);
    if (h == nullptr || AtomicRef<std::uint32_t>(h->magic).load(std::memory_order_acquire) != kVideoMagic) {
        return false; // region ajena o todavia inicializandose
    }
    region_ = std::move(region);
    header_ = h;
    name_ = name;
    instanceId_ = h->instanceId;
    // El primer receive() solo entrega frames NUEVOS.
    lastSeenPub_ = AtomicRef<std::uint64_t>(h->pubCount).load(std::memory_order_acquire);
    lastFrameNs_ = monoNs();
    lastCheckNs_ = lastFrameNs_;
    subscribed_ = true;
    return true;
}

bool SpoutReceiver::receive(void* dst, std::size_t cap, FrameMeta& metaOut) {
    if (!subscribed_ || header_ == nullptr || dst == nullptr) {
        return false;
    }
    const std::size_t bytes = header_->frameBytes;
    if (bytes > cap) {
        return false; // el buffer de destino no alcanza
    }
    AtomicRef<std::uint64_t> pub(header_->pubCount);
    // Pocos reintentos: chocar con el escritor requiere que de la vuelta a los
    // tres buffers durante una copia.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint64_t p = pub.load(std::memory_order_acquire);
        if (p <= lastSeenPub_) {
            return false; // no hay frame nuevo (no bloquea, no reentrega)
        }
        const std::uint32_t idx = static_cast<std::uint32_t>(p % kVideoBuffers);
        VideoBufferHeader& bh = header_->buffers[idx];
        AtomicRef<std::uint64_t> seq(bh.seq);
        const std::uint64_t s1 = seq.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0) {
            continue; // se esta escribiendo: probar con el siguiente anuncio
        }
        // Un cuadro más chico que el slot (su tamaño real): solo sus bytes. Si el tamaño no
        // es creíble (el escritor lo está cambiando), el seqlock lo descarta abajo.
        FrameMeta m{bh.frameIndex, bh.qpc, bh.contenidoQpc, bh.ancho, bh.alto};
        std::size_t usados = bytes;
        if (m.ancho != 0 && m.alto != 0 && m.ancho <= header_->width && m.alto <= header_->height)
            usados = static_cast<std::size_t>(m.ancho) * m.alto * 4u;
        else
            m.ancho = m.alto = 0;
        std::memcpy(dst, bufferAt(header_, idx), usados);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq.load(std::memory_order_relaxed) != s1) {
            continue; // el escritor piso el buffer durante la copia
        }
        metaOut = m;
        lastSeenPub_ = p;
        lastFrameNs_ = monoNs();
        temblor_.publicado(AtomicRef<std::uint64_t>(header_->envioNs).load(std::memory_order_relaxed), periodoNs());
        return true;
    }
    return false;
}

bool SpoutReceiver::hasData() const {
    if (!subscribed_ || header_ == nullptr) {
        return false;
    }
    return AtomicRef<std::uint64_t>(header_->pubCount).load(std::memory_order_acquire) > lastSeenPub_;
}

int SpoutReceiver::width() const { return header_ != nullptr ? static_cast<int>(header_->width) : 0; }

int SpoutReceiver::height() const { return header_ != nullptr ? static_cast<int>(header_->height) : 0; }

std::size_t SpoutReceiver::frameBytes() const {
    return header_ != nullptr ? static_cast<std::size_t>(header_->frameBytes) : 0;
}

std::uint64_t SpoutReceiver::periodoNs() const {
    return header_ != nullptr ? AtomicRef<std::uint64_t>(header_->periodoNs).load(std::memory_order_relaxed) : 0;
}

std::chrono::steady_clock::time_point SpoutReceiver::proximaMirada(std::chrono::steady_clock::duration pasoQuieto) const {
    const auto ahora = std::chrono::steady_clock::now();
    if (header_ == nullptr) return ahora + pasoQuieto;
    const std::uint64_t periodo = periodoNs();
    return mirada::proxima(ahora, AtomicRef<std::uint64_t>(header_->envioNs).load(std::memory_order_relaxed), periodo,
                           pasoQuieto, temblor_.adelanto(periodo));
}

bool SpoutReceiver::stale() {
    if (!subscribed_) {
        return true;
    }
    const std::uint64_t now = monoNs();
    constexpr std::uint64_t kSecond = 1000000000ULL;
    // Llegan frames: el productor esta vivo, no hace falta mirar.
    if (now - lastFrameNs_ < kSecond || now - lastCheckNs_ < kSecond) {
        return false;
    }
    lastCheckNs_ = now;
    SharedMemoryRegion probe;
    if (!SharedMemoryRegion::open(name_.c_str(), probe).ok) {
        return true; // el nombre ya no existe: el productor termino
    }
    VideoSlotHeader* h = asSlot(probe);
    return h == nullptr || h->instanceId != instanceId_;
}

} // namespace ba::ipc
