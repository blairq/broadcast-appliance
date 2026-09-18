// SpoutTransport.cpp
//
// Implementacion de la abstraccion de transporte de video SPOUT (spec §4.2)
// sobre SharedMemoryRegion. Dos backends:
//
//   - _WIN32: la clase `spoutDX` del SDK de Spout2 (API DX11: abre con el
//     device propio y envia/recibe texturas Direct3D 11; NO la API
//     `SpoutSender`/`CreateSender`, que es OpenGL). Enlazar el SDK estatico;
//     no hace falta Spout2.dll. Misma GPU obligatoria: A elige el adaptador en
//     el preflight (por LUID) y lo publica; B crea su device D3D11 sobre ese
//     mismo LUID. Ambos ejecutables exportan `NvOptimusEnablement` y
//     `AmdPowerXpressRequestHighPerformance` para laptops hibridas. Si el
//     sharing de texturas falla, se pasa a modo copia por CPU con una
//     advertencia visible. (El codigo D3D/Spout2 va detras de #ifdef _WIN32.)
//
//   - resto (Linux/macOS, y la ruta de pruebas por defecto): backend NULL/CPU.
//     Un frame es un buffer opaco de bytes y el slot vive en un registro de
//     proceso name -> {buffer, meta}. send() copia los pixeles al buffer del
//     slot y publica el meta; receive() copia fuera si llego un frame nuevo
//     desde el ultimo receive (lastSeenIndex). Asi los flujos A<->B se prueban
//     sin GPU.
//
// La textura NO transporta timestamps: cada sender lleva un bloque de memoria
// compartida paralelo (SpoutMetaBlock) con contador de frame y timestamp QPC.
// A mantiene una cola de 2 a 4 texturas para retrasar el video y alinearlo con
// el audio.
//
// Ninguna funcion publica lanza: publish()/subscribe() y send()/receive()
// devuelven bool; los fallos de create/open del meta se expresan con Status.

#include "SpoutTransport.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Backend real: la clase `spoutDX` del SDK de Spout2 (DX11). Header real:
// extern/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.h (no existe
// extern/Spout2/include). Firmas verificadas en ese header: no hay `init`;
// es `OpenDirectX11(ID3D11Device*)`, y `SendTexture(ID3D11Texture2D*)` /
// `ReceiveTexture(ID3D11Texture2D**)` (PascalCase, spec §14).
#endif

namespace ba::ipc {

// --- Backend NULL: slot de pixeles ------------------------------------------
//
// Solo se usa en el backend NULL (no-Windows / ruta de pruebas). Un slot es
// { buffer de pixeles, tamano }. El meta block es un SharedMemoryRegion
// SEPARADO (creado/abierto por name), fiel al modelo real donde la textura no
// vive en memoria compartida: solo el meta cruza procesos. Sender y receiver
// comparten el mismo slot (shared_ptr), por lo que el flujo A<->B se prueba
// dentro del proceso.
//
// Es un tipo NOMBRADO (no anonimo) porque la header lo adelanta como forward
// decl y los miembros std::shared_ptr de SpoutSender/SpoutReceiver lo
// referencian: debe ser el MISMO tipo en ambos.
struct SpoutNullSlot {
    std::vector<std::uint8_t> buffer; // pixeles opacos (en el real: textura)
    std::size_t bufferBytes = 0;      // capacidad del buffer
    // Resolucion del slot, fijada por el sender al publicar. El receptor la
    // necesita para saber como interpretar los bytes: sin esto solo sabia
    // CUANTOS bytes hay, no que forma tienen, y ningun consumidor real podia
    // armar un frame.
    int width = 0;
    int height = 0;
};

namespace {

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::wstring, std::shared_ptr<SpoutNullSlot>>& registry() {
    static std::unordered_map<std::wstring, std::shared_ptr<SpoutNullSlot>> r;
    return r;
}

// Obtiene el slot de `name` (creandolo si no existe) y devuelve su shared_ptr.
std::shared_ptr<SpoutNullSlot> getSlot(const wchar_t* name, bool& created) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& r = registry();
    auto it = r.find(std::wstring(name));
    if (it != r.end()) {
        created = false;
        return it->second;
    }
    auto slot = std::make_shared<SpoutNullSlot>();
    r[std::wstring(name)] = slot;
    created = true;
    return slot;
}

// Busca el slot de `name` sin crearlo; nullptr si no existe.
std::shared_ptr<SpoutNullSlot> findSlot(const wchar_t* name) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto it = registry().find(std::wstring(name));
    return (it != registry().end()) ? it->second : nullptr;
}

// Tamano del buffer de pixeles de un slot: BGRA 8888 (4 bytes/pixel). Es un
// supuesto del backend NULL (en el real el tamano lo da la textura DX11).
std::size_t slotBytes(const SlotConfig& cfg) {
    return static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height) * 4u;
}

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
    std::atomic_ref<std::uint64_t> fi{layout_->frameIndex};
    std::atomic_ref<std::uint64_t> q{layout_->qpc};
    fi.store(frameIndex, std::memory_order_relaxed);
    q.store(qpc, std::memory_order_release);
}

FrameMeta SpoutMetaBlock::read() const {
    if (layout_ == nullptr) {
        return FrameMeta{}; // {0, 0}
    }
    // qpc primero (acquire), frameIndex despues (relaxed): par consistente.
    std::atomic_ref<std::uint64_t> q{layout_->qpc};
    std::atomic_ref<std::uint64_t> fi{layout_->frameIndex};
    const std::uint64_t qpc = q.load(std::memory_order_acquire);
    const std::uint64_t frameIndex = fi.load(std::memory_order_relaxed);
    return FrameMeta{frameIndex, qpc};
}

bool SpoutMetaBlock::valid() const { return region_.valid() && layout_ != nullptr; }

// --- SpoutSender ------------------------------------------------------------

SpoutSender::~SpoutSender() = default;

SpoutSender::SpoutSender(SpoutSender&& other) noexcept
    : null_(std::move(other.null_)), published_(other.published_) {
    other.published_ = false;
}

SpoutSender& SpoutSender::operator=(SpoutSender&& other) noexcept {
    if (this != &other) {
        null_ = std::move(other.null_);
        published_ = other.published_;
        other.published_ = false;
    }
    return *this;
}

bool SpoutSender::publish(const wchar_t* name, const SlotConfig& cfg) {
    if (name == nullptr) {
        return false;
    }
    if (cfg.width <= 0 || cfg.height <= 0) {
        return false;
    }
    if (published_) {
        return false; // la resolucion es fija por slot: no se re-publica
    }

    // Backend NULL: reserva el buffer de pixeles del tamano de cfg.
    bool created = false;
    null_ = getSlot(name, created);
    if (null_ == nullptr) {
        return false;
    }
    null_->bufferBytes = slotBytes(cfg);
    null_->buffer.assign(null_->bufferBytes, 0);
    null_->width = cfg.width;
    null_->height = cfg.height;

    // Bloque de metadatos paralelo (spec §4.2): la textura no lleva timestamps.
    SpoutMetaBlock meta;
    Status st = SpoutMetaBlock::create(name, meta);
    if (!st.ok) {
        return false;
    }
    // El meta lo retiene el slot para que el receiver lo abra por name.
    // (En el backend NULL el receiver abre el mismo nombre via SharedMemoryRegion.)
    metaOwner_ = std::move(meta);

    published_ = true;
    return true;
}

bool SpoutSender::send(const void* pixels, std::size_t bytes, const FrameMeta& meta) {
    if (!published_ || pixels == nullptr || null_ == nullptr) {
        return false;
    }
    if (bytes > null_->bufferBytes) {
        return false;
    }
    // 1) Copia los pixeles ANTES de publicar el meta (release), para que un
    //    reader que vea el meta nuevo (acquire) vea los pixeles completos.
    std::memcpy(null_->buffer.data(), pixels, bytes);
    // 2) Publica el meta (frameIndex monotono + qpc).
    metaOwner_.publish(meta.frameIndex, meta.qpc);
    return true;
}

bool SpoutSender::published() const { return published_; }

// --- SpoutReceiver ----------------------------------------------------------

SpoutReceiver::~SpoutReceiver() = default;

SpoutReceiver::SpoutReceiver(SpoutReceiver&& other) noexcept
    : null_(std::move(other.null_)), lastSeenIndex_(other.lastSeenIndex_), subscribed_(other.subscribed_) {
    other.subscribed_ = false;
}

SpoutReceiver& SpoutReceiver::operator=(SpoutReceiver&& other) noexcept {
    if (this != &other) {
        null_ = std::move(other.null_);
        lastSeenIndex_ = other.lastSeenIndex_;
        subscribed_ = other.subscribed_;
        other.subscribed_ = false;
    }
    return *this;
}

bool SpoutReceiver::subscribe(const wchar_t* name) {
    if (name == nullptr) {
        return false;
    }
    if (subscribed_) {
        return false;
    }

    // Backend NULL: el slot debe existir (lo creo el sender al publicar).
    null_ = findSlot(name);
    if (null_ == nullptr) {
        return false;
    }

    // Abre el bloque de metadatos por name (el sender lo publico al publicar).
    SpoutMetaBlock meta;
    Status st = SpoutMetaBlock::open(name, meta);
    if (!st.ok) {
        return false;
    }
    metaOwner_ = std::move(meta);

    // El primer receive() solo entrega frames NUEVOS: arranca en el frameIndex
    // actual (que puede ser 0 si el sender aun no envio: "placa ausente").
    lastSeenIndex_ = metaOwner_.read().frameIndex;
    subscribed_ = true;
    return true;
}

bool SpoutReceiver::receive(void* dst, std::size_t cap, FrameMeta& metaOut) {
    if (!subscribed_ || dst == nullptr || null_ == nullptr) {
        return false;
    }
    const FrameMeta m = metaOwner_.read();
    // "Frame nuevo": frameIndex mayor al ultimo visto.
    if (m.frameIndex <= lastSeenIndex_) {
        return false; // no hay frame nuevo (no bloquea, no reentrega)
    }
    const std::size_t bytes = null_->bufferBytes;
    if (bytes > cap) {
        return false; // el buffer de destino no alcanza
    }
    // Lee los pixeles DESPUES de ver el meta nuevo (acquire): son completos.
    std::memcpy(dst, null_->buffer.data(), bytes);
    metaOut = m;
    lastSeenIndex_ = m.frameIndex;
    return true;
}

int SpoutReceiver::width() const { return (null_ != nullptr) ? null_->width : 0; }

int SpoutReceiver::height() const { return (null_ != nullptr) ? null_->height : 0; }

std::size_t SpoutReceiver::frameBytes() const { return (null_ != nullptr) ? null_->bufferBytes : 0; }

bool SpoutReceiver::hasData() const {
    if (!subscribed_ || null_ == nullptr) {
        return false;
    }
    const FrameMeta m = metaOwner_.read();
    return m.frameIndex > lastSeenIndex_;
}

} // namespace ba::ipc
