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
    CHECK(!pr::puedeArrancar(medida(20000, 2, 300), u, ba::ipc::kMaxInvitados + 1, true).empty(), "más que el cupo más grande no");
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
    ok.empezar(1, 1, "externo", "x");
    ok.participantes(1000, json::array({{{"id", "p"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}}}));
    ok.terminar(2000, "a mano");
    CHECK(ok.final()["bien"].get<bool>(), "todos con video y sin cortes: bien");
    pr::Informe cortada;
    cortada.empezar(1, 1, "hinchada", "x");
    cortada.participantes(1000, json::array({{{"id", "p"}, {"prueba", true}, {"connected", true}, {"mediaUp", true}}}));
    cortada.terminar(2000, "guardia", true);
    CHECK(!cortada.final()["bien"].get<bool>() && cortada.porGuardia(), "cortada por la guardia: no salió bien");

    // --- Repartido entre máquinas: la memoria de esta -----------------------------------------
    // 7000 - (40 + 40) × 40 = 3800 < 4500: con los 40 acá, no; con 10 acá, 7000 - 50 × 40 = 5000: sí.
    CHECK(!pr::puedeArrancarRepartido(medida(7000, 2, 300), u, 40, 40).empty(), "repartido: con todos acá pesa como local");
    CHECK(pr::puedeArrancarRepartido(medida(7000, 2, 300), u, 40, 10).empty(), "repartido: con pocos acá, entra");
    CHECK(pr::puedeArrancarRepartido(medida(7000, 2, 300), u, 40, 0).empty(), "repartido: nada acá = generador externo");

    // --- Decodificar ------------------------------------------------------------------------
    {
        pr::Decodificar d;
        CHECK(d.modo == "muestra" && d.pct == 5, "de fábrica: una muestra del 5 %");
        CHECK(!pr::leerDecodificar("algo", 5, d).empty(), "un modo que no existe: no");
        CHECK(!pr::leerDecodificar("muestra", 0, d).empty() && !pr::leerDecodificar("muestra", 51, d).empty(), "la muestra va de 1 a 50");
        CHECK(pr::leerDecodificar("nada", 0, d).empty() && d.modo == "nada", "nada (el % no importa)");
        CHECK(pr::argDecodificar(d, 200) == "nada" && pr::pctEfectivo(d, 200) == 0, "nada → --decodificar nada");
        CHECK(pr::leerDecodificar("todo", 0, d).empty() && pr::argDecodificar(d, 200) == "todo", "todo → --decodificar todo");
        CHECK(pr::leerDecodificar("muestra", 5, d).empty(), "muestra del 5 %");
        CHECK(pr::argDecodificar(d, 200) == "5", "con 200, el 5 % (10 decodifican)");
        CHECK(pr::pctEfectivo(d, 10) == 30 && pr::argDecodificar(d, 10) == "30", "con 10, sube al 30 % (al menos 3)");
        CHECK(pr::pctEfectivo(d, 40) == 7.5 && pr::argDecodificar(d, 40) == "7.5", "con 40, 7,5 % (3 de 40)");
        CHECK(pr::pctEfectivo(d, 2) == 100, "con muy pocos, todos");
        const json j = pr::aJson(d, 10);
        CHECK(j["modo"] == "muestra" && j["pct"] == 5 && j["pctEfectivo"].get<double>() == 30, "para el informe: modo, pedido y efectivo");
    }

    // --- Repartir entre máquinas ------------------------------------------------------------
    {
        CHECK(!pr::puedeRepartir(10, 0, false).empty(), "sin otras y sin generar acá: nadie genera");
        CHECK(!pr::puedeRepartir(10, pr::kMaxOtrasMaquinas + 1, true).empty(), "más máquinas que el tope: no");
        CHECK(!pr::puedeRepartir(1, 2, true).empty(), "más máquinas que participantes: no");
        CHECK(pr::puedeRepartir(200, 1, false).empty(), "200 en otra, esta solo coordina: sí");
        pr::Reparto r = pr::repartir(200, 1, false);
        CHECK(r.maquinas == 1 && r.local == 0 && r.procesosLocales == 0, "esta solo coordina: no genera nada");
        CHECK(r.porMaquina == 200 && r.procesosPorOtra == 2 && r.porProcesoOtra == 100 && r.trabajadores == 2,
              "200 en una Mac: dos procesos de 100 (tope 150 por proceso)");
        r = pr::repartir(200, 1, true);
        CHECK(r.maquinas == 2 && r.porMaquina == 100 && r.local == 100 && r.procesosLocales == 1 && r.procesosPorOtra == 1 &&
                  r.trabajadores == 2,
              "200 entre esta y otra: 100 y 100, un proceso cada una");
        r = pr::repartir(10, 0, false);
        CHECK(!r.varias() && r.estaGenera && r.local == 10, "sin otras: esta genera todo (aunque no se haya pedido)");
        const std::string sh = pr::comandoTrabajador(pr::repartir(200, 1, false), "192.168.1.10", 7890, "CLAVE123", false);
        CHECK(sh == "HINCHADA_CLAVE=CLAVE123 ./hinchada trabajar --coordinador 192.168.1.10:7890 --por-proceso 100", "la línea (sh)");
        const std::string ps = pr::comandoTrabajador(pr::repartir(200, 1, false), "192.168.1.10", 7890, "CLAVE123", true);
        CHECK(ps.find("$env:HINCHADA_CLAVE=\"CLAVE123\"") == 0 && ps.find(".\\hinchada.exe trabajar") != std::string::npos,
              "la línea (PowerShell)");
        const std::string consola =
            "hinchada: escuchando trabajadores y sondas en el puerto 7890\n"
            "   >>> se sumó el trabajador «esta-maquina-1» (mac, hasta 100)\n"
            "   >>> se sumó el trabajador «pc-177-4242» (pc-177, hasta 100)\n"
            "   >>> se sumó el trabajador «pc-178-11» (pc-178, hasta 100)\n"
            "   >>> se cortó el trabajador «pc-178-11»\n";
        int otras = -1;
        CHECK(pr::trabajadoresSumados(consola, &otras) == 2 && otras == 1, "sumados: 2 (uno de acá y uno de otra; el que se cortó no)");
        CHECK(pr::trabajadoresSumados("", &otras) == 0 && otras == 0, "sin consola, ninguno");
    }

    // --- El código del generador -------------------------------------------------------------
    {
        pr::CodigoGenerador c;
        c.coordinador = "192.168.1.10:7890";
        c.clave = "abcdefghijklmnop";
        c.procesos = 2;
        c.porProceso = 100;
        c.maquina = "mac-sala";
        const std::string cod = pr::armarCodigoGenerador(c);
        CHECK(cod.rfind("BAGEN1.", 0) == 0, "empieza con BAGEN1.");
        pr::CodigoGenerador l;
        CHECK(pr::leerCodigoGenerador("  " + cod + "\n", l) && l.coordinador == c.coordinador && l.clave == c.clave && l.procesos == 2 &&
                  l.porProceso == 100 && l.maquina == "mac-sala",
              "se lee entero (con espacios alrededor)");
        CHECK(!pr::leerCodigoGenerador("BACAP1.xxxx", l), "un código de capacidad no es de generador");
        c.coordinador = "sin-puerto";
        CHECK(!pr::leerCodigoGenerador(pr::armarCodigoGenerador(c), l), "sin puerto: no");
        c.coordinador = "h:7890";
        c.procesos = 0;
        CHECK(!pr::leerCodigoGenerador(pr::armarCodigoGenerador(c), l), "procesos fuera de rango: no");
    }

    // --- La CPU de las generadoras (muestras.jsonl del coordinador) ---------------------------
    {
        const json linea = {{"maquinas",
                             json::array({{{"maquina", "mac-sala"}, {"rol", "probador"}, {"cpuPct", 60},
                                           {"procesos", json::array({{{"nombre", "probador"}}, {{"nombre", "ba-gateway"}}})}},
                                          {{"maquina", "pc-177"}, {"rol", "probador"}, {"cpuPct", 45}, {"procesos", json::array()}},
                                          {{"maquina", "servidor"}, {"rol", "sonda"}, {"cpuPct", 30}}})}};
        const auto g = pr::generadorasDeMuestra(linea);
        CHECK(g.size() == 1 && g.count("pc-177") && g.at("pc-177") == 45, "solo las generadoras (ni la medida ni la sonda)");
        CHECK(pr::generadorasDeMuestra(json()).empty(), "sin línea, nada");
    }

    // --- El informe: cómo se generó --------------------------------------------------------------
    {
        pr::Informe g;
        g.empezar(1000, 2, "hinchada", "x");
        pr::Decodificar d;
        g.probador({{"decodificar", pr::aJson(d, 2)}, {"maquinas", {{"otras", 1}, {"estaGenera", false}, {"sumadas", 1}}}});
        pr::Muestra mm;
        mm.t = 2000;
        mm.cpuGenerador = 10;
        g.muestra(mm);
        mm.cpuGenerador = 30;
        g.muestra(mm);
        g.generadoras({{"pc-177", 40}});
        g.generadoras({{"pc-177", 60}});
        g.terminar(3000, "a mano");
        const json f = g.final();
        CHECK(f["picos"]["cpuGenerador"].get<double>() == 30 && f["promedios"]["cpuGenerador"].get<double>() == 20,
              "la CPU del probador: pico y promedio");
        CHECK(f["probador"]["decodificar"]["modo"] == "muestra" && f["probador"]["maquinas"]["sumadas"] == 1, "el informe dice cómo se generó");
        CHECK(f["generadoras"].size() == 1 && f["generadoras"][0]["cpuProm"].get<double>() == 50 && f["generadoras"][0]["cpuPico"].get<double>() == 60,
              "la CPU de cada generadora");
        const std::string nota = g.nota();
        CHECK(nota.find("decodific") != std::string::npos && nota.find("pc-177") != std::string::npos, "la nota dice qué decodificó y las generadoras");
    }

    std::printf(g_fallas == 0 ? "test_prueba_sala_reglas: OK\n" : "test_prueba_sala_reglas: %d fallas\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
