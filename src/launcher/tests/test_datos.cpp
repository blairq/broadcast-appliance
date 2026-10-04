// test_datos.cpp — la carpeta de datos al lado de la .app (macOS) y la mudanza
//
//   1. Con la raíz de datos igual al bundle (Windows/Linux), no hace nada.
//   2. La primera vez se mudan los datos de adentro del bundle (Config,
//      Modelos…) a la carpeta nueva; una carpeta vacía del bundle no pisa nada.
//   3. Si la carpeta nueva ya tiene datos, no se tocan (ni se mezclan).
//   4. Graficos/ y Extensiones/ se siembran: se copia lo de fábrica que falta,
//      sin pisar lo que el usuario cambió.
//   5. Si no se puede crear la carpeta, se usa el bundle.

#ifndef _WIN32
#include <unistd.h>
#endif
#include "Datos.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using ba::launcher::prepararDatos;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

void escribir(const fs::path& p, const std::string& t) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << t;
}
std::string leer(const fs::path& p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() / "ba-test-datos";
    fs::remove_all(base);
    const fs::path bundle = base / "BroadcastAppliance.app" / "Contents" / "Resources";
    const fs::path datos = base / "BroadcastAppliance Datos";
    std::vector<std::string> avisos;

    // 1.
    fs::create_directories(bundle / "Config");
    CHECK(prepararDatos(bundle, bundle, avisos) == bundle && avisos.empty(), "misma carpeta: nada");

    // 2.
    escribir(bundle / "Config" / "app.json", "{\"a\":1}");
    escribir(bundle / "Modelos" / "m" / "model.safetensors", "pesos");
    fs::create_directories(bundle / "Logs"); // vacía: no se muda nada
    escribir(bundle / "Graficos" / "zocalo.json", "fabrica");
    escribir(bundle / "Graficos" / "ticker.json", "fabrica");
    CHECK(prepararDatos(bundle, datos, avisos) == datos, "devuelve la carpeta nueva");
    CHECK(leer(datos / "Config" / "app.json") == "{\"a\":1}" && !fs::exists(bundle / "Config" / "app.json"),
          "Config se muda");
    CHECK(leer(datos / "Modelos" / "m" / "model.safetensors") == "pesos", "Modelos se muda");
    CHECK(!fs::exists(datos / "Logs"), "una carpeta vacía del bundle no se muda");
    CHECK(leer(datos / "Graficos" / "zocalo.json") == "fabrica" && fs::exists(bundle / "Graficos" / "zocalo.json"),
          "Graficos se siembra (y queda en el bundle)");

    // 3. y 4.
    escribir(bundle / "Config" / "app.json", "{\"viejo\":true}"); // reaparece adentro (otra versión)
    escribir(datos / "Graficos" / "zocalo.json", "editado");
    escribir(bundle / "Graficos" / "nuevo.json", "fabrica");
    avisos.clear();
    prepararDatos(bundle, datos, avisos);
    CHECK(leer(datos / "Config" / "app.json") == "{\"a\":1}", "con datos en la carpeta nueva, no se pisan");
    CHECK(leer(datos / "Graficos" / "zocalo.json") == "editado", "lo editado de Graficos no se pisa");
    CHECK(leer(datos / "Graficos" / "nuevo.json") == "fabrica", "lo nuevo de fábrica se siembra");

    // 5. (Como root —Docker, CI— los permisos no frenan nada: no se prueba.)
#ifndef _WIN32
    if (geteuid() != 0) {
        const fs::path ro = base / "solo-lectura";
        fs::create_directories(ro);
        fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec);
        avisos.clear();
        CHECK(prepararDatos(bundle, ro / "Datos", avisos) == bundle && !avisos.empty(),
              "sin poder crear la carpeta: se usa el bundle y se avisa");
        fs::permissions(ro, fs::perms::owner_all);
    }
#endif
    fs::remove_all(base);
    std::printf(g_fallos == 0 ? "test_datos: OK\n" : "test_datos: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
