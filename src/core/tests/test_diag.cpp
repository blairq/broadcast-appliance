// test_diag.cpp
//
// Pruebas unitarias del diagnostico (spec §11.4): LogRotator (logs rotativos
// por proceso) y DiagExport (exportar bundle + stub de minidump). Sin framework
// externo: es un main() que devuelve 0 si todo pasa y 1 si algo falla (lo
// registra CTest). Debe PASAR EN LINUX.
//
// Casos (spec de la tarea):
//   1. Rotacion: con maxBytes pequeno, escribir suficientes lineas fuerza >= 2
//      rotaciones. Se verifica que el archivo activo se "encogio" (queda con
//      muchisimas menos lineas que las escritas), que existen ba.log.1 y
//      ba.log.2, y que el mas antiguo (ba.log.3, fuera de keepFiles=2) fue
//      podado.
//   2. exportBundle: devuelve ok=true, la outPath existe y el bundle contiene
//      una linea de log conocida (un marcador) + el header del bundle.
//   3. writeMinidump: el stub (GATED:Win) devuelve false en no-Windows.
//
// Cada test usa un directorio temporal propio (std::filesystem::temp + sufijo
// unico) y lo limpia al terminar.

#include "diag/DiagExport.hpp"
#include "diag/LogRotator.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <random>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <unistd.h> // getpid() para el nombre del temp dir
#endif

using ba::diag::DiagExport;
using ba::diag::ExportResult;
using ba::diag::LogRotator;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);            \
            ++g_failures;                                                      \
        }                                                                      \
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
    const std::string base =
        (std::filesystem::temp_directory_path() / "ba_diag_test").string();
    static int counter = 0;
    std::string dir = base + "_" + std::to_string(uniqueSeed()) + "_" +
                      std::to_string(++counter);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Limpia un directorio temporal (best-effort).
void removeDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// Lee un archivo completo a memoria; devuelve false si no existe.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string s((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
    out = s;
    return true;
}

// Cuenta lineas (ocurrencias de '\n') en un archivo; 0 si no existe.
std::size_t countLines(const std::string& path) {
    std::string content;
    if (!readFileAll(path, content)) {
        return 0;
    }
    std::size_t n = 0;
    for (char c : content) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

// --- 1) rotacion: activo se encogio, .1/.2 existen, .3 podado ----------------

void testRotation() {
    const std::string dir = makeTempDir();
    // maxBytes pequeno (40) y lineas de ~49-50 bytes: cada linea (tras la
    // primera) supera el umbral y fuerza una rotacion. keepFiles=2 -> se
    // conservan ba.log.1 y ba.log.2; ba.log.3 se poda.
    constexpr std::size_t maxBytes = 40;
    constexpr std::size_t keepFiles = 2;
    LogRotator rot(dir, maxBytes, keepFiles);

    const std::string marker = "DIAG-MARKER-XYZ";
    const int total = 10;
    for (int i = 0; i < total; ++i) {
        rot.write(std::string("line-") + std::to_string(i) + " " + marker);
    }

    const std::string active = (std::filesystem::path(dir) / "ba.log").string();
    const std::string r1 = (std::filesystem::path(dir) / "ba.log.1").string();
    const std::string r2 = (std::filesystem::path(dir) / "ba.log.2").string();
    const std::string r3 = (std::filesystem::path(dir) / "ba.log.3").string();

    // El activo y los dos rotados existen; el tercero (fuera de keepFiles) no.
    CHECK(std::filesystem::exists(active));
    CHECK(std::filesystem::exists(r1));
    CHECK(std::filesystem::exists(r2));
    CHECK(!std::filesystem::exists(r3));

    // El activo se "encogio": tiene muchisimas menos lineas que las escritas
    // (la rotacion lo reseteo; sin rotacion tendria las 10).
    const std::size_t activeLines = countLines(active);
    CHECK(activeLines >= 1);
    CHECK(activeLines < static_cast<std::size_t>(total));

    // files() lista el activo y los rotados (no vacio, incluye al activo).
    const std::vector<std::string> listed = rot.files();
    CHECK(!listed.empty());
    bool sawActive = false;
    for (const auto& p : listed) {
        if (p == active) {
            sawActive = true;
        }
    }
    CHECK(sawActive);

    removeDir(dir);
}

// --- 2) exportBundle: ok, outPath existe, contiene linea conocida ------------

void testExportBundle() {
    const std::string dir = makeTempDir();
    const std::string marker = "DIAG-MARKER-EXPORT";
    LogRotator rot(dir, /*maxBytes=*/40, /*keepFiles=*/3);
    for (int i = 0; i < 6; ++i) {
        rot.write(std::string("exp-") + std::to_string(i) + " " + marker);
    }

    const std::string outPath = (std::filesystem::path(dir) / "bundle.txt").string();
    ExportResult res = DiagExport::exportBundle(dir, outPath);
    CHECK(res.ok);
    CHECK(res.path == outPath);
    CHECK(std::filesystem::exists(outPath));

    std::string content;
    CHECK(readFileAll(outPath, content));
    // El bundle incluye el header y al menos una linea de log con el marcador.
    CHECK(content.find("#BA-DIAG v1") != std::string::npos);
    CHECK(content.find(marker) != std::string::npos);

    removeDir(dir);
}

// --- 3) writeMinidump: el stub devuelve false (GATED:Win) --------------------

void testMinidumpStub() {
    const std::string dir = makeTempDir();
    const std::string dumpPath =
        (std::filesystem::path(dir) / "dumps" / "x.dmp").string();
    // El stub no esta cableado: devuelve false en cualquier plataforma.
    CHECK(DiagExport::writeMinidump(dumpPath) == false);
    removeDir(dir);
}

} // namespace

int main() {
    testRotation();
    testExportBundle();
    testMinidumpStub();

    if (g_failures == 0) {
        std::printf("test_diag: OK\n");
        return 0;
    }
    std::printf("test_diag: %d fallo(s)\n", g_failures);
    return 1;
}
