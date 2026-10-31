// agent/Vocabulario: corrección por sonido, solo con confianza alta.
#include "agent/Vocabulario.hpp"
#include "asamblea/Cuerpo.hpp"
#include "persist/ColaEscritura.hpp"

#include <cstdio>
#include <filesystem>

using ba::app::agent::Vocabulario;
namespace fs = std::filesystem;

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
    const fs::path dir = fs::temp_directory_path() / "ba-test-vocabulario";
    fs::remove_all(dir);
    {
        Vocabulario v(dir / "vocabulario.json");
        CHECK(ba::persist::cola().vaciar() && fs::exists(dir / "vocabulario.json"), "siembra el archivo");
        // Las localidades vienen vacías de fábrica (son de cada lugar): las de la prueba.
        v.ponerGrupo("localidades", {"Comodoro Rivadavia", "Trelew", "Cholila"});
        CHECK(Vocabulario::fonetica("Rivadavia") == Vocabulario::fonetica("ribadabia"), "b y v suenan igual");
        CHECK(Vocabulario::fonetica("Cholila") == Vocabulario::fonetica("chollila") ||
                  Vocabulario::fonetica("Cholila") != Vocabulario::fonetica("Chubut"),
              "clave fonética estable");
        nlohmann::json r = nlohmann::json::array();
        CHECK(v.corregir("llegamos a comodoro ribadabia ayer", &r) == "llegamos a Comodoro Rivadavia ayer",
              "multipalabra por sonido");
        CHECK(r.size() == 1, "registra el reemplazo");
        CHECK(v.corregir("en trelev, hoy") == "en Trelew, hoy", "conserva la puntuación");
        CHECK(v.corregir("la casa de mi madre") == "la casa de mi madre", "no toca palabras comunes");
        CHECK(v.corregir("hubo quorum") == "hubo quórum", "tildes del vocabulario");
        v.agregar("Fernández Lacoste", {"fernandes lacost"}, "cuerpo");
        CHECK(v.corregir("habla fernandes lacost") == "habla Fernández Lacoste", "variante propia");
        v.ponerGrupo("cuerpo", {"Gutiérrez"});
        CHECK(v.corregir("gutierres") == "Gutiérrez", "grupo del cuerpo");
        v.activarGrupo("cuerpo", false);
        CHECK(v.corregir("gutierres") == "gutierres", "grupo apagado no corrige");
        CHECK(v.quitar("Trelew") && v.corregir("trelev") == "trelev", "quitar");
    }
    {
        Vocabulario v(dir / "vocabulario.json");
        CHECK(v.corregir("trelev") == "trelev", "lo quitado sigue quitado al recargar");
    }
    {
        // Los nombres del cuerpo: el nombre entero y el apellido solo (si no es corto).
        ba::app::asamblea::Cuerpo c(dir / "cuerpo.json");
        std::string err;
        c.guardar({{"nombre", "Ana Gutiérrez"}, {"banca", "1"}}, &err);
        c.guardar({{"nombre", "Luis Paz"}, {"banca", "2"}}, &err);
        Vocabulario v(dir / "vocabulario.json");
        CHECK(v.cargarCuerpo(c) == 3, "nombres y el apellido largo (Paz no va solo)");
        v.activarGrupo("cuerpo", true); // arriba quedó apagado (y se conserva al recargar)
        CHECK(v.corregir("dijo gutierres") == "dijo Gutiérrez", "el apellido solo corrige");
    }
    std::printf(fallas ? "\n%d fallas\n" : "\ntodo ok\n", fallas);
    return fallas ? 1 : 0;
}
