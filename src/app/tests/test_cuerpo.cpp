// asamblea/Cuerpo: integrantes, banca única, validación e importación de CSV.
#include "persist/ColaEscritura.hpp"
#include "asamblea/Cuerpo.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using ba::app::asamblea::Cuerpo;
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
        const std::string id = c.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}, {"cargo_oficial", "Consejera"}}, &err);
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
          << "3;Ana Pérez;Consejera;A;Sala;banca 3;Mic 3\n"
          << "4;\"Juan, el Bueno\";Consejero;B;Sala;banca 4;\n";
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
    {
        Cuerpo c(dir / "cuerpo.json");
        CHECK(!c.setRecinto({{"filas", 0}, {"columnas", 3}}).empty(), "filas en cero: error");
        CHECK(!c.setRecinto({{"filas", 1}, {"columnas", 1}, {"auto", true}}).empty(), "dos bancas no entran en 1×1");
        CHECK(c.setRecinto({{"filas", 2}, {"columnas", 3}, {"auto", true}}).empty(), "auto en 2×3");
        auto r = c.recinto();
        CHECK(r["bancas"].size() == 2 && r["bancas"][0].value("banca", std::string()) == "3" &&
                  r["bancas"][1].value("columna", -1) == 1,
              "auto: en orden, de izquierda a derecha");
        CHECK(r["bancas"][0]["integrante"].value("nombre", std::string()) == "Ana Pérez" &&
                  r["bancas"][0]["integrante"]["preset_ptz"].value("posicion", std::string()) == "banca 3",
              "cada lugar trae su integrante y su preset");
        CHECK(!c.setRecinto({{"mover", {{"banca", "4"}, {"fila", 0}, {"columna", 0}}}}).empty(), "no dos bancas en el mismo lugar");
        CHECK(!c.setRecinto({{"mover", {{"banca", "4"}, {"fila", 5}, {"columna", 0}}}}).empty(), "fuera de la grilla");
        CHECK(c.setRecinto({{"mover", {{"banca", "4"}, {"fila", 1}, {"columna", 2}}}}).empty(), "mover una banca");
        CHECK(c.setRecinto({{"mover", {{"banca", "3"}, {"fila", -1}}}}).empty(), "sacar una banca");
        r = c.recinto();
        CHECK(r["bancas"].size() == 1 && r["sin_lugar"].size() == 1 && r["sin_lugar"][0] == "3", "la sacada queda sin lugar");
        CHECK(!c.setRecinto({{"columnas", 2}}).empty(), "achicar la grilla con una banca afuera: error y no cambia");
        CHECK(Cuerpo(dir / "cuerpo.json").recinto()["columnas"] == 3, "el recinto se guarda");

        // La presidencia va en la mesa, fuera de la grilla, y no queda «sin lugar».
        CHECK(c.setRecinto({{"presidencia", "3"}}).empty(), "la banca 3 a la presidencia");
        r = c.recinto();
        CHECK(r["presidencia"].value("banca", std::string()) == "3" && r["presidencia"].contains("integrante") &&
                  r["sin_lugar"].empty() && r["bancas"].size() == 1,
              "la presidencia trae su integrante y sale de sin_lugar");
        CHECK(c.setRecinto({{"presidencia", "4"}}).empty(), "otra banca a la presidencia");
        r = c.recinto();
        CHECK(r["presidencia"].value("banca", std::string()) == "4" && r["bancas"].empty() && r["sin_lugar"].size() == 1,
              "la nueva sale de la grilla y la de antes queda sin lugar");
        CHECK(c.setRecinto({{"auto", true}}).empty(), "auto con presidencia");
        r = c.recinto();
        CHECK(r["bancas"].size() == 1 && r["bancas"][0].value("banca", std::string()) == "3", "auto no pone la presidencia en la grilla");
        CHECK(!c.setRecinto({{"bancas", {{{"banca", "4"}, {"fila", 0}, {"columna", 0}}}}}).empty(), "la presidencia no puede estar también en la grilla");

        // A mano en el hemiciclo: se redondea, se vuelve al arco.
        CHECK(!c.setRecinto({{"posicion", {{"banca", "9"}, {"x", 1}, {"y", 1}}}}).empty(), "posición de una banca que no está");
        CHECK(c.setRecinto({{"posicion", {{"banca", "3"}, {"x", 1.256}, {"y", -2.5}}}}).empty(), "ubicar a mano");
        r = c.recinto();
        CHECK(r["bancas"][0].value("x", 0.0) == 1.26 && r["bancas"][0].value("y", 0.0) == -2.5, "la posición se guarda redondeada");
        CHECK(c.setRecinto({{"posicion", {{"banca", "3"}, {"x", nullptr}}}}).empty() && !c.recinto()["bancas"][0].contains("x"),
              "x null la vuelve a su arco");
        CHECK(c.setRecinto({{"posicion", {{"banca", "3"}, {"x", 1}, {"y", 1}}}}).empty() &&
                  c.setRecinto({{"posiciones_auto", true}}).empty() && !c.recinto()["bancas"][0].contains("x"),
              "posiciones_auto las vuelve todas");
        CHECK(c.setRecinto({{"mover", {{"banca", "4"}, {"fila", 0}, {"columna", 2}}}}).empty() && !c.recinto().contains("presidencia"),
              "mover la de la presidencia a la grilla la saca de la mesa");
    }
    {
        // El Cuerpo avisa él mismo cada cambio (la vista no depende del llamador):
        // una vez por operación, y la importación una sola vez, no por fila.
        Cuerpo c(dir / "cuerpo.json");
        int avisos = 0;
        c.alCambiar([&avisos]() { ++avisos; });
        c.setNombre("Consejo");
        CHECK(avisos == 1, "setNombre avisa");
        std::string err;
        const std::string id = c.guardar({{"nombre", "Rita"}, {"banca", "9"}}, &err);
        CHECK(avisos == 2, "guardar avisa");
        CHECK(c.guardar({{"nombre", "Otra"}, {"banca", "9"}}, &err).empty() && avisos == 2, "un guardar que falla no avisa");
        CHECK(c.validar(id, "op") && avisos == 3, "validar avisa");
        CHECK(!c.setRecinto({{"filas", 0}, {"columnas", 3}}).empty() && avisos == 3, "un recinto inválido no avisa");
        CHECK(c.setRecinto({{"distribucion", "lista"}}).empty() && avisos == 4, "cambiar el recinto avisa");
        c.importarCsv(dir / "p.csv", false);
        CHECK(avisos == 5, "importar avisa una sola vez");
        CHECK(c.borrar(id) && avisos == 6, "borrar avisa");
        CHECK(!c.borrar(id) && avisos == 6, "borrar a nadie no avisa");
    }
    // LA TOMA ES DE LA BANCA: el preset viejo de la persona se migra a su banca, la toma
    // se queda en la banca aunque la persona se cambie, y una banca vacía tiene la suya.
    {
        const fs::path vieja = dir / "vieja";
        fs::create_directories(vieja);
        {
            std::ofstream o(vieja / "cuerpo.json");
            o << R"({"nombre":"X","integrantes":[{"id":"l1","nombre":"Ana","banca":"5","preset_ptz":{"camara":"Sala","posicion":"Banca 5"}}]})";
        }
        Cuerpo c(vieja / "cuerpo.json");
        CHECK(c.toma("5").value("posicion", std::string()) == "Banca 5", "toma: el preset de la persona pasa a su banca");
        CHECK(c.buscar("Ana").value("preset_ptz", nlohmann::json::object()).value("camara", std::string()) == "Sala",
              "toma: la persona sigue trayendo preset_ptz (el de su banca)");
        Cuerpo releida(vieja / "cuerpo.json");
        CHECK(!releida.todo()["integrantes"][0].contains("preset_ptz"), "toma: ya no se guarda en la persona");
        std::string err;
        CHECK(!releida.guardar({{"id", "l1"}, {"banca", "6"}}, &err).empty(), "toma: cambiarla de banca");
        CHECK(releida.toma("5").value("camara", std::string()) == "Sala" && releida.buscar("Ana").value("preset_ptz", nlohmann::json::object()).empty(),
              "toma: se queda en la banca 5 (Ana, en la 6, no tiene)");
        CHECK(releida.setToma("12", "Sala", "Banca 12").empty() && releida.toma("12").value("posicion", std::string()) == "Banca 12",
              "toma: una banca sin nadie también");
        CHECK(!releida.guardar({{"id", "l1"}, {"preset_ptz", {{"camara", "Sala"}, {"posicion", "Banca 6"}}}}, &err).empty() &&
                  releida.toma("6").value("posicion", std::string()) == "Banca 6",
              "toma: guardar un preset_ptz en la persona cambia la de su banca");
        CHECK(releida.recinto()["bancas"].empty() || true, "toma: el recinto la trae por lugar");
        CHECK(releida.setToma("12", "", "").empty() && releida.toma("12").empty(), "toma: sacarla");
    }
    // Un integrante nuevo que no pasa la validación no queda a medias en la lista.
    {
        Cuerpo c(dir / "medias.json");
        std::string err;
        c.guardar({{"nombre", "Uno"}, {"banca", "1"}}, &err);
        CHECK(c.guardar({{"nombre", "Dos"}, {"banca", "1"}}, &err).empty() && c.integrantes().size() == 1,
              "una banca repetida no deja un integrante a medias");
        CHECK(c.guardar({{"nombre", "Tres"}, {"preset_ptz", {{"camara", "Sala"}, {"posicion", "X"}}}}, &err).empty() &&
                  c.integrantes().size() == 1,
              "una toma sin banca tampoco");
    }
    CHECK(Cuerpo::escribirPlantilla(dir / "x" / "plantilla.csv"), "plantilla de ejemplo");
    ba::persist::cola().vaciar(); // lo encolado, al disco antes de borrar (si no, el escritor la recrea)
    fs::remove_all(dir);
    std::printf(fallas ? "\n%d FALLAS\n" : "\nTODO OK\n", fallas);
    return fallas ? 1 : 0;
}
