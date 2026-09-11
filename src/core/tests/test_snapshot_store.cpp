// test_snapshot_store.cpp
//
// Pruebas unitarias del snapshot con doble generacion (spec §11.1/§11.2) y del
// modelo unico de estado StateDoc. Sin framework externo: es un main() que
// devuelve 0 si todo pasa y 1 si algo falla (lo registra CTest).
//
// Casos (spec de la tarea):
//   1. write y luego readLatest devuelven el mismo doc (secciones + secuencia).
//   2. Doble generacion: dos escrituras van a A y luego a B; readLatest
//      devuelve la de mayor secuencia.
//   3. Corrupcion: se corrompen los bytes de la generacion MAS ANTIGUA;
//      readLatest sigue devolviendo la mas nueva valida.
//   4. Checksum: se altera un archivo para que su checksum no coincida; se
//      trata como invalido y se usa la otra generacion.
//   5. Fault-injection: setFaultAtStep(Rename) -> esa escritura falla
//      (physicalWrites no cambia en ese intento) y el snapshot valido anterior
//      sigue siendo legible; se limpia el fault y flushNow() -> exito.
//   6. Debounce: 5 scheduleWrite(immediate=false) rapidos -> physicalWrites
//      sube como maximo 1; un scheduleWrite(immediate=true) fuerza la escritura.
//
// Cada test usa un directorio temporal propio (std::filesystem::temp + sufijo
// unico) y lo limpia al terminar. El debounce se prueba con un reloj real
// (steady_clock): 5 llamadas rapidas caben con holgura en los 500 ms.

#include "persist/SnapshotStore.hpp"
#include "persist/StateDoc.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <random>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h> // getpid() para el nombre del temp dir
#endif

using ba::persist::ErrorCode;
using ba::persist::SnapshotStore;
using ba::persist::StateDoc;
using ba::persist::Status;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

// --- helpers ----------------------------------------------------------------

// Semilla unica por proceso/ejecucion para el nombre del temp dir. En Windows
// no hay getpid(); usamos un random_device sembrado una sola vez.
int uniqueSeed() {
#if defined(_WIN32)
    static const int seed = static_cast<int>(std::random_device{}());
    return seed;
#else
    return ::getpid();
#endif
}

// Crea un directorio temporal unico y lo devuelve. El caller lo limpia.
std::string makeTempDir() {
    const std::string base = (std::filesystem::temp_directory_path() / "ba_snap_test").string();
    static int counter = 0;
    std::string dir = base + "_" + std::to_string(uniqueSeed()) + "_" + std::to_string(++counter);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Limpia un directorio temporal (best-effort).
void removeDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// Construye un StateDoc de prueba con secciones representativas (incluye
// comillas, backslashes y saltos de linea para ejercitar el escape JSON).
StateDoc makeDoc(std::uint64_t seq, bool clean = false) {
    StateDoc doc;
    doc.schemaVersion = 1;
    doc.sequence = seq;
    doc.cleanShutdown = clean;
    doc.sections["devices"] = "{\"primary\":\"USB Cam \\\"Pro\\\"\",\"backup\":\"HDMI\"}";
    doc.sections["scenes"] = "{\"active\":\"Sala\",\"list\":[\"Intro\",\"Sala\"]}";
    doc.sections["volumes"] = "{\"master\":0.85,\"guest\":0.6}";
    doc.sections["lower_third"] = "{\"text\":\"Buenas noches\\nEquipo\"}";
    doc.sections["tunnel"] = "{\"id\":\"abc123\",\"token\":\"t\"}";
    doc.sections["live"] = "{\"on\":true,\"key\":\"sk\"}";
    return doc;
}

// Lee un archivo completo a memoria; devuelve false si no existe.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = s;
    return true;
}

// Escribe un string completo a un archivo (binario). Devuelve true si se
// pudo abrir y escribir.
bool writeFileAll(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// Corrompe UN byte del body de un archivo de snapshot (cambia el checksum).
// `offsetFromStart` es relativo al inicio del archivo; se elige un byte dentro
// del body (despues del header). Devuelve false si no se pudo corromper.
bool corruptBodyByte(const std::string& path, std::size_t offsetFromStart) {
    std::string content;
    if (!readFileAll(path, content)) {
        return false;
    }
    if (offsetFromStart >= content.size()) {
        return false;
    }
    // Invierte el byte: garantiza un cambio (0->1 o 1->0).
    content[offsetFromStart] = static_cast<char>(content[offsetFromStart] ^ 0x01);
    return writeFileAll(path, content);
}

// Devuelve el offset de un byte dentro del body (despues del '\n' del header).
std::size_t bodyOffset(const std::string& path, std::size_t fromBodyStart) {
    std::string content;
    if (!readFileAll(path, content)) {
        return 0;
    }
    const std::size_t nl = content.find('\n');
    if (nl == std::string::npos) {
        return 0;
    }
    return nl + 1 + fromBodyStart;
}

// --- 1) write -> readLatest devuelve el mismo doc ---------------------------

void testWriteThenRead() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    StateDoc doc = makeDoc(/*seq=*/7, /*clean=*/true);
    store.scheduleWrite(doc, /*immediate=*/true);
    CHECK(store.physicalWrites() == 1);

    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.schemaVersion == 1);
    CHECK(out.sequence == 7);
    CHECK(out.cleanShutdown == true);
    // Todas las secciones, byte a byte (incluye comillas/backslashes/newlines).
    CHECK(out.sections.size() == doc.sections.size());
    for (const auto& [name, blob] : doc.sections) {
        auto it = out.sections.find(name);
        CHECK(it != out.sections.end());
        if (it != out.sections.end()) {
            CHECK(it->second == blob);
        }
    }
    removeDir(dir);
}

// --- 2) doble generacion: A luego B; readLatest toma la mas nueva -----------

void testDoubleGeneration() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // Primera escritura -> generacion A (nextIsA_ empieza en true).
    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    CHECK(std::filesystem::exists(store.pathA()));
    CHECK(!std::filesystem::exists(store.pathB()));

    // Segunda escritura -> generacion B (alterna A/B).
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);
    CHECK(std::filesystem::exists(store.pathA()));
    CHECK(std::filesystem::exists(store.pathB()));
    CHECK(store.physicalWrites() == 2);

    // readLatest devuelve la de MAYOR secuencia (la de B, seq=2).
    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 2);

    // Verificacion directa de cada generacion: A tiene seq=1, B tiene seq=2.
    // (readLatest ya eligio la mas nueva; esto confirma que cada archivo
    //  guardo su propia secuencia, no la del otro.)
    {
        std::string contentA, contentB;
        CHECK(readFileAll(store.pathA(), contentA));
        CHECK(readFileAll(store.pathB(), contentB));
        // El header de A debe decir seq=1 y el de B seq=2.
        CHECK(contentA.find("seq=1 ") != std::string::npos);
        CHECK(contentB.find("seq=2 ") != std::string::npos);
    }
    removeDir(dir);
}

// --- 3) corrupcion de la generacion mas antigua -----------------------------

void testCorruptionOlderGeneration() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // A = seq 1 (mas antigua), B = seq 2 (mas nueva).
    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);

    // Corrompe UN byte del body de A (la mas antigua). Rompe su checksum.
    const std::size_t off = bodyOffset(store.pathA(), /*fromBodyStart=*/5);
    CHECK(corruptBodyByte(store.pathA(), off));

    // readLatest sigue devolviendo la mas nueva valida (B, seq=2).
    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 2);
    removeDir(dir);
}

// --- 4) checksum mismatch: se usa la otra generacion ------------------------

void testChecksumMismatch() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // A = seq 1, B = seq 2.
    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);

    // Altera un byte del body de B (la mas nueva) para que su checksum no
    // coincida. B queda INVALIDA; readLatest debe caer a A (seq=1).
    const std::size_t off = bodyOffset(store.pathB(), /*fromBodyStart=*/5);
    CHECK(corruptBodyByte(store.pathB(), off));

    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    // B invalida -> se usa la otra generacion (A, seq=1).
    CHECK(out.sequence == 1);
    removeDir(dir);
}

// --- 5) fault-injection en Rename -------------------------------------------

void testFaultInjectionRename() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // Snapshot valido anterior (seq=1).
    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    CHECK(store.physicalWrites() == 1);

    // Inyecta fallo en el rename: la proxima escritura fisica fracasa.
    store.setFaultAtStep(SnapshotStore::Step::Rename);
    const std::uint64_t before = store.physicalWrites();
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);
    // El intento fallo: physicalWrites NO cambio.
    CHECK(store.physicalWrites() == before);
    // El doc seq=2 queda pendiente en memoria (no se pierde).
    CHECK(store.hasPending());
    CHECK(store.pendingDoc().sequence == 2);

    // El snapshot valido ANTERIOR sigue siendo legible (seq=1).
    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 1);

    // Limpia el fault y fuerza la escritura pendiente: exito.
    store.setFaultAtStep(SnapshotStore::Step::None);
    store.flushNow();
    CHECK(store.physicalWrites() == before + 1);
    CHECK(!store.hasPending());

    // Ahora readLatest devuelve el seq=2 (la escritura que se reintento).
    rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 2);
    removeDir(dir);
}

// --- 6) debounce: 5 rapidos -> como maximo 1 escritura ----------------------

void testDebounce() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    const std::uint64_t before = store.physicalWrites();
    // 5 escrituras no-immediatas rapidas: solo la primera escribe (debounce de
    // 500 ms); las 4 restantes quedan pendientes.
    for (int i = 0; i < 5; ++i) {
        store.scheduleWrite(makeDoc(/*seq=*/static_cast<std::uint64_t>(i + 1)),
                            /*immediate=*/false);
    }
    const std::uint64_t delta = store.physicalWrites() - before;
    CHECK(delta <= 1);
    // El doc pendiente es el ULTIMO (seq=5), no se pierden los intermedios.
    CHECK(store.hasPending());
    CHECK(store.pendingDoc().sequence == 5);

    // Un evento critico (immediate=true) fuerza la escritura AHORA.
    const std::uint64_t beforeCrit = store.physicalWrites();
    store.scheduleWrite(makeDoc(/*seq=*/6), /*immediate=*/true);
    CHECK(store.physicalWrites() == beforeCrit + 1);
    CHECK(!store.hasPending());

    // readLatest devuelve el ultimo escrito (seq=6).
    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 6);
    removeDir(dir);
}

// --- bonus: disco ausente no pierde estado ----------------------------------

void testDiskAbsentKeepsState() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // Disco ausente: la escritura no toca el disco; el doc queda en memoria.
    store.setDiskPresent(false);
    store.scheduleWrite(makeDoc(/*seq=*/3), /*immediate=*/true);
    CHECK(store.physicalWrites() == 0);
    CHECK(store.hasPending());
    CHECK(store.pendingDoc().sequence == 3);

    // Vuelve el disco: flushNow() escribe el doc pendiente.
    store.setDiskPresent(true);
    store.flushNow();
    CHECK(store.physicalWrites() == 1);
    CHECK(!store.hasPending());

    StateDoc out;
    Status rst = store.readLatest(out);
    CHECK(rst.ok);
    CHECK(out.sequence == 3);
    removeDir(dir);
}

} // namespace

int main() {
    testWriteThenRead();
    testDoubleGeneration();
    testCorruptionOlderGeneration();
    testChecksumMismatch();
    testFaultInjectionRename();
    testDebounce();
    testDiskAbsentKeepsState();

    if (g_failures == 0) {
        std::printf("test_snapshot_store: OK\n");
        return 0;
    }
    std::printf("test_snapshot_store: %d fallo(s)\n", g_failures);
    return 1;
}
