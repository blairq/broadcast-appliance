// test_channel_strip.cpp
//
// Pruebas unitarias del channel strip nativo (spec 7.2) y de sus presets
// (spec 7.3). Sin framework externo: es un main() que devuelve 0 si todo pasa
// y 1 si algo falla (lo registra CTest).
//
// Casos:
//   1. no-NaN: 4800 muestras de una senal 440 Hz + ruido a traves de la cadena
//      completa con cada uno de los 3 presets; cada muestra de salida debe ser
//      std::isfinite y |x| < 1.5.
//   2. HPF: una senal de 50 Hz a traves de un strip con hpfFreq=100 (gate/comp/
//      lim/de-esser/EQ desactivados) se atenúa mas que una senal de 4 kHz al
//      mismo nivel de entrada.
//   3. gate: gate activo (thr -45, range -12), comp/lim off; una senal quieta
//      (-60 dBFS) se atenúa fuertemente; una senal fuerte (-6 dBFS) queda cerca
//      de unity. Se compara el RMS de salida.
//   4. preset-change no-click: senal estacionaria a -20 dBFS con presetPodium,
//      luego setParams(presetLavalier) y se sigue; no debe haber un salto de
//      |x| > 0.5 dentro de 2 ms del cambio.

#include "dsp/ChannelStrip.hpp"
#include "dsp/Presets.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

using ba::dsp::ChannelStrip;
using ba::dsp::presetHandheld;
using ba::dsp::presetLavalier;
using ba::dsp::presetPodium;
using ba::dsp::StripParams;

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
constexpr double kPi = 3.14159265358979323846;

// RMS de salida medidos por los tests (se llenan dentro de cada test).
double g_rms50 = 0.0;
double g_rms4k = 0.0;
double g_rmsQuiet = 0.0;
double g_rmsLoud = 0.0;

/// dB -> lineal.
double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

/// RMS de un buffer mono.
double rms(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0;
    }
    double acc = 0.0;
    for (float x : v) {
        acc += static_cast<double>(x) * static_cast<double>(x);
    }
    return std::sqrt(acc / static_cast<double>(v.size()));
}

/// Genera `n` FRAMES estéreo interleaved de una senal seno a `freq` Hz con
/// amplitud `amp` (lineal) y un ruido blanco de amplitud `noiseAmp` (lineal,
/// determinista por LCG). Devuelve el buffer de 2*n floats.
std::vector<float> makeSineNoise(int n, double freq, double amp, double noiseAmp, unsigned seed) {
    std::vector<float> buf(static_cast<std::size_t>(2 * n));
    unsigned state = seed;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kFs;
        const double s = amp * std::sin(2.0 * kPi * freq * t);
        // Ruido blanco determinista en [-1,1].
        state = state * 1664525u + 1013904223u;
        const double rnd = (static_cast<double>(state) / 4294967296.0) * 2.0 - 1.0;
        const float sample = static_cast<float>(s + noiseAmp * rnd);
        buf[static_cast<std::size_t>(2 * i)] = sample;
        buf[static_cast<std::size_t>(2 * i + 1)] = sample;
    }
    return buf;
}

/// Genera `n` FRAMES estéreo interleaved de una senal seno pura a `freq` Hz
/// con amplitud `amp` (lineal), sin ruido.
std::vector<float> makeSine(int n, double freq, double amp) {
    std::vector<float> buf(static_cast<std::size_t>(2 * n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kFs;
        const float s = static_cast<float>(amp * std::sin(2.0 * kPi * freq * t));
        buf[static_cast<std::size_t>(2 * i)] = s;
        buf[static_cast<std::size_t>(2 * i + 1)] = s;
    }
    return buf;
}

/// Configura un strip "neutro": solo HPF activo, todo lo demas off.
StripParams neutralHpf(double hpfFreq) {
    StripParams p;
    p.fs = kFs;
    p.hpfFreq = hpfFreq;
    p.hpfSlopeDbOct = 18.0;
    // EQ desactivado (por defecto).
    for (int i = 0; i < 4; ++i) {
        p.eq[i].enabled = false;
    }
    p.deEsserEnabled = false;
    p.gateEnabled = false;
    p.compEnabled = false;
    p.limEnabled = false;
    return p;
}

/// 1) no-NaN: cadena completa con cada preset.
void testNoNaN() {
    const int n = 4800;
    const auto presets = {presetPodium(), presetLavalier(), presetHandheld()};
    for (const auto& p : presets) {
        ChannelStrip strip;
        strip.setParams(p, /*smoothMs=*/5.0);
        auto buf = makeSineNoise(n, 440.0, dbToLin(-10.0), dbToLin(-40.0),
                                 /*seed=*/12345u);
        strip.process(buf.data(), n);
        for (float x : buf) {
            CHECK(std::isfinite(x));
            CHECK(std::fabs(x) < 1.5);
        }
    }
}

/// 2) HPF: 50 Hz se atenúa mas que 4 kHz.
void testHpf() {
    const int n = 4800;
    const double amp = dbToLin(-6.0);

    // 50 Hz a traves del HPF 100 Hz.
    {
        ChannelStrip strip;
        strip.setParams(neutralHpf(100.0), /*smoothMs=*/0.0);
        auto buf = makeSine(n, 50.0, amp);
        strip.process(buf.data(), n);
        // RMS del canal L (mono en ambos canales).
        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            mono[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(2 * i)];
        }
        g_rms50 = rms(mono);
    }
    // 4 kHz a traves del mismo HPF 100 Hz.
    {
        ChannelStrip strip;
        strip.setParams(neutralHpf(100.0), /*smoothMs=*/0.0);
        auto buf = makeSine(n, 4000.0, amp);
        strip.process(buf.data(), n);
        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            mono[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(2 * i)];
        }
        g_rms4k = rms(mono);
    }
    // El HPF debe atenuar mas a 50 Hz que a 4 kHz.
    CHECK(g_rms50 < g_rms4k);
    // Margen: 50 Hz debe caer al menos 6 dB respecto a 4 kHz.
    const double ratioDb = 20.0 * std::log10((g_rms50 + 1e-9) / (g_rms4k + 1e-9));
    CHECK(ratioDb < -6.0);
}

/// 3) gate: quieta se atenúa, fuerte queda cerca de unity.
void testGate() {
    const int n = 4800;
    // Strip con solo gate activo.
    auto params = neutralHpf(10.0); // HPF muy bajo (~neutro)
    params.gateEnabled = true;
    params.gateThresholdDb = -45.0;
    params.gateRangeDb = 12.0;
    params.gateAttackMs = 1.0;
    params.gateReleaseMs = 100.0;

    // Quieta: -60 dBFS.
    {
        ChannelStrip strip;
        strip.setParams(params, /*smoothMs=*/0.0);
        auto buf = makeSine(n, 440.0, dbToLin(-60.0));
        strip.process(buf.data(), n);
        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            mono[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(2 * i)];
        }
        g_rmsQuiet = rms(mono);
    }
    // Fuerte: -6 dBFS.
    {
        ChannelStrip strip;
        strip.setParams(params, /*smoothMs=*/0.0);
        auto buf = makeSine(n, 440.0, dbToLin(-6.0));
        strip.process(buf.data(), n);
        std::vector<float> mono(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            mono[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(2 * i)];
        }
        g_rmsLoud = rms(mono);
    }
    // La senal quieta debe quedar muy por debajo de la fuerte.
    CHECK(g_rmsQuiet < g_rmsLoud);
    // La fuerte debe quedar cerca de unity (RMS de salida ~ RMS de entrada).
    const double inLoudRms = dbToLin(-6.0) * std::sqrt(0.5);
    CHECK(g_rmsLoud > 0.5 * inLoudRms);
    // La quieta debe estar atenuada al menos 10 dB respecto a la fuerte.
    const double ratioDb = 20.0 * std::log10((g_rmsQuiet + 1e-9) / (g_rmsLoud + 1e-9));
    CHECK(ratioDb < -10.0);
}

/// 4) preset-change no-click.
void testNoClickOnPresetChange() {
    const int n = 4800;
    const double amp = dbToLin(-20.0);
    ChannelStrip strip;
    strip.setParams(presetPodium(), /*smoothMs=*/5.0);

    // Establece un estado estacionario con presetPodium.
    auto buf = makeSine(n, 440.0, amp);
    strip.process(buf.data(), n);

    // Cambia a presetLavalier en el frame `changeFrame`.
    const int changeFrame = n / 2;
    // Procesa la primera mitad con podium, cambia, y procesa la segunda mitad.
    strip.reset();
    strip.setParams(presetPodium(), /*smoothMs=*/5.0);
    auto buf2 = makeSine(n, 440.0, amp);
    // Procesa hasta changeFrame.
    strip.process(buf2.data(), changeFrame);
    // Cambio de preset.
    strip.setParams(presetLavalier(), /*smoothMs=*/5.0);
    // Procesa el resto.
    strip.process(buf2.data() + 2 * changeFrame, n - changeFrame);

    // Busca el maximo salto |x[i] - x[i-1]| dentro de 2 ms del cambio.
    const int window = static_cast<int>(0.002 * kFs); // 2 ms en muestras
    double maxJump = 0.0;
    for (int i = changeFrame; i < std::min(n, changeFrame + window); ++i) {
        const float cur = buf2[static_cast<std::size_t>(2 * i)];
        const float prev = buf2[static_cast<std::size_t>(2 * (i - 1))];
        const double jump = std::fabs(static_cast<double>(cur) - static_cast<double>(prev));
        if (jump > maxJump) {
            maxJump = jump;
        }
    }
    // No debe haber un salto > 0.5 dentro de 2 ms del cambio.
    CHECK(maxJump < 0.5);
}

} // namespace

int main() {
    testNoNaN();
    testHpf();
    testGate();
    testNoClickOnPresetChange();

    if (g_failures == 0) {
        std::printf("test_channel_strip: OK\n");
        return 0;
    }
    std::printf("test_channel_strip: %d fallo(s)\n", g_failures);
    return 1;
}
