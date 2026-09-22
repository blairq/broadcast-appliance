// ============================================================================
// src/app/tests/test_sincro_labios.cpp — medir el retardo entre labios y voz
//
//   1. Buscar golpes: encuentra el aplauso, no el ruido de fondo, y no cuenta
//      dos veces el eco.
//   2. Un retardo conocido se recupera con la senal inventada.
//   3. La mediana aguanta un aplauso mal detectado.
//   4. El jitter se reporta como dispersion, y con dispersion alta la medicion
//      NO se marca confiable.
//   5. Los casos en que no se puede medir se explican, no se inventan.
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "camaras/SincroLabios.hpp"

using namespace ba::app::camaras;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

/// Una señal de `durMs` a 100 muestras por segundo, con piso `piso` y un pico
/// de `altura` en cada instante de `picos`.
std::vector<Muestra> senal(int durMs, double piso, double altura, const std::vector<int>& picos,
                           int pasoMs = 10) {
    std::vector<Muestra> s;
    for (int t = 0; t <= durMs; t += pasoMs) {
        double v = piso;
        for (const int p : picos) {
            // Un golpe corto: sube de golpe y cae en unos 40 ms, como un
            // aplauso de verdad.
            const int d = t - p;
            if (d >= 0 && d < 50) v = std::max(v, piso + altura * (1.0 - d / 50.0));
        }
        s.push_back(Muestra{t, v});
    }
    return s;
}

void buscar() {
    // Tres aplausos, bien separados, muy por encima del piso.
    const auto s = senal(3000, -55, 45, {500, 1200, 2100});
    const auto g = buscarGolpes(s, 20);
    CHECK(g.size() == 3, "encuentra los tres aplausos");
    if (g.size() == 3) {
        CHECK(g[0].ms == 500 && g[1].ms == 1200 && g[2].ms == 2100, "y en su instante");
    }

    // Ruido de fondo solo: no hay ningun golpe que encontrar.
    std::vector<Muestra> ruido;
    for (int t = 0; t <= 3000; t += 10)
        ruido.push_back(Muestra{t, -55 + (t % 70) * 0.05}); // ondula, no salta
    CHECK(buscarGolpes(ruido, 20).empty(), "el ruido de fondo no es un aplauso");

    // El eco: el mismo aplauso rebotando 60 ms despues no cuenta dos veces.
    const auto conEco = senal(1500, -55, 45, {400, 460});
    CHECK(buscarGolpes(conEco, 20).size() == 1, "el eco no cuenta como otro aplauso");

    // Una señal muy corta no alcanza para saber que es lo normal.
    CHECK(buscarGolpes({{0, 1}, {10, 99}}, 20).empty(), "con dos muestras no se puede decir nada");
}

void retardo_conocido() {
    // La camara llega 320 ms tarde. Cinco aplausos.
    const std::vector<int> cuando{500, 1200, 2000, 2800, 3600};
    std::vector<int> conRetardo;
    for (const int t : cuando) conRetardo.push_back(t + 320);

    const auto ga = buscarGolpes(senal(4500, -55, 45, cuando), 20);
    const auto gv = buscarGolpes(senal(4500, 2, 60, conRetardo), 20);
    CHECK(ga.size() == 5 && gv.size() == 5, "se detectan los cinco de cada lado");

    const Medicion m = medirRetardo(ga, gv);
    CHECK(m.ok, "se puede medir");
    CHECK(m.retardoMs == 320, "recupera el retardo exacto");
    CHECK(m.aplausos == 5, "emparejó los cinco");
    CHECK(m.dispersionMs == 0, "sin jitter, sin dispersion");
    CHECK(m.confiable, "y la medicion es confiable");
    CHECK(m.problema.empty(), "sin nada que aclarar");
}

void aguanta_un_mal_aplauso() {
    // Cuatro buenos y uno que el detector ubico 400 ms tarde (alguien tosio).
    std::vector<Golpe> a{{500, 30}, {1200, 30}, {2000, 30}, {2800, 30}, {3600, 30}};
    std::vector<Golpe> v{{800, 30}, {1500, 30}, {2300, 30}, {3100, 30}, {4300, 30}};
    const Medicion m = medirRetardo(a, v);
    CHECK(m.retardoMs == 300, "la mediana ignora el aplauso mal detectado");
    CHECK(m.aplausos == 5, "igual los empareja a todos");
}

void jitter() {
    // Una camara que NO tiene un retardo estable: entre aplausos va de 200 a
    // 400 ms. Un promedio ahi es un numero que no describe nada.
    std::vector<Golpe> a{{500, 30}, {1200, 30}, {2000, 30}, {2800, 30}, {3600, 30}};
    std::vector<Golpe> v{{700, 30}, {1600, 30}, {2200, 30}, {3200, 30}, {3850, 30}};
    const Medicion m = medirRetardo(a, v);
    CHECK(m.ok, "se mide igual");
    CHECK(m.dispersionMs > 40, "la dispersion lo refleja");
    CHECK(!m.confiable, "y por eso NO se da por confiable");
    CHECK(m.problema.find("jitter") != std::string::npos, "y se dice por que");

    // Un jitter chico (±30 ms) si se acepta: por debajo de lo que se nota.
    std::vector<Golpe> v2{{760, 30}, {1540, 30}, {2280, 30}, {3140, 30}, {3900, 30}};
    const Medicion suave = medirRetardo(a, v2);
    CHECK(suave.confiable, "un jitter por debajo de lo que se nota no invalida la medicion");
}

void cuando_no_se_puede() {
    CHECK(!medirRetardo({}, {{100, 5}}).ok, "sin audio no hay nada que medir");
    CHECK(medirRetardo({}, {{100, 5}}).problema.find("escuchó") != std::string::npos,
          "y se dice cual falto");
    CHECK(medirRetardo({{100, 5}}, {}).problema.find("vio") != std::string::npos, "del otro lado igual");

    // Aplausos de los dos lados pero separados por mas de lo posible: no son
    // el mismo aplauso, y decirlo es mejor que dar un numero inventado.
    const Medicion lejos = medirRetardo({{0, 5}}, {{9000, 5}});
    CHECK(!lejos.ok, "no empareja aplausos separados por 9 segundos");
    CHECK(lejos.problema.find("coincide") != std::string::npos, "y lo explica");

    // Dos aplausos alcanzan para un numero, pero no para confiar en el.
    const Medicion pocos = medirRetardo({{0, 5}, {1000, 5}}, {{200, 5}, {1200, 5}});
    CHECK(pocos.ok && pocos.retardoMs == 200, "con dos se mide");
    CHECK(!pocos.confiable, "pero no se confia");
    CHECK(pocos.problema.find("más") != std::string::npos, "y se piden mas");
}

void mediana_basica() {
    CHECK(mediana({}) == 0, "sin datos, cero");
    CHECK(mediana({5}) == 5, "uno solo");
    CHECK(mediana({3, 1, 2}) == 2, "impares: el del medio");
    CHECK(mediana({4, 1, 2, 3}) == 2, "pares: el promedio del par del medio");
}

} // namespace

int main() {
    mediana_basica();
    buscar();
    retardo_conocido();
    aguanta_un_mal_aplauso();
    jitter();
    cuando_no_se_puede();
    if (g_fallos == 0) std::printf("test_sincro_labios: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
