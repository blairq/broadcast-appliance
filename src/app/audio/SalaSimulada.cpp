// src/app/audio/SalaSimulada.cpp — la sala de mentira para medir sin que suene nada (ver LazoMedicion.hpp)
//
// No es una simulación acústica fina: tiene lo que la calibración tiene que
// encontrar (el directo a la distancia justa, primeras reflexiones de las seis
// superficies, modos de la sala rectangular que cambian de un punto a otro y una
// cola con su RT60) para probar la medición, el análisis y la interfaz de noche.
#include "audio/LazoMedicion.hpp"

#include <algorithm>
#include <cmath>

namespace ba::app::audio {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kC = 343.0;

double num(const nlohmann::json& j, const char* k, double d) {
    const auto it = j.find(k);
    return it != j.end() && it->is_number() ? it->get<double>() : d;
}
} // namespace

std::vector<float> simularSala(const nlohmann::json& sala, const nlohmann::json& pa, const nlohmann::json& mic,
                               const std::vector<float>& x, int sr) {
    const double L = num(sala, "largo", 8), W = num(sala, "ancho", 6), H = num(sala, "alto", 3);
    const double rt60 = num(sala, "rt60", 0.6);
    const double xs = num(pa, "x", 1), ys = num(pa, "y", 1), zs = num(pa, "z", 1.5);
    const double xr = num(mic, "x", L / 2), yr = num(mic, "y", W / 2), zr = num(mic, "z", 1.2);
    const size_t n = x.size();
    // Lo que tarda la placa en sacar y volver a meter el audio.
    const size_t latencia = static_cast<size_t>(0.004 * sr);
    std::vector<double> y(n + static_cast<size_t>(sr * (rt60 + 0.3)) + latencia, 0.0);

    // Directo y primeras reflexiones (fuentes imagen de primer orden, paredes que absorben 30 %).
    struct Tap {
        size_t d;
        double g;
    };
    std::vector<Tap> taps;
    const auto tap = [&](double sx, double sy, double sz, double g) {
        const double d = std::max(0.3, std::sqrt(std::pow(sx - xr, 2) + std::pow(sy - yr, 2) + std::pow(sz - zr, 2)));
        taps.push_back({latencia + static_cast<size_t>(d / kC * sr), g / d});
    };
    tap(xs, ys, zs, 1.0);
    const double refl = std::sqrt(0.7);
    tap(-xs, ys, zs, refl);
    tap(2 * L - xs, ys, zs, refl);
    tap(xs, -ys, zs, refl);
    tap(xs, 2 * W - ys, zs, refl);
    tap(xs, ys, -zs, refl);
    tap(xs, ys, 2 * H - zs, refl);
    const double dDirecto = std::max(0.3, std::sqrt(std::pow(xs - xr, 2) + std::pow(ys - yr, 2) + std::pow(zs - zr, 2)));
    for (const auto& t : taps)
        for (size_t i = 0; i < n && i + t.d < y.size(); ++i) y[i + t.d] += t.g * x[i];

    // Los modos (resonadores de segundo orden) hasta 200 Hz: su peso depende de dónde
    // están el parlante y el micrófono (coseno de cada eje), como en una sala real.
    const size_t d0 = taps.front().d;
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 5; ++b)
            for (int c = 0; c < 3; ++c) {
                if (a + b + c == 0) continue;
                const double f = kC / 2 * std::sqrt(std::pow(a / L, 2) + std::pow(b / W, 2) + std::pow(c / H, 2));
                if (f > 200) continue;
                const double peso = std::cos(a * kPi * xs / L) * std::cos(b * kPi * ys / W) * std::cos(c * kPi * zs / H) *
                                    std::cos(a * kPi * xr / L) * std::cos(b * kPi * yr / W) * std::cos(c * kPi * zr / H);
                // Los axiales pesan más (menos reflexiones por ciclo: menos pérdida).
                const int ceros = (a == 0) + (b == 0) + (c == 0);
                const double g = (ceros == 2 ? 1.0 : ceros == 1 ? 0.5 : 0.25) * peso;
                const double t60 = rt60 * 1.6; // en graves la sala absorbe menos
                const double r = std::pow(10.0, -3.0 / (t60 * sr)), w = 2 * kPi * f / sr;
                const double c1 = 2 * r * std::cos(w), c2 = -r * r;
                const double k = g * (1 - r) * 0.6 / dDirecto;
                double y1 = 0, y2 = 0;
                for (size_t i = 0; i + d0 < y.size(); ++i) {
                    const double v = (i < n ? k * x[i] : 0.0) + c1 * y1 + c2 * y2;
                    y2 = y1;
                    y1 = v;
                    y[i + d0] += v;
                }
            }

    // La cola difusa: un reverberador de Schroeder (4 peines en paralelo y 2 pasatodos) con su RT60.
    {
        const double pDel[4] = {0.0297, 0.0371, 0.0411, 0.0437};
        std::vector<double> difusa(y.size(), 0.0);
        for (double pd : pDel) {
            const size_t D = static_cast<size_t>(pd * sr);
            const double g = std::pow(10.0, -3.0 * pd / rt60);
            std::vector<double> lin(D, 0.0);
            size_t k = 0;
            for (size_t i = 0; i < y.size(); ++i) {
                const double in = i >= d0 && i - d0 < n ? x[i - d0] : 0.0;
                const double out = lin[k];
                lin[k] = in + g * out;
                k = (k + 1) % D;
                difusa[i] += out;
            }
        }
        for (double pd : {0.005, 0.0017}) {
            const size_t D = static_cast<size_t>(pd * sr);
            std::vector<double> lin(D, 0.0);
            size_t k = 0;
            for (double& v : difusa) {
                const double buf = lin[k];
                const double out = -0.7 * v + buf;
                lin[k] = v + 0.7 * out;
                k = (k + 1) % D;
                v = out;
            }
        }
        // Más difusa cuanto más lejos del parlante (el campo reverberante no cae con la distancia).
        const double gd = 0.12;
        for (size_t i = 0; i < y.size(); ++i) y[i] += gd * difusa[i];
    }

    // El ruido de la sala y del micrófono (-75 dBFS).
    unsigned semilla = 777u + static_cast<unsigned>(xr * 100 + yr * 10);
    std::vector<float> r(y.size());
    for (size_t i = 0; i < y.size(); ++i) {
        semilla = semilla * 1664525u + 1013904223u;
        const double ruido = (static_cast<double>(semilla >> 8) / 8388608.0 - 1.0) * 1.8e-4;
        r[i] = static_cast<float>(y[i] + ruido);
    }
    return r;
}

} // namespace ba::app::audio
