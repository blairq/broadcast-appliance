// test_fault.cpp
//
// Pruebas unitarias del andamiaje generico de inyeccion de fallos (spec §13,
// "Pruebas y criterios de aceptacion" / "Checklist de inyeccion de fallas").
// Sin framework externo: es un main() que devuelve 0 si todo pasa y 1 si algo
// falla (lo registra CTest).
//
// Casos (spec de la tarea):
//   1. arm(DiskFull) -> armed() == DiskFull y fire() lo consume (devuelve true
//      y deja el registro en None).
//   2. arm(ChecksumMismatch) -> el helper writeWithFault() produce una copia
//      temporal cuyo byte difiere del original (checksum que no coincide).
//   3. disarm() -> armed() == None.
//
// Ademas (mismo archivo, autocontenido):
//   4. fire() sin fallo armado devuelve false (no consume nada).
//   5. writeWithFault() con None armado produce una copia EXACTA (byte a byte).
//   6. writeWithFault() con ChecksumMismatch armado altera EXACTAMENTE un byte.
//
// Cada test usa un archivo/directorio temporal propio y lo limpia al terminar.
// DEBE PASAR EN LINUX (gcc).
//
// Nota: FaultInjector es una ayuda de TEST/INTEGRACION (no del steady RT
// path). armed()/fire() son atomicos thread-safe; aqui se ejercitan en un solo
// thread, que es el caso de uso del test.

#include "fault/FaultInjector.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using ba::fault::FaultInjector;
using ba::fault::FaultKind;
using ba::fault::writeWithFault;

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

// Escribe un string completo a un archivo (binario). Devuelve true si se pudo.
bool writeFileAll(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// Cuenta los bytes que difieren entre dos cadenas (hasta el minimo de
// longitudes; si las longitudes difieren, se cuenta como diferencia).
std::size_t countDifferingBytes(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::size_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
    return diff;
}

// --- 1) arm(DiskFull) -> armed()==DiskFull y fire() lo consume --------------

void testArmDiskFullAndFire() {
    FaultInjector inj;
    // Estado inicial: sin fallo.
    CHECK(inj.armed() == FaultKind::None);

    inj.arm(FaultKind::DiskFull);
    // El componente bajo prueba lo ve armado.
    CHECK(inj.armed() == FaultKind::DiskFull);

    // fire() consume el fallo: devuelve true y deja el registro en None.
    CHECK(inj.fire() == true);
    CHECK(inj.armed() == FaultKind::None);

    // Un segundo fire() ya no consume nada (no habia fallo).
    CHECK(inj.fire() == false);
}

// --- 2) arm(ChecksumMismatch) -> copia con un byte alterado -----------------

void testChecksumMismatchProducesCorruptedCopy() {
    const std::string src = (std::filesystem::temp_directory_path() / "ba_fault_src.bin").string();
    const std::string dst = (std::filesystem::temp_directory_path() / "ba_fault_dst.bin").string();

    // Contenido de prueba con bytes variados (incluye 0x00 y 0xFF para que el
    // XOR 0x01 siempre cambie el primer byte).
    const std::string content =
        std::string("\x00\x01\x02\xFF\x10\x20\x30\x40", 8) + "legis-studio-fault-injection-checksum-mismatch";

    CHECK(writeFileAll(src, content));

    FaultInjector inj;
    inj.arm(FaultKind::ChecksumMismatch);
    CHECK(inj.armed() == FaultKind::ChecksumMismatch);

    // El helper produce la copia. Con ChecksumMismatch armado, debe alterar
    // exactamente un byte.
    CHECK(writeWithFault(inj, src, dst));

    std::string original, corrupted;
    CHECK(readFileAll(src, original));
    CHECK(readFileAll(dst, corrupted));

    // Mismo tamano (no se agrega ni quita byte), pero con bytes distintos.
    CHECK(corrupted.size() == original.size());
    CHECK(countDifferingBytes(original, corrupted) == 1);

    // El byte alterado es el primero (el que el helper toca).
    CHECK(static_cast<unsigned char>(corrupted[0]) ==
          static_cast<unsigned char>(static_cast<unsigned char>(original[0]) ^ 0x01u));

    // Limpieza del fault y de los archivos temporales.
    inj.disarm();
    std::error_code ec;
    std::filesystem::remove(src, ec);
    std::filesystem::remove(dst, ec);
}

// --- 3) disarm() -> armed()==None -------------------------------------------

void testDisarm() {
    FaultInjector inj;
    inj.arm(FaultKind::PluginCrash);
    CHECK(inj.armed() == FaultKind::PluginCrash);

    inj.disarm();
    CHECK(inj.armed() == FaultKind::None);
    // Tras desarmar, fire() no consume nada.
    CHECK(inj.fire() == false);
}

// --- 4) fire() sin fallo armado devuelve false ------------------------------

void testFireWithoutFault() {
    FaultInjector inj;
    // Sin armar nada: fire() devuelve false y no cambia el estado.
    CHECK(inj.fire() == false);
    CHECK(inj.armed() == FaultKind::None);
}

// --- 5) writeWithFault con None armado -> copia EXACTA ----------------------

void testWriteWithFaultNoneIsExactCopy() {
    const std::string src = (std::filesystem::temp_directory_path() / "ba_fault_src2.bin").string();
    const std::string dst = (std::filesystem::temp_directory_path() / "ba_fault_dst2.bin").string();

    const std::string content = "legis-studio-fault-injection-exact-copy\x00\xFF";
    CHECK(writeFileAll(src, content));

    FaultInjector inj;
    // Sin fallo armado (None): la copia debe ser byte a byte igual.
    CHECK(writeWithFault(inj, src, dst));

    std::string a, b;
    CHECK(readFileAll(src, a));
    CHECK(readFileAll(dst, b));
    CHECK(a == b);
    CHECK(countDifferingBytes(a, b) == 0);

    std::error_code ec;
    std::filesystem::remove(src, ec);
    std::filesystem::remove(dst, ec);
}

// --- 6) writeWithFault con otro kind armado -> copia EXACTA -----------------
// (Solo ChecksumMismatch altera bytes; p. ej. Timeout no debe corromper.)

void testWriteWithFaultOtherKindIsExactCopy() {
    const std::string src = (std::filesystem::temp_directory_path() / "ba_fault_src3.bin").string();
    const std::string dst = (std::filesystem::temp_directory_path() / "ba_fault_dst3.bin").string();

    const std::string content = "legis-studio-fault-injection-timeout-kind";
    CHECK(writeFileAll(src, content));

    FaultInjector inj;
    inj.arm(FaultKind::Timeout); // un kind distinto de ChecksumMismatch
    CHECK(writeWithFault(inj, src, dst));

    std::string a, b;
    CHECK(readFileAll(src, a));
    CHECK(readFileAll(dst, b));
    CHECK(a == b); // Timeout no corrompe bytes

    inj.disarm();
    std::error_code ec;
    std::filesystem::remove(src, ec);
    std::filesystem::remove(dst, ec);
}

} // namespace

int main() {
    testArmDiskFullAndFire();
    testChecksumMismatchProducesCorruptedCopy();
    testDisarm();
    testFireWithoutFault();
    testWriteWithFaultNoneIsExactCopy();
    testWriteWithFaultOtherKindIsExactCopy();

    if (g_failures == 0) {
        std::printf("test_fault: OK\n");
        return 0;
    }
    std::printf("test_fault: %d fallo(s)\n", g_failures);
    return 1;
}
