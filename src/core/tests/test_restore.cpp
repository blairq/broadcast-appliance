// test_restore.cpp
//
// Pruebas unitarias de la RESTORACION (spec §11.3) y los flujos de
// RECUPERACION (Restore). Sin framework externo: es un main() que devuelve 0
// si todo pasa y 1 si algo falla (lo registra CTest).
//
// Casos (spec de la tarea):
//   1. restore() de un snapshot valido -> Ok y el StateDoc coincide byte a byte
//      (secciones + secuencia + cleanShutdown).
//   2. restore() con los bytes del cuerpo corrompidos -> ChecksumMismatch.
//   3. recover(): se corrompe la generacion MAS NUEVA y se conserva una antigua
//      buena -> devuelve el estado de la antigua buena.
//   3b. recover(): se BORRA la generacion mas nueva, se conserva la antigua ->
//      devuelve el estado de la antigua buena.
//   4. recover() sobre un dir vacio -> Missing.
//
// Cada test usa un directorio temporal propio (std::filesystem::temp + sufijo
// unico) y lo limpia al terminar. El snapshot se produce con SnapshotStore
// (doble generacion A/B, spec §11.2) para que Restore lea lo que SnapshotStore
// escribio: es la verificacion de que el checksum reutilizado es el mismo.

#include "persist/Restore.hpp"
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

using ba::persist::Restore;
using ba::persist::RestoreResult;
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
    const std::string base = (std::filesystem::temp_directory_path() / "ba_restore_test").string();
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

// Un StateDoc de prueba con secciones representativas (comillas, backslashes y
// saltos de linea para ejercitar el escape JSON).
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

// Lee un archivo completo a memoria; false si no existe.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = s;
    return true;
}

// Escribe un string completo a un archivo (binario).
bool writeFileAll(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// Corrompe el PRIMER byte de `marker` dentro del body (un byte de un valor de
// seccion, que es un string), de modo que el JSON SIGA siendo un StateDoc
// valido pero el checksum cambie. Asi Restore lo clasifica como
// ChecksumMismatch (bytes alterados) y no como Corrupt (cuerpo no interpretable).
// False si no se encontro el marker o no se pudo escribir.
bool corruptBodyMarker(const std::string& path, const std::string& marker) {
    std::string content;
    if (!readFileAll(path, content)) {
        return false;
    }
    const std::size_t nl = content.find('\n');
    if (nl == std::string::npos) {
        return false;
    }
    // Busca el marker dentro del body (despues del '\n' del header).
    const std::size_t pos = content.find(marker, nl + 1);
    if (pos == std::string::npos) {
        return false;
    }
    // Invierte el byte: garantiza un cambio (0->1 o 1->0). Al ser un byte de
    // un valor string ASCII, el JSON sigue siendo valido.
    content[pos] = static_cast<char>(content[pos] ^ 0x01);
    return writeFileAll(path, content);
}

// Compara dos StateDoc byte a byte (escalares + todas las secciones).
bool sameDoc(const StateDoc& a, const StateDoc& b) {
    if (a.schemaVersion != b.schemaVersion) {
        return false;
    }
    if (a.sequence != b.sequence) {
        return false;
    }
    if (a.cleanShutdown != b.cleanShutdown) {
        return false;
    }
    if (a.sections.size() != b.sections.size()) {
        return false;
    }
    for (const auto& [name, blob] : a.sections) {
        auto it = b.sections.find(name);
        if (it == b.sections.end()) {
            return false;
        }
        if (it->second != blob) {
            return false;
        }
    }
    return true;
}

// --- 1) restore() de un snapshot valido -> Ok + estado coincide -------------

void testRestoreOk() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    StateDoc doc = makeDoc(/*seq=*/42, /*clean=*/true);
    store.scheduleWrite(doc, /*immediate=*/true);
    CHECK(store.physicalWrites() == 1);

    // restore() sobre la generacion escrita (A, la unica).
    StateDoc out;
    RestoreResult r = Restore::restore(store.pathA(), out);
    CHECK(r.status == RestoreResult::Status::Ok);
    CHECK(sameDoc(out, doc));
    removeDir(dir);
}

// --- 2) restore() con bytes corrompidos -> ChecksumMismatch -----------------

void testRestoreChecksumMismatch() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    store.scheduleWrite(makeDoc(/*seq=*/7), /*immediate=*/true);

    // Corrompe un byte DENTRO de un valor de seccion de A (string): el JSON
    // sigue siendo un StateDoc valido pero el checksum ya no coincide.
    CHECK(corruptBodyMarker(store.pathA(), "USB"));

    StateDoc out;
    RestoreResult r = Restore::restore(store.pathA(), out);
    CHECK(r.status == RestoreResult::Status::ChecksumMismatch);
    removeDir(dir);
}

// --- 3) recover(): la mas nueva corrupta, se conserva la antigua ------------

void testRecoverSkipsCorruptNewest() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    // A = seq 1 (mas antigua), B = seq 2 (mas nueva).
    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);
    CHECK(std::filesystem::exists(store.pathA()));
    CHECK(std::filesystem::exists(store.pathB()));

    // Corrompe un byte dentro de un valor de seccion de la generacion MAS NUEVA
    // (B, seq 2): queda invalida (checksum), el cuerpo sigue siendo interpretable.
    CHECK(corruptBodyMarker(store.pathB(), "USB"));

    // recover() debe SALTAR la B corrupta y devolver la A buena (seq 1).
    StateDoc out;
    RestoreResult r = Restore::recover(dir, out);
    CHECK(r.status == RestoreResult::Status::Ok);
    CHECK(out.sequence == 1);
    removeDir(dir);
}

// --- 3b) recover(): la mas nueva BORRADA, se conserva la antigua ------------

void testRecoverSkipsDeletedNewest() {
    const std::string dir = makeTempDir();
    SnapshotStore store;
    Status st = store.init(dir);
    CHECK(st.ok);

    store.scheduleWrite(makeDoc(/*seq=*/1), /*immediate=*/true);
    store.scheduleWrite(makeDoc(/*seq=*/2), /*immediate=*/true);

    // Borra la generacion MAS NUEVA (B, seq 2).
    std::error_code ec;
    std::filesystem::remove(store.pathB(), ec);
    CHECK(!std::filesystem::exists(store.pathB()));

    // recover() debe devolver la A buena (seq 1).
    StateDoc out;
    RestoreResult r = Restore::recover(dir, out);
    CHECK(r.status == RestoreResult::Status::Ok);
    CHECK(out.sequence == 1);
    removeDir(dir);
}

// --- 4) recover() sobre un dir vacio -> Missing -----------------------------

void testRecoverEmptyDir() {
    const std::string dir = makeTempDir(); // dir vacio (sin snapshots)
    StateDoc out;
    RestoreResult r = Restore::recover(dir, out);
    CHECK(r.status == RestoreResult::Status::Missing);
    removeDir(dir);
}

} // namespace

int main() {
    testRestoreOk();
    testRestoreChecksumMismatch();
    testRecoverSkipsCorruptNewest();
    testRecoverSkipsDeletedNewest();
    testRecoverEmptyDir();

    if (g_failures == 0) {
        std::printf("test_restore: OK\n");
        return 0;
    }
    std::printf("test_restore: %d fallo(s)\n", g_failures);
    return 1;
}
