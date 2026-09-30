// test_sha256.cpp
//
// Pruebas unitarias del SHA-256 del launcher (Sha256.hpp), el que verifica el
// bundle contra manifest.json. Sin framework externo: es un main() que
// devuelve 0 si todo pasa y 1 si algo falla (lo registra CTest).
//
// Casos: los vectores de prueba de FIPS 180-4 (vacio, "abc", el de 448 bits y
// un millon de 'a', que cruza muchos bloques) y hashFile() sobre un archivo,
// que es como lo usa Integrity. Hasta este test el hash salia distinto al de
// sha256sum y la verificacion de integridad fallaba siempre.

#include "Sha256.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using ba::launcher::Sha256;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

std::string hex(const std::string& msg) {
    Sha256 sh;
    sh.update(msg.data(), msg.size());
    const auto d = sh.digest();
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (unsigned char b : d) {
        out.push_back(hexd[b >> 4]);
        out.push_back(hexd[b & 0xF]);
    }
    return out;
}

} // namespace

int main() {
    CHECK(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // hashFile: el camino que usa la verificacion de integridad.
    const auto path = std::filesystem::temp_directory_path() / "ba_test_sha256.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "abc";
    }
    CHECK(Sha256::hashFile(path.wstring()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(path);
    CHECK(Sha256::hashFile(path.wstring()).empty()); // ausente -> vacio

    if (g_failures == 0) {
        std::printf("test_sha256: OK\n");
        return 0;
    }
    std::printf("test_sha256: %d fallo(s)\n", g_failures);
    return 1;
}
