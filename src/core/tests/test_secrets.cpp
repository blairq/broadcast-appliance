// test_secrets.cpp
//
// Pruebas unitarias de la bóveda de secretos cifrada en disco (spec §12, D7):
//   1. Guardar 3 secretos con una passphrase maestra y save() a un archivo
//      temporal.
//   2. Cifrado en reposo: los bytes crudos del archivo NO contienen NINGÚN
//      valor en claro (el plaintext está bajo AES-256-GCM).
//   3. Round-trip: un SecretsStore NUEVO carga con la passphrase correcta y
//      todos los valores vuelven intactos.
//   4. Passphrase equivocada: load() falla LIMPIAMENTE (tag GCM no coincide)
//      y devuelve false; get() sobre ese store devuelve false.
//   5. get() de una clave inexistente devuelve false.
//
// Sin framework externo: es un main() que devuelve 0 si todo pasa y 1 si algo
// falla (lo registra CTest). Es de control path: hace E/S de disco y KDF
// (Argon2id), no corre en el thread de audio/RT.
//
// DEBE PASAR EN LINUX (gcc).

#include "security/SecretsStore.hpp"

#include <cstddef>
#include <cstdio>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#include <filesystem>
#include <fstream>
#include <string>

using ba::security::SecretsStore;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// Lee un archivo completo como string (para inspeccionar los bytes crudos).
std::string readWholeFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return {};
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return raw;
}

// 1) + 2) Guardar 3 secretos, save(), y verificar que el archivo NO contiene
//         ningún valor en claro (cifrado en reposo).
void testSaveAndEncryptedAtRest() {
    const std::string path = (std::filesystem::temp_directory_path() / "ba_secrets_test.json").string();

    const std::string kStream = "srtmk_live_9f8e7d6c5b4a";
    const std::string kTurn = "turn_user_1234_secret";
    const std::string kOAuth = "refresh_tok_abcdef0123456789";
    const std::string kPass = "passphrase-maestra-de-prueba";

    SecretsStore store;
    CHECK(store.set("stream_key", kStream));
    CHECK(store.set("turn_user", kTurn));
    CHECK(store.set("oauth_refresh", kOAuth));
    CHECK(store.size() == 3);

    CHECK(store.save(path, kPass));

    // El archivo debe existir y no estar vacío.
    const std::string raw = readWholeFile(path);
    CHECK(!raw.empty());

    // Cifrado en reposo: NINGÚN valor en claro aparece en los bytes crudos.
    CHECK(raw.find(kStream) == std::string::npos);
    CHECK(raw.find(kTurn) == std::string::npos);
    CHECK(raw.find(kOAuth) == std::string::npos);
    // Ni siquiera la passphrase maestra debe aparecer en claro.
    CHECK(raw.find(kPass) == std::string::npos);

    // Limpieza.
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 3) Round-trip: un store NUEVO carga con la passphrase correcta y todos los
//    valores vuelven intactos.
void testRoundTripFreshStore() {
    const std::string path = (std::filesystem::temp_directory_path() / "ba_secrets_test.json").string();

    const std::string kStream = "srtmk_live_9f8e7d6c5b4a";
    const std::string kTurn = "turn_user_1234_secret";
    const std::string kOAuth = "refresh_tok_abcdef0123456789";
    const std::string kPass = "passphrase-maestra-de-prueba";

    SecretsStore writer;
    CHECK(writer.set("stream_key", kStream));
    CHECK(writer.set("turn_user", kTurn));
    CHECK(writer.set("oauth_refresh", kOAuth));
    CHECK(writer.save(path, kPass));

    // Store NUEVO: carga con la passphrase correcta.
    SecretsStore reader;
    CHECK(reader.load(path, kPass));
    CHECK(reader.size() == 3);

    std::string v;
    CHECK(reader.get("stream_key", v) && v == kStream);
    CHECK(reader.get("turn_user", v) && v == kTurn);
    CHECK(reader.get("oauth_refresh", v) && v == kOAuth);

    // 5) get() de una clave inexistente devuelve false.
    CHECK(reader.get("no_existe", v) == false);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 4) Passphrase equivocada: load() falla LIMPIAMENTE (tag GCM no coincide) y
//    devuelve false; get() sobre ese store devuelve false.
void testWrongPassphraseFailsCleanly() {
    const std::string path = (std::filesystem::temp_directory_path() / "ba_secrets_test.json").string();

    const std::string kStream = "srtmk_live_9f8e7d6c5b4a";
    const std::string kPass = "passphrase-maestra-de-prueba";

    SecretsStore writer;
    CHECK(writer.set("stream_key", kStream));
    CHECK(writer.save(path, kPass));

    // Store NUEVO con passphrase DISTINTA: load() debe devolver false.
    SecretsStore wrongReader;
    CHECK(wrongReader.load(path, "passphrase-ECOTRADA") == false);
    // Y get() sobre ese store (sin estado cargado) devuelve false.
    std::string v;
    CHECK(wrongReader.get("stream_key", v) == false);

    // Un store con passphrase vacía también falla limpiamente.
    SecretsStore emptyReader;
    CHECK(emptyReader.load(path, "") == false);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

/// La boveda se escribe con permisos de solo-dueño y sin dejar temporales.
///
/// Dos invariantes que antes no se cumplian: el archivo se creaba con el umask
/// del proceso (podia quedar legible por todo el sistema) y se escribia con
/// truncado directo sobre el destino, asi que un corte a mitad borraba todos
/// los secretos. Ahora se escribe un temporal privado y se renombra encima.
void testFilePermissionsAndAtomicity() {
#if !defined(_WIN32)
    const std::string path = (std::filesystem::temp_directory_path() / "ba_secrets_perm.json").string();
    std::remove(path.c_str());

    SecretsStore st;
    st.set("streamKey", "valor-sensible");
    CHECK(st.save(path, "passphrase-de-prueba"));

    struct stat sb {};
    CHECK(::stat(path.c_str(), &sb) == 0);
    // Ni grupo ni otros deben tener ningun permiso.
    const mode_t perms = sb.st_mode & 0777;
    CHECK(perms == (S_IRUSR | S_IWUSR));

    // El temporal de la escritura atomica no debe quedar en el directorio.
    const std::string tmp = path + ".tmp";
    struct stat tb {};
    CHECK(::stat(tmp.c_str(), &tb) != 0);

    // Y el contenido sigue siendo recuperable tras el rename.
    SecretsStore re;
    CHECK(re.load(path, "passphrase-de-prueba"));
    std::string v;
    CHECK(re.get("streamKey", v));
    CHECK(v == "valor-sensible");

    std::remove(path.c_str());
#endif
}

int main() {
    testFilePermissionsAndAtomicity();
    testSaveAndEncryptedAtRest();
    testRoundTripFreshStore();
    testWrongPassphraseFailsCleanly();

    if (g_failures == 0) {
        std::printf("test_secrets: OK\n");
        return 0;
    }
    std::printf("test_secrets: %d fallo(s)\n", g_failures);
    return 1;
}
