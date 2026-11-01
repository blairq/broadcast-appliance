// src/app/tests/test_capas_remotos.cpp — qué cámaras de remotos van en alta (sala/CapasRemotos)
//
//   - al aire: las cámaras de remotos visibles en la escena al aire de cualquier salida y
//     en sus capas DSK (no su pantalla compartida: esa no tiene capas);
//   - siguiente: las de la escena armada (el preview), menos las que ya están al aire;
//   - una fuente oculta no cuenta.

#include <cstdio>
#include <string>

#include "emision/FotoMotor.hpp"
#include "sala/CapasRemotos.hpp"

using ba::app::FotoMotor;
using json = nlohmann::json;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);     \
            ++g_fallos;                                                          \
        }                                                                        \
    } while (0)

FotoMotor::Fuente fuente(const std::string& nombre, const std::string& remota, bool visible = true) {
    FotoMotor::Fuente f;
    f.nombre = nombre;
    f.remota = remota;
    f.visible = visible;
    return f;
}
} // namespace

int main() {
    FotoMotor f;
    f.escenas.push_back({"Charla", "", false, {fuente("Fondo", ""), fuente("Invitado 1", "guest:1"), fuente("Invitado 2", "guest:2")}});
    f.escenas.push_back({"Grilla", "", false, {fuente("Invitado 3", "guest:3"), fuente("Invitado 2", "guest:2"), fuente("Oculto", "guest:5", false)}});
    f.escenas.push_back({"Remoto", "", false, {fuente("Remoto con la palabra", "remoto:1")}});
    f.escenas.push_back({"Zócalos", "", false, {fuente("Ventanita", "guest:4")}});
    FotoMotor::Salida principal;
    principal.porOmision = true;
    principal.alAire = "Charla";
    principal.armada = "Grilla";
    principal.dsk = {"Ventanita"};
    FotoMotor::Salida otra;
    otra.id = "sala";
    otra.alAire = "Remoto";
    f.salidas = {principal, otra};

    const json c = ba::app::sala::capasNecesarias(f);
    CHECK(c["aire"] == json({"guest:1", "guest:2", "guest:4", "remoto:1"}), "al aire: las dos salidas y la capa DSK");
    CHECK(c["siguiente"] == json({"guest:3"}), "armada: la que va a salir, sin la que ya está al aire ni la oculta");

    FotoMotor vacia;
    const json v = ba::app::sala::capasNecesarias(vacia);
    CHECK(v["aire"].empty() && v["siguiente"].empty(), "sin salidas: nada");
    if (g_fallos == 0) std::printf("test_capas_remotos: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
