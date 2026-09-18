// SpoutTransport.hpp
//
// Proposito:
//   Abstraccion del transporte de video SPOUT (spec §4.2) sobre la primitiva
//   SharedMemoryRegion. El backend real es la clase `spoutDX` del SDK de Spout2
//   (API DX11: abre con el device propio y envia/recibe texturas Direct3D 11;
//   NO la API `SpoutSender`/`CreateSender`, que es OpenGL) y es solo-Windows.
//   En Linux (y en la ruta de pruebas por defecto) se usa un backend NULL/CPU:
//   un frame es un buffer opaco de bytes y el contrato (metadatos por frame,
//   "un sender existe siempre", no-new-frame) queda testeable sin GPU.
//   No depende de Qt ni de libobs.
//
// Backend real (solo _WIN32, documentado; el codigo D3D/Spout2 va detras de
// #ifdef _WIN32):
//   - `spoutDX` del SDK de Spout2, enlazado estatico (no hace falta Spout2.dll).
//   - Misma GPU obligatoria: A elige el adaptador en el preflight (por LUID) y
//     lo publica; B crea su device D3D11 sobre ese mismo LUID.
//   - Ambos ejecutables exportan `NvOptimusEnablement` y
//     `AmdPowerXpressRequestHighPerformance` para laptops hibridas.
//   - Si el sharing de texturas falla, se pasa a modo copia por CPU con una
//     advertencia visible (spec §4.2).
//   - La textura NO transporta timestamps: por eso cada sender lleva un bloque
//     de memoria compartida paralelo (SpoutMetaBlock) con contador de frame y
//     timestamp QPC. A mantiene una cola de 2 a 4 texturas para retrasar el
//     video y alinearlo con el audio.
//
// Modelo de threading:
//   - Un sender es un solo ESCRITOR: publish() se llama una vez (config fija
//     por slot, NUNCA redimensionada en caliente) y send() avanza el bloque de
//     metadatos. Un receiver es un solo LECTOR: subscribe() una vez y
//     receive()/hasData() desde su hilo de video.
//   - El camino de datos no toma locks bloqueantes: la sincronizacion es
//     acquire/release sobre los campos del SpoutMetaBlock (atomic_ref<uint64_t>,
//     lock-free). El buffer de pixeles se escribe ANTES de publicar el meta
//     (release) y se lee DESPUES de ver el meta nuevo (acquire), de modo que el
//     reader nunca ve un frame incompleto.
//   - En el backend NULL el "slot" vive en un registro de proceso
//     name -> {buffer, meta, secuencia}; sender y receiver comparten el mismo
//     objeto, por lo que el flujo A<->B se prueba dentro del proceso.
//   - Los objetos son move-only y no son thread-safe por si mismos: son vistas
//     de un slot. La seguridad entre threads la aporta el layout (atomic_ref),
//     no esta clase.
//
// Estrategia de errores:
//   - publish()/subscribe() devuelven bool: true en exito, false en fallo
//     (name nulo, config invalido, slot ya publicado, o el nombre no existe en
//     el backend NULL). No lanzan.
//   - send()/receive() devuelven bool y son no-fallibles sobre un objeto valido:
//     si el objeto no esta publicado/suscrito, no tocan memoria y devuelven
//     false. receive() devuelve false si no hay frame nuevo desde el ultimo
//     receive (no bloquea, no reentrega el mismo frame).
//   - hasData() es no-fallible: false si no hay frame nuevo o el objeto no esta
//     suscrito.
//   - El destructor/assign no lanzan.
//
// Nota de diseño (supuesto, spec regla 4):
//   La spec nombra a la clase real `SpoutSender` (API DX11). Aqui `SpoutSender`
//   y `SpoutReceiver` son la ABSTRACCION de ba_core que en Windows se respalda
//   con `spoutDX` y en Linux con el backend NULL. El nombre del sender Spout
//   (p. ej. `BA.Guest.1`, `BA.Program`) lo decide el caller; esta lib solo
//   transporta. "Un sender existe siempre" (si no hay invitado, B publica una
//   placa) es responsabilidad del caller: esta lib no inventa frames.

#pragma once

#include "ipc/AtomicRef.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <string>

#include "ipc/SharedMemoryRegion.hpp" // ba::ipc::SharedMemoryRegion, Status, ErrorCode

namespace ba::ipc {

/// Tamano de una linea de cache en bytes (para el layout del meta block).
inline constexpr std::size_t kSpoutCacheLine = 64;

/// Metadatos por frame (spec §4.2 "Metadatos por frame").
///
/// La textura Spout no transporta timestamps, asi que cada sender publica estos
/// dos valores en un bloque de memoria compartida paralelo (SpoutMetaBlock).
/// `frameIndex` es el contador monotono de frames del sender (el receiver lo
/// usa para detectar "frame nuevo"); `qpc` es el timestamp QPC del frame (el
/// mismo reloj que libobs via os_gettime_ns, spec §4.3, para alinear video con
/// audio sin deriva). POD plano, trivialmente copiable.
struct FrameMeta {
    std::uint64_t frameIndex = 0;
    std::uint64_t qpc = 0;
};

/// Layout binario del bloque de metadatos en memoria compartida.
///
/// Los dos campos viven en LINEAS DE CACHE SEPARADAS (alignas(64)) para evitar
/// false sharing entre el writer (que toca frameIndex y qpc) y los readers.
/// Se acceden con std::atomic_ref<std::uint64_t> (lock-free, ver static_asserts
/// del .cpp). El orden de los campos y su alineacion garantizan que cada campo
/// este alineado a 8 bytes (requisito de atomic_ref) y en su propia cache line.
struct SpoutMetaLayout {
    alignas(kSpoutCacheLine) std::uint64_t frameIndex = 0;
    alignas(kSpoutCacheLine) std::uint64_t qpc = 0;
};

// atomic_ref<uint64_t> debe ser lock-free para que el camino de datos no tome
// locks (spec §4.2, analogo al ring de audio §4.3).
static_assert(AtomicRef<std::uint64_t>::is_always_lock_free,
              "AtomicRef<std::uint64_t> debe ser lock-free en esta plataforma");
// El layout debe alinearse a una cache line y poner qpc en otra linea que
// frameIndex (false sharing writer/reader).
static_assert(alignof(SpoutMetaLayout) == kSpoutCacheLine,
              "SpoutMetaLayout debe alinearse a 64 bytes (cache line)");
static_assert(offsetof(SpoutMetaLayout, qpc) >= kSpoutCacheLine,
              "qpc debe estar en otra linea de cache que frameIndex");

/// Bloque de memoria compartida con los metadatos por frame de un sender
/// (spec §4.2). Respaldado por SharedMemoryRegion.
///
/// Move-only: transferir la vista del buffer es barato; copiarla esta prohibida.
/// El destructor (o una asignacion move) desmapa el buffer.
///
/// Uso tipico:
///   - Sender: `SpoutMetaBlock::create(name, block)` al publicar el slot; a
///     partir de alli `publish(frameIndex, qpc)` por cada frame enviado.
///   - Receiver: `SpoutMetaBlock::open(name, block)` y `read()` para ver el
///     ultimo frame publicado (lock-free).
class SpoutMetaBlock {
   public:
    SpoutMetaBlock() = default;
    ~SpoutMetaBlock();

    // Move-only.
    SpoutMetaBlock(SpoutMetaBlock&& other) noexcept;
    SpoutMetaBlock& operator=(SpoutMetaBlock&& other) noexcept;
    SpoutMetaBlock(const SpoutMetaBlock&) = delete;
    SpoutMetaBlock& operator=(const SpoutMetaBlock&) = delete;

    /// Crea el bloque de metadatos y publica el `name`.
    ///
    /// Reserva sizeof(SpoutMetaLayout) bytes, deja frameIndex=0 y qpc=0 y
    /// publica el nombre. El sender lo llama una vez por slot.
    ///
    /// :param name: nombre logico del bloque (UTF-16). Debe ser distinto de
    ///              nulo. (En Windows se antepone el namespace `Local\`.)
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito; si no, code es un
    ///           ErrorCode negativo (kInvalidArgument si name es nulo, etc.).
    static Status create(const wchar_t* name, SpoutMetaBlock& out);

    /// Reencuentra un bloque ya publicado con `name` y lo mapea.
    ///
    /// Es el camino del receiver para leer los metadatos del sender. No crea
    /// nada: si el nombre no existe, falla con kNotFound.
    ///
    /// :param name: nombre logico publicado previamente por `create`.
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito.
    static Status open(const wchar_t* name, SpoutMetaBlock& out);

    /// Publica un par de metadatos (lado ESCRITOR).
    ///
    /// Escribe frameIndex (relaxed) y luego qpc (release): el reader que vea el
    /// qpc nuevo (acquire) ve tambien el frameIndex correspondiente. No-fail: si
    /// el objeto no es valido no toca memoria.
    ///
    /// :param frameIndex: contador monotono de frame del sender.
    /// :param qpc: timestamp QPC del frame.
    void publish(std::uint64_t frameIndex, std::uint64_t qpc);

    /// Lee el ultimo par de metadatos publicado (lado LECTOR, lock-free).
    ///
    /// Lee qpc (acquire) y luego frameIndex (relaxed) para obtener un par
    /// consistente. No-fail: si el objeto no es valido devuelve {0, 0}.
    ///
    /// :returns: FrameMeta con frameIndex y qpc de una misma publicacion.
    FrameMeta read() const;

    /// Indica si el objeto posee un mapa vigente del bloque.
    bool valid() const;

   private:
    SharedMemoryRegion region_;         // vista (map) del buffer; move-only
    SpoutMetaLayout* layout_ = nullptr; // puntero cacheado (valido si region_.valid())
};

/// Configuracion fija de un slot de video (spec §4.2 "Resolucion fija por
/// slot").
///
/// Default 1280x720 (720p); opcion 1920x1080 (1080p). Un slot NUNCA se
/// redimensiona en caliente: la resolucion se fija en publish() y no cambia
/// mientras el sender exista. B resuelve letterbox/pillarbox/rotacion al
/// componer el frame DENTRO del slot, no cambiando su tamano.
struct SlotConfig {
    int width = 1280;
    int height = 720;
};

/// Layout del slot en memoria compartida (backend CPU, definido en el .cpp).
struct VideoSlotHeader;

/// Abstraccion de un SENDER Spout (spec §4.2).
///
/// En Windows se respalda con `spoutDX` (DX11, misma GPU por LUID, export de
/// NvOptimusEnablement/AmdPowerXpressRequestHighPerformance, fallback a copia
/// por CPU si el sharing de texturas falla). En Linux / ruta de pruebas usa el
/// backend NULL: un frame es un buffer opaco de bytes y el slot vive en un
/// registro de proceso.
///
/// Move-only. "Un sender existe siempre": el caller publica una placa si no hay
/// invitado en el slot; esta clase no inventa frames por su cuenta.
class SpoutSender {
   public:
    SpoutSender() = default;
    ~SpoutSender();

    // Move-only.
    SpoutSender(SpoutSender&& other) noexcept;
    SpoutSender& operator=(SpoutSender&& other) noexcept;
    SpoutSender(const SpoutSender&) = delete;
    SpoutSender& operator=(const SpoutSender&) = delete;

    /// Publica el slot con el `name` y la `cfg` (lado ESCRITOR, una vez).
    ///
    /// Crea el bloque de metadatos paralelo y (en el backend NULL) reserva el
    /// buffer de pixeles del tamano de `cfg`. La resolucion queda FIJA a partir
    /// de aqui (no se redimensiona en caliente).
    ///
    /// :param name: nombre logico del sender (p. ej. L"BA.Guest.1", L"BA.Program").
    /// :param cfg: resolucion fija del slot (default 720p).
    /// :returns: true en exito; false si name es nulo, cfg invalido o el slot ya
    ///           estaba publicado.
    bool publish(const wchar_t* name, const SlotConfig& cfg);

    /// Envia un frame: copia `pixels` al buffer del slot y publica `meta` en el
    /// bloque de metadatos (lado ESCRITOR).
    ///
    /// El orden es: copiar los pixeles ANTES de publicar el meta (release), de
    /// modo que un reader que vea el meta nuevo (acquire) vea los pixeles
    /// completos. `meta.frameIndex` debe ser monotono creciente respecto al
    /// frame anterior (el receiver lo usa para detectar "frame nuevo").
    ///
    /// :param pixels: buffer de pixeles (opaco en el backend NULL; en el real es
    ///                el contenido de la textura). Debe ser distinto de nulo.
    /// :param bytes: tamano en bytes de `pixels`. Debe ser <= al tamano del
    ///               buffer del slot.
    /// :param meta: metadatos a publicar (frameIndex + qpc).
    /// :returns: true si se envio; false si el sender no esta publicado, pixels
    ///           es nulo o `bytes` excede el buffer del slot.
    bool send(const void* pixels, std::size_t bytes, const FrameMeta& meta);

    /// Indica si el sender ya publico su slot.
    bool published() const;

   private:
    // Backend CPU: region compartida con cabecera + triple buffer.
    SharedMemoryRegion region_;
    VideoSlotHeader* header_ = nullptr;
    bool published_ = false;
};

/// Abstraccion de un RECEPTOR Spout (spec §4.2).
///
/// En Windows se respalda con `spoutDX` (recibe texturas DX11 del sender sobre
/// la misma GPU). En Linux / ruta de pruebas usa el backend NULL: recibe bytes
/// del buffer del slot del registro de proceso.
///
/// Move-only. Un solo lector por receiver.
class SpoutReceiver {
   public:
    SpoutReceiver() = default;
    ~SpoutReceiver();

    // Move-only.
    SpoutReceiver(SpoutReceiver&& other) noexcept;
    SpoutReceiver& operator=(SpoutReceiver&& other) noexcept;
    SpoutReceiver(const SpoutReceiver&) = delete;
    SpoutReceiver& operator=(const SpoutReceiver&) = delete;

    /// Se suscribe a un sender ya publicado con `name` (lado LECTOR, una vez).
    ///
    /// Abre el bloque de metadatos del sender y (en el backend NULL) toma
    /// referencia al slot. `lastSeenIndex` se inicializa al frameIndex actual
    /// para que el primer receive() solo entregue frames NUEVOS.
    ///
    /// :param name: nombre logico publicado por SpoutSender::publish.
    /// :returns: true en exito; false si name es nulo o el sender no existe.
    bool subscribe(const wchar_t* name);

    /// Recibe el siguiente frame nuevo (lado LECTOR).
    ///
    /// Si llego un frame con frameIndex mayor al ultimo visto, copia los
    /// pixeles a `dst` (hasta `cap` bytes), escribe `metaOut` y avanza
    /// lastSeenIndex; devuelve true. Si no hay frame nuevo, devuelve false sin
    /// tocar `dst`/`metaOut` (no bloquea, no reentrega el mismo frame).
    ///
    /// :param dst: buffer de destino para los pixeles. Debe ser distinto de
    ///             nulo.
    /// :param cap: capacidad en bytes de `dst`. Debe ser >= al tamano del frame.
    /// :param metaOut: recibe los metadatos del frame entregado.
    /// :returns: true si se entrego un frame nuevo; false si no hay frame nuevo
    ///           o el receiver no esta suscrito.
    bool receive(void* dst, std::size_t cap, FrameMeta& metaOut);

    /// Indica si hay un frame nuevo por recibir (lado LECTOR, no destructivo).
    ///
    /// :returns: true si llego un frame con frameIndex mayor al ultimo visto;
    ///           false si no hay frame nuevo o el receiver no esta suscrito.
    bool hasData() const;

    /// Ancho del slot en pixeles (0 si no esta suscrito).
    ///
    /// La fija el sender al publicar y no cambia en caliente. Un consumidor la
    /// necesita para interpretar los bytes: sin ella solo se sabe cuantos bytes
    /// hay, no que forma tienen.
    int width() const;

    /// Alto del slot en pixeles (0 si no esta suscrito).
    int height() const;

    /// Tamano en bytes de un frame del slot (0 si no esta suscrito).
    std::size_t frameBytes() const;

    /// true si el sender al que estamos suscritos ya no existe o fue
    /// reemplazado por otro (el proceso productor se reinicio y publico el
    /// mismo nombre de nuevo). El receptor seguiria mirando la memoria vieja
    /// para siempre -- video congelado -- si no se resuscribe.
    ///
    /// Solo mira de verdad (reabre el nombre) si no llegaron frames en el
    /// ultimo segundo, y como mucho una vez por segundo: se puede llamar en
    /// cada vuelta del bucle del consumidor.
    bool stale();

   private:
    // Backend CPU: region compartida con cabecera + triple buffer.
    SharedMemoryRegion region_;
    VideoSlotHeader* header_ = nullptr;
    std::wstring name_;
    std::uint64_t instanceId_ = 0;
    std::uint64_t lastSeenPub_ = 0;
    std::uint64_t lastFrameNs_ = 0; // reloj monotono del ultimo frame (o de la suscripcion)
    std::uint64_t lastCheckNs_ = 0; // ultimo chequeo de stale()
    bool subscribed_ = false;
};

} // namespace ba::ipc
