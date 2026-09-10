// test_health_block.cpp
//
// Pruebas unitarias del bloque de salud "BA.Health" (spec §4.4) sobre la
// primitiva SharedMemoryRegion. Sin framework externo: es un main() que
// devuelve 0 si todo pasa y 1 si algo falla (lo registra CTest).
//
// Casos (spec de la tarea):
//   1. beat() y luego read() devuelven Running + un timestamp reciente.
//   2. isStale() pasa a true cuando el umbral se supera (nowQpc inyectado).
//   3. Varios slots son independientes entre si.
//   4. Reabrir (open tras create) ve los mismos datos: simula al launcher
//      leyendo los heartbeats de sus hijos.
//
// Se inyecta nowQpc() de forma determinista en isStale() para que el test no
// dependa del reloj real; beat() usa nowQpc() interno y se valida que el
// timestamp estampado quede entre el "antes" y el "despues" medidos.

#include "ipc/HealthBlock.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using ba::ipc::HealthBlock;
using ba::ipc::ProcessState;
using ba::ipc::Snapshot;
using ba::ipc::Status;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);            \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// 1) beat() -> read() devuelve Running + timestamp reciente.
void testBeatThenRead() {
    HealthBlock block;
    Status st = HealthBlock::create(HealthBlock::kName, /*slotCount=*/4, block);
    CHECK(st.ok);
    CHECK(block.valid());
    CHECK(block.slotCount() == 4);

    const std::uint64_t before = HealthBlock::nowQpc();
    block.beat(0);
    const std::uint64_t after = HealthBlock::nowQpc();

    Snapshot snap = block.read(0);
    CHECK(snap.state == ProcessState::Running);
    // El timestamp estampado debe estar entre el "antes" y el "despues".
    CHECK(snap.lastBeatQpc >= before);
    CHECK(snap.lastBeatQpc <= after);
    // "Reciente": la diferencia con el "despues" es minima (mismo reloj).
    CHECK(after - snap.lastBeatQpc < 1'000'000'000ULL); // < 1 s

    // Un slot que nunca latio sigue Unknown y con lastBeatQpc == 0.
    Snapshot idle = block.read(1);
    CHECK(idle.state == ProcessState::Unknown);
    CHECK(idle.lastBeatQpc == 0);
}

// 2) isStale() pasa a true cuando el umbral se supera (nowQpc inyectado).
void testIsStaleFlips() {
    HealthBlock block;
    Status st = HealthBlock::create(HealthBlock::kName, /*slotCount=*/2, block);
    CHECK(st.ok);

    block.beat(0);
    const std::uint64_t beat = block.read(0).lastBeatQpc;
    CHECK(beat != 0);

    const std::uint64_t threshold = HealthBlock::kDefaultHangThresholdNs; // 5 s

    // Fresco: ahora = beat + umbral/2  ->  no estancado.
    CHECK(block.isStale(0, beat + threshold / 2, threshold) == false);
    // Exactamente en el umbral -> estancado (>=).
    CHECK(block.isStale(0, beat + threshold, threshold) == true);
    // Muy atrasado -> estancado.
    CHECK(block.isStale(0, beat + 10 * threshold, threshold) == true);

    // Slot sin latidos: estancado si ya paso el umbral desde la epoca.
    CHECK(block.isStale(1, 0, threshold) == false); // ahora == 0, no paso
    CHECK(block.isStale(1, threshold, threshold) == true);

    // Slot invalido: no estancado (no-fail).
    CHECK(block.isStale(99, beat + 10 * threshold, threshold) == false);
}

// 3) Varios slots son independientes entre si.
void testSlotsIndependent() {
    HealthBlock block;
    Status st = HealthBlock::create(HealthBlock::kName, /*slotCount=*/4, block);
    CHECK(st.ok);

    // Cada slot publica su identidad y su estado de forma independiente.
    block.setIdentity(0, /*pid=*/100, /*version=*/1);
    block.setIdentity(1, /*pid=*/200, /*version=*/2);
    block.setIdentity(2, /*pid=*/300, /*version=*/3);

    block.setState(0, ProcessState::Running);
    block.setState(1, ProcessState::Starting);
    block.setState(2, ProcessState::Hung);
    // slot 3 queda Unknown.

    Snapshot s0 = block.read(0);
    Snapshot s1 = block.read(1);
    Snapshot s2 = block.read(2);
    Snapshot s3 = block.read(3);

    CHECK(s0.pid == 100 && s0.state == ProcessState::Running);
    CHECK(s1.pid == 200 && s1.state == ProcessState::Starting);
    CHECK(s2.pid == 300 && s2.state == ProcessState::Hung);
    CHECK(s3.pid == 0 && s3.state == ProcessState::Unknown);

    // Un beat en un slot no altera los demas.
    block.beat(1);
    CHECK(block.read(1).state == ProcessState::Running);
    CHECK(block.read(0).state == ProcessState::Running); // intacto
    CHECK(block.read(2).state == ProcessState::Hung);     // intacto
    CHECK(block.read(0).pid == 100);                      // pid intacto
    CHECK(block.read(1).pid == 200);                      // beat preserva pid

    // isStale por slot es independiente: solo el que no latio es estancado.
    const std::uint64_t beat1 = block.read(1).lastBeatQpc;
    const std::uint64_t threshold = HealthBlock::kDefaultHangThresholdNs;
    CHECK(block.isStale(1, beat1 + threshold / 2, threshold) == false);
    CHECK(block.isStale(2, beat1 + 10 * threshold, threshold) == true); // s2 sin beat
}

// 4) Reabrir (open tras create) ve los mismos datos: simula al launcher
//    leyendo los heartbeats de sus hijos.
void testReopenSeesSameData() {
    HealthBlock writer;
    Status st = HealthBlock::create(HealthBlock::kName, /*slotCount=*/4, writer);
    CHECK(st.ok);

    // El "hijo" publica su identidad y latidos.
    writer.setIdentity(0, /*pid=*/4242, /*version=*/7);
    writer.setState(0, ProcessState::Running);
    writer.beat(0);
    const std::uint64_t beat = writer.read(0).lastBeatQpc;
    CHECK(beat != 0);

    // El "launcher" abre el mismo bloque por nombre y lee los datos.
    HealthBlock reader;
    Status ost = HealthBlock::open(HealthBlock::kName, reader);
    CHECK(ost.ok);
    CHECK(reader.valid());
    CHECK(reader.slotCount() == 4);

    Snapshot seen = reader.read(0);
    CHECK(seen.pid == 4242);
    CHECK(seen.state == ProcessState::Running);
    CHECK(seen.version == 7);
    CHECK(seen.lastBeatQpc == beat);

    // El launcher puede evaluar isStale sobre lo que ve del hijo.
    const std::uint64_t threshold = HealthBlock::kDefaultHangThresholdNs;
    CHECK(reader.isStale(0, beat + threshold / 2, threshold) == false);
    CHECK(reader.isStale(0, beat + 10 * threshold, threshold) == true);

    // Un open de un nombre que no existe falla limpiamente (kNotFound).
    HealthBlock missing;
    Status mst = HealthBlock::open(L"BA.NoExiste", missing);
    CHECK(!mst.ok);
    CHECK(!missing.valid());
}

} // namespace

int main() {
    testBeatThenRead();
    testIsStaleFlips();
    testSlotsIndependent();
    testReopenSeesSameData();

    if (g_failures == 0) {
        std::printf("test_health_block: OK\n");
        return 0;
    }
    std::printf("test_health_block: %d fallo(s)\n", g_failures);
    return 1;
}
