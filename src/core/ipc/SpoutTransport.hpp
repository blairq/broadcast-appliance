// SpoutTransport.hpp
//
// Proposito:
//   El canal de video entre procesos (spec §4.2): A (la app) y B (el gateway)
//   se pasan cuadros por memoria compartida con nombre (`BA.Guest.<n>`,
//   `BA.Program`, los canales de cada salida). Es el MISMO transporte en las
//   tres plataformas: memoria compartida por CPU (SharedMemoryRegion: en
//   Windows CreateFileMappingW en `Local\`, en macOS/Linux shm_open + mmap
//   MAP_SHARED) con triple buffer. No hay texturas compartidas ni Spout2/spoutDX
//   (el nombre «Spout» quedó de la spec original): cada cuadro se copia a la
//   region y el lector lo copia afuera. No depende de Qt.
//
// Layout del slot (ver SpoutTransport.cpp):
//   [ VideoSlotHeader | buffer 0 | buffer 1 | buffer 2 ], buffers alineados a
//   4096 B. Cada buffer lleva su secuencia (seqlock), su frameIndex y su sello
//   QPC: los sellos viajan con el cuadro. `instanceId` cambia si el productor
//   se reinicia y vuelve a publicar el nombre (stale()).
//
// Modelo de threading:
//   - Un sender es un solo ESCRITOR: publish() se llama una vez (config fija
//     por slot, NUNCA redimensionada en caliente) y send() escribe en el buffer
//     que no es el ultimo publicado y lo anuncia. Un receiver es un solo LECTOR:
//     subscribe() una vez y receive()/hasData() desde su hilo de video; puede
//     haber varios receivers del mismo slot.
//   - El camino de datos no toma locks: acquire/release con atomic_ref sobre la
//     cabecera. El escritor nunca espera; el lector reintenta si el escritor dio
//     la vuelta a los tres buffers mientras copiaba, y nunca ve un cuadro a medio
//     escribir. Sin cuadro nuevo el lector duerme hasta que el escritor toca el
//     timbre (ipc/Timbre.hpp, F3-13: esperarNuevo) o, con un escritor que no toca,
//     hasta el próximo esperado (proximaMirada, F2-14).
//   - Los objetos son move-only y no son thread-safe por si mismos: son vistas
//     de un slot. La seguridad entre threads la aporta el layout (atomic_ref),
//     no esta clase.
//
// Estrategia de errores:
//   - publish()/subscribe() devuelven bool: true en exito, false en fallo
//     (name nulo, config invalido, slot ya publicado, o el nombre no existe).
//     No lanzan.
//   - send()/receive() devuelven bool y son no-fallibles sobre un objeto valido:
//     si el objeto no esta publicado/suscrito, no tocan memoria y devuelven
//     false. receive() devuelve false si no hay frame nuevo desde el ultimo
//     receive (no bloquea, no reentrega el mismo frame).
//   - hasData() es no-fallible: false si no hay frame nuevo o el objeto no esta
//     suscrito.
//   - El destructor/assign no lanzan.
//
// El nombre del slot lo decide el caller; esta lib solo transporta. "Un sender
// existe siempre" (si no hay invitado, B publica una placa) es responsabilidad
// del caller: esta lib no inventa frames.

#pragma once

#include "ipc/AtomicRef.hpp"
#include "ipc/Mirada.hpp"
#include "ipc/Timbre.hpp"
#include <atomic>
#include <chrono>
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
/// Viajan en la cabecera de cada buffer del slot, junto con el cuadro.
/// `frameIndex` es el contador monotono de frames del sender (el receiver lo
/// usa para detectar "frame nuevo"); `qpc` es el sello del frame en el reloj
/// de los sellos entre procesos (nowQpc: QueryPerformanceCounter en Windows,
/// steady_clock en ns en macOS/Linux; el mismo del anillo de audio, spec §4.3,
/// para alinear video con audio sin deriva). POD plano, trivialmente copiable.
///
/// `contenidoQpc` (F2-11): la hora, en el mismo reloj, a la que corresponde lo que
/// muestra el cuadro (la del programa en A, la misma con que se sella su audio); 0 = no
/// se sabe. `qpc` sigue siendo cuándo se publicó (la edad del salto la mide con eso).
/// `ancho`/`alto` (F2-13, V3): el tamaño real del cuadro, si es más chico que el canal
/// (sus filas van juntas, ancho×4 bytes, desde el principio del buffer); 0 = el del canal.
struct FrameMeta {
    std::uint64_t frameIndex = 0;
    std::uint64_t qpc = 0;
    std::uint64_t contenidoQpc = 0;
    std::uint32_t ancho = 0;
    std::uint32_t alto = 0;
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

/// Bloque de memoria compartida con un par de metadatos (spec §4.2), respaldado
/// por SharedMemoryRegion. SpoutSender/SpoutReceiver NO lo usan (los metadatos
/// van en la cabecera de cada buffer del slot); queda como primitiva suelta y
/// solo lo ejercita test_spout_transport.
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

/// El formato de los píxeles de un canal: lo declara el productor al publicar y lo
/// respeta cada lector (F3-01). BGRA de fábrica (lo que se pasan A y el gateway); NV12
/// (1,5 B/píxel: el plano Y y el UV entrelazado, seguidos, sin relleno) es el del
/// emisor aparte, lo que toma el codificador sin convertir.
enum class FormatoCanal : std::uint32_t { BGRA = 0, NV12 = 1 };

/// Los bytes de un cuadro entero de `ancho`×`alto` en `f` (NV12 pide ancho y alto pares).
constexpr std::size_t bytesDeCuadro(int ancho, int alto, FormatoCanal f) {
    const std::size_t px = static_cast<std::size_t>(ancho) * static_cast<std::size_t>(alto);
    return f == FormatoCanal::NV12 ? px + px / 2 : px * 4u;
}

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
    /// Cada cuánto publica el productor (ns), si lo sabe (el lienzo de A): el lector lo usa
    /// para dormir hasta el próximo cuadro (ipc/Mirada.hpp). 0 = se mide de lo que publica.
    std::uint64_t periodoNs = 0;
    /// El formato de los píxeles (NV12: ancho y alto pares; los cuadros, siempre enteros).
    FormatoCanal formato = FormatoCanal::BGRA;
};

/// Layout del slot en memoria compartida (backend CPU, definido en el .cpp).
struct VideoSlotHeader;

/// El ESCRITOR de un slot de video (spec §4.2): memoria compartida con triple
/// buffer en las tres plataformas (ver arriba). Un frame es un buffer de bytes en
/// el formato del slot (SlotConfig::formato: BGRA, o NV12 el del emisor).
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
    /// Crea la region compartida (cabecera + tres buffers del tamano de `cfg`)
    /// con un `instanceId` nuevo. La resolucion queda FIJA a partir
    /// de aqui (no se redimensiona en caliente).
    ///
    /// :param name: nombre logico del sender (p. ej. L"BA.Guest.1", L"BA.Program").
    /// :param cfg: resolucion fija del slot (default 720p).
    /// :returns: true en exito; false si name es nulo, cfg invalido o el slot ya
    ///           estaba publicado.
    bool publish(const wchar_t* name, const SlotConfig& cfg);

    /// Envia un frame: copia `pixels` y `meta` al buffer siguiente del slot
    /// (seqlock) y recien despues lo anuncia (lado ESCRITOR).
    ///
    /// El orden es: copiar los pixeles ANTES de anunciar el cuadro (release), de
    /// modo que un reader que vea el anuncio (acquire) vea los pixeles
    /// completos. `meta.frameIndex` debe ser monotono creciente respecto al
    /// frame anterior (el receiver lo usa para detectar "frame nuevo").
    ///
    /// :param pixels: buffer de pixeles (opaco para esta lib). Debe ser distinto
    ///                de nulo.
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
    Timbre timbre_; // en la cabecera de region_: se suelta antes que ella (ver el .cpp)
    VideoSlotHeader* header_ = nullptr;
    bool published_ = false;
    bool periodoDicho_ = false;     // el productor dijo su período: no se mide
    std::uint64_t anteriorNs_ = 0;  // cuándo se publicó el anterior (para medir el período)
};

/// El LECTOR de un slot de video (spec §4.2): abre la region compartida del
/// sender y copia afuera el ultimo cuadro completo (ver arriba).
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
    /// Abre la region compartida del sender y lee su cabecera (tamano,
    /// `instanceId`). `lastSeenIndex` se inicializa al frameIndex actual
    /// para que el primer receive() solo entregue frames NUEVOS.
    ///
    /// :param name: nombre logico publicado por SpoutSender::publish.
    /// :returns: true en exito; false si name es nulo o el sender no existe.
    bool subscribe(const wchar_t* name);

    /// Recibe el siguiente frame nuevo (lado LECTOR).
    ///
    /// Si llego un frame con frameIndex mayor al ultimo visto, copia los
    /// pixeles a `dst` (hasta `cap` bytes; si el cuadro trae su tamaño real
    /// —metaOut.ancho/alto— solo ancho×alto×4, con las filas juntas), escribe
    /// `metaOut` y avanza
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

    /// El formato que declaró el productor (BGRA si no está suscrito o es de antes).
    FormatoCanal formato() const;

    /// Sin cuadro nuevo, cuándo volver a mirar (F2-14): justo después del próximo que el
    /// productor debería publicar, según su último envío y su período (ipc/Mirada.hpp);
    /// `pasoQuieto` si no publica hace más de 1 s.
    std::chrono::steady_clock::time_point proximaMirada(std::chrono::steady_clock::duration pasoQuieto) const;
    /// Cada cuánto publica el productor (ns; 0 = todavía no se sabe).
    std::uint64_t periodoNs() const;

    /// El timbre (F3-13). Sin cuadro nuevo:
    ///
    ///     const auto visto = rx.timbre();   // ANTES de mirar
    ///     if (!rx.hasData()) rx.esperarNuevo(visto, pasoQuieto);
    ///
    /// duerme hasta que el productor toque (un cuadro nuevo) o, si no toca (uno viejo, o un
    /// sistema sin la primitiva), hasta proximaMirada. Con timbre el plazo es mirada::kVigia
    /// (por si el productor se murió sin tocar), o `aMasTardar` si es antes (el lector tiene
    /// algo que hacer a una hora). true si tocaron.
    std::uint32_t timbre() const { return timbre_.visto(); }
    bool esperarNuevo(std::uint32_t visto, std::chrono::steady_clock::duration pasoQuieto,
                      std::chrono::steady_clock::time_point aMasTardar = std::chrono::steady_clock::time_point::max());
    /// ¿El productor toca el timbre?
    bool tocaTimbre() const { return timbre_.vivo(); }
    /// Despierta al que duerme en esperarNuevo (otro hilo del mismo proceso: parar).
    void despertar() { timbre_.despertar(); }

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
    Timbre timbre_; // en la cabecera de region_: se suelta antes que ella (ver el .cpp)
    VideoSlotHeader* header_ = nullptr;
    std::wstring name_;
    std::uint64_t instanceId_ = 0;
    std::uint64_t lastSeenPub_ = 0;
    std::uint64_t lastFrameNs_ = 0; // reloj monotono del ultimo frame (o de la suscripcion)
    std::uint64_t lastCheckNs_ = 0; // ultimo chequeo de stale()
    bool subscribed_ = false;
    mirada::Temblor temblor_;       // cuánto tiembla el productor (proximaMirada)
};

} // namespace ba::ipc
