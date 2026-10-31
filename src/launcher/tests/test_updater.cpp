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
//   6. Datos al lado del programa (macOS): el respaldo va ahi.
//   7. Con salidas al aire (A vivo y latiendo, al-aire.json con algo prendido)
//      se pospone sin tocar nada; sin aire, se instala.
//   8. La copia preparada se verifica ENTERA contra su manifest.json antes de
//      instalar, y el cache de integridad queda hecho en los datos.

#include "Updater.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Integrity.hpp"
#include "ipc/HealthBlock.hpp"
#include "Log.hpp"
#include "platform/SingleInstance.hpp"
#include "Sha256.hpp"

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
    o.salud = L"BA.Health.test-updater"; // nunca el de una app abierta en la maquina
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

    // --- 6) datos al lado del programa (macOS: «<App> Datos» junto a la .app) -------------
    {
        const fs::path root2 = base / "sistema2";
        const fs::path datos2 = base / "sistema2 Datos";
        makeVersion(root2, "1");
        spit(datos2 / "Config" / "app.json", "LA CONFIG DE VERDAD");
        spit(datos2 / "Credenciales" / "x.cred", "EL TOKEN DE VERDAD");
        UpdateOptions o = opts(root2, v2);
        o.datos = datos2;
        r = runUpdate(o);
        CHECK(r.ok);
        CHECK(r.backup.parent_path().parent_path() == datos2); // el respaldo va en la carpeta de datos
        CHECK(slurp(r.backup / "Config" / "app.json") == "LA CONFIG DE VERDAD");
        CHECK(slurp(r.backup / "Credenciales" / "x.cred") == "EL TOKEN DE VERDAD");
    }

    // --- 7) con aire: pospuesta ------------------------------------------------------------
    {
        const fs::path root3 = base / "sistema3";
        makeVersion(root3, "1");
        UpdateOptions o = opts(root3, v2);
        ba::ipc::HealthBlock salud;
        CHECK(ba::ipc::HealthBlock::create(o.salud.c_str(), ba::ipc::kSlotsSalud, salud).ok);
        salud.setIdentity(ba::ipc::kSlotSaludApp, 4242, ba::ipc::kVersionProceso);
        salud.beat(ba::ipc::kSlotSaludApp);
        spit(root3 / "Runtime" / "al-aire.json", R"({"streaming":false,"recording":true,"cerrado":false})");
        r = runUpdate(o);
        CHECK(!r.ok && r.pospuesta && r.message.find("al aire") != std::string::npos);
        CHECK(slurp(root3 / kApp) == "app 1");                 // no se toco nada
        CHECK(!fs::exists(root3 / ".actualizacion-nueva"));
        CHECK(!fs::exists(root3 / "Respaldos"));
        // Sin nada prendido (o con A que ya no late), se instala.
        spit(root3 / "Runtime" / "al-aire.json", R"({"streaming":false,"recording":false,"cerrado":false})");
        r = runUpdate(o);
        CHECK(r.ok && !r.pospuesta && slurp(root3 / kApp) == "app 2");
    }

    // --- 8) integridad completa de la copia y cache hecho -----------------------------------
    {
        const fs::path root4 = base / "sistema4";
        const fs::path datos4 = base / "sistema4 Datos";
        const fs::path v4 = base / "v4";
        makeVersion(root4, "1");
        makeVersion(v4, "4");
        const std::string sha = ba::launcher::Sha256::hashFile((v4 / kApp).wstring());
        CHECK(sha.size() == 64);
        spit(v4 / "manifest.json", std::string(R"({"files":[{"path":")") + kApp + R"(","sha256":")" + sha + R"("}]})");
        spit(datos4 / "Cache" / "qml-viejo.qmlc", "QML DE LA VERSION VIEJA");
        UpdateOptions o = opts(root4, v4);
        o.datos = datos4;
        r = runUpdate(o);
        CHECK(r.ok && slurp(root4 / kApp) == "app 4");
        CHECK(!fs::exists(datos4 / "Cache" / "qml-viejo.qmlc")); // el Cache/ de los DATOS
        const std::string cache = slurp(ba::launcher::Integrity::cacheEn(datos4));
        CHECK(cache.find(sha) != std::string::npos);              // el primer arranque no relee
        // La verificación es sobre la copia, no sobre el origen.
        spit(v4 / kApp, "app 4 adulterada");
        r = runUpdate(o);
        CHECK(!r.ok && r.message.find("manifest") != std::string::npos);
        CHECK(slurp(root4 / kApp) == "app 4");
        CHECK(!fs::exists(root4 / ".actualizacion-nueva"));
    }

    ba::launcher::Log::close();
    fs::remove_all(base);
    if (g_failures == 0) std::printf("test_updater: OK\n");
    return g_failures == 0 ? 0 : 1;
}
