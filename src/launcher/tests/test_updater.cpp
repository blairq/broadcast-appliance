// test_updater.cpp — el actualizador reemplaza el programa y NUNCA los datos
//
//   1. Una version nueva valida reemplaza bin/, data/, Web/ y el launcher;
//      Config/, Credenciales/, Recordings/... quedan intactos (aunque la
//      version nueva traiga carpetas de datos vacias o distintas).
//   2. Se respalda Credenciales/ y Config/ en Respaldos/<fecha>/.
//   3. La version anterior queda en .actualizacion-anterior/ y
//      --update-rollback vuelve a ella (y otra vez a la nueva).
//   4. Se rechaza: una carpeta que no es una version, una carpeta DENTRO del
//      sistema, y un manifest que no coincide (sin tocar nada).
//   5. Con la app abierta (lock tomado) espera y termina con error claro.

#include "Updater.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Log.hpp"
#include "platform/SingleInstance.hpp"

namespace fs = std::filesystem;
using ba::launcher::runUpdate;
using ba::launcher::UpdateOptions;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

#ifdef _WIN32
const char* kLauncher = "BroadcastAppliance.exe";
const char* kApp = "bin/64bit/ba-app.exe";
#else
const char* kLauncher = "BroadcastAppliance";
const char* kApp = "bin/64bit/ba-app";
#endif

void spit(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void makeVersion(const fs::path& dir, const std::string& v) {
    spit(dir / kLauncher, "launcher " + v);
    spit(dir / kApp, "app " + v);
    spit(dir / "data" / "libobs" / "x.effect", "shader " + v);
    spit(dir / "Web" / "guest" / "index.html", "web " + v);
    fs::create_directories(dir / "Config");     // carpetas vacias del esqueleto:
    fs::create_directories(dir / "Recordings"); // no deben pisar nada
    spit(dir / "Config" / "app.json", "{\"de\":\"la version nueva\"}");
}

UpdateOptions opts(const fs::path& root, const fs::path& src) {
    UpdateOptions o;
    o.root = root;
    o.source = src;
    o.waitLockMs = 500;
    o.lockName = "test-updater-" + std::to_string(reinterpret_cast<std::uintptr_t>(&g_failures) & 0xffff);
    return o;
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() / "ba_test_updater";
    fs::remove_all(base);
    const fs::path root = base / "sistema";
    const fs::path v2 = base / "v2";
    ba::launcher::Log::init((base / "updater.log").string());

    makeVersion(root, "1");
    fs::remove(root / "Config" / "app.json");
    spit(root / "Config" / "app.json", "MI CONFIGURACION");
    spit(root / "Credenciales" / "youtube-token.cred", "MI TOKEN");
    spit(root / "Recordings" / "grabacion.mkv", "MI VIDEO");
    spit(root / "bin" / "64bit" / "viejo.so", "sobra");
    makeVersion(v2, "2");

    // --- 1) + 2) actualizar ----------------------------------------------------------
    auto r = runUpdate(opts(root, v2));
    CHECK(r.ok);
    if (!r.ok) std::printf("   %s\n", r.message.c_str());
    CHECK(slurp(root / kLauncher) == "launcher 2");
    CHECK(slurp(root / kApp) == "app 2");
    CHECK(slurp(root / "Web" / "guest" / "index.html") == "web 2");
    CHECK(!fs::exists(root / "bin" / "64bit" / "viejo.so")); // bin/ se reemplaza entero
    CHECK(slurp(root / "Config" / "app.json") == "MI CONFIGURACION");
    CHECK(slurp(root / "Credenciales" / "youtube-token.cred") == "MI TOKEN");
    CHECK(slurp(root / "Recordings" / "grabacion.mkv") == "MI VIDEO");
    CHECK(slurp(r.backup / "Credenciales" / "youtube-token.cred") == "MI TOKEN");
    CHECK(slurp(r.backup / "Config" / "app.json") == "MI CONFIGURACION");
    CHECK(slurp(root / ".actualizacion-anterior" / kApp) == "app 1");
    CHECK(!fs::exists(root / ".actualizacion-nueva"));

    // --- 3) rollback ida y vuelta ---------------------------------------------------------
    UpdateOptions rb = opts(root, {});
    rb.rollback = true;
    r = runUpdate(rb);
    CHECK(r.ok && slurp(root / kApp) == "app 1" && fs::exists(root / "bin" / "64bit" / "viejo.so"));
    CHECK(slurp(root / "Credenciales" / "youtube-token.cred") == "MI TOKEN");
    r = runUpdate(rb);
    CHECK(r.ok && slurp(root / kApp) == "app 2");

    // --- 4) rechazos ----------------------------------------------------------------------
    r = runUpdate(opts(root, base / "no-existe"));
    CHECK(!r.ok && r.message.find("no parece") != std::string::npos);
    makeVersion(root / "Recordings" / "v3", "3");
    r = runUpdate(opts(root, root / "Recordings" / "v3"));
    CHECK(!r.ok && r.message.find("FUERA") != std::string::npos);
    const fs::path bad = base / "v-danada";
    makeVersion(bad, "X");
    spit(bad / "manifest.json",
         std::string(R"({"files":[{"path":")") + kApp +
             R"(","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}]})");
    r = runUpdate(opts(root, bad));
    CHECK(!r.ok);
    CHECK(slurp(root / kApp) == "app 2"); // no se toco nada

    // --- 5) app abierta ---------------------------------------------------------------------
    {
        ba::platform::SingleInstance held;
        UpdateOptions o = opts(root, v2);
        CHECK(ba::platform::SingleInstance::acquire(o.lockName, held));
        r = runUpdate(o);
        CHECK(!r.ok && r.message.find("abierto") != std::string::npos);
    }

    ba::launcher::Log::close();
    fs::remove_all(base);
    if (g_failures == 0) std::printf("test_updater: OK\n");
    return g_failures == 0 ? 0 : 1;
}
