// ============================================================================
// src/app/tests/test_audio_dsp.cpp — analizador y generador
//
//   1. Seno de 1 kHz a -20 dBFS en los dos canales: pico -20, RMS -20 (AES),
//      -20 LUFS (referencia BS.1770), la banda de 1 kHz arriba de las demas.
//   2. Ruido blanco y rosa a -20 dBFS RMS: el analizador mide -20; el blanco
//      sube 3 dB por octava en tercios y el rosa queda plano.
//   3. Piso de ruido y nivel activo salen del historial (ruido + "voz").
// ============================================================================

#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/Analyzer.hpp"

using namespace ba::app::audio;

namespace {
int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

Analyzer::Snapshot measure(Generator::Config cfg, double seconds) {
    Generator g;
    g.setSampleRate(48000);
    g.configure(cfg);
    Analyzer a;
    a.setSampleRate(48000);
    std::vector<float> l(480), r(480);
    float* ch[2] = {l.data(), r.data()};
    for (int b = 0; b < static_cast<int>(seconds * 100); ++b) {
        g.render(ch, 2, 480);
        a.feed(ch, 2, 480);
    }
    return a.snapshot();
}
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
} // namespace

int main() {
    Generator::Config sine;
    sine.kind = Generator::Kind::Sine;
    sine.freq = 1000;
    sine.levelDb = -20;
    auto s = measure(sine, 5);
    std::printf("seno: pico %.2f rms %.2f lufsM %.2f lufsI %.2f banda1k %.2f banda250 %.2f\n", s.peakDb[0],
                s.rmsDb[0], s.lufsM, s.lufsI, s.bandsDb[16], s.bandsDb[10]);
    CHECK(near(s.peakDb[0], -20, 0.1), "pico -20");
    CHECK(near(s.rmsDb[0], -20, 0.2), "RMS AES -20");
    CHECK(near(s.lufsM, -20, 0.3) && near(s.lufsI, -20, 0.3), "-20 LUFS (BS.1770)");
    CHECK(near(s.bandsDb[16], -20, 1.0), "banda de 1 kHz a -20 dB");
    CHECK(s.bandsDb[10] < -50, "250 Hz muy abajo");
    CHECK(near(s.correlation, 1, 0.01), "L=R: correlacion 1");

    Generator::Config white;
    white.kind = Generator::Kind::White;
    white.levelDb = -20;
    s = measure(white, 8);
    std::printf("blanco: rms %.2f  banda 100 %.1f  banda 10k %.1f\n", s.rmsDb[0], s.bandsLongDb[6],
                s.bandsLongDb[26]);
    CHECK(near(s.rmsDb[0], -20, 0.5), "blanco a -20 RMS");
    CHECK(near(s.bandsLongDb[26] - s.bandsLongDb[6], 20, 3),
          "blanco: +3 dB/oct en tercios (100 Hz -> 10 kHz = +20 dB)");

    Generator::Config pink;
    pink.kind = Generator::Kind::Pink;
    pink.levelDb = -20;
    s = measure(pink, 8);
    std::printf("rosa: rms %.2f  banda 100 %.1f  banda 1k %.1f  banda 10k %.1f\n", s.rmsDb[0],
                s.bandsLongDb[6], s.bandsLongDb[16], s.bandsLongDb[26]);
    CHECK(near(s.rmsDb[0], -20, 1.0), "rosa a -20 RMS");
    CHECK(near(s.bandsLongDb[26], s.bandsLongDb[6], 3) && near(s.bandsLongDb[16], s.bandsLongDb[6], 3),
          "rosa: plano en tercios");

    // Piso de ruido y nivel activo: 10 s de ruido a -60 y 10 s de "voz" (seno a -18).
    {
        Analyzer a;
        a.setSampleRate(48000);
        Generator g;
        g.setSampleRate(48000);
        std::vector<float> l(480), r(480);
        float* ch[2] = {l.data(), r.data()};
        Generator::Config n = pink;
        n.levelDb = -60;
        Generator::Config v = sine;
        v.levelDb = -18;
        for (int k = 0; k < 20; ++k) {
            g.configure(k % 2 == 0 ? n : v);
            for (int b = 0; b < 100; ++b) {
                g.render(ch, 2, 480);
                a.feed(ch, 2, 480);
            }
        }
        const auto rep = Analyzer::report(a.snapshot());
        std::printf("%s\n", rep.dump().c_str());
        CHECK(near(rep.value("pisoDeRuidoDbfs", 0.0), -60, 1.5), "piso de ruido -60 dBFS");
        CHECK(near(rep.value("nivelActivoDbfs", 0.0), -18, 1.5), "nivel activo -18 dBFS");
    }
    if (g_failures == 0) std::printf("test_audio_dsp: OK\n");
    return g_failures == 0 ? 0 : 1;
}
