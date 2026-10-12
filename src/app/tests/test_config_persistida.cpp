// La configuración chica que se guarda en Config/ (modelo): lo de fábrica sin
// archivo, cambiar y que vuelva al recargar, y que guardar una clave no borre
// las demás.
#include "agent/RotuloTraduccionConfig.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "arranque/InterfazConfig.hpp"
#include "arranque/ModoVivoConfig.hpp"
#include "escenas/AjustesEscenas.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace ba::app;

static int fallas = 0;
#define CHECK(c, m)                                \
    do {                                           \
        if (!(c)) {                                \
            std::printf("FALLA: %s\n", m);         \
            ++fallas;                              \
        } else {                                   \
            std::printf("ok: %s\n", m);            \
        }                                          \
    } while (0)

int main() {
    const fs::path dir = fs::temp_directory_path() / "ba-test-config-persistida";
    fs::remove_all(dir);
    {
        CHECK(config::leerObjeto(dir / "Config" / "nada.json").empty(), "sin archivo: {}");
        fs::create_directories(dir / "Config");
        std::ofstream(dir / "Config" / "roto.json") << "{no es json";
        CHECK(config::leerObjeto(dir / "Config" / "roto.json").empty(), "archivo roto: {}");
    }
    {
        config::ModoVivoConfig m(dir);
        CHECK(m.activo() && !m.incluirGrabando() && m.paginas().contains("configuracion"), "modo en vivo de fábrica");
        CHECK(m.cambiar({{"incluir_grabando", true}, {"paginas", {"plugins"}}}).empty(), "cambiar el modo en vivo");
        CHECK(m.activo(), "lo que no viene, queda");
        config::ModoVivoConfig otra(dir);
        CHECK(otra.incluirGrabando() && otra.paginas() == QStringList{"plugins"}, "el modo en vivo vuelve al recargar");
    }
    {
        escenas::AjustesEscenas a(dir);
        CHECK(!a.corteDirectoEditor(), "corte directo: de fábrica no");
        CHECK(a.setCorteDirectoEditor(true) && !a.setCorteDirectoEditor(true), "cambiar avisa solo si cambió");
        CHECK(escenas::AjustesEscenas(dir).corteDirectoEditor(), "el corte directo vuelve al recargar");
    }
    {
        agent::RotuloTraduccionConfig r(dir);
        CHECK(r.activo() && r.capa() == 4, "rótulo de traducción de fábrica");
        r.setActivo(false);
        CHECK(!agent::RotuloTraduccionConfig(dir).activo(), "el rótulo apagado vuelve al recargar");
    }
    {
        config::InterfazConfig i(dir);
        CHECK(i.valor("ancho").is_null(), "interfaz: sin valor, null");
        i.guardar("ancho", 320);
        i.guardar("pestana", "audio");
        const config::InterfazConfig otra(dir);
        CHECK(otra.valor("ancho") == 320 && otra.valor("pestana") == "audio", "guardar una clave no borra las demás");
    }
    fs::remove_all(dir);
    std::printf(fallas ? "\n%d fallas\n" : "\ntodo ok\n", fallas);
    return fallas ? 1 : 0;
}
