// ============================================================================
// src/core/dsp/LimitadorTP.cpp — ver LimitadorTP.hpp
// ============================================================================
#include "dsp/LimitadorTP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace ba::dsp {

namespace {

constexpr int kFases = 4;              // ×4: lo que pide la BS.1770 para el true peak
constexpr int kTapsPorFase = 12;
constexpr int kTaps = kFases * kTapsPorFase;

double besselI0(double x) {
    double suma = 1, termino = 1;
    for (int k = 1; k < 30; ++k) {
        termino *= (x / (2 * k)) * (x / (2 * k));
        suma += termino;
    }
    return suma;
}

/// Los coeficientes del interpolador ×4: sinc con ventana de Kaiser (β = 8),
/// cada fase normalizada a ganancia 1.
const std::array<std::array<float, kTapsPorFase>, kFases>& coeficientes() {
    static const auto c = []() {
        std::array<std::array<float, kTapsPorFase>, kFases> k{};
        constexpr double beta = 8.0;
        const double centro = (kTaps - 1) / 2.0;
        for (int f = 0; f < kFases; ++f) {
            double suma = 0;
            for (int t = 0; t < kTapsPorFase; ++t) {
                const int n = f + kFases * t;
                const double x = (n - centro) / kFases;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double r = (n - centro) / centro;
                const double w = besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(beta);
                k[f][t] = static_cast<float>(sinc * w);
                suma += k[f][t];
            }
            for (int t = 0; t < kTapsPorFase; ++t) k[f][t] = static_cast<float>(k[f][t] / suma);
        }
        return k;
    }();
    return c;
}

double interpolarMax(const std::vector<float>& h, std::size_t pos) {
    // h es circular de kTapsPorFase; pos apunta a la muestra más nueva.
    const auto& c = coeficientes();
    double m = 0;
    for (int f = 0; f < kFases; ++f) {
        double y = 0;
        for (int t = 0; t < kTapsPorFase; ++t)
            y += c[f][t] * h[(pos + kTapsPorFase - t) % kTapsPorFase];
        m = std::max(m, std::fabs(y));
    }
    return m;
}

} // namespace

void LimitadorTP::preparar(int canales, int frecuencia) {
    canales_ = std::max(1, canales);
    frecuencia_ = std::max(8000, frecuencia);
    rearmar();
}

void LimitadorTP::ponerTecho(double dbtp) {
    techoDb_ = std::clamp(dbtp, -20.0, 0.0);
    techoLin_ = std::pow(10.0, techoDb_ / 20.0);
}

void LimitadorTP::ponerLookahead(double ms) {
    lookaheadMs_ = std::clamp(ms, 1.0, 20.0);
    if (canales_ > 0) rearmar();
}

void LimitadorTP::ponerLiberacion(double ms) {
    liberacionMs_ = std::clamp(ms, 10.0, 2000.0);
    coefLib_ = 1.0 - std::exp(-1.0 / (liberacionMs_ * 1e-3 * frecuencia_));
}

void LimitadorTP::rearmar() {
    lookahead_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(lookaheadMs_ * 1e-3 * frecuencia_)));
    hist_.assign(static_cast<std::size_t>(canales_), std::vector<float>(kTapsPorFase, 0.0f));
    histPos_ = 0;
    demoraBuf_.assign(lookahead_ * static_cast<std::size_t>(canales_), 0.0f);
    demoraPos_ = 0;
    minimos_.clear();
    hBuf_.assign(lookahead_, 1.0f);
    hPos_ = 0;
    hSuma_ = static_cast<double>(lookahead_);
    n_ = 0;
    g_ = 1.0;
    ponerTecho(techoDb_);
    ponerLiberacion(liberacionMs_);
}

double LimitadorTP::picoVerdadero(const float* frame) {
    histPos_ = (histPos_ + 1) % kTapsPorFase;
    double m = 0;
    for (int c = 0; c < canales_; ++c) {
        hist_[static_cast<std::size_t>(c)][histPos_] = frame[c];
        m = std::max(m, std::fabs(static_cast<double>(frame[c])));
        m = std::max(m, interpolarMax(hist_[static_cast<std::size_t>(c)], histPos_));
    }
    return m;
}

void LimitadorTP::procesar(float* datos, std::size_t frames) {
    if (canales_ <= 0) return;
    const auto ch = static_cast<std::size_t>(canales_);
    // Un poco por debajo del techo: el interpolador no es perfecto y la ganancia
    // varía dentro de un pico.
    const double techo = techoLin_ * 0.995;
    for (std::size_t i = 0; i < frames; ++i) {
        float* fr = datos + i * ch;
        // 1. La ganancia que necesita esta muestra (por su true peak).
        const double p = picoVerdadero(fr);
        const float g = static_cast<float>(activo_ && p > techo ? techo / p : 1.0);
        // 2. Mínimo de las últimas L+1.
        while (!minimos_.empty() && minimos_.back().second >= g) minimos_.pop_back();
        minimos_.emplace_back(n_, g);
        while (minimos_.front().first + lookahead_ < n_) minimos_.pop_front();
        const float h = minimos_.front().second;
        // 3. Promedio de las últimas L.
        hSuma_ += h - hBuf_[hPos_];
        hBuf_[hPos_] = h;
        hPos_ = (hPos_ + 1) % lookahead_;
        const double a = hSuma_ / static_cast<double>(lookahead_);
        // La liberación: baja enseguida, sube despacio.
        g_ = a < g_ ? a : g_ + (a - g_) * coefLib_;
        // Sale la muestra de hace L, con la ganancia.
        float* d = &demoraBuf_[demoraPos_ * ch];
        for (std::size_t c = 0; c < ch; ++c) {
            const float salida = d[c];
            d[c] = fr[c];
            fr[c] = static_cast<float>(salida * g_);
        }
        demoraPos_ = (demoraPos_ + 1) % lookahead_;
        ++n_;
        // Cada tanto, que la suma no acumule error de redondeo.
        if ((n_ & 0xFFFF) == 0) {
            hSuma_ = 0;
            for (float x : hBuf_) hSuma_ += x;
        }
    }
    redActual_ = g_ < 1.0 ? 20.0 * std::log10(std::max(g_, 1e-6)) : 0.0;
    redMax_ = std::min(redMax_, redActual_);
}

double LimitadorTP::reduccionDb() const { return redActual_; }

double LimitadorTP::reduccionMaxDb() {
    const double r = redMax_;
    redMax_ = 0;
    return r;
}

void LimitadorTP::MedidorTP::preparar(int canales) {
    canales_ = std::max(1, canales);
    hist_.assign(static_cast<std::size_t>(canales_), std::vector<float>(kTapsPorFase, 0.0f));
    pos_ = 0;
}

double LimitadorTP::MedidorTP::medir(const float* datos, std::size_t frames) {
    double m = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        pos_ = (pos_ + 1) % kTapsPorFase;
        for (int c = 0; c < canales_; ++c) {
            const float x = datos[i * static_cast<std::size_t>(canales_) + static_cast<std::size_t>(c)];
            hist_[static_cast<std::size_t>(c)][pos_] = x;
            m = std::max(m, std::fabs(static_cast<double>(x)));
            m = std::max(m, interpolarMax(hist_[static_cast<std::size_t>(c)], pos_));
        }
    }
    return m;
}

} // namespace ba::dsp
