// ============================================================================
// src/app/tests/test_reglas_sala.cpp — las reglas de la sala que no hablan con nadie
//
//   1. Los códigos de error de la API se leen en castellano; un texto que no
//      es un código pasa tal cual.
//   2. La recomendación de la prueba técnica: eco, red y avisos, y «todo bien»
//      cuando no hay nada que decir.
//   3. El proxy: solo las claves que entiende, `acme` campo por campo, y
//      prendido sin dominio no se acepta (ni toca la configuración).
// ============================================================================
#include <cstdio>
#include <string>

#include "sala/ReglasSala.hpp"

using nlohmann::json;
using namespace ba::app::sala;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

bool contiene(const std::string& s, const std::string& parte) { return s.find(parte) != std::string::npos; }

void errores() {
    CHECK(contiene(explicarError("full"), "llena"), "full = sala llena");
    CHECK(contiene(explicarError("not_found"), "ya no está"), "not_found");
    CHECK(contiene(explicarError("empty"), "fila"), "empty = no hay nadie más en la fila");
    CHECK(contiene(explicarError("offline"), "no responde"), "offline");
    CHECK(contiene(explicarError(""), "no responde"), "sin código = no responde");
    CHECK(explicarError("la sala no contesta") == "la sala no contesta", "un texto pasa tal cual");
}

void prueba() {
    CHECK(recomendacionPrueba(json::object()) == "Todo bien: sin eco y la red alcanza.", "nada que decir");
    CHECK(recomendacionPrueba(json{{"error", "no está conectado"}}) == "no está conectado", "el error manda");
    const std::string eco = recomendacionPrueba(json{{"eco", true}, {"eco_db", 12.34}});
    CHECK(contiene(eco, "12.3 dB") && contiene(eco, "auriculares"), "eco con su nivel");
    CHECK(contiene(recomendacionPrueba(json{{"red", {{"semaforo", "rojo"}}}}), "no alcanza"), "red roja");
    CHECK(contiene(recomendacionPrueba(json{{"red", {{"semaforo", "ambar"}}}}), "justa"), "red ámbar");
    const std::string dos = recomendacionPrueba(json{{"eco", true}, {"eco_db", 11.0}, {"aviso", "Sin video."}});
    CHECK(contiene(dos, "auriculares") && contiene(dos, "Sin video."), "eco y aviso juntos");
}

void proxy() {
    const json actual{{"activo", false}, {"dominio", "viejo.ar"}, {"acme", {{"email", "a@b.ar"}, {"pruebas", true}}}};
    json nueva;
    CHECK(mezclarProxy(actual, json{{"activo", true}, {"acme", {{"activo", true}}}, {"cualquiera", 1}}, nueva).empty(),
          "cambio válido");
    CHECK(nueva.value("activo", false), "prendido");
    CHECK(nueva.value("dominio", "") == "viejo.ar", "lo que no viene queda");
    CHECK(nueva["acme"].value("email", "") == "a@b.ar" && nueva["acme"].value("activo", false),
          "acme se mezcla campo por campo");
    CHECK(!nueva.contains("cualquiera"), "una clave desconocida no entra");

    json sinTocar = json{{"marca", 1}};
    CHECK(mezclarProxy(actual, json{{"activo", true}, {"dominio", ""}}, sinTocar) == "falta el dominio",
          "prendido sin dominio");
    CHECK(sinTocar.value("marca", 0) == 1, "con error no se toca la salida");
    CHECK(!mezclarProxy(actual, json::array(), sinTocar).empty(), "cambios que no son un objeto");
    CHECK(mezclarProxy(json(), json{{"activo", false}}, nueva).empty() && !nueva.value("activo", true),
          "sin configuración previa");
}

} // namespace

int main() {
    errores();
    prueba();
    proxy();
    if (g_fallos == 0) std::printf("test_reglas_sala: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
