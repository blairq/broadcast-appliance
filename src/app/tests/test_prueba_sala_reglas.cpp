// test_prueba_sala_reglas.cpp — la prueba de sala: lo puro (sala/PruebaSalaReglas)
//
//   la guardia (no arranca con poca memoria, carga alta, sin saber la memoria o si los
//   N no entran; corta por memoria, hilos y carga; lo que no se sabe no corta), quién es
//   de prueba y que quede fuera de lo que cuenta (padrón, quórum, votación, acta), el
//   rótulo, y el informe (entraron, con video, fallidos, cortes, la HD, los picos, la
//   nota para el chequeo del evento).

#include <cstdio>
#include <string>

#include "sala/PruebaSalaReglas.hpp"

namespace pr = ba::app::sala::prueba;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

pr::Medida medida(double mem, double carga, int hilos) {
    pr::Medida m;
    m.memLibreMb = mem;
    m.carga = carga;
    m.hilosApp = hilos;
    return m;
}
} // namespace

int main() {
    const pr::Umbrales u;
    // --- Arrancar ---------------------------------------------------------------------------
    CHECK(pr::puedeArrancar(medida(20000, 2, 300), u, 10, true).empty(), "arranca con la máquina quieta");
    CHECK(!pr::puedeArrancar(medida(5000, 2, 300), u, 10, true).empty(), "no arranca con poca memoria");
    CHECK(!pr::puedeArrancar(medida(-1, 2, 300), u, 10, true).empty(), "no arranca sin saber la memoria");
    CHECK(!pr::puedeArrancar(medida(20000, 9, 300), u, 10, true).empty(), "no arranca con la máquina ocupada");
    CHECK(pr::puedeArrancar(medida(20000, -1, 300), u, 10, true).empty(), "sin carga medible (Windows) arranca igual");
    CHECK(!pr::puedeArrancar(medida(20000, 2, 300), u, 0, true).empty(), "sin participantes no");
    CHECK(!pr::puedeArrancar(medida(20000, 2, 300), u, 65, true).empty(), "más que el cupo más grande no");
    // 7000 MB - 40 × 80 = 3800 < 4500 (1,5 × el corte): los 40 no entran; 10 sí.
    CHECK(!pr::puedeArrancar(medida(7000, 2, 300), u, 40, true).empty(), "no arranca si los N bajarían la memoria al corte");
    CHECK(pr::puedeArrancar(medida(7000, 2, 300), u, 10, true).empty(), "con menos, sí");
    // Con el generador en otra máquina pesa la mitad: 7000 - 40 × 40 = 5400 ≥ 4500.
    CHECK(pr::puedeArrancar(medida(7000, 2, 300), u, 40, false).empty(), "con un generador externo pesa la mitad");

    // --- Cortar ------------------------------------------------------------------------------
    CHECK(pr::debeCortar(medida(8000, 5, 500), u).empty(), "sigue con la máquina bien");
    CHECK(!pr::debeCortar(medida(2900, 5, 500), u).empty(), "corta con menos de 3 GB disponibles");
    CHECK(!pr::debeCortar(medida(8000, 5, 1300), u).empty(), "corta con más de 1200 hilos");
    CHECK(!pr::debeCortar(medida(8000, 41, 500), u).empty(), "corta con carga mayor a 40");
    CHECK(pr::debeCortar(medida(-1, -1, -1), u).empty(), "lo que no se sabe no corta");

    // --- De prueba: fuera de lo que cuenta ---------------------------------------------------
    const json gente = json::array({{{"id", "a"}, {"name", "Ana"}, {"prueba", false}},
                                    {{"id", "p1"}, {"name", "hinchada-1"}, {"prueba", true}},
                                    {{"id", "b"}, {"name", "Beto"}},
                                    {{"id", "p2"}, {"name", "hinchada-2"}, {"prueba", true}}});
    CHECK(pr::esDePrueba(gente[1]) && !pr::esDePrueba(gente[0]) && !pr::esDePrueba(gente[2]), "esDePrueba lee la marca");
    const json cuentan = pr::sinPrueba(gente);
    CHECK(cuentan.size() == 2 && cuentan[0]["id"] == "a" && cuentan[1]["id"] == "b",
          "para el padrón, el quórum y el acta quedan solo los reales");
    CHECK(pr::sinPrueba(json()).empty(), "sin lista, nada");
    CHECK(pr::nombreConRotulo("Ana", false) == "Ana", "el real sin rótulo");
    CHECK(pr::nombreConRotulo("hinchada-1", true).find("prueba") != std::string::npos, "el de prueba con «prueba»");

    // --- El informe ---------------------------------------------------------------------------
    pr::Informe inf;
    inf.empezar(1000, 3, "hinchada", "prueba linea L0");
    inf.participantes(2000, json::array({{{"id", "p1"}, {"name", "h1"}, {"prueba", true}, {"connected", true}, {"mediaUp", false}, {"joinedMs", 1500}},
                                         {{"id", "a"}, {"name", "Ana"}, {"connected", true}, {"mediaUp", true}}}));
    inf.participantes(3000, json::array({{{"id", "p1"}, {"name", "h1"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}, {"joinedMs", 1500}},
                                         {{"id", "p2"}, {"name", "h2"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}, {"joinedMs", 2500}}}));
    // p2 se cae (la sala le guarda el lugar) y vuelve: un corte.
    inf.participantes(4000, json::array({{{"id", "p1"}, {"name", "h1"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}},
                                         {{"id", "p2"}, {"name", "h2"}, {"prueba", true}, {"connected", false}, {"mediaUp", false}}}));
    inf.participantes(5000, json::array({{{"id", "p1"}, {"name", "h1"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}},
                                         {{"id", "p2"}, {"name", "h2"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}}}));
    inf.hd("p1", 400);
    inf.hd("p2", 900);
    pr::Muestra m;
    m.t = 5000;
    m.kbpsSubida = 3000;
    m.kbpsBajada = 9000;
    m.cpuApp = 20;
    m.cpuGateway = 12;
    m.memLibreMb = 9000;
    m.carga = 3;
    m.hilosApp = 400;
    inf.muestra(m);
    m.kbpsSubida = 5000;
    m.memLibreMb = 8000;
    inf.muestra(m);
    json v = inf.vivo(6000);
    CHECK(v["entraron"] == 2 && v["conVideo"] == 2, "en vivo: entraron dos, los dos con video");
    CHECK(v["fallidos"] == 1, "el tercero que se pidió y no llegó es un fallido");
    CHECK(v["cortes"] == 1, "un corte (se cayó y volvió)");
    CHECK(v["hd"]["cuantos"] == 2 && v["hd"]["max"] == 900, "la HD de los dos");
    CHECK(v["picos"]["kbpsSubida"].get<double>() == 5000 && v["picos"]["memLibreMinMb"].get<double>() == 8000,
          "picos de subida y memoria mínima");
    CHECK(v["aVideoMs"]["p50"].get<int>() >= 0, "tiempo hasta tener video");
    // Lo que pase después de terminar no cuenta (los echa la limpieza).
    inf.terminar(7000, "a mano");
    inf.participantes(8000, json::array());
    inf.hd("p1", 99999);
    const json f = inf.final();
    CHECK(f["cortes"] == 1 && f["hd"]["max"] == 900, "la limpieza no suma cortes ni HD");
    CHECK(f["segundos"] == 6 && f["personas"].size() == 2 && f.contains("serie"), "el final trae la duración, las personas y la serie");
    CHECK(!f["bien"].get<bool>(), "con un fallido y un corte no salió bien");
    CHECK(inf.nota().find("2") != std::string::npos && inf.nota().find("Mbps") != std::string::npos, "la nota resume");

    pr::Informe ok;
    ok.empezar(0, 1, "externo", "x");
    ok.participantes(1000, json::array({{{"id", "p"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}}}));
    ok.terminar(2000, "a mano");
    CHECK(ok.final()["bien"].get<bool>(), "todos con video y sin cortes: bien");
    pr::Informe cortada;
    cortada.empezar(0, 1, "hinchada", "x");
    cortada.participantes(1000, json::array({{{"id", "p"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}}}));
    cortada.terminar(2000, "guardia", true);
    CHECK(!cortada.final()["bien"].get<bool>() && cortada.porGuardia(), "cortada por la guardia: no salió bien");

    std::printf(g_fallas == 0 ? "test_prueba_sala_reglas: OK\n" : "test_prueba_sala_reglas: %d fallas\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
