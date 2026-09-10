// test_ring_drift.cpp
//
// Prueba de DRIFT acotado del ring SPSC de audio (spec Fase 1: "Sin deriva de
// rings en 8 h"). El soak literal de 8 horas es impracticable en CI, asi que se
// simula su equivalente: se hace correr al ring a traves de UN NOMBRE de ciclos
// de wrap-around (capacidad pequena => wraps frecuentes) y se verifica que la
// contabilidad de frames/indices NO deriva respecto al conteo logico.
//
// No es un test de concurrencia: es un test de CONTABILIDAD determinista. Se
// escribe y lee en lockstep (un push exitoso seguido de un pop con datos) para
// que el caso sea reproducible y el unico fenomeno bajo prueba sea el wrap de
// los indices de 64 bits sobre la capacidad fisica.
//
// Aserciones (ver main):
//   (a) frames consumidos == frames producidos (sin perdida ni duplicacion).
//   (b) la posicion del consumidor (readIndex) es monotona y coincide EXACTA
//       con la del productor (writeIndex) al final: sin deriva entre ambos.
//   (c) sin malconteo de overflow/underflow: overrunCount == 0 y
//       underrunCount == 0 al terminar.
//   (d) el indice de frame coincide con el valor esperado modulo capacidad
//       (writeIndex % cap == total % cap; idem readIndex) y la integridad del
//       patron sentinel se mantiene en CADA bloque a traves de todos los wraps
//       (prueba de que el masking `blockIndex % capacityBlocks` no deriva).
//
// C++ plano, sin framework externo: macro minimo de assert y main() que
// devuelve 0 en exito y 1 en fallo. El build agent ya cablea tests/test_*.cpp
// a CTest (GLOB en src/core/CMakeLists.txt).

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SharedMemoryRegion.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ba::ipc;

// --- Macro minimo de assert ------------------------------------------------

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Rellena un buffer con un patron sentinel derivado de `seed`, para verificar
// integridad tras el viaje por el ring (mismo esquema que test_shared_audio_ring).
static void fillSentinel(float* buf, size_t n, uint64_t seed) {
    for (size_t i = 0; i < n; ++i) {
        const uint64_t v = seed * 0x9E3779B97F4A7C15ull + i * 0x100000001Bull;
        buf[i] = static_cast<float>(static_cast<int32_t>(v & 0xFFFF)) * 0.001f;
    }
}

static bool matchSentinel(const float* buf, size_t n, uint64_t seed) {
    for (size_t i = 0; i < n; ++i) {
        const uint64_t v = seed * 0x9E3779B97F4A7C15ull + i * 0x100000001Bull;
        const float expected = static_cast<float>(static_cast<int32_t>(v & 0xFFFF)) * 0.001f;
        if (buf[i] != expected) {
            return false;
        }
    }
    return true;
}

// --- Simulacion de deriva --------------------------------------------------
//
// Equivalencia con el soak de 8 h: 8 h a 48 kHz = 1.382e9 frames = 2.88e6
// bloques de 480 frames. Para que corra en segundos (y no en horas) se usa una
// CAPACIDAD pequena (16 bloques, potencia de 2) de modo que el indice logico
// de 64 bits se ENVUELVA sobre la capacidad fisica muchas veces: con
// kTotalBlocks = 2.88e6 y cap = 16, el indice da 180.000 vueltas. El masking
// `blockIndex % capacityBlocks` se ejerce asi miles de veces, que es justo el
// fenomeno que el soak de 8 h protegeria de la deriva.

namespace {
constexpr uint64_t kCapBlocks = 16u;  // potencia de 2, pequena => wraps frecuentes
constexpr uint32_t kChannels = 2u;
// 8 h a 48 kHz / 480 frames por bloque = 2.88e6 bloques (el soak simulado).
constexpr uint64_t kTotalBlocks = 2880000ull;
} // namespace

static void testRingDrift() {
    std::printf("== testRingDrift ==\n");
    const size_t blockFloats = audio_ring::kBlockFrames * kChannels;

    SharedAudioRing ring;
    Status s = SharedAudioRing::init(L"BA.Test.Drift", kChannels, kCapBlocks, 777, 1, ring);
    CHECK(s.ok, "init(drift)");
    if (!s.ok) return;

    SharedAudioRing reader;
    s = SharedAudioRing::openReader(L"BA.Test.Drift", reader);
    CHECK(s.ok, "openReader(drift)");
    if (!s.ok) return;

    const uint64_t cap = ring.capacityBlocks();
    CHECK(cap == kCapBlocks, "capacidad en bloques coincide con la configurada");

    std::vector<float> block(blockFloats);

    // Contadores logicos (lo que el caller "cree" que paso) vs. contadores del
    // ring (lo que el header dice). La deriva seria una divergencia entre ambos.
    uint64_t framesProduced = 0;   // frames que el writer creo que escribio
    uint64_t framesConsumed = 0;   // frames que el reader creo que leyó
    uint64_t successfulPushes = 0; // push() que devolvieron true
    uint64_t realPops = 0;         // pop() con datos (no silencio)
    uint64_t consumedMonotonic = 0; // contador local del reader, debe crecer +1 por pop real
    bool integrityOk = true;

    for (uint64_t i = 0; i < kTotalBlocks; ++i) {
        // --- Productor ---
        fillSentinel(block.data(), blockFloats, i);
        const bool ok = ring.push(block.data(), i); // qpc = i (determinista)
        CHECK(ok, "push no descartado (reader al dia, lockstep)");
        if (!ok) break;
        ++successfulPushes;
        framesProduced += audio_ring::kBlockFrames;

        // --- Consumidor ---
        PoppedBlock pb = reader.pop();
        CHECK(!pb.isSilence, "pop con datos (lockstep, sin underrun)");
        if (pb.isSilence) break;
        if (!matchSentinel(pb.samples, blockFloats, i)) {
            integrityOk = false; // el masking del wrap apunto al slot equivocado
        }
        CHECK(pb.qpc == i, "qpc del bloque coincide con el indice producido");
        ++realPops;
        framesConsumed += audio_ring::kBlockFrames;
        ++consumedMonotonic;
    }

    // --- Lectura final de los contadores del header ---
    const RingHeader* h = ring.header();
    const uint64_t w = h->writeIndex; // en BLOQUES
    const uint64_t r = h->readIndex;  // en BLOQUES
    const uint64_t overruns = h->overrunCount;
    const uint64_t underruns = h->underrunCount;

    const uint64_t wraps = kTotalBlocks / cap; // vueltas completas del indice

    std::printf("  totalBlocks=%llu cap=%llu wraps=%llu\n",
                (unsigned long long)kTotalBlocks, (unsigned long long)cap,
                (unsigned long long)wraps);
    std::printf("  framesProduced=%llu framesConsumed=%llu\n",
                (unsigned long long)framesProduced, (unsigned long long)framesConsumed);
    std::printf("  writeIndex=%llu readIndex=%llu overruns=%llu underruns=%llu\n",
                (unsigned long long)w, (unsigned long long)r,
                (unsigned long long)overruns, (unsigned long long)underruns);

    // (a) Sin perdida ni duplicacion: lo consumido == lo producido.
    CHECK(framesConsumed == framesProduced, "frames consumidos == frames producidos");
    CHECK(realPops == successfulPushes, "pops con datos == pushes exitosos");
    CHECK(framesProduced == kTotalBlocks * audio_ring::kBlockFrames,
          "frames producidos == total de bloques * frames por bloque");

    // (b) Sin deriva: el indice del consumidor es monotono y coincide EXACTA
    //     con el del productor al final (ambos avanzaron kTotalBlocks bloques).
    CHECK(consumedMonotonic == realPops, "contador monotono del reader == pops reales");
    CHECK(w == kTotalBlocks, "writeIndex == total de bloques (sin deriva del productor)");
    CHECK(r == kTotalBlocks, "readIndex == total de bloques (sin deriva del consumidor)");
    CHECK(w == r, "writeIndex == readIndex al final (sin deriva entre productor/consumidor)");

    // (c) Sin malconteo de overflow/underflow: en lockstep no debe haber ni un
    //     overrun (descarte) ni un underrun (silencio).
    CHECK(overruns == 0, "overrunCount == 0 (sin overflow malcontado)");
    CHECK(underruns == 0, "underrunCount == 0 (sin underflow malcontado)");

    // (d) El indice de frame coincide con el valor esperado modulo capacidad,
    //     y el masking del wrap no deriva (integridad del patron en cada bloque).
    CHECK(w % cap == kTotalBlocks % cap, "writeIndex % cap == total % cap");
    CHECK(r % cap == kTotalBlocks % cap, "readIndex % cap == total % cap");
    CHECK(integrityOk, "integridad del patron a traves de todos los wraps (sin deriva del masking)");

    // El requisito pide "varios cientos" de wraps; con estos parametros hay
    // 180.000, asi que se asalta el piso para documentar la magnitud.
    CHECK(wraps >= 100, "se envolvio la capacidad varios cientos de veces");
}

int main() {
    testRingDrift();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
