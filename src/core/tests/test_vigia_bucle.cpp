// test_vigia_bucle.cpp
//
// diag/VigiaBucle con el reloj inyectado: el retraso de cada turno, el histograma,
// el p99 del último minuto, el culpable (lo último que empezó antes de la hora del
// turno), los peores culpables y un aviso por culpable cada 10 s.
// Y platform/Hilo: el nombre del hilo que llama, en este sistema.

#include "diag/VigiaBucle.hpp"
#include "platform/Hilo.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    std::int64_t ahora = 1'000'000; // us
    std::vector<std::string> avisos;
    ba::diag::VigiaBucle v([&ahora]() { return ahora; }, 50, 50);
    v.alAvisar([&avisos](const std::string& t) { avisos.push_back(t); });

    // Primer turno: solo toma la hora.
    v.tic();
    CHECK(v.informe()["vueltas"] == 0, "el primer turno no cuenta");

    // 100 turnos a horario (con 1 ms de juego): sin tarde.
    for (int i = 0; i < 100; ++i) {
        ahora += 50'000 + 500;
        v.anotar("QTimer", 1);
        v.tic();
    }
    auto inf = v.informe();
    CHECK(inf["vueltas"] == 100, "100 vueltas");
    CHECK(inf["tarde"] == 0, "ninguna tarde");
    CHECK(inf["histograma"][0]["veces"] == 100, "todas en [0,1) ms");
    CHECK(inf["ultimo_minuto"]["p99_ms"].get<double>() < 1.0, "p99 bajo");
    CHECK(avisos.empty(), "sin avisos");

    // Un turno que llega 120 ms tarde: el culpable es lo que empezó ANTES de la hora
    // en que tocaba (lo que lo tapó), no lo que corrió después.
    const std::int64_t tocaba = ahora + 50'000;
    ahora += 10'000;
    v.anotar("QQuickWindowQmlImpl", 77); // empieza antes de la hora y se queda 160 ms
    ahora = tocaba + 120'000;
    v.anotar(std::string("mcp escena_cambiar")); // empezó después: no es el culpable
    v.tic();
    inf = v.informe();
    CHECK(inf["tarde"] == 1, "una tarde");
    CHECK(inf["ultimo_tarde"]["que"] == "QQuickWindowQmlImpl, evento 77", "culpable: el que tapó el turno");
    CHECK(inf["ultimo_tarde"]["ms"].get<double>() == 120.0, "120 ms tarde");
    CHECK(avisos.size() == 1, "un aviso");
    // [100, 200) ms es el balde 7 (bordes 1 2 5 10 20 50 100 200 …).
    CHECK(inf["histograma"][7]["veces"] == 1, "en el balde de 100-200 ms");

    // Una herramienta MCP que tapa el turno dos veces seguidas: un solo aviso en 10 s,
    // y queda primera entre los culpables (por total).
    for (int k = 0; k < 2; ++k) {
        const std::int64_t t = ahora + 50'000;
        ahora += 1'000;
        v.anotar(std::string("mcp sistema_recursos"));
        ahora = t + 300'000;
        v.tic();
    }
    inf = v.informe();
    CHECK(inf["tarde"] == 3, "tres tarde");
    CHECK(avisos.size() == 2, "un aviso por culpable cada 10 s");
    CHECK(inf["culpables"][0]["que"] == "mcp sistema_recursos", "el peor por total");
    CHECK(inf["culpables"][0]["veces"] == 2, "dos veces");
    CHECK(inf["culpables"][0]["max_ms"].get<double>() == 300.0, "máximo 300 ms");
    CHECK(inf["ultimo_minuto"]["max_ms"].get<double>() == 300.0, "máximo del minuto");
    CHECK(inf["ultimo_minuto"]["p99_ms"].get<double>() >= 120.0, "p99 con las tardes");

    // Pasados 10 s, el mismo culpable vuelve a avisar.
    ahora += 10'000'000;
    v.tic(); // este llega 10 s tarde, culpable el mismo (lo último anotado antes)
    CHECK(avisos.size() == 3, "vuelve a avisar pasados 10 s");

    // El minuto es un anillo: 2000 turnos a horario sacan las tardes del p99.
    for (int i = 0; i < 2000; ++i) {
        ahora += 50'000;
        v.tic();
    }
    inf = v.informe();
    CHECK(inf["ultimo_minuto"]["muestras"] == 1200, "un minuto de turnos de 50 ms");
    CHECK(inf["ultimo_minuto"]["max_ms"].get<double>() == 0.0, "las tardes ya salieron del minuto");
    CHECK(inf["tarde"] == 4, "pero quedan en la cuenta");

    // platform/Hilo: el nombre del hilo (en Linux se corta a 15 bytes, sin partir UTF-8).
    std::string leido, largo;
    std::thread([&]() {
        ba::platform::nombrarHilo("vigía-prueba");
        leido = ba::platform::nombreDelHilo();
        ba::platform::nombrarHilo("p12/compositor-de-la-salida-principal");
        largo = ba::platform::nombreDelHilo();
    }).join();
    CHECK(leido == "vigía-prueba", "el nombre del hilo, con tilde");
    CHECK(!largo.empty() && largo.rfind("p12/compositor", 0) == 0, "un nombre largo empieza igual");

    if (g_failures == 0) std::printf("test_vigia_bucle: OK\n");
    return g_failures == 0 ? 0 : 1;
}
