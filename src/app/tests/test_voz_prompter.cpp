// src/app/tests/test_voz_prompter.cpp — las cuentas de la voz del teleprompter
//
// agent/VozPrompterPlan: palabras contadas como el gateway, frases, dónde va el
// teleprompter y a qué velocidad decir cada frase.

#include "agent/VozPrompterPlan.hpp"

#include <cmath>
#include <cstdio>

using namespace ba::app::agent::prompter_voz;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

bool cerca(double a, double b) { return std::fabs(a - b) < 1e-6; }

} // namespace

int main() {
    const std::string t = "Buenas noches.  Hoy hablamos\tde economía,\n\ny del clima. ¿Llueve mañana?";
    const auto ps = palabras(t);
    CHECK(ps.size() == 11, "palabras: cualquier racha de espacios separa (como el gateway)");
    CHECK(textoDe(t, ps, 0, 2) == "Buenas noches.", "el texto de una frase, con su puntuación");

    const auto fs = frases(t, ps);
    CHECK(fs.size() == 4, "frases: punto, párrafo y signo de pregunta cortan");
    CHECK(fs[0].desde == 0 && fs[0].hasta == 2, "«Buenas noches.»");
    CHECK(fs[1].desde == 2 && fs[1].hasta == 6, "«Hoy hablamos de economía,» (corta el párrafo)");
    CHECK(fs[3].desde == 9 && fs[3].hasta == 11, "«¿Llueve mañana?»");

    std::string largo;
    for (int i = 0; i < 50; ++i)
        largo += "palabra ";
    const auto fl = frases(largo, palabras(largo), 22);
    CHECK(fl.size() == 3 && fl[0].hasta == 22, "una frase larga se corta cada 22 palabras");

    Estado e;
    e.posicion = 10;
    e.ppm = 120; // 2 palabras por segundo
    e.corriendo = true;
    e.desdeMs = 1000;
    CHECK(cerca(posicionEn(e, 4000), 16), "a 120 ppm, en 3 s avanzó 6 palabras");
    CHECK(cerca(llegaEn(e, 20), 6000), "a la palabra 20 llega 5 s después");
    e.corriendo = false;
    CHECK(cerca(posicionEn(e, 4000), 10), "parado, no avanza");

    CHECK(cerca(velocidadPara(10, 150, 2.5), 1.0), "150 ppm con una voz de 2,5 palabras/s: velocidad 1");
    CHECK(cerca(velocidadPara(10, 300, 2.5), 2.0), "300 ppm: el doble");
    CHECK(cerca(velocidadPara(10, 40, 2.5), 0.6), "muy lento: acotado a 0,6 (no se deforma)");

    std::printf(g_failures == 0 ? "test_voz_prompter: OK\n" : "test_voz_prompter: %d fallos\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
