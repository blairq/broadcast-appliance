// test_shared_audio_ring.cpp
//
// Pruebas de unidad del ring SPSC de audio (spec 4.3). C++ plano, sin
// framework externo: un macro minimo de assert y un main() que devuelve 0 en
// exito y 1 en fallo. El build agent cablea src/core/tests/test_*.cpp a CTest.
//
// Casos:
//   1. concurrencia: 1 writer + 1 reader, N bloques; sin deadlock, popped ==
//      pushed menos drops documentados, contadores underrun/overrun consistentes.
//   2. wraparound: envolver la capacidad al menos 2 veces; integridad de datos
//      (patron sentinel) y correccion del wrap de indices.
//   3. reinicio del escritor: bump de generacion; el lector resincroniza
//      (descarta lo viejo, aplica fade) y sigue con los datos nuevos.
//   + chequeos deterministas de los contadores de overrun/underrun.
//   4. al ritmo del escritor: un bloque cada 10 ms y un lector con trabajo por
//      vuelta. Sacando uno y durmiendo 10 ms se pierden bloques (el retorno de
//      audio del gateway, antes); con leerPendientes no se pierde ninguno.

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SharedMemoryRegion.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace ba::ipc;
using namespace std::chrono_literals;

// --- Macro minimo de assert ------------------------------------------------

static int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);                                       \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

// Rellena un buffer con un patron sentinel derivado de `seed`, para verificar
// integridad tras el viaje por el ring.
static void fillSentinel(float* buf, size_t n, uint64_t seed) {
    for (size_t i = 0; i < n; ++i) {
        // Determinista y distinguible por (seed, i).
        const uint64_t v = seed * 0x9E3779B97F4A7C15ull + i * 0x100000001B3ull;
        buf[i] = static_cast<float>(static_cast<int32_t>(v & 0xFFFF)) * 0.001f;
    }
}

static bool matchSentinel(const float* buf, size_t n, uint64_t seed) {
    for (size_t i = 0; i < n; ++i) {
        const uint64_t v = seed * 0x9E3779B97F4A7C15ull + i * 0x100000001B3ull;
        const float expected = static_cast<float>(static_cast<int32_t>(v & 0xFFFF)) * 0.001f;
        if (buf[i] != expected) {
            return false;
        }
    }
    return true;
}

// Reloj monotono en "ns" para los timestamps QPC de prueba.
static uint64_t nowNs() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

// --- 1. Concurrencia -------------------------------------------------------

static void testConcurrency() {
    std::printf("== testConcurrency ==\n");
    constexpr uint64_t N = 2000;
    // Capacidad >= N => el writer nunca se adelanta mas que la capacidad, por
    // lo que NO hay overruns en este caso (drops documentados = 0). Los
    // underruns si pueden ocurrir (reader mas rapido) y se cuentan.
    constexpr uint64_t capBlocks = 4096; // potencia de 2, >= N
    constexpr uint32_t channels = 2;
    const size_t blockFloats = audio_ring::kBlockFrames * channels;

    SharedAudioRing ring;
    Status s = SharedAudioRing::init(L"BA.Test.Concurrency", channels, capBlocks, 111, 1, ring);
    CHECK(s.ok, "init(concurrency)");
    if (!s.ok) return;

    SharedAudioRing reader;
    s = SharedAudioRing::openReader(L"BA.Test.Concurrency", reader);
    CHECK(s.ok, "openReader(concurrency)");
    if (!s.ok) return;

    std::vector<float> block(blockFloats);
    uint64_t successfulPushes = 0;
    uint64_t droppedPushes = 0;

    std::thread writer([&] {
        for (uint64_t i = 0; i < N; ++i) {
            fillSentinel(block.data(), blockFloats, i);
            const bool ok = ring.push(block.data(), nowNs());
            if (ok) {
                ++successfulPushes;
            } else {
                ++droppedPushes;
            }
        }
    });

    uint64_t poppedReal = 0;
    uint64_t poppedSilence = 0;
    bool integrityOk = true;
    uint64_t nextExpected = 0;
    // Timeout de seguridad contra deadlock: si no llega a N en 30 s, algo fallo.
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    // Buffer de destino de pop(): el bloque se copia aca, no se apunta al ring.
    std::vector<float> outBlk(blockFloats);
    while (poppedReal < N && std::chrono::steady_clock::now() < deadline) {
        PoppedBlock pb;
        CHECK(reader.pop(outBlk.data(), pb), "pop(out) valido");
        if (pb.isSilence) {
            ++poppedSilence;
            continue;
        }
        // El bloque real debe coincidir con el patron del indice esperado.
        if (!matchSentinel(pb.samples, blockFloats, nextExpected)) {
            integrityOk = false;
        }
        ++poppedReal;
        ++nextExpected;
    }
    writer.join();

    const uint64_t overruns = ring.header()->overrunCount;
    const uint64_t underruns = ring.header()->underrunCount;

    std::printf("  pushed=%llu dropped=%llu poppedReal=%llu silence=%llu "
                "overruns=%llu underruns=%llu\n",
                (unsigned long long)successfulPushes, (unsigned long long)droppedPushes,
                (unsigned long long)poppedReal, (unsigned long long)poppedSilence,
                (unsigned long long)overruns, (unsigned long long)underruns);

    CHECK(poppedReal == N, "no deadlock: el lector recibio todos los bloques");
    CHECK(integrityOk, "integridad de datos en concurrencia");
    // popped == pushed menos drops documentados. Con cap >= N, drops = 0.
    CHECK(droppedPushes == overruns, "drops documentados == overrunCount");
    CHECK(poppedReal == successfulPushes - droppedPushes, "popped == pushed - drops");
    // Consistencia de contadores: los underruns son los pops de silencio.
    CHECK(underruns == poppedSilence, "underrunCount == pops de silencio");
    // Con capacidad >= N no debe haber overruns.
    CHECK(overruns == 0, "sin overruns con capacidad >= N");
}

// --- 2. Wraparound ---------------------------------------------------------

static void testWraparound() {
    std::printf("== testWraparound ==\n");
    constexpr uint64_t capBlocks = 8; // potencia de 2
    constexpr uint32_t channels = 2;
    const size_t blockFloats = audio_ring::kBlockFrames * channels;
    // Envolver al menos 2 veces: 2*cap + margen.
    constexpr uint64_t total = capBlocks * 3; // 3 vueltas completas

    SharedAudioRing ring;
    Status s = SharedAudioRing::init(L"BA.Test.Wrap", channels, capBlocks, 222, 1, ring);
    CHECK(s.ok, "init(wrap)");
    if (!s.ok) return;

    SharedAudioRing reader;
    s = SharedAudioRing::openReader(L"BA.Test.Wrap", reader);
    CHECK(s.ok, "openReader(wrap)");
    if (!s.ok) return;

    std::vector<float> block(blockFloats);
    // Escribimos y leemos de forma entrelazada para forzar el wrap del indice
    // logico sobre la capacidad fisica, verificando el patron en cada paso.
    bool integrityOk = true;
    std::vector<float> outBlk(blockFloats);
    for (uint64_t i = 0; i < total; ++i) {
        fillSentinel(block.data(), blockFloats, i);
        const bool ok = ring.push(block.data(), nowNs());
        CHECK(ok, "push no descartado en wrap (reader al dia)");
        if (!ok) break;

        PoppedBlock pb;
        CHECK(reader.pop(outBlk.data(), pb), "pop(out) valido en wrap");
        CHECK(!pb.isSilence, "pop con datos en wrap");
        if (pb.isSilence) break;
        if (!matchSentinel(pb.samples, blockFloats, i)) {
            integrityOk = false;
        }
    }

    // Verificacion del wrap de indices: tras total bloques, writeIndex == total
    // y readIndex == total (ambos han dado total/capBlocks vueltas).
    const uint64_t w = ring.header()->writeIndex;
    const uint64_t r = ring.header()->readIndex;
    std::printf("  writeIndex=%llu readIndex=%llu cap=%llu vueltas=%llu\n", (unsigned long long)w,
                (unsigned long long)r, (unsigned long long)capBlocks,
                (unsigned long long)(total / capBlocks));
    CHECK(integrityOk, "integridad de datos (sentinel) a traves del wrap");
    CHECK(w == total, "writeIndex wrap correcto");
    CHECK(r == total, "readIndex wrap correcto");
    CHECK(total / capBlocks >= 2, "se envolvio la capacidad al menos 2 veces");
}

// --- 3. Reinicio del escritor ---------------------------------------------

static void testWriterRestart() {
    std::printf("== testWriterRestart ==\n");
    constexpr uint64_t capBlocks = 16;
    constexpr uint32_t channels = 2;
    const size_t blockFloats = audio_ring::kBlockFrames * channels;

    SharedAudioRing ring;
    Status s = SharedAudioRing::init(L"BA.Test.Restart", channels, capBlocks, 333, 1, ring);
    CHECK(s.ok, "init(restart)");
    if (!s.ok) return;

    SharedAudioRing reader;
    s = SharedAudioRing::openReader(L"BA.Test.Restart", reader);
    CHECK(s.ok, "openReader(restart)");
    if (!s.ok) return;

    std::vector<float> block(blockFloats);

    // Fase 1: el writer original (gen 1) escribe 3 bloques.
    for (uint64_t i = 0; i < 3; ++i) {
        fillSentinel(block.data(), blockFloats, 1000 + i);
        CHECK(ring.push(block.data(), nowNs()), "push fase 1");
    }

    // El lector consume los 3 bloques viejos (antes del reinicio).
    std::vector<float> outBlk(blockFloats);
    for (uint64_t i = 0; i < 3; ++i) {
        PoppedBlock pb;
        CHECK(reader.pop(outBlk.data(), pb), "pop(out) valido fase 1");
        CHECK(!pb.isSilence, "pop fase 1 con datos");
        CHECK(matchSentinel(pb.samples, blockFloats, 1000 + i), "datos fase 1");
    }

    // Fase 2: simular reinicio del writer -> nueva generacion (y nuevo PID).
    // Se reabre como writer con generation 2.
    SharedAudioRing ring2;
    s = SharedAudioRing::openWriter(L"BA.Test.Restart", 444, 2, ring2);
    CHECK(s.ok, "openWriter(gen 2)");
    if (!s.ok) return;

    // El lector detecta el cambio de generacion en el proximo pop: descarta lo
    // bufferizado, resincroniza readIndex a writeIndex y aplica fade-in.
    PoppedBlock resync;
    CHECK(reader.pop(outBlk.data(), resync), "pop(out) valido en resync");
    CHECK(resync.isFadeIn, "fade-in aplicado tras cambio de generacion");

    // Fase 3: el writer nuevo escribe datos nuevos; el lector los recibe.
    bool newDataOk = true;
    for (uint64_t i = 0; i < 3; ++i) {
        fillSentinel(block.data(), blockFloats, 2000 + i);
        CHECK(ring2.push(block.data(), nowNs()), "push fase 3");
        PoppedBlock pb;
        CHECK(reader.pop(outBlk.data(), pb), "pop(out) valido fase 3");
        CHECK(!pb.isSilence, "pop fase 3 con datos");
        if (!matchSentinel(pb.samples, blockFloats, 2000 + i)) {
            newDataOk = false;
        }
    }
    CHECK(newDataOk, "el lector sigue con los datos del writer nuevo");

    // El header refleja la nueva identidad.
    CHECK(ring2.header()->generation == 2, "generacion actualizada a 2");
    CHECK(ring2.header()->writerPid == 444, "writerPid actualizado a 444");
}

// --- Contadores deterministas ---------------------------------------------

static void testCounters() {
    std::printf("== testCounters ==\n");
    // Overrun: anillo pequeno, escribir mas bloques que la capacidad sin leer.
    {
        constexpr uint64_t capBlocks = 2;
        constexpr uint32_t channels = 1;
        const size_t blockFloats = audio_ring::kBlockFrames * channels;
        SharedAudioRing ring;
        Status s = SharedAudioRing::init(L"BA.Test.Overrun", channels, capBlocks, 1, 1, ring);
        CHECK(s.ok, "init(overrun)");
        if (!s.ok) return;
        std::vector<float> block(blockFloats, 0.0f);
        int okCount = 0;
        for (uint64_t i = 0; i < capBlocks + 3; ++i) {
            if (ring.push(block.data(), nowNs())) ++okCount;
        }
        const uint64_t overruns = ring.header()->overrunCount;
        std::printf("  ok=%d overruns=%llu\n", okCount, (unsigned long long)overruns);
        CHECK(okCount == (int)capBlocks, "solo caben capBlocks sin leer");
        CHECK(overruns == 3, "overrunCount == bloques descartados");
    }
    // Underrun: leer sin datos incrementa underrunCount.
    {
        constexpr uint64_t capBlocks = 4;
        constexpr uint32_t channels = 1;
        SharedAudioRing ring;
        Status s = SharedAudioRing::init(L"BA.Test.Underrun", channels, capBlocks, 1, 1, ring);
        CHECK(s.ok, "init(underrun)");
        if (!s.ok) return;
        SharedAudioRing reader;
        s = SharedAudioRing::openReader(L"BA.Test.Underrun", reader);
        CHECK(s.ok, "openReader(underrun)");
        if (!s.ok) return;
        int silenceCount = 0;
        std::vector<float> outBlk(reader.blockFloats());
        for (int i = 0; i < 5; ++i) {
            PoppedBlock pb;
            CHECK(reader.pop(outBlk.data(), pb), "pop(out) valido en underrun");
            if (pb.isSilence) ++silenceCount;
        }
        const uint64_t underruns = ring.header()->underrunCount;
        std::printf("  silence=%d underruns=%llu\n", silenceCount, (unsigned long long)underruns);
        CHECK(silenceCount == 5, "5 pops de silencio");
        CHECK(underruns == 5, "underrunCount == pops de silencio");
    }
}

// Escribe un bloque cada 10 ms durante `ms` y devuelve los descartados; el lector
// hace `vuelta` en un bucle propio hasta que el escritor termina.
template <typename Vuelta>
static uint64_t alRitmo(const wchar_t* nombre, int ms, Vuelta vuelta) {
    SharedAudioRing ring, reader;
    CHECK(SharedAudioRing::init(nombre, 2, 16, 333, 1, ring).ok, "init(ritmo)");
    CHECK(SharedAudioRing::openReader(nombre, reader).ok, "openReader(ritmo)");
    std::atomic<bool> fin{false};
    std::thread lector([&] {
        while (!fin.load()) vuelta(reader);
    });
    std::vector<float> blk(reader.blockFloats(), 0.25f);
    auto t = std::chrono::steady_clock::now();
    for (int i = 0; i < ms / 10; ++i) {
        t += 10ms;
        std::this_thread::sleep_until(t);
        ring.push(blk.data(), static_cast<uint64_t>(i));
    }
    fin = true;
    lector.join();
    return reader.descartados();
}

static void testAlRitmo() {
    std::vector<float> buf;
    uint64_t leidos = 0;
    // El trabajo de cada vuelta (repartir a los invitados), ~3 ms.
    const auto trabajo = [] { std::this_thread::sleep_for(3ms); };
    const uint64_t antes = alRitmo(L"BA.Test.RitmoViejo", 3000, [&](SharedAudioRing& r) {
        if (buf.size() < r.blockFloats()) buf.resize(r.blockFloats());
        PoppedBlock pb;
        r.pop(buf.data(), pb);
        trabajo();
        std::this_thread::sleep_for(10ms);
    });
    CHECK(antes > 0, "un bloque por vuelta y dormir 10 ms: el lector no llega y se pierden bloques");
    const uint64_t ahora = alRitmo(L"BA.Test.RitmoNuevo", 3000, [&](SharedAudioRing& r) {
        leerPendientes(r, buf, [&](const PoppedBlock& pb, size_t) {
            if (!pb.isSilence) ++leidos;
        });
        trabajo();
        std::this_thread::sleep_for(2ms);
    });
    CHECK(ahora == 0, "leerPendientes: no se pierde ningun bloque");
    CHECK(leidos >= 295, "leerPendientes: llegan todos los bloques, sin silencio inventado");
    std::printf("al ritmo: antes %llu descartados, ahora %llu (%llu leidos)\n", (unsigned long long)antes,
                (unsigned long long)ahora, (unsigned long long)leidos);
}

int main() {
    testConcurrency();
    testWraparound();
    testWriterRestart();
    testCounters();
    testAlRitmo();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
