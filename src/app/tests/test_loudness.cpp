// LoudnessPrograma contra los casos de EBU R128 / ITU-R BS.1770:
//   - seno estéreo de 1 kHz a -23 dBFS -> -23 LUFS integrado (±0,3) y TP ≈ -23 dBTP;
//   - silencio -> sin integrado (compuerta absoluta);
//   - mitad a -20 y mitad a -30 dBFS -> LRA ≈ 10 LU.
// Y el AGC lento hacia el objetivo (dsp/AgcLoudness, en balimitador antes del
// limitador), con un programa sintético de nivel variable: bajo, alto, una
// pausa y otra vez bajo. Llega al objetivo sin pasar ±6 dB ni 1 dB/s, se
// queda quieto en la pausa y el limitador sigue cuidando el techo.
#include "persist/ColaEscritura.hpp"
#include "audio/LoudnessPrograma.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "dsp/AgcLoudness.hpp"
#include "dsp/LimitadorTP.hpp"

#include <algorithm>

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
        // Se guarda por la cola de persistencia (persist::cola): al disco, al vaciarla.
        CHECK(o.poner({{"perfil", "plataforma14"}}).empty() && ba::persist::cola().vaciar() &&
                  fs::exists(dir / "Config" / "loudness.json"),
              "objetivo válido: queda guardado");
        LoudnessPrograma otro(48000);
        ba::app::audio::ObjetivoLoudness(dir, otro).cargar();
        CHECK(otro.objetivo().perfil == "plataforma14" && otro.objetivo().lufs == -14, "al arrancar vuelve el guardado");
        fs::remove_all(dir);
    }
    {
        // El AGC: objetivo -23 LUFS, delante del limitador a -1 dBTP; lo que sale se mide.
        ba::dsp::AgcLoudness agc;
        agc.preparar(2, 48000);
        agc.ponerObjetivo(-23);
        agc.ponerActivo(true);
        ba::dsp::LimitadorTP lim;
        lim.preparar(2, 48000);
        lim.ponerTecho(-1);
        LoudnessPrograma medido(48000);
        double fase = 0, peorPasoDb = 0, anterior = 0;
        // Pasa `seg` segundos de un seno de 1 kHz a `dbfs` (o silencio) por la cadena;
        // devuelve el corto (3 s) de lo que sale al final.
        const auto tramo = [&](double dbfs, double seg, bool silencio = false) {
            const double a = silencio ? 0.0 : std::pow(10.0, dbfs / 20.0);
            std::vector<float> iv(960), L(480), R(480);
            for (int n = 0; n < static_cast<int>(seg * 100); ++n) {
                for (int i = 0; i < 480; ++i) {
                    iv[2 * i] = iv[2 * i + 1] = static_cast<float>(a * std::sin(fase));
                    fase += 2 * std::numbers::pi * 1000 / 48000.0;
                }
                agc.procesar(iv.data(), 480);
                lim.procesar(iv.data(), 480);
                for (int i = 0; i < 480; ++i) L[i] = iv[2 * i], R[i] = iv[2 * i + 1];
                const float* ch[2] = {L.data(), R.data()};
                medido.alimentar(ch, 2, 480);
                // La velocidad: cuánto se movió la ganancia en este bloque de 10 ms.
                peorPasoDb = std::max(peorPasoDb, std::fabs(agc.gananciaDb() - anterior) / 0.01);
                anterior = agc.gananciaDb();
            }
            return medido.foto().s;
        };
        const double bajo = tramo(-30, 60); // 7 LU abajo: corrige lo más que puede (+6)
        std::printf("  AGC: a -30 dBFS sale %.2f LUFS con %+.2f dB\n", bajo, agc.gananciaDb());
        CHECK(std::abs(agc.gananciaDb() - 6.0) < 0.05, "AGC: 7 LU abajo, sube hasta +6 dB y no más");
        CHECK(std::abs(bajo - (-24)) < 0.5, "AGC: lo que sale queda a 1 LU del objetivo");
        const double alto = tramo(-17, 60); // 6 LU arriba
        std::printf("  AGC: a -17 dBFS sale %.2f LUFS con %+.2f dB\n", alto, agc.gananciaDb());
        CHECK(std::abs(alto - (-23)) < 0.5, "AGC: 6 LU arriba, lo baja al objetivo");
        const double antesDeLaPausa = agc.gananciaDb();
        tramo(0, 30, true);
        std::printf("  AGC: después de 30 s de silencio, %+.2f dB (antes %+.2f)\n", agc.gananciaDb(), antesDeLaPausa);
        CHECK(std::abs(agc.gananciaDb() - antesDeLaPausa) < 0.05, "AGC: en silencio queda congelado");
        CHECK(agc.congelado(), "y lo dice");
        tramo(-26, 30);
        std::printf("  AGC: peor velocidad %.3f dB/s; true peak de lo que salió %.2f dBTP\n", peorPasoDb, medido.foto().tp);
        CHECK(peorPasoDb <= 1.01, "AGC: nunca más de 1 dB por segundo");
        CHECK(medido.foto().tp <= -0.95, "el limitador sigue cuidando el techo");
        CHECK(std::abs(medido.foto().s - (-23)) < 0.5, "AGC: vuelve al objetivo después de la pausa");

        // Apagado: vuelve a 0 dB a la misma velocidad (sin saltos) y se queda ahí.
        agc.ponerActivo(false);
        peorPasoDb = 0;
        tramo(-26, 10);
        CHECK(std::abs(agc.gananciaDb()) < 0.01 && peorPasoDb <= 1.01, "AGC apagado: vuelve a 0 dB despacio");
    }
    std::printf(fallas ? "\n%d FALLAS\n" : "\nTODO OK\n", fallas);
    return fallas ? 1 : 0;
}
