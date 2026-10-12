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

#include "platform/Paths.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using ba::launcher::prepararConfiguraciones;
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

    // Las configuraciones: lo de la configuración se MUDA a Configuraciones/Principal (sin copiar),
    // launcher.json queda en la máquina y las semillas van a la configuración activa.
    escribir(datos / "Config" / "launcher.json", "{\"shadowCopy\":true}");
    escribir(datos / "Media" / "logo.png", "png");
    avisos.clear();
    const fs::path principal = prepararConfiguraciones(bundle, datos, avisos);
    CHECK(principal == datos / "Configuraciones" / "Principal", "la activa es Principal");
    CHECK(leer(principal / "Config" / "app.json") == "{\"a\":1}" && !fs::exists(datos / "Config"),
          "Config se muda a Principal");
    CHECK(leer(principal / "Media" / "logo.png") == "png", "Media también (es de la configuración)");
    CHECK(leer(datos / "launcher.json") == "{\"shadowCopy\":true}" && !fs::exists(principal / "Config" / "launcher.json"),
          "launcher.json queda en la máquina");
    CHECK(leer(datos / "Modelos" / "m" / "model.safetensors") == "pesos", "Modelos queda en la máquina");
    CHECK(leer(principal / "Graficos" / "zocalo.json") == "fabrica" && fs::exists(bundle / "Graficos" / "zocalo.json"),
          "Graficos se siembra en la configuración (y queda en el bundle)");
    CHECK(ba::platform::configRootFor(datos) == principal, "configuraciones.json dice la activa");

    // 3. y 4.
    escribir(bundle / "Config" / "app.json", "{\"viejo\":true}"); // reaparece adentro (otra versión)
    escribir(principal / "Graficos" / "zocalo.json", "editado");
    escribir(bundle / "Graficos" / "nuevo.json", "fabrica");
    avisos.clear();
    prepararDatos(bundle, datos, avisos);
    prepararConfiguraciones(bundle, datos, avisos);
    CHECK(leer(principal / "Config" / "app.json") == "{\"a\":1}", "la segunda vez no se muda ni se pisa nada");
    CHECK(leer(principal / "Graficos" / "zocalo.json") == "editado", "lo editado de Graficos no se pisa");
    CHECK(leer(principal / "Graficos" / "nuevo.json") == "fabrica", "lo nuevo de fábrica se siembra");

    // La activa que ya no está: la primera que haya.
    fs::create_directories(datos / "Configuraciones" / "Demo");
    escribir(datos / "configuraciones.json", "{\"activa\": \"Borrada\"}");
    avisos.clear();
    const fs::path otra = prepararConfiguraciones(bundle, datos, avisos);
    CHECK(otra.parent_path() == datos / "Configuraciones" && !avisos.empty(), "sin la activa: la primera que haya, y se avisa");
    CHECK(ba::platform::configRootFor(datos) == otra, "y queda anotada");

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
    // 6. Programa y datos en la MISMA carpeta (Windows). La 2.0.0 del 2026-10-08 mudaba
    //    a la configuración las carpetas del programa que se llaman como las de datos:
    //    `plugins/lsp` (sin distinguir mayúsculas era «Plugins»: la integridad daba sus
    //    archivos por ausentes y una instalación nueva arrancaba con la pantalla de
    //    recuperación) y las semillas Graficos/Extensiones.
    {
        const fs::path w = base / "windows";
        escribir(w / "plugins" / "lsp" / "lsp-plugins.clap", "clap");
        escribir(w / "plugins" / "mi-plugin" / "plugin.json", "{}"); // uno del usuario (instalaciones viejas)
        escribir(w / "Graficos" / "zocalo.json", "fabrica");
        escribir(w / "Config" / "app.json", "{\"b\":2}");
        avisos.clear();
        const fs::path p = prepararConfiguraciones(w, w, avisos);
        CHECK(leer(w / "plugins" / "lsp" / "lsp-plugins.clap") == "clap", "juntos: plugins/lsp queda en el programa");
        CHECK(!fs::exists(p / "Plugins" / "lsp"), "juntos: plugins/lsp no se muda");
        CHECK(leer(p / "Plugins" / "mi-plugin" / "plugin.json") == "{}", "juntos: lo del usuario de plugins/ sí");
        CHECK(leer(w / "Graficos" / "zocalo.json") == "fabrica" && leer(p / "Graficos" / "zocalo.json") == "fabrica",
              "juntos: la semilla se copia y queda");
        CHECK(leer(p / "Config" / "app.json") == "{\"b\":2}", "juntos: Config se muda");

        // Una instalación que ya quedó rota (lo mudó la versión anterior): se repara.
        const fs::path r = base / "rota";
        escribir(r / "Configuraciones" / "Principal" / "Plugins" / "lsp" / "lsp-plugins.clap", "clap");
        escribir(r / "Configuraciones" / "Principal" / "Graficos" / "zocalo.json", "fabrica");
        escribir(r / "configuraciones.json", "{\"activa\": \"Principal\"}");
        avisos.clear();
        prepararConfiguraciones(r, r, avisos);
        CHECK(leer(r / "plugins" / "lsp" / "lsp-plugins.clap") == "clap", "rota: plugins/lsp vuelve al programa");
        CHECK(leer(r / "Graficos" / "zocalo.json") == "fabrica", "rota: la semilla vuelve al programa");
    }

    fs::remove_all(base);
    std::printf(g_fallos == 0 ? "test_datos: OK\n" : "test_datos: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
