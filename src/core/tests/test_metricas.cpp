// test_metricas.cpp
//
// diag/Metricas: contadores, medidores y histogramas (sus percentiles), el texto de
// Prometheus, el JSON, los colectores (lo que ya mide otro se copia al exportar),
// el valor de una serie para las alarmas y que observar desde varios hilos no pierda nada.

#include "diag/Metricas.hpp"

#include <cmath>
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

static bool contiene(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }

int main() {
    using ba::diag::Metricas;
    Metricas m;

    // Contador: la misma serie es la misma referencia; otra etiqueta, otra serie.
    auto& c = m.contador("ba_prueba_total", "ayuda", {{"canal", "a"}});
    c.sumar();
    c.sumar(4);
    CHECK(&m.contador("ba_prueba_total", "ayuda", {{"canal", "a"}}) == &c, "misma serie, misma referencia");
    m.contador("ba_prueba_total", "ayuda", {{"canal", "b\"x"}}).sumar(2);
    CHECK(m.valor("ba_prueba_total", {{"canal", "a"}}).value_or(-1) == 5, "contador a = 5");
    CHECK(m.valores("ba_prueba_total").size() == 2, "dos series");

    // Medidor: desconocido hasta que se pone (nunca un 0 que parezca medido).
    auto& g = m.medidor("ba_edad_ms", "edad");
    CHECK(!m.valor("ba_edad_ms").has_value(), "medidor sin poner: desconocido");
    g.poner(12.5);
    CHECK(m.valor("ba_edad_ms").value_or(-1) == 12.5, "medidor puesto");

    // Histograma: 100 observaciones de 1 a 100 ms.
    auto& h = m.histograma("ba_lat_ms", "latencia", {{"herramienta", "status"}});
    for (int i = 1; i <= 100; ++i) h.observar(i);
    CHECK(h.cuenta() == 100, "100 observaciones");
    CHECK(std::fabs(h.suma() - 5050) < 1e-6, "suma 5050");
    const double p50 = h.percentil(0.5), p99 = h.percentil(0.99);
    CHECK(p50 >= 20 && p50 <= 50, "p50 en su casillero (20-50]");
    CHECK(p99 >= 50 && p99 <= 100, "p99 en su casillero (50-100]");
    CHECK(m.valor("ba_lat_ms:p99", {{"herramienta", "status"}}).has_value(), "p99 por valor()");
    CHECK(m.valor("ba_lat_ms:cuenta", {{"herramienta", "status"}}).value_or(0) == 100, "cuenta por valor()");
    CHECK(!m.valor("ba_lat_ms", {{"herramienta", "status"}}).has_value(), "histograma sin sufijo: nada");
    CHECK(m.histograma("ba_vacio_ms", "x").percentil(0.5) < 0, "sin observaciones: -1");

    // Un colector copia lo que mide otro, justo antes de exportar.
    int llamadas = 0;
    const int id = m.agregarColector([&llamadas](Metricas& mm) {
        ++llamadas;
        mm.medidor("ba_colectado", "de otro lado").poner(7);
    });
    const std::string t = m.texto();
    CHECK(llamadas == 1, "el colector corrió al exportar");
    CHECK(contiene(t, "# TYPE ba_prueba_total counter"), "TYPE del contador");
    CHECK(contiene(t, "ba_prueba_total{canal=\"a\"} 5"), "serie del contador");
    CHECK(contiene(t, "canal=\"b\\\"x\""), "comillas escapadas en la etiqueta");
    CHECK(contiene(t, "# TYPE ba_lat_ms histogram"), "TYPE del histograma");
    CHECK(contiene(t, "ba_lat_ms_bucket{herramienta=\"status\",le=\"+Inf\"} 100"), "casillero +Inf acumulado");
    CHECK(contiene(t, "ba_lat_ms_count{herramienta=\"status\"} 100"), "_count");
    CHECK(contiene(t, "ba_colectado 7"), "lo colectado sale");
    CHECK(contiene(t, "ba_vacio_ms_count 0"), "histograma vacío también sale");
    m.quitarColector(id);
    (void)m.json();
    CHECK(llamadas == 1, "sin el colector, no corre más");

    const nlohmann::json j = m.json();
    CHECK(j["ba_lat_ms"]["tipo"] == "histogram", "json: tipo");
    CHECK(j["ba_lat_ms"]["series"][0]["etiquetas"]["herramienta"] == "status", "json: etiquetas");
    CHECK(j["ba_lat_ms"]["series"][0]["p99"].is_number(), "json: p99");

    // El mismo nombre con otro tipo no rompe lo exportado (va a una serie suelta).
    m.medidor("ba_prueba_total", "otro tipo").poner(1);
    CHECK(m.valor("ba_prueba_total", {{"canal", "a"}}).value_or(-1) == 5, "el contador sigue igual");

    // Observar desde varios hilos a la vez: atómico, no se pierde nada.
    auto& hh = m.histograma("ba_hilos_ms", "hilos");
    auto& cc = m.contador("ba_hilos_total", "hilos");
    std::vector<std::thread> hilos;
    for (int k = 0; k < 4; ++k)
        hilos.emplace_back([&hh, &cc]() {
            for (int i = 0; i < 10000; ++i) {
                hh.observar(3);
                cc.sumar();
            }
        });
    for (auto& th : hilos) th.join();
    CHECK(hh.cuenta() == 40000 && cc.valor() == 40000, "40000 de cuatro hilos");
    CHECK(std::fabs(hh.suma() - 120000) < 1e-6, "la suma con CAS no pierde nada");

    if (g_failures == 0) std::printf("test_metricas: OK\n");
    return g_failures == 0 ? 0 : 1;
}
