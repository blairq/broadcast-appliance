// test_esquema.cpp — la versión de los JSON de Config/ (persist/Esquema.hpp, F3-06)
//
// Sin `esquema` es el 1; migrar corre en orden solo los pasos que faltan y lo marca;
// uno escrito por una versión más nueva no se toca. Y el registro central de los
// ajustes: cada clave una vez, rangos coherentes, el valor de siempre de su tipo y
// dentro del rango, y validar() rechaza lo que no corresponde.

#include "ajustes/Registro.hpp"
#include "persist/Esquema.hpp"

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
