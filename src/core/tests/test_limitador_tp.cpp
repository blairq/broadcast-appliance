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
//   7. Los caminos rápidos (bloque bajo el techo: sin interpolar; limitador quieto: solo
//      la demora) dan lo mismo que el cálculo completo de siempre (una copia acá), con
//      bloques de tamaños raros, tramos fuertes y suaves y el limitador apagado.

#include "dsp/LimitadorTP.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <numbers>
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
        const float x = static_cast<float>(ampl * std::sin(2 * std::numbers::pi * frec * i / fs + fase));
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

/// El limitador tal como era antes de los caminos rápidos (2026-11-02): cada muestra
/// interpolada, la historia circular. La referencia para el punto 7.
class Referencia {
   public:
    void preparar(int canales, double techoDb, double lookaheadMs) {
        ch_ = canales;
        techo_ = std::pow(10.0, techoDb / 20.0);
        L_ = static_cast<std::size_t>(std::lround(lookaheadMs * 1e-3 * 48000));
        hist_.assign(static_cast<std::size_t>(ch_), std::vector<float>(12, 0.0f));
        demora_.assign(L_ * static_cast<std::size_t>(ch_), 0.0f);
        hBuf_.assign(L_, 1.0f);
        hSuma_ = static_cast<double>(L_);
        coefLib_ = 1.0 - std::exp(-1.0 / (100.0 * 1e-3 * 48000));
        // Los mismos coeficientes que el limitador (sinc con Kaiser β = 8, ×4, 12 por fase).
        const double centro = (48 - 1) / 2.0;
        auto i0 = [](double x) {
            double suma = 1, t = 1;
            for (int k = 1; k < 30; ++k) {
                t *= (x / (2 * k)) * (x / (2 * k));
                suma += t;
            }
            return suma;
        };
        for (int f = 0; f < 4; ++f) {
            double suma = 0;
            for (int t = 0; t < 12; ++t) {
                const int n = f + 4 * t;
                const double x = (n - centro) / 4;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double r = (n - centro) / centro;
                c_[f][t] = static_cast<float>(sinc * i0(8.0 * std::sqrt(std::max(0.0, 1 - r * r))) / i0(8.0));
                suma += c_[f][t];
            }
            for (int t = 0; t < 12; ++t) c_[f][t] = static_cast<float>(c_[f][t] / suma);
        }
    }
    bool activo = true;
    void procesar(float* datos, std::size_t frames) {
        const auto ch = static_cast<std::size_t>(ch_);
        const double techo = techo_ * 0.995;
        for (std::size_t i = 0; i < frames; ++i) {
            float* fr = datos + i * ch;
            histPos_ = (histPos_ + 1) % 12;
            double p = 0;
            for (std::size_t c = 0; c < ch; ++c) {
                hist_[c][histPos_] = fr[c];
                p = std::max(p, std::fabs(static_cast<double>(fr[c])));
                for (int f = 0; f < 4; ++f) {
                    double y = 0;
                    for (int t = 0; t < 12; ++t) y += c_[f][t] * hist_[c][(histPos_ + 12 - t) % 12];
                    p = std::max(p, std::fabs(y));
                }
            }
            const float g = static_cast<float>(activo && p > techo ? techo / p : 1.0);
            while (!min_.empty() && min_.back().second >= g) min_.pop_back();
            min_.emplace_back(n_, g);
            while (min_.front().first + L_ < n_) min_.pop_front();
            const float h = min_.front().second;
            hSuma_ += h - hBuf_[hPos_];
            hBuf_[hPos_] = h;
            hPos_ = (hPos_ + 1) % L_;
            const double a = hSuma_ / static_cast<double>(L_);
            g_ = a < g_ ? a : g_ + (a - g_) * coefLib_;
            float* d = &demora_[demoraPos_ * ch];
            for (std::size_t c = 0; c < ch; ++c) {
                const float s = d[c];
                d[c] = fr[c];
                fr[c] = static_cast<float>(s * g_);
            }
            demoraPos_ = (demoraPos_ + 1) % L_;
            ++n_;
            if ((n_ & 0xFFFF) == 0) {
                hSuma_ = 0;
                for (float x : hBuf_) hSuma_ += x;
            }
        }
    }

   private:
    int ch_ = 2;
    double techo_ = 1, hSuma_ = 0, g_ = 1, coefLib_ = 0;
    std::size_t L_ = 1, histPos_ = 0, hPos_ = 0, demoraPos_ = 0;
    std::uint64_t n_ = 0;
    std::vector<std::vector<float>> hist_;
    std::vector<float> demora_, hBuf_;
    std::deque<std::pair<std::uint64_t, float>> min_;
    std::array<std::array<float, 12>, 4> c_{};
};
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
        const auto v = tono(12000, 1.0, std::numbers::pi / 4, 96000);
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
    {
        // 7. Contra la referencia: suave (bajo el techo), fuerte, impulsos, de nuevo suave
        // largo (vuelve a quedar quieto), con bloques de 1, 7, 480, 13, 960 cuadros.
        std::mt19937 r(11);
        std::normal_distribution<float> ruido(0.0f, 1.0f);
        std::vector<float> v(48000 * 6 * 2);
        for (std::size_t i = 0; i < v.size() / 2; ++i) {
            const double t = static_cast<double>(i) / 48000;
            double amp = 0.05;                     // −26 dBFS: muy bajo el techo
            if (t > 1.0 && t < 1.5) amp = 1.5;     // fuerte: limita
            if (t > 3.0 && t < 3.2) amp = 0.6;     // cerca: la cota no alcanza, se interpola
            float x = static_cast<float>(amp * std::sin(2 * std::numbers::pi * 523.0 * t)) + 0.01f * ruido(r);
            if (i == 48000 * 4 + 17) x = 1.3f;     // un golpe suelto
            v[2 * i] = x;
            v[2 * i + 1] = -0.8f * x;
        }
        for (const bool activo : {true, false})
            for (const double anticipo : {5.0, 1.0}) {
                LimitadorTP l;
                l.ponerLookahead(anticipo);
                l.preparar(2, 48000);
                l.ponerActivo(activo);
                Referencia ref;
                ref.preparar(2, -1.0, anticipo);
                ref.activo = activo;
                std::vector<float> a = v, b = v;
                const std::size_t bloques[] = {1, 7, 480, 13, 960};
                std::size_t k = 0;
                double tRapido = 0, tRef = 0;
                for (std::size_t i = 0; i < a.size() / 2; ++k) {
                    const std::size_t n = std::min(bloques[k % 5], a.size() / 2 - i);
                    auto t0 = std::chrono::steady_clock::now();
                    l.procesar(&a[2 * i], n);
                    auto t1 = std::chrono::steady_clock::now();
                    ref.procesar(&b[2 * i], n);
                    auto t2 = std::chrono::steady_clock::now();
                    tRapido += std::chrono::duration<double>(t1 - t0).count();
                    tRef += std::chrono::duration<double>(t2 - t1).count();
                    i += n;
                }
                double dif = 0;
                std::size_t distintos = 0;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    dif = std::max(dif, std::fabs(static_cast<double>(a[i]) - b[i]));
                    if (a[i] != b[i]) ++distintos;
                }
                std::printf("contra la referencia (activo %d, %.0f ms): dif máx %.2e, %zu muestras distintas; %.1f ms contra %.1f ms\n",
                            activo ? 1 : 0, anticipo, dif, distintos, tRapido * 1e3, tRef * 1e3);
                CHECK(dif <= 1e-7, "los caminos rápidos dan lo mismo que el cálculo completo");
            }
        // El costo con lo de siempre (un retorno con voces, bajo el techo): bloques de 10 ms.
        LimitadorTP l;
        l.ponerLookahead(1.0);
        l.preparar(2, 48000);
        Referencia ref;
        ref.preparar(2, -1.0, 1.0);
        std::vector<float> a = tono(300, 0.2, 0, 48000 * 4), b = a;
        auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < a.size(); i += 960) l.procesar(&a[i], 480);
        auto t1 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < b.size(); i += 960) ref.procesar(&b[i], 480);
        auto t2 = std::chrono::steady_clock::now();
        std::printf("4 s de voz bajo el techo: %.2f ms (antes %.2f ms)\n", std::chrono::duration<double>(t1 - t0).count() * 1e3,
                    std::chrono::duration<double>(t2 - t1).count() * 1e3);
        CHECK(a == b, "bajo el techo: idéntico a la referencia");
    }
    std::printf("test_limitador_tp: %s\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
