// test_esquema.cpp — la versión de los JSON de Config/ (persist/Esquema.hpp, F3-06)
//
// Sin `esquema` es el 1; migrar corre en orden solo los pasos que faltan y lo marca;
// uno escrito por una versión más nueva no se toca. El lector/escritor único de
// Config/ (persist/ArchivoConfig.hpp): cada archivo de la tabla se lee viejo y sale
// con su esquema (y guardado, también); uno más nuevo se lee como está y no se pisa.
// Y el registro central de los
// ajustes: cada clave una vez, rangos coherentes, el valor de siempre de su tipo y
// dentro del rango, y validar() rechaza lo que no corresponde.

#include "ajustes/Registro.hpp"
#include "persist/ArchivoConfig.hpp"
#include "persist/Esquema.hpp"

#include <filesystem>
#include <fstream>

#include <cstdio>
#include <set>
#include <string>

namespace {
int fallas = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FALLA: %s (linea %d)\n", #c, __LINE__);              \
            ++fallas;                                                         \
        }                                                                     \
    } while (0)
} // namespace

int main() {
    using nlohmann::json;
    using ba::persist::esquemaDe;
    using ba::persist::migrar;

    // Los pasos: el 1 → 2 agrega «b», el 2 → 3 renombra «a» a «c».
    const std::vector<ba::persist::PasoMigracion> pasos = {
        [](json& j) { j["b"] = 2; },
        [](json& j) {
            j["c"] = j.value("a", 0);
            j.erase("a");
        },
    };
    json viejo{{"a", 7}};
    CHECK(esquemaDe(viejo) == 1);
    CHECK(migrar(viejo, 3, pasos, "x.json") == 1);
    CHECK(viejo == json({{"b", 2}, {"c", 7}, {"esquema", 3}}));

    json medio{{"a", 1}, {"esquema", 2}};
    migrar(medio, 3, pasos, "x.json");
    CHECK(!medio.contains("b") && medio["c"] == 1 && medio["esquema"] == 3); // solo el paso que faltaba

    json nuevo{{"z", 1}, {"esquema", 9}};
    CHECK(migrar(nuevo, 3, pasos, "x.json") == 9);
    CHECK(nuevo == json({{"z", 1}, {"esquema", 9}})); // más nuevo: se lee como está

    json actual{{"c", 1}, {"esquema", 3}};
    migrar(actual, 3, pasos, "x.json");
    CHECK(actual == json({{"c", 1}, {"esquema", 3}}));

    // El lector/escritor de Config/, archivo por archivo de la tabla.
    namespace fs = std::filesystem;
    using ba::persist::cola;
    const fs::path dir = fs::temp_directory_path() / "ba-test-esquema" / "Config";
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir);
    const auto escribir = [](const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::trunc) << s; };
    const auto delDisco = [](const fs::path& p) {
        std::ifstream f(p);
        return json::parse(f, nullptr, false);
    };
    std::set<std::string> nombres;
    for (const auto& e : ba::persist::esquemasConfig()) {
        CHECK(nombres.insert(e.nombre).second);                               // cada archivo una vez
        CHECK(e.actual >= 1 && e.pasos.size() == static_cast<size_t>(e.actual - 1)); // un paso por versión
        const fs::path a = dir / e.nombre;
        // Viejo (sin `esquema`): sale marcado con el actual.
        escribir(a, R"({"dato": 1})");
        const json leido = ba::persist::leerConfig(a);
        CHECK(leido.value("esquema", 0) == e.actual);
        CHECK(leido.dump().find("\"dato\":1") != std::string::npos); // lo de antes sigue (aunque se mueva)
        // Una lista suelta de antes (los que la vuelven objeto): sale objeto, con la lista
        // adentro. Los que solo mueven un mapa no la aceptan (sale {}).
        if (e.actual > 1) {
            escribir(a, "[1, 2]");
            const json l = ba::persist::leerConfig(a);
            bool esta = false;
            for (const auto& [k, v] : l.items()) esta = esta || v == json({1, 2});
            CHECK(l.empty() || (esta && l.value("esquema", 0) == e.actual));
        }
        // Guardado: con el esquema actual.
        CHECK(ba::persist::guardarConfig(a, leido));
        CHECK(cola().vaciar());
        CHECK(delDisco(a).value("esquema", 0) == e.actual);
    }
    CHECK(nombres.size() > 60);
    // Uno que no está en la tabla: el 1.
    escribir(dir / "nuevo-sin-declarar.json", R"({"a": 1})");
    CHECK(ba::persist::leerConfig(dir / "nuevo-sin-declarar.json").value("esquema", 0) == 1);
    // Uno de una versión más nueva: se lee como está y guardar no lo pisa (avisa).
    escribir(dir / "debate.json", R"({"turno_s": 30, "esquema": 99, "de_la_nueva": true})");
    const json masNuevo = ba::persist::leerConfig(dir / "debate.json");
    CHECK(masNuevo.value("esquema", 0) == 99 && masNuevo.value("de_la_nueva", false));
    CHECK(!ba::persist::guardarConfig(dir / "debate.json", {{"turno_s", 60}}));
    CHECK(cola().vaciar());
    CHECK(delDisco(dir / "debate.json") == json({{"turno_s", 30}, {"esquema", 99}, {"de_la_nueva", true}}));
    // Sin archivo, o roto: {}.
    CHECK(ba::persist::leerConfig(dir / "no-esta.json").empty());
    escribir(dir / "roto.json", "{no es json");
    CHECK(ba::persist::leerConfig(dir / "roto.json").empty());
    fs::remove_all(dir.parent_path());

    // El registro central.
    std::set<std::string> claves;
    for (const auto& d : ba::ajustes::registro()) {
        CHECK(claves.insert(d.clave).second); // cada clave una vez
        const std::string tipo = d.tipo, alcance = d.alcance;
        CHECK(alcance == "live" || alcance == "restart" || alcance == "gateway" || alcance == "motor");
        CHECK(ba::ajustes::validar(d.clave, d.defecto).empty()); // el de siempre vale
        if (tipo == "int" || tipo == "float") CHECK(d.min <= d.max);
    }
    CHECK(claves.size() > 100);
    CHECK(ba::ajustes::buscar("stream.colaMs") != nullptr);
    CHECK(!ba::ajustes::validar("stream.colaMs", 1).empty());       // fuera de rango
    CHECK(!ba::ajustes::validar("stream.colaMs", 1000.5).empty());  // no es entero
    CHECK(!ba::ajustes::validar("stream.colaMs", "mil").empty());   // no es número
    CHECK(!ba::ajustes::validar("entradas.redModo", "otro").empty()); // no es una opción
    CHECK(ba::ajustes::validar("entradas.redModo", "normal").empty());
    CHECK(!ba::ajustes::validar("no.existe", 1).empty());

    std::printf(fallas == 0 ? "test_esquema: OK\n" : "test_esquema: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
