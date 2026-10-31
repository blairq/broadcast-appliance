// ============================================================================
// src/core/tests/test_deriva_audio.cpp — la deriva de una placa, corregida (dsp/Deriva)
//
// Una placa sintética que va a 48 000·(1 + 100 ppm) respecto del reloj del
// estudio, con bloques de 480 muestras que llegan con 0 a 3 ms de temblor.
// Lo que captura es una señal lenta conocida, x(t) = sen(2π·t/10 s), muestreada
// en la hora REAL de captura de cada muestra: así se puede ver, en lo que
// sale, si el sello de cada cuadro dice la hora de captura de verdad (A/V).
//
//   1. Una hora entera (lineal: lo que se prueba es el lazo, no el filtro):
//      0 reanclajes (sin la corrección habría uno cada ~8 min), la deriva
//      estimada a ±2 ppm de 100 y el sello a ±2 ms de la hora de captura.
//   2. Lo mismo con el sinc rápido de verdad, 3 minutos: engancha igual.
//   3. Una placa que se corta 400 ms: un reanclaje (la red de seguridad) y
//      sigue enganchada; el corte se marca.
//   4. A −100 ppm (la placa lenta), igual.
// ============================================================================

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "dsp/Deriva.hpp"

using namespace ba::dsp;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr double kPi = 3.14159265358979323846;
constexpr double kR = 48000;
constexpr double kPeriodoSenal = 10.0; // s

double senal(double t) { return std::sin(2 * kPi * t / kPeriodoSenal); }

struct Resultado {
    std::uint64_t reanclajes = 0;
    double ppm = 0;
    double peorAvMs = 0;  ///< el peor |sello − hora de captura| después de enganchar
    double peorFaseMs = 0;
    int cortes = 0;
};

/// `segundos` de una placa a `ppm`; `cortarEn` > 0: en ese segundo la placa se
/// traba `cortarS` (no llega nada y después llega lo que viene, sin lo perdido).
Resultado correr(double ppm, double segundos, Quality q, double cortarEn = -1, double cortarS = 0) {
    Deriva d;
    Deriva::Opciones o;
    o.canales = 1;
    o.calidad = q;
    d.configurar(o);
    const double rd = kR * (1 + ppm * 1e-6);
    std::mt19937 azar(7);
    std::uniform_real_distribution<double> temblor(0.0, 0.003);
    constexpr int kN = 480;
    std::vector<float> in(kN), out(d.capacidadPara(kN));
    Resultado r;
    const double inicio = 100.0; // la hora del estudio cuando arranca la placa
    double captura0 = inicio;    // hora real de captura de la muestra 0 de la placa
    std::uint64_t i = 0;         // muestras de la placa
    bool cortado = false;
    for (;;) {
        double t = captura0 + static_cast<double>(i) / rd; // hora de captura de la muestra i
        if (t - inicio > segundos) break;
        if (cortarEn > 0 && !cortado && t - inicio > cortarEn) {
            captura0 += cortarS; // lo que no se capturó en el corte
            cortado = true;
            t = captura0 + static_cast<double>(i) / rd;
        }
        for (int k = 0; k < kN; ++k) in[static_cast<size_t>(k)] = static_cast<float>(senal(captura0 + static_cast<double>(i + k) / rd));
        i += kN;
        const double llegada = captura0 + static_cast<double>(i) / rd + temblor(azar);
        double pts = 0;
        bool corte = false;
        const std::size_t m = d.procesar(in.data(), kN, llegada, out.data(), out.size(), pts, corte);
        if (corte) ++r.cortes;
        if (!d.enganchado()) continue;
        r.peorFaseMs = std::max(r.peorFaseMs, std::fabs(d.desfaseMs()));
        // A/V: en los cuadros donde la señal cambia rápido, cuánto difiere lo que
        // sale de lo que la señal valía a la hora del sello.
        for (std::size_t k = 0; k < m; k += 97) {
            const double ts = pts + static_cast<double>(k) / kR;
            const double pendiente = 2 * kPi / kPeriodoSenal * std::cos(2 * kPi * ts / kPeriodoSenal);
            if (std::fabs(pendiente) < 0.3) continue;
            const double errorS = (senal(ts) - out[k]) / pendiente; // > 0: el contenido es más viejo que el sello
            r.peorAvMs = std::max(r.peorAvMs, std::fabs(errorS) * 1000);
        }
    }
    r.reanclajes = d.reanclajes();
    r.ppm = d.ppm();
    return r;
}

} // namespace

int main() {
    // 1.
    const Resultado hora = correr(100, 3600, Quality::Linear);
    std::printf("1 h a +100 ppm: %llu reanclajes, %.2f ppm estimados, peor A/V %.3f ms, peor fase %.3f ms\n",
                static_cast<unsigned long long>(hora.reanclajes), hora.ppm, hora.peorAvMs, hora.peorFaseMs);
    CHECK(hora.reanclajes == 0, "una hora sin reanclajes");
    CHECK(std::fabs(hora.ppm - 100) < 2, "la deriva estimada, a ±2 ppm");
    CHECK(hora.peorAvMs < 2, "el sello a ±2 ms de la hora de captura");
    CHECK(hora.cortes == 1, "solo el primer bloque marca corte");

    // 2.
    const Resultado sinc = correr(100, 180, Quality::SincFastest);
    std::printf("3 min con sinc: %llu reanclajes, %.2f ppm, peor A/V %.3f ms\n",
                static_cast<unsigned long long>(sinc.reanclajes), sinc.ppm, sinc.peorAvMs);
    CHECK(sinc.reanclajes == 0, "con sinc tampoco reancla");
    CHECK(std::fabs(sinc.ppm - 100) < 5, "y engancha la deriva");
    CHECK(sinc.peorAvMs < 2, "con el sello en su lugar");

    // 3.
    const Resultado corte = correr(100, 120, Quality::Linear, 60, 0.4);
    std::printf("con un corte de 400 ms: %llu reanclajes, %d cortes, %.2f ppm\n",
                static_cast<unsigned long long>(corte.reanclajes), corte.cortes, corte.ppm);
    CHECK(corte.reanclajes == 1, "el corte es un reanclaje");
    CHECK(corte.cortes == 2, "y se marca (más el primero)");
    CHECK(std::fabs(corte.ppm - 100) < 5, "la deriva estimada se conserva");

    // 4.
    const Resultado lenta = correr(-100, 900, Quality::Linear);
    std::printf("15 min a -100 ppm: %llu reanclajes, %.2f ppm, peor A/V %.3f ms\n",
                static_cast<unsigned long long>(lenta.reanclajes), lenta.ppm, lenta.peorAvMs);
    CHECK(lenta.reanclajes == 0 && std::fabs(lenta.ppm + 100) < 2 && lenta.peorAvMs < 2, "la placa lenta, igual");

    if (g_fallos == 0) std::printf("test_deriva_audio: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
