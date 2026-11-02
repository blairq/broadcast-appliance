// ============================================================================
// src/core/dsp/LimitadorTP.cpp — ver LimitadorTP.hpp
// ============================================================================
#include "dsp/LimitadorTP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

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

/// La mayor ganancia posible del interpolador: |y| ≤ esto × el mayor |x| de su historia.
double gananciaMaxima() {
    static const double g = [] {
        const auto& c = coeficientes();
        double m = 0;
        for (const auto& fase : c) {
            double s = 0;
            for (float x : fase) s += std::fabs(static_cast<double>(x));
            m = std::max(m, s);
        }
        return m * (1 + 1e-9); // el redondeo de la suma (en double) no la puede pasar
    }();
    return g;
}

double interpolarMax(const std::vector<float>& h, std::size_t pos) {
    // h guarda la historia dos veces seguidas (2 × kTapsPorFase): la más nueva en pos y en
    // pos + kTapsPorFase. Las 12 de cada fase se leen contiguas, de la más nueva para atrás
    // (lo mismo, en el mismo orden, que la cuenta circular).
    const auto& c = coeficientes();
    const float* x = h.data() + pos + kTapsPorFase;
    double m = 0;
    for (int f = 0; f < kFases; ++f) {
        double y = 0;
        for (int t = 0; t < kTapsPorFase; ++t) y += c[f][t] * x[-t];
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
    hist_.assign(static_cast<std::size_t>(canales_), std::vector<float>(2 * kTapsPorFase, 0.0f));
    histPos_ = 0;
    demoraBuf_.assign(lookahead_ * static_cast<std::size_t>(canales_), 0.0f);
    demoraPos_ = 0;
    minimos_.clear();
    hBuf_.assign(lookahead_, 1.0f);
    hPos_ = 0;
    hSuma_ = static_cast<double>(lookahead_);
    unos_ = lookahead_;
    n_ = 0;
    g_ = 1.0;
    ponerTecho(techoDb_);
    ponerLiberacion(liberacionMs_);
}

void LimitadorTP::guardarHistoria(const float* frame) {
    histPos_ = (histPos_ + 1) % kTapsPorFase;
    for (int c = 0; c < canales_; ++c) {
        std::vector<float>& h = hist_[static_cast<std::size_t>(c)];
        h[histPos_] = h[histPos_ + kTapsPorFase] = frame[c];
    }
}

double LimitadorTP::picoVerdadero(const float* frame) {
    guardarHistoria(frame);
    double m = 0;
    for (int c = 0; c < canales_; ++c) {
        m = std::max(m, std::fabs(static_cast<double>(frame[c])));
        m = std::max(m, interpolarMax(hist_[static_cast<std::size_t>(c)], histPos_));
    }
    return m;
}

bool LimitadorTP::bajoElTecho(const float* datos, std::size_t frames, double techo) const {
    // Cada pico del bloque sale de 12 muestras: las del bloque y las de antes (la historia).
    float m = 0;
    for (const auto& h : hist_)
        for (float x : h) m = std::max(m, std::fabs(x));
    const std::size_t n = frames * static_cast<std::size_t>(canales_);
    for (std::size_t i = 0; i < n; ++i) m = std::max(m, std::fabs(datos[i]));
    return static_cast<double>(m) * gananciaMaxima() <= techo; // NaN: false (por el camino de siempre)
}

bool LimitadorTP::quieto() const {
    return g_ >= 1.0 - 1e-9 && unos_ >= lookahead_ && minimos_.size() <= 1 && (minimos_.empty() || minimos_.front().second == 1.0f);
}

void LimitadorTP::soloDemorar(float* datos, std::size_t frames) {
    // Lo mismo que el bucle con g = 1 en cada muestra: la ventana queda en 1 (una sola
    // entrada en el mínimo, la suma en L) y sale la muestra de hace L tal cual. (g_ estaba
    // a menos de 1e-9 de 1: lo que cambia es menos que el redondeo del float.)
    const auto ch = static_cast<std::size_t>(canales_);
    // La historia del interpolador, antes de que la demora pise el bloque: solo cuentan
    // las últimas 12, cada una en el lugar que le habría tocado.
    const std::size_t k = std::min<std::size_t>(frames, kTapsPorFase);
    histPos_ = (histPos_ + frames - k) % kTapsPorFase;
    for (std::size_t j = frames - k; j < frames; ++j) guardarHistoria(datos + j * ch);
    for (std::size_t i = 0; i < frames; ++i) {
        float* fr = datos + i * ch;
        float* d = &demoraBuf_[demoraPos_ * ch];
        for (std::size_t c = 0; c < ch; ++c) std::swap(d[c], fr[c]);
        if (++demoraPos_ == lookahead_) demoraPos_ = 0;
    }
    n_ += frames;
    hPos_ = (hPos_ + frames) % lookahead_;
    unos_ += frames;
    minimos_.clear();
    minimos_.emplace_back(n_ - 1, 1.0f);
    hSuma_ = static_cast<double>(lookahead_);
    g_ = 1.0;
}

template <bool conPico>
void LimitadorTP::procesarCon(float* datos, std::size_t frames, double techo) {
    const auto ch = static_cast<std::size_t>(canales_);
    for (std::size_t i = 0; i < frames; ++i) {
        float* fr = datos + i * ch;
        // 1. La ganancia que necesita esta muestra (por su true peak). Sin pico: el bloque
        // entero está bajo el techo (bajoElTecho), la ganancia es 1.
        float g = 1.0f;
        if constexpr (conPico) {
            const double p = picoVerdadero(fr);
            g = static_cast<float>(activo_ && p > techo ? techo / p : 1.0);
        } else {
            guardarHistoria(fr);
        }
        // 2. Mínimo de las últimas L+1.
        while (!minimos_.empty() && minimos_.back().second >= g) minimos_.pop_back();
        minimos_.emplace_back(n_, g);
        while (minimos_.front().first + lookahead_ < n_) minimos_.pop_front();
        const float h = minimos_.front().second;
        unos_ = h == 1.0f ? unos_ + 1 : 0;
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
}

void LimitadorTP::procesar(float* datos, std::size_t frames) {
    if (canales_ <= 0 || frames == 0) return;
    // Un poco por debajo del techo: el interpolador no es perfecto y la ganancia
    // varía dentro de un pico.
    const double techo = techoLin_ * 0.995;
    const bool sinPicos = !activo_ || bajoElTecho(datos, frames, techo);
    if (sinPicos && quieto())
        soloDemorar(datos, frames);
    else if (sinPicos)
        procesarCon<false>(datos, frames, techo);
    else
        procesarCon<true>(datos, frames, techo);
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
    hist_.assign(static_cast<std::size_t>(canales_), std::vector<float>(2 * kTapsPorFase, 0.0f));
    pos_ = 0;
}

double LimitadorTP::MedidorTP::medir(const float* datos, std::size_t frames) {
    double m = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        pos_ = (pos_ + 1) % kTapsPorFase;
        for (int c = 0; c < canales_; ++c) {
            const float x = datos[i * static_cast<std::size_t>(canales_) + static_cast<std::size_t>(c)];
            std::vector<float>& h = hist_[static_cast<std::size_t>(c)];
            h[pos_] = h[pos_ + kTapsPorFase] = x;
            m = std::max(m, std::fabs(static_cast<double>(x)));
            m = std::max(m, interpolarMax(hist_[static_cast<std::size_t>(c)], pos_));
        }
    }
    return m;
}

} // namespace ba::dsp
