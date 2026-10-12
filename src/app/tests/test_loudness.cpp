// LoudnessPrograma contra los casos de EBU R128 / ITU-R BS.1770:
//   - seno estéreo de 1 kHz a -23 dBFS -> -23 LUFS integrado (±0,3) y TP ≈ -23 dBTP;
//   - silencio -> sin integrado (compuerta absoluta);
//   - mitad a -20 y mitad a -30 dBFS -> LRA ≈ 10 LU.
#include "audio/LoudnessPrograma.hpp"
#include "audio/ObjetivoLoudness.hpp"

#include <numbers>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using ba::app::audio::LoudnessPrograma;

static int fallas = 0;
#define CHECK(c, m)                                    \
    do {                                               \
        if (!(c)) {                                    \
            std::printf("FALLA: %s\n", m);             \
            ++fallas;                                  \
        } else {                                       \
            std::printf("ok: %s\n", m);                \
        }                                              \
    } while (0)

static void seno(LoudnessPrograma& l, double dbfs, double seg, double f = 1000) {
    const double a = std::pow(10.0, dbfs / 20.0);
    const int bloque = 480;
    std::vector<float> L(bloque), R(bloque);
    static double fase = 0;
    for (int n = 0; n < static_cast<int>(seg * 48000 / bloque); ++n) {
        for (int i = 0; i < bloque; ++i) {
            L[i] = R[i] = static_cast<float>(a * std::sin(fase));
            fase += 2 * std::numbers::pi * f / 48000.0;
        }
        const float* ch[2] = {L.data(), R.data()};
        l.alimentar(ch, 2, bloque);
    }
}

int main() {
    {
        LoudnessPrograma l(48000);
        seno(l, -23, 20);
        const auto f = l.foto();
        std::printf("  I=%.2f M=%.2f S=%.2f TP=%.2f\n", f.i, f.m, f.s, f.tp);
        CHECK(std::abs(f.i - (-23)) < 0.3, "seno 1 kHz a -23 dBFS mide -23 LUFS integrado");
        CHECK(std::abs(f.tp - (-23)) < 0.5, "true peak del seno a -23 dBFS");
        CHECK(f.lra < 1.0, "un seno parejo no tiene rango (LRA ~0)");
    }
    {
        LoudnessPrograma l(48000);
        std::vector<float> z(480, 0.0f);
        const float* ch[2] = {z.data(), z.data()};
        for (int i = 0; i < 500; ++i) l.alimentar(ch, 2, 480);
        CHECK(l.foto().i <= -70, "silencio: sin integrado (compuerta absoluta)");
    }
    {
        LoudnessPrograma l(48000);
        seno(l, -20, 20);
        seno(l, -30, 20);
        const auto f = l.foto();
        std::printf("  LRA=%.2f I=%.2f\n", f.lra, f.i);
        CHECK(std::abs(f.lra - 10) < 1.5, "mitad a -20 y mitad a -30: LRA ~10 LU");
    }
    {
        LoudnessPrograma l(48000);
        CHECK(l.ponerObjetivo({{"perfil", "a85"}}).empty() && l.objetivo().lufs == -24, "perfil A/85: -24 LKFS");
        CHECK(!l.ponerObjetivo({{"perfil", "custom"}, {"lufs", 3}}).empty(), "custom fuera de rango: error");
    }
    {
        // El objetivo guardado (Config/loudness.json): se guarda solo si vale, y vuelve al arrancar.
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "ba-test-objetivo-loudness";
        fs::remove_all(dir);
        LoudnessPrograma l(48000);
        ba::app::audio::ObjetivoLoudness o(dir, l);
        CHECK(!o.poner({{"perfil", "custom"}, {"lufs", 3}}).empty() && !fs::exists(dir / "Config" / "loudness.json"),
              "objetivo inválido: error y nada guardado");
        CHECK(o.poner({{"perfil", "plataforma14"}}).empty() && fs::exists(dir / "Config" / "loudness.json"),
              "objetivo válido: queda guardado");
        LoudnessPrograma otro(48000);
        ba::app::audio::ObjetivoLoudness(dir, otro).cargar();
        CHECK(otro.objetivo().perfil == "plataforma14" && otro.objetivo().lufs == -14, "al arrancar vuelve el guardado");
        fs::remove_all(dir);
    }
    std::printf(fallas ? "\n%d FALLAS\n" : "\nTODO OK\n", fallas);
    return fallas ? 1 : 0;
}
