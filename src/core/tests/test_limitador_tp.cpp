// test_limitador_tp.cpp — el limitador del máster con true peak (dsp/LimitadorTP)
//
//   1. Un tono a +6 dBFS sale a ≤ −1 dBTP (medido con el medidor de true peak
//      independiente, a la salida, ya pasada la demora).
//   2. Un tono a −12 dBFS pasa tal cual (solo demorado L muestras).
//   3. Un tono pensado para tener picos ENTRE muestras (fs/4 con fase de 45°:
//      las muestras quedan en 0,707 del pico real) se limita por el pico real.
//   4. Un golpe aislado (un impulso de 1 muestra a +3 dBFS) tampoco pasa.
//   5. La demora es la declarada (5 ms a 48 kHz = 240 muestras).
//   6. Sin NaN ni valores infinitos con ruido fuerte.

#include "dsp/LimitadorTP.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using ba::dsp::LimitadorTP;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

double db(double x) { return 20 * std::log10(std::max(x, 1e-12)); }

std::vector<float> tono(double frec, double ampl, double fase, int frames, int fs = 48000) {
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float x = static_cast<float>(ampl * std::sin(2 * M_PI * frec * i / fs + fase));
        v[2 * i] = x;
        v[2 * i + 1] = x;
    }
    return v;
}

/// Procesa de a bloques de 480 y mide el true peak de lo que sale (sin el primer medio segundo).
double salidaTP(LimitadorTP& l, std::vector<float> v) {
    for (size_t i = 0; i < v.size(); i += 960) l.procesar(&v[i], std::min<size_t>(480, (v.size() - i) / 2));
    LimitadorTP::MedidorTP m;
    m.preparar(2);
    const size_t desde = 24000 * 2;
    m.medir(&v[desde - 2 * 64], 64); // calienta la historia del medidor
    return m.medir(&v[desde], (v.size() - desde) / 2);
}
} // namespace

int main() {
    std::printf("== test_limitador_tp ==\n");
    {
        LimitadorTP l;
        l.preparar(2, 48000);
        const double tp = salidaTP(l, tono(997, 2.0, 0, 96000)); // +6 dBFS
        std::printf("tono a +6 dBFS sale a %.2f dBTP (reducción %.1f dB)\n", db(tp), l.reduccionDb());
        CHECK(db(tp) <= -1.0 + 0.05, "un tono a +6 dBFS sale a <= -1 dBTP");
        CHECK(db(tp) > -2.0, "y no baja de más");
        CHECK(l.demora() == 240, "5 ms a 48 kHz = 240 muestras");
    }
    {
        LimitadorTP l;
        l.preparar(2, 48000);
        auto v = tono(440, 0.25, 0, 48000);
        const auto orig = v;
        for (size_t i = 0; i < v.size(); i += 960) l.procesar(&v[i], 480);
        double err = 0;
        for (size_t i = 240 * 2; i < v.size(); ++i) err = std::max(err, std::fabs(static_cast<double>(v[i]) - orig[i - 240 * 2]));
        std::printf("tono a -12 dBFS: error máximo contra el original demorado %.2e\n", err);
        CHECK(err < 1e-6, "por debajo del techo pasa tal cual (demorado)");
    }
    {
        // fs/4 con fase de 45°: cada muestra en ±0,707 del pico. Con pico real de
        // 1,0 (0 dBTP) las muestras dan −3 dBFS: un limitador por muestra no haría nada.
        LimitadorTP l;
        l.preparar(2, 48000);
        const auto v = tono(12000, 1.0, M_PI / 4, 96000);
        double picoMuestras = 0;
        for (float x : v) picoMuestras = std::max(picoMuestras, std::fabs(static_cast<double>(x)));
        const double tp = salidaTP(l, v);
        std::printf("fs/4 a 45°: muestras a %.2f dBFS; sale a %.2f dBTP\n", db(picoMuestras), db(tp));
        CHECK(db(picoMuestras) < -2.5, "las muestras no pasan el techo");
        CHECK(db(tp) <= -1.0 + 0.1, "pero el pico real se limita");
    }
    {
        LimitadorTP l;
        l.preparar(2, 48000);
        std::vector<float> v(96000 * 2, 0.0f);
        v[60000 * 2] = v[60000 * 2 + 1] = 1.41f; // +3 dBFS, una muestra
        const double tp = salidaTP(l, v);
        std::printf("impulso a +3 dBFS sale a %.2f dBTP\n", db(tp));
        CHECK(db(tp) <= -1.0 + 0.1, "un golpe aislado no pasa");
    }
    {
        LimitadorTP l;
        l.preparar(2, 48000);
        std::mt19937 r(7);
        std::normal_distribution<float> d(0.0f, 3.0f);
        std::vector<float> v(48000 * 2);
        for (auto& x : v) x = d(r);
        for (size_t i = 0; i < v.size(); i += 960) l.procesar(&v[i], 480);
        bool finitos = true;
        double m = 0;
        for (size_t i = 480; i < v.size(); ++i) {
            finitos = finitos && std::isfinite(v[i]);
            m = std::max(m, std::fabs(static_cast<double>(v[i])));
        }
        CHECK(finitos, "ruido fuerte: sin NaN ni infinitos");
        CHECK(db(m) <= -1.0, "ruido fuerte: ninguna muestra pasa el techo");
    }
    std::printf("test_limitador_tp: %s\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
