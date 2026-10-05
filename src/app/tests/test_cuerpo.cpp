// legislatura/Cuerpo: integrantes, banca única, validación e importación de CSV.
#include "legislatura/Cuerpo.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using ba::app::legislatura::Cuerpo;
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
    const fs::path dir = fs::temp_directory_path() / "ba-test-cuerpo";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        Cuerpo c(dir / "cuerpo.json");
        std::string err;
        const std::string id = c.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}, {"cargo_oficial", "Diputada"}}, &err);
        CHECK(!id.empty(), "crea un integrante");
        CHECK(c.guardar({{"nombre", "Otro"}, {"banca", "3"}}, &err).empty() && err.find("ya es de") != std::string::npos,
              "la banca es única");
        CHECK(c.validar(id, "op") && c.buscar("3").contains("validado_por"), "validar");
        c.guardar({{"id", id}, {"cargo_oficial", "Vicepresidenta"}}, &err);
        CHECK(!c.buscar(id).contains("validado_por"), "cambiar el cargo saca la validación");
        CHECK(c.buscar("ANA PEREZ").value("id", std::string()) == id, "busca por nombre sin tildes ni mayúsculas");
    }
    {
        std::ofstream o(dir / "p.csv");
        o << "\xEF\xBB\xBF" << "Banca;Nombre;Cargo;Bloque;Cámara;Posición;Micrófono\n"
          << "3;Ana Pérez;Diputada;A;Sala;banca 3;Mic 3\n"
          << "4;\"Juan, el Bueno\";Diputado;B;Sala;banca 4;\n";
    }
    {
        Cuerpo c(dir / "cuerpo.json");
        const auto r = c.importarCsv(dir / "p.csv", false);
        CHECK(r.value("importados", 0) == 1 && r.value("actualizados", 0) == 1, "importa (1 nuevo, 1 actualizado por banca)");
        const auto j = c.buscar("4");
        CHECK(j.value("nombre", std::string()) == "Juan, el Bueno", "campo entre comillas con coma");
        CHECK(c.buscar("3").value("preset_ptz", nlohmann::json::object()).value("posicion", std::string()) == "banca 3",
              "cámara y posición como preset PTZ");
        CHECK(c.buscar("3").value("microfono", nlohmann::json::object()).value("fuente", std::string()) == "Mic 3",
              "micrófono de la banca");
    }
    CHECK(Cuerpo::escribirPlantilla(dir / "x" / "plantilla.csv"), "plantilla de ejemplo");
    fs::remove_all(dir);
    std::printf(fallas ? "\n%d FALLAS\n" : "\nTODO OK\n", fallas);
    return fallas ? 1 : 0;
}
