// test_credentials.cpp
//
// Pruebas de la carpeta de credenciales (security/CredentialStore):
//   1. Ida y vuelta; el archivo no tiene el valor en claro; permisos 0600.
//   2. Integridad: un byte alterado, un archivo renombrado (el nombre es dato
//      asociado) o la clave de otro equipo -> no descifra.
//   3. Nombres invalidos (rutas, mayusculas, punto inicial) se rechazan.
//   4. Importacion de versiones anteriores: Config/... en claro pasa a
//      Credenciales/ cifrado y el original desaparece; lo que ya existia no se
//      pisa, salvo el certificado propio que se deja en Credenciales/.
//   5. Dos procesos (aca: 8 threads) que arrancan juntos terminan con la MISMA
//      clave del equipo.
//
// Sin framework: main() devuelve 0 si todo pasa.

#include "security/CredentialStore.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using ba::security::CredentialStore;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

fs::path freshDir(const char* tag) {
    const fs::path d = fs::temp_directory_path() / (std::string("ba_cred_") + tag);
    fs::remove_all(d);
    return d;
}

void testRoundTrip() {
    const fs::path d = freshDir("rt");
    const CredentialStore s(d);
    const std::string secret = "refresh-token-SUPERSECRETO-1234";
    CHECK(s.write("youtube-token", secret));
    std::string back;
    CHECK(s.read("youtube-token", back) && back == secret);
    const std::string raw = slurp(d / "youtube-token.cred");
    CHECK(!raw.empty() && raw.find("SUPERSECRETO") == std::string::npos);
    CHECK(s.exists("youtube-token") && !s.exists("otra"));
    CHECK(s.list() == std::vector<std::string>{"youtube-token"});
#ifndef _WIN32
    const auto perms = fs::status(d / "youtube-token.cred").permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    // Reescribir reemplaza; vacio es valido.
    CHECK(s.write("youtube-token", "") && s.read("youtube-token", back) && back.empty());
    CHECK(s.remove("youtube-token") && !s.exists("youtube-token"));
    CHECK(!s.read("youtube-token", back));
    fs::remove_all(d);
}

void testIntegrity() {
    const fs::path d = freshDir("int");
    const CredentialStore s(d);
    CHECK(s.write("mcp-token", "abcdef0123456789abcdef0123456789"));
    std::string out;
    // Byte alterado.
    std::string raw = slurp(d / "mcp-token.cred");
    raw[raw.size() - 1] ^= 0x01;
    spit(d / "mcp-token.cred", raw);
    CHECK(!s.read("mcp-token", out));
    // Renombrado: el nombre es dato asociado.
    CHECK(s.write("mcp-token", "x"));
    fs::copy_file(d / "mcp-token.cred", d / "wireguard-key.cred");
    CHECK(!s.read("wireguard-key", out));
    // Clave de otro equipo.
    const fs::path d2 = freshDir("int2");
    const CredentialStore other(d2);
    CHECK(other.write("tmp", "y")); // crea su propia clave
    fs::copy_file(d / "mcp-token.cred", d2 / "mcp-token.cred");
    CHECK(!other.read("mcp-token", out));
    fs::remove_all(d);
    fs::remove_all(d2);
}

void testNames() {
    CHECK(CredentialStore::validName("tls-autofirmado-clave"));
    CHECK(!CredentialStore::validName(""));
    CHECK(!CredentialStore::validName("../x"));
    CHECK(!CredentialStore::validName("a/b"));
    CHECK(!CredentialStore::validName(".oculto"));
    CHECK(!CredentialStore::validName("Mayus"));
    const CredentialStore s(freshDir("names"));
    CHECK(!s.write("../escape", "x"));
    fs::remove_all(s.dir());
}

void testImportLegacy() {
    const fs::path root = freshDir("legacy");
    spit(root / "Config" / "twitch-token.json", R"({"refresh_token":"R1"})");
    spit(root / "Config" / "mcp-token", "tok\n");
    spit(root / "Config" / "wireguard.key", "AAAA\n");
    spit(root / "Config" / "tls" / "selfsigned-key.pem", "-----BEGIN EC PRIVATE KEY-----");
    spit(root / "Config" / "app.json", "{}"); // configuracion: NO se toca
    const CredentialStore s(root / "Credenciales");
    CHECK(s.write("youtube-token", "NUEVO")); // ya existe: no se pisa
    spit(root / "Config" / "youtube-token.json", "VIEJO");

    const auto imported = s.importLegacy(root);
    CHECK(imported.size() == 5);
    std::string v;
    CHECK(s.read("twitch-token", v) && v == R"({"refresh_token":"R1"})");
    CHECK(s.read("mcp-token", v) && v == "tok\n");
    CHECK(s.read("wireguard-key", v) && v == "AAAA\n");
    CHECK(s.read("tls-autofirmado-clave", v) && v.find("PRIVATE KEY") != std::string::npos);
    CHECK(s.read("youtube-token", v) && v == "NUEVO");
    CHECK(!fs::exists(root / "Config" / "twitch-token.json"));
    CHECK(!fs::exists(root / "Config" / "mcp-token"));
    CHECK(!fs::exists(root / "Config" / "youtube-token.json"));
    CHECK(!fs::exists(root / "Config" / "tls")); // quedo vacia
    CHECK(fs::exists(root / "Config" / "app.json"));
    // Idempotente.
    CHECK(s.importLegacy(root).empty());
    // Certificado propio nuevo dejado en Credenciales/: reemplaza al anterior.
    CHECK(s.write("tls-propio-cert", "VIEJO"));
    spit(root / "Credenciales" / "cert.pem", "NUEVO-CERT");
    spit(root / "Credenciales" / "key.pem", "NUEVA-CLAVE");
    CHECK(s.importLegacy(root).size() == 2);
    CHECK(s.read("tls-propio-cert", v) && v == "NUEVO-CERT");
    CHECK(s.read("tls-propio-clave", v) && v == "NUEVA-CLAVE");
    CHECK(!fs::exists(root / "Credenciales" / "key.pem"));
    fs::remove_all(root);
}

void testConcurrentKey() {
    const fs::path d = freshDir("race");
    std::vector<std::thread> ts;
    std::atomic<int> ok{0};
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&, i]() {
            const CredentialStore s(d);
            if (s.write("t" + std::to_string(i), "v" + std::to_string(i))) ++ok;
        });
    }
    for (auto& t : ts)
        t.join();
    CHECK(ok == 8);
    const CredentialStore s(d);
    int readable = 0;
    for (int i = 0; i < 8; ++i) {
        std::string v;
        if (s.read("t" + std::to_string(i), v) && v == "v" + std::to_string(i)) ++readable;
    }
    CHECK(readable == 8); // todos cifraron con la misma clave
    fs::remove_all(d);
}

} // namespace

int main() {
    testRoundTrip();
    testIntegrity();
    testNames();
    testImportLegacy();
    testConcurrentKey();
    if (g_failures == 0) std::printf("test_credentials: OK\n");
    return g_failures == 0 ? 0 : 1;
}
