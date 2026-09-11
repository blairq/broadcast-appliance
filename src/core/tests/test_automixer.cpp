// test_automixer.cpp
//
// Pruebas unitarias del automixer de mesa redonda (spec 7.3, estilo Dugan).
// Sin framework externo: es un main() que devuelve 0 si todo pasa y 1 si algo
// falla (lo registra CTest).
//
// Casos:
//   1. Un micro activo, el resto en silencio -> su ganancia ~= 1.0, las otras
//      ~= 0.
//   2. Dos micros a niveles IGUALES -> cada uno ~= 0.5.
//   3. N micros a niveles iguales -> cada uno ~= 1/N.
//   4. Todos en silencio -> todas las ganancias 0 y std::isfinite, sin NaN.
//   5. "Ultimo micro abierto": se impulsa el micro A y luego A cae en silencio
//      (B en silencio) -> gainFor(A) sigue > 0 durante la ventana de hold
//      justo despues de que A cae.
//   6. Atenuacion por cantidad de micros abiertos: a nivel individual igual,
//      la ganancia por micro con 1 abierto > la ganancia por micro con 2
//      abiertos.
//   7. No-NaN/acotado: se alimentan unos cientos de niveles aleatorios; cada
//      gainFor es finita y dentro de [0, bound].

#include "dsp/Automixer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

using ba::dsp::Automixer;
using ba::dsp::AutomixerParams;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

constexpr double kFs = 48000.0;

/// dB -> lineal.
double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

/// Parametros de test: umbral alto (-60 dBFS) para que solo el nivel que
/// alimentemos cuente como "abierto", y tiempos cortos para que la envolvente
/// converja en pocas muestras.
AutomixerParams testParams() {
    AutomixerParams p;
    p.fs = kFs;
    p.openThresholdDb = -60.0; // ~1e-3 lineal: muy bajo, solo ruido cuenta
    p.attackMs = 1.0;          // ataque rapido
    p.releaseMs = 50.0;        // liberacion corta (para que caiga en el test 5)
    p.holdMs = 100.0;          // hold de 100 ms
    p.holdFloorDb = -20.0;     // piso de hold ~0.1 lineal
    p.maxOpenMics = 4;
    p.targetSummedGain = 1.0;
    return p;
}

/// Impulsa `frames` muestras de nivel constante `level` en el canal `ch`
/// (el resto de canales a 0). Devuelve el Automixer por valor no; muta el
/// recibido.
void drive(Automixer& am, int ch, float level, int frames) {
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < am.maxChannels(); ++c) {
            am.setLevel(c, (c == ch) ? level : 0.0f);
        }
    }
}

/// 1) Un micro activo, el resto en silencio.
void testOneActive() {
    Automixer am(4);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    // Impulsa el canal 0 a -6 dBFS hasta converger la envolvente.
    drive(am, 0, static_cast<float>(dbToLin(-6.0)), 4800);
    const float g0 = am.gainFor(0);
    const float g1 = am.gainFor(1);
    const float g2 = am.gainFor(2);
    const float g3 = am.gainFor(3);
    CHECK(std::isfinite(g0));
    CHECK(g0 > 0.9f); // ~= 1.0
    CHECK(g1 < 0.05f);
    CHECK(g2 < 0.05f);
    CHECK(g3 < 0.05f);
}

/// 2) Dos micros a niveles iguales.
void testTwoEqual() {
    Automixer am(4);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    const float lvl = static_cast<float>(dbToLin(-10.0));
    for (int i = 0; i < 4800; ++i) {
        am.setLevel(0, lvl);
        am.setLevel(1, lvl);
        am.setLevel(2, 0.0f);
        am.setLevel(3, 0.0f);
    }
    const float g0 = am.gainFor(0);
    const float g1 = am.gainFor(1);
    CHECK(std::isfinite(g0) && std::isfinite(g1));
    CHECK(g0 > 0.4f && g0 < 0.6f); // ~= 0.5
    CHECK(g1 > 0.4f && g1 < 0.6f);
    // Simetricia: ambos casi iguales.
    CHECK(std::fabs(g0 - g1) < 0.05f);
}

/// 3) N micros a niveles iguales -> cada uno ~= 1/N.
void testNEqual() {
    const int n = 4;
    Automixer am(n);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    const float lvl = static_cast<float>(dbToLin(-12.0));
    for (int i = 0; i < 4800; ++i) {
        for (int c = 0; c < n; ++c) {
            am.setLevel(c, lvl);
        }
    }
    for (int c = 0; c < n; ++c) {
        const float g = am.gainFor(c);
        CHECK(std::isfinite(g));
        const float expected = 1.0f / static_cast<float>(n);
        CHECK(std::fabs(g - expected) < 0.05f); // ~= 1/N
    }
}

/// 4) Todos en silencio -> ganancias 0 y finitas, sin NaN.
void testAllSilent() {
    Automixer am(8);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    // Sin alimentar nada (estado inicial) y despues de alimentar silencio.
    for (int i = 0; i < 8; ++i) {
        const float g = am.gainFor(i);
        CHECK(std::isfinite(g));
        CHECK(g == 0.0f);
    }
    // Alimentar silencio explicito durante un rato.
    for (int i = 0; i < 4800; ++i) {
        for (int c = 0; c < 8; ++c) {
            am.setLevel(c, 0.0f);
        }
    }
    for (int i = 0; i < 8; ++i) {
        const float g = am.gainFor(i);
        CHECK(std::isfinite(g));
        CHECK(g == 0.0f);
    }
}

/// 5) "Ultimo micro abierto": A cae en silencio, gainFor(A) sigue > 0 durante
///    la ventana de hold.
void testLastOpenHold() {
    Automixer am(2);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    // Impulsa A (canal 0) a -10 dBFS hasta converger.
    drive(am, 0, static_cast<float>(dbToLin(-10.0)), 4800);
    // A esta abierto: su ganancia debe ser alta.
    CHECK(am.gainFor(0) > 0.5f);

    // Ahora A cae en silencio (0.0) y B sigue en silencio. La envolvente de A
    // decae con la liberacion (50 ms). Durante la ventana de hold (100 ms)
    // gainFor(A) debe mantenerse > 0 (piso de hold).
    // Medimos justo despues de que A cae: la envolvente aun esta alta y el
    // hold esta activo.
    const int holdFrames = static_cast<int>(0.050 * kFs); // 50 ms de silencio
    for (int i = 0; i < holdFrames; ++i) {
        am.setLevel(0, 0.0f);
        am.setLevel(1, 0.0f);
    }
    // A los 50 ms de silencio, la envolvente de A decaio pero el hold (100 ms)
    // sigue activo: gainFor(A) debe ser > 0.
    const float gA = am.gainFor(0);
    CHECK(std::isfinite(gA));
    CHECK(gA > 0.0f);
    // Y debe estar cerca del piso de hold (no caido a 0).
    const float floor = static_cast<float>(dbToLin(-20.0));
    CHECK(gA >= floor * 0.5f);
}

/// 6) Atenuacion por cantidad de micros abiertos: a nivel individual igual,
///    ganancia por micro con 1 abierto > con 2 abiertos.
void testOpenCountAttenuation() {
    const float lvl = static_cast<float>(dbToLin(-10.0));

    // Caso 1: un solo micro abierto.
    Automixer am1(4);
    am1.setParams(testParams(), /*smoothMs=*/0.0);
    drive(am1, 0, lvl, 4800);
    const float gOne = am1.gainFor(0);

    // Caso 2: dos micros abiertos a nivel igual.
    Automixer am2(4);
    am2.setParams(testParams(), /*smoothMs=*/0.0);
    for (int i = 0; i < 4800; ++i) {
        am2.setLevel(0, lvl);
        am2.setLevel(1, lvl);
        am2.setLevel(2, 0.0f);
        am2.setLevel(3, 0.0f);
    }
    const float gTwo = am2.gainFor(0);

    CHECK(std::isfinite(gOne) && std::isfinite(gTwo));
    // Con 1 abierto, el micro se lleva ~todo; con 2 abiertos, se lo reparte.
    CHECK(gOne > gTwo);
    // Margen: la diferencia debe ser significativa (no solo ruido numerico).
    CHECK(gOne - gTwo > 0.1f);
}

/// 7) No-NaN/acotado: cientos de niveles aleatorios, cada gainFor finita y en
///    [0, bound].
void testNoNaNBounded() {
    const int n = 8;
    Automixer am(n);
    am.setParams(testParams(), /*smoothMs=*/0.0);
    unsigned state = 987654321u;
    const int frames = 400;
    const float bound = 1.5f; // tope holgado (las ganancias deben quedar en [0,1])
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < n; ++c) {
            // Niveles aleatorios en [0,1] (determinista por LCG).
            state = state * 1664525u + 1013904223u;
            const float rnd = static_cast<float>(static_cast<double>(state) / 4294967296.0);
            am.setLevel(c, rnd);
        }
        // Cada gainFor debe ser finita y acotada.
        for (int c = 0; c < n; ++c) {
            const float g = am.gainFor(c);
            CHECK(std::isfinite(g));
            CHECK(g >= 0.0f);
            CHECK(g <= bound);
        }
    }
}

} // namespace

int main() {
    testOneActive();
    testTwoEqual();
    testNEqual();
    testAllSilent();
    testLastOpenHold();
    testOpenCountAttenuation();
    testNoNaNBounded();

    if (g_failures == 0) {
        std::printf("test_automixer: OK\n");
        return 0;
    }
    std::printf("test_automixer: %d fallo(s)\n", g_failures);
    return 1;
}
