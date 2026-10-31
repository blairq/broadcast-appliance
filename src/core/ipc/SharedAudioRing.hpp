// SharedAudioRing.hpp
//
// Proposito:
//   Ring buffer SPSC (un escritor, un lector) de audio en memoria compartida,
//   el contrato IPC de audio de la spec 4.3. Transporta Float32 intercalado en
//   bloques de 480 frames (10 ms a 48 kHz), cada uno con su timestamp QPC.
//   No depende de Qt; corre sobre SharedMemoryRegion.
//
// Modelo de threading:
//   - Un thread ESCRITOR llama a init()/push() (p. ej. el hilo de streaming de
//     baanillosink en A, o el de conversacion en B).
//   - Un thread LECTOR llama a pop() (p. ej. el thread de audio del otro
//     proceso).
//   - El camino de datos NO toma locks bloqueantes: la sincronizacion es
//     acquire/release sobre writeIndex/readIndex (std::atomic_ref<uint64_t>).
//     Los contadores de underrun/overrun y lastWriteQpc se tocan con
//     memory_order_relaxed porque son diagnosticos, no de control de flujo.
//   - La capacidad la elige el caller: hoy todos usan kDefaultCapacityBlocks
//     (16 bloques = 160 ms; baanillosink en A, MediaPump en B). El nivel real
//     del anillo (cuánto espera un bloque) lo fija la cadencia del lector, no
//     esta clase: la spec 4.3 pedía 20 a 40 ms y no hay nada que lo sostenga.
//
// Estrategia de errores:
//   - init() devuelve Status; en fallo deja el objeto vacio.
//   - push() ante overrun (el lector no llego) incrementa overrunCount y
//     DESCARTA el bloque (no bloquea, no reescribe datos leidos).
//   - pop() ante underrun (sin datos) llena el buffer del llamador de silencio
//     con fade e incrementa underrunCount.
//   - Si cambia la generacion del escritor (writerPid/generation distintos de
//     los cacheados), el lector descarta lo bufferizado, resincroniza
//     readIndex a writeIndex y aplica un fade-in de 10 ms.
//
// Nota de diseño (supuesto, spec regla 4):
//   La spec dice "capacidad en frames (potencia de 2)". Como los datos se
//   transfieren en bloques de 480 frames (480 = 32*15, no potencia de 2), un
//   anillo a nivel de frame con capacidad potencia de 2 no puede ser
//   simultaneamente multiplo de 480. Resolvemos haciendo el anillo a nivel de
//   BLOQUE: capacityBlocks es potencia de 2, capacityFrames = capacityBlocks*480,
//   y los indices de 64 bits cuentan BLOQUES (el masking de wrap usa
//   capacityBlocks). El header publica capacityFrames tal como lo nombra la
//   spec; la propiedad de potencia de 2 aplica a capacityBlocks.

#pragma once

#include "ipc/AtomicRef.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HealthBlock.hpp"
#include "MedidaTransporte.hpp"
#include "Mirada.hpp"
#include "SharedMemoryRegion.hpp"

namespace ba::ipc {

/// Constantes del layout del ring (spec 4.3).
namespace audio_ring {

inline constexpr uint32_t kMagic = 0x42415249u; // "BARI"
inline constexpr uint32_t kVersion = 1u;
inline constexpr uint32_t kSampleRate = 48000u;
inline constexpr uint32_t kBlockFrames = 480u; // 10 ms a 48 kHz
inline constexpr uint32_t kDefaultChannels = 2u;
inline constexpr uint64_t kDefaultCapacityBlocks = 16u; // 16*10 ms = 160 ms
inline constexpr size_t kCacheLine = 64u;

} // namespace audio_ring

/// Header versionado del ring, a offset 0 de la region compartida.
///
/// Los indices de escritura y lectura viven en lineas de caché propias para
/// evitar false sharing entre el escritor y el lector. Se acceden con
/// std::atomic_ref<uint64_t> (static_assert de lock-free abajo).
struct alignas(audio_ring::kCacheLine) RingHeader {
    uint32_t magic = audio_ring::kMagic;
    uint32_t version = audio_ring::kVersion;
    uint32_t writerPid = 0;
    uint32_t generation = 0;
    uint32_t sampleRate = audio_ring::kSampleRate;
    uint32_t channels = audio_ring::kDefaultChannels;
    uint64_t capacityFrames = 0; // = capacityBlocks * kBlockFrames

    // Lineas de caché propias: el escritor toca writeIndex, el lector readIndex.
    alignas(audio_ring::kCacheLine) uint64_t writeIndex = 0; // en BLOQUES
    alignas(audio_ring::kCacheLine) uint64_t readIndex = 0;  // en BLOQUES

    uint64_t lastWriteQpc = 0;
    uint64_t underrunCount = 0;
    uint64_t overrunCount = 0;
    /// Cuándo escribió el escritor el último bloque (steady_clock en ns; lastWriteQpc es el
    /// sello del bloque, que puede ser la hora del medio): el lector duerme hasta el próximo
    /// (proximaMirada, F2-14). Cabe en la última línea de caché: el tamaño no cambia.
    uint64_t escritoNs = 0;
};
static_assert(sizeof(RingHeader) == 3 * audio_ring::kCacheLine, "la cabecera del anillo no cambia de tamaño");

// Los indices se acceden con std::atomic_ref<uint64_t>; debe ser lock-free
// para que el camino de datos no tome locks (spec 4.3).
static_assert(AtomicRef<uint64_t>::is_always_lock_free,
              "AtomicRef<uint64_t> debe ser lock-free en esta plataforma");

/// Resultado de un pop(): metadatos del bloque copiado al buffer del llamador.
struct PoppedBlock {
    const float* samples = nullptr; // apunta al `out` del llamador (no al ring)
    uint64_t qpc = 0;               // timestamp QPC del bloque
    bool isSilence = false;         // true si fue un underrun (silencio+fade)
    bool isFadeIn = false;          // true si aplica fade-in de 10 ms
};

/// Ring SPSC de audio sobre SharedMemoryRegion.
///
/// Move-only. El writer llama a init()/push(); el reader a pop(). No hay locks
/// en el camino de datos.
class SharedAudioRing {
   public:
    SharedAudioRing() = default;
    ~SharedAudioRing() = default;

    SharedAudioRing(SharedAudioRing&&) noexcept = default;
    SharedAudioRing& operator=(SharedAudioRing&&) noexcept = default;
    SharedAudioRing(const SharedAudioRing&) = delete;
    SharedAudioRing& operator=(const SharedAudioRing&) = delete;

    /// Crea la region compartida y escribe el header (lado ESCRITOR).
    ///
    /// :param name: nombre logico del ring (p. ej. L"BA.Audio.Guest.1").
    /// :param channels: canales de audio (default 2).
    /// :param capacityBlocks: capacidad en bloques, POTENCIA DE 2 (default 16).
    /// :param writerPid: PID del escritor (para deteccion de reinicio).
    /// :param generation: generacion del escritor (se incrementa por arranque).
    /// :returns: Status; en exito el objeto queda listo para push().
    static Status init(const wchar_t* name, uint32_t channels, uint64_t capacityBlocks, uint32_t writerPid,
                       uint32_t generation, SharedAudioRing& out);

    /// Abre un ring ya creado y lo usa como LECTOR.
    ///
    /// :param name: nombre publicado por init().
    /// :returns: Status; en exito el objeto queda listo para pop().
    static Status openReader(const wchar_t* name, SharedAudioRing& out);

    /// Abre un ring ya creado y lo usa como ESCRITOR (mismo writerPid/generation
    /// que el header). Util para reabrir tras un reinicio del proceso.
    static Status openWriter(const wchar_t* name, uint32_t writerPid, uint32_t generation,
                             SharedAudioRing& out);

    /// Escribe un bloque de 480 frames (interleaved) con su timestamp QPC.
    ///
    /// Avanza writeIndex atomicamente (release). Si al escribir se pisarian
    /// datos aun no leidos (overrun), incrementa overrunCount y DESCARTA el
    /// bloque (no bloquea).
    ///
    /// :param samples: buffer con kBlockFrames*channels floats.
    /// :param qpc: timestamp QPC del bloque.
    /// :returns: true si se escribio, false si se descarto por overrun.
    bool push(const float* samples, uint64_t qpc);

    /// Lee el siguiente bloque COPIANDOLO a `out` (lado LECTOR).
    ///
    /// El bloque se copia ANTES de publicar readIndex: mientras el lector aun
    /// no publico el avance, el escritor ve el slot como ocupado y no lo pisa
    /// (push() descarta por overrun en vez de sobrescribir). Devolver un
    /// puntero al interior del ring seria una carrera: el escritor podria
    /// reclamar ese slot mientras el llamador todavia lee.
    ///
    /// Avanza readIndex atomicamente (release). Ante underrun llena `out` de
    /// silencio e incrementa underrunCount. Ante cambio de generacion del
    /// escritor descarta lo bufferizado, resincroniza readIndex a writeIndex y
    /// aplica fade-in de 10 ms.
    ///
    /// :param out: buffer del llamador con al menos blockFloats() floats.
    /// :param meta: recibe qpc y las banderas isSilence/isFadeIn; `meta.samples`
    ///              queda apuntando a `out` (los datos ya son del llamador).
    /// :returns: true si se leyo (con datos o con silencio); false si `out` es
    ///           nulo o el ring no esta mapeado.
    bool pop(float* out, PoppedBlock& meta);

    /// Lado LECTOR: cuantos bloques escritos todavia no se leyeron (0 si el ring
    /// no esta mapeado). Para leer al ritmo del escritor: todo lo que haya, sin
    /// pop() de mas (ante underrun pop() entrega silencio, no "nada").
    uint64_t pendientes() const;

    /// Bloques que el escritor descarto porque el lector no llegaba (overrunCount).
    uint64_t descartados() const;

    /// Lado LECTOR, sin bloque nuevo: cuándo volver a mirar (F2-14). Justo después del
    /// próximo bloque que el escritor debería escribir (el último más 10 ms, ipc/Mirada.hpp);
    /// `pasoQuieto` si no escribe hace más de 1 s.
    std::chrono::steady_clock::time_point proximaMirada(std::chrono::steady_clock::duration pasoQuieto) const;
    /// Cuánto después de su sello escribió el escritor el último bloque (ns; -1 si todavía
    /// no escribió). Con un escritor que sella con la hora del medio (baanillosink: el
    /// programa) es lo que tarda su audio en llegar: la latencia de la mezcla. El gateway
    /// demora con eso el video del programa para quien mira (F2-11). Se puede mirar sin leer
    /// (un lector que nunca saca bloques no le roba nada al que lee).
    int64_t retrasoDelEscritorNs() const;

    /// Tamano de un bloque en floats (kBlockFrames * channels).
    size_t blockFloats() const;

    /// Capacidad en bloques.
    uint64_t capacityBlocks() const;

    /// Puntero al header (solo lectura para el caller).
    const RingHeader* header() const;

    /// Puntero a la region subyacente (para diagnostico).
    SharedMemoryRegion* region();

    /// Lado LECTOR: true si el escritor al que estamos conectados ya no existe
    /// o fue reemplazado (el proceso productor se reinicio y volvio a crear el
    /// ring con el mismo nombre). Sin resuscribirse, el lector seguiria leyendo
    /// un ring que ya nadie escribe: audio mudo para siempre.
    ///
    /// Solo reabre el nombre si no llegaron datos en el ultimo segundo, y como
    /// mucho una vez por segundo: se puede llamar en cada vuelta del bucle.
    /// La identidad del escritor es (writerPid, generation) del header.
    bool stale();

   private:
    RingHeader* hdr() const;
    // Offset (en bytes) del inicio del area de datos.
    static size_t dataOffset();
    // Puntero al buffer de un bloque dado su indice logico (en bloques).
    // (No const en el retorno: el area es compartida; el caller castea a const.)
    float* blockAt(uint64_t blockIndex) const;

    SharedMemoryRegion region_;
    bool isWriter_ = false;
    // Estado cacheado del lector para detectar cambio de generacion.
    uint32_t cachedWriterPid_ = 0;
    uint32_t cachedGeneration_ = 0;
    bool hasCached_ = false;
    bool pendingFadeIn_ = false; // aplica fade-in al proximo pop tras resync
    // Para stale(): nombre abierto y relojes monotonos (ns).
    std::wstring name_;
    uint64_t lastDataNs_ = 0;
    uint64_t lastCheckNs_ = 0;
    mirada::Temblor temblor_; // cuánto tiembla el escritor (proximaMirada)
};

/// Lee TODO lo que el escritor dejo en el anillo, bloque por bloque, y se lo pasa a
/// `alBloque(const PoppedBlock&, size_t floats)`. Es la forma de leer al ritmo del
/// escritor: llamarla seguido (cada pocos ms) y dormir entre llamadas.
///
/// Sacar un bloque por vuelta y dormir el largo de un bloque NO alcanza: con el
/// trabajo de cada vuelta el lector va mas lento que el escritor, el anillo se llena
/// (la capacidad entera de atraso) y el escritor descarta bloques: cortes en el audio.
/// Sin pendientes no se lee nada: pop() inventaria silencio.
template <typename F>
void leerPendientes(SharedAudioRing& r, std::vector<float>& buf, F&& alBloque) {
    const size_t n = r.blockFloats();
    if (buf.size() < n) buf.resize(n);
    for (uint64_t k = r.pendientes(); k > 0; --k) {
        PoppedBlock blk;
        if (!r.pop(buf.data(), blk)) return;
        alBloque(blk, n);
    }
}

/// leerPendientes, midiendo el anillo (F0-02): la ocupación al mirar, los descartes del
/// escritor y la edad y los huecos de cada bloque leído.
template <typename F>
void leerPendientes(SharedAudioRing& r, std::vector<float>& buf, MedidaTransporte& medida, F&& alBloque) {
    medida.mirar(r.pendientes());
    medida.descartesDelEscritor(r.descartados());
    leerPendientes(r, buf, [&](const PoppedBlock& blk, size_t n) {
        medida.bloque(blk.qpc, HealthBlock::nowQpc());
        alBloque(blk, n);
    });
}

} // namespace ba::ipc
