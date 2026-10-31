// test_termica.cpp
//
// platform/Termica con un /sys falso (zonas con sus puntos de disparo y sensores de
// hwmon), los niveles por temperatura y por tope de frecuencia, y diag::PoliticaTermica
// con estados falsos: apaga en orden (superres, modelos, vistas, invitados), de a un
// paso, sostenido; en crítico sin esperar; vuelve de a uno y en orden inverso después de
// un rato frío; apagada, devuelve todo. diag::GanchosTermicos: cada paso por su gancho
// (en orden y de vuelta al revés), los que no tienen quedan «sin gancho» y un gancho
// que llega tarde se aplica ya. Y la lectura de este sistema no revienta.

#include "diag/PoliticaTermica.hpp"
#include "platform/Termica.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace fs = std::filesystem;
using ba::platform::NivelTermico;

static void escribir(const fs::path& p, const std::string& v) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << v << "\n";
}

int main() {
    // --- Niveles.
    CHECK(ba::platform::nivelPorTemperatura(50, 90, 105) == NivelTermico::Normal, "50 °C normal");
    CHECK(ba::platform::nivelPorTemperatura(82, 90, 105) == NivelTermico::Templado, "a 10 °C de frenar: templado");
    CHECK(ba::platform::nivelPorTemperatura(92, 90, 105) == NivelTermico::Serio, "pasó el pasivo: serio");
    CHECK(ba::platform::nivelPorTemperatura(101, 90, 105) == NivelTermico::Critico, "a 5 °C del crítico");
    CHECK(ba::platform::nivelPorTemperatura(80, -1, -1) == NivelTermico::Templado, "sin puntos: 85/100 de fábrica");
    CHECK(ba::platform::nivelPorLimite(100) == NivelTermico::Normal && ba::platform::nivelPorLimite(80) == NivelTermico::Serio &&
              ba::platform::nivelPorLimite(50) == NivelTermico::Critico && ba::platform::nivelPorLimite(-1) == NivelTermico::Desconocido,
          "niveles por tope de frecuencia");
    NivelTermico n;
    CHECK(ba::platform::nivelDeNombre("serio", n) && n == NivelTermico::Serio, "nombre → nivel");

    // --- Un /sys falso.
    const fs::path raiz = fs::temp_directory_path() / ("ba-termica-" + std::to_string(::time(nullptr)));
    const fs::path th = raiz / "thermal", hw = raiz / "hwmon";
    escribir(th / "thermal_zone0" / "temp", "45000");
    escribir(th / "thermal_zone0" / "trip_point_0_type", "passive");
    escribir(th / "thermal_zone0" / "trip_point_0_temp", "90000");
    escribir(th / "thermal_zone0" / "trip_point_1_type", "critical");
    escribir(th / "thermal_zone0" / "trip_point_1_temp", "105000");
    escribir(th / "cooling_device0" / "type", "Processor"); // no es una zona: se ignora
    auto e = ba::platform::leerTermicaLinux(th, hw);
    CHECK(e.nivel == NivelTermico::Normal && e.temperaturaC == 45 && e.fuente == "thermal_zone0", "zona fría");
    // Un sensor de hwmon caliente (la CPU) manda sobre la zona.
    escribir(hw / "hwmon1" / "temp1_input", "93000");
    escribir(hw / "hwmon1" / "temp1_max", "90000");
    escribir(hw / "hwmon1" / "temp1_crit", "105000");
    e = ba::platform::leerTermicaLinux(th, hw);
    CHECK(e.nivel == NivelTermico::Serio && e.temperaturaC == 93 && e.fuente == "hwmon1/temp1", "la más caliente manda");
    e = ba::platform::leerTermicaLinux(raiz / "no-hay", raiz / "tampoco");
    CHECK(e.nivel == NivelTermico::Desconocido && e.temperaturaC < 0, "sin /sys: desconocido");
    fs::remove_all(raiz);

    // --- La política con estados falsos (la hora la da el test).
    ba::diag::PoliticaTermica::Config c;
    c.sostenidoS = 10;
    c.escalonS = 20;
    c.recuperacionS = 60;
    ba::diag::PoliticaTermica p(c);
    std::int64_t t = 1'000'000;
    const auto correr = [&](NivelTermico nv, int segundos) {
        for (int i = 0; i < segundos; ++i) p.actualizar(nv, t += 1000);
        return p.apagados();
    };
    CHECK(correr(NivelTermico::Normal, 30) == 0, "normal: nada");
    CHECK(correr(NivelTermico::Templado, 5) == 0, "templado 5 s: todavía no (sostenido)");
    CHECK(correr(NivelTermico::Templado, 10) == 1 && p.apagado("superres"), "templado sostenido: superres");
    CHECK(correr(NivelTermico::Templado, 120) == 1, "templado no pasa del primer paso");
    CHECK(!p.apagado("modelos"), "modelos sigue");
    CHECK(correr(NivelTermico::Serio, 11) == 2 && p.apagado("modelos"), "serio sostenido 10 s: modelos");
    CHECK(correr(NivelTermico::Serio, 10) == 2, "de a escalones");
    CHECK(correr(NivelTermico::Serio, 15) == 3 && p.apagado("vistas"), "serio: vistas");
    CHECK(correr(NivelTermico::Serio, 300) == 3, "serio no toca los invitados");
    CHECK(correr(NivelTermico::Critico, 1) == 4 && p.apagado("invitados"), "crítico: enseguida");
    CHECK(p.informe(t)["apagados"].size() == 4, "informe: los cuatro");
    // Se alivia: nada vuelve antes de un minuto; después, de a uno y del último al primero.
    CHECK(correr(NivelTermico::Normal, 60) == 4, "frío 59 s: todavía no vuelve");
    CHECK(correr(NivelTermico::Normal, 1) == 3 && !p.apagado("invitados") && p.apagado("vistas"), "vuelven los invitados primero");
    CHECK(correr(NivelTermico::Normal, 59) == 3, "un minuto entre pasos");
    CHECK(correr(NivelTermico::Normal, 1) == 2, "vuelven las vistas");
    CHECK(correr(NivelTermico::Normal, 200) == 0, "con el tiempo, todo de vuelta");
    // Apagada: un crítico no apaga nada.
    c.activa = false;
    p.configurar(c);
    CHECK(correr(NivelTermico::Critico, 30) == 0, "política apagada: nada");
    // Nunca el programa ni la grabación: no son pasos.
    for (const auto& paso : ba::diag::PoliticaTermica::pasos())
        CHECK(paso != "programa" && paso != "grabacion", "el programa y la grabación no se apagan");

    // --- Los ganchos de cada paso, con un estado térmico falso: cada paso lo atiende su
    // gancho (superres, modelos, vistas sin mirar, resolución de invitados), en orden, y se
    // devuelve al revés; uno sin gancho queda en sinGancho hasta que llega el suyo.
    {
        c.activa = true;
        ba::diag::PoliticaTermica pol(c);
        ba::diag::GanchosTermicos g;
        std::map<std::string, int> apagado; // +1 al apagar, -1 al devolver
        std::vector<std::string> orden;
        for (const char* paso : {"superres", "modelos", "invitados"}) // «vistas» llega después
            g.poner(paso, [&, paso = std::string(paso)](bool apagar) {
                apagado[paso] += apagar ? 1 : -1;
                orden.push_back((apagar ? "-" : "+") + paso);
                return true;
            });
        const auto paso = [&](NivelTermico nv, int segundos) {
            for (int i = 0; i < segundos; ++i) g.aplicar(pol.actualizar(nv, t += 1000));
            return g.aplicados();
        };
        CHECK(paso(NivelTermico::Serio, 60) == 3, "serio sostenido: tres pasos");
        CHECK(apagado["superres"] == 1 && apagado["modelos"] == 1, "superres y modelos, por su gancho");
        CHECK(g.sinGancho() == std::vector<std::string>{"vistas"}, "vistas sin gancho: queda anotado");
        bool vistas = false;
        g.poner("vistas", [&](bool apagar) {
            vistas = apagar;
            orden.push_back(apagar ? "-vistas" : "+vistas");
            return true;
        });
        CHECK(vistas && g.sinGancho().empty(), "el gancho que llega tarde se aplica ya");
        CHECK(paso(NivelTermico::Critico, 1) == 4 && apagado["invitados"] == 1, "crítico: la resolución de invitados");
        CHECK(paso(NivelTermico::Normal, 400) == 0, "frío: todo de vuelta");
        CHECK(!vistas && apagado["superres"] == 0 && apagado["modelos"] == 0 && apagado["invitados"] == 0,
              "cada gancho devolvió lo suyo");
        const std::vector<std::string> esperado{"-superres", "-modelos", "-vistas", "-invitados",
                                                "+invitados", "+vistas",  "+modelos", "+superres"};
        CHECK(orden == esperado, "se apaga en orden y se devuelve al revés");
        // Un gancho que no puede (devuelve false) queda sin atender.
        ba::diag::GanchosTermicos h;
        h.poner("superres", [](bool) { return false; });
        h.aplicar(1);
        CHECK(h.sinGancho() == std::vector<std::string>{"superres"}, "un gancho que no pudo: sin atender");
    }

    // La lectura de este sistema anda (en una Mac: thermalState; en Linux: /sys).
    const auto aca = ba::platform::leerTermica();
    std::printf("térmica de esta máquina: %s (%s)\n", ba::platform::nombre(aca.nivel), aca.fuente.c_str());

    if (g_failures == 0) std::printf("test_termica: OK\n");
    return g_failures == 0 ? 0 : 1;
}
