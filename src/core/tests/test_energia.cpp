// ============================================================================
// src/core/tests/test_energia.cpp — el corte de luz (F1-11)
//
//   1. decidirEnergia: con red, nada; en batería, avisar; por debajo del % o de
//      los minutos, cerrar las grabaciones (si la política lo pide).
//   2. leerPowerSupply con carpetas falsas (lo que lee Linux): red online, una
//      notebook descargando, un UPS con time_to_empty, la batería del mouse no cuenta.
//   3. itemRespaldo: sin batería ni UPS falta; con UPS está.
//   4. En esta máquina: leerEnergia y preparacion no se caen y dicen algo.
// ============================================================================
#include "platform/Energia.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace fs = std::filesystem;

void fuente(const fs::path& dir, const std::string& nombre, const std::map<std::string, std::string>& archivos) {
    fs::create_directories(dir / nombre);
    for (const auto& [k, v] : archivos) std::ofstream(dir / nombre / k) << v << "\n";
}
} // namespace

int main() {
    using namespace ba::platform;
    // 1
    PoliticaEnergia pol;
    EstadoEnergia e;
    e.conocido = true;
    e.hayBateria = true;
    e.porcentaje = 80;
    CHECK(decidirEnergia(e, pol) == AccionEnergia::Nada, "1: con red");
    e.enBateria = true;
    CHECK(decidirEnergia(e, pol) == AccionEnergia::Avisar, "1: en batería");
    e.porcentaje = 20;
    CHECK(decidirEnergia(e, pol) == AccionEnergia::CerrarGrabaciones, "1: al 20 %");
    e.porcentaje = 90;
    e.minutos = 8;
    CHECK(decidirEnergia(e, pol) == AccionEnergia::CerrarGrabaciones, "1: a 8 min");
    pol.cerrarGrabaciones = false;
    CHECK(decidirEnergia(e, pol) == AccionEnergia::Avisar, "1: la política no cierra");

    // 2
    const fs::path base = fs::temp_directory_path() / "ba-test-energia";
    std::error_code ec;
    fs::remove_all(base, ec);
    {
        const fs::path d = base / "pc";
        fuente(d, "AC", {{"type", "Mains"}, {"online", "1"}});
        const EstadoEnergia r = leerPowerSupply(d);
        CHECK(r.conocido && !r.hayBateria && !r.enBateria, "2: PC con red y sin batería");
    }
    {
        const fs::path d = base / "notebook";
        fuente(d, "AC", {{"type", "Mains"}, {"online", "0"}});
        fuente(d, "BAT0", {{"type", "Battery"}, {"status", "Discharging"}, {"capacity", "47"}, {"energy_now", "20000000"},
                           {"power_now", "10000000"}});
        fuente(d, "hidpp_battery_0", {{"type", "Battery"}, {"scope", "Device"}, {"capacity", "5"}, {"status", "Discharging"}});
        const EstadoEnergia r = leerPowerSupply(d);
        CHECK(r.hayBateria && r.enBateria && r.fuente == "bateria", "2: notebook descargando");
        CHECK(r.porcentaje == 47, "2: el mouse no cuenta");
        CHECK(r.minutos == 120, "2: energía / potencia");
    }
    {
        const fs::path d = base / "ups";
        fuente(d, "AC", {{"type", "Mains"}, {"online", "0"}});
        fuente(d, "ups0", {{"type", "UPS"}, {"capacity", "90"}, {"time_to_empty_now", "1500"}});
        const EstadoEnergia r = leerPowerSupply(d);
        CHECK(r.enBateria && r.fuente == "ups" && r.minutos == 25, "2: UPS");
    }
    fs::remove_all(base, ec);

    // 3
    CHECK(itemRespaldo(EstadoEnergia{}).estado == "falta", "3: sin nada");
    EstadoEnergia u;
    u.hayBateria = true;
    u.fuente = "ups";
    CHECK(itemRespaldo(u).estado == "ok", "3: con UPS");

    // 4
    const EstadoEnergia aca = leerEnergia();
    std::printf("esta máquina: conocido=%d bateria=%d enBateria=%d %d%% %d min fuente=%s\n", aca.conocido, aca.hayBateria,
                aca.enBateria, aca.porcentaje, aca.minutos, aca.fuente.c_str());
    const auto items = preparacion(fs::temp_directory_path());
    CHECK(!items.empty(), "4: preparación");
    for (const auto& it : items) {
        std::printf("  [%s] %s: %s%s%s\n", it.estado.c_str(), it.id.c_str(), it.detalle.c_str(), it.como.empty() ? "" : " → ",
                    it.como.c_str());
        CHECK(!it.titulo.empty() && !it.estado.empty(), "4: cada punto con título y estado");
    }

    std::printf(g_fallos == 0 ? "test_energia: OK\n" : "test_energia: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
