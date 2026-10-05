// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio/LoudnessPrograma.hpp"

#include <algorithm>
#include <cmath>

namespace ba::app::audio {

namespace {
constexpr double kPi = 3.14159265358979323846;

double aLufs(double energia) { return energia > 0 ? -0.691 + 10.0 * std::log10(energia) : -120.0; }
double aDb(double lineal) { return lineal > 0 ? 20.0 * std::log10(lineal) : -120.0; }

} // namespace

LoudnessPrograma::LoudnessPrograma(double tasa) : tasa_(tasa) {
    // K-weighting (BS.1770-4) para cualquier tasa: estante alto y pasa altos.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / tasa_), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0;
        b.b1 = 2.0 * (K * K - Vh) / a0;
        b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        estante_ = {b, b};
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / tasa_);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0;
        b.b1 = -2.0;
        b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        pasaAltos_ = {b, b};
    }
    porBloque_ = static_cast<std::uint32_t>(tasa_ / 10.0);
    // FIR de interpolación x4: sinc con ventana de Blackman, 48 taps en 4 fases.
    for (int fase = 0; fase < 4; ++fase)
        for (int t = 0; t < kTaps; ++t) {
            const int n = t * 4 + fase;
            const double x = (n - 23.5) / 4.0;
            const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * n / 47.0) + 0.08 * std::cos(4 * kPi * n / 47.0);
            fir_[fase][t] = static_cast<float>(sinc * w);
        }
}

void LoudnessPrograma::resetInterno() {
    for (auto& b : estante_) b.z1 = b.z2 = 0;
    for (auto& b : pasaAltos_) b.z1 = b.z2 = 0;
    enBloque_ = 0;
    suma_[0] = suma_[1] = 0;
    bloques_.clear();
    hist400_.fill(0);
    histCorto_.fill(0);
    tpMax_ = tpVentana_ = 0;
    segundos_ = 0;
    std::lock_guard<std::mutex> l(mu_);
    foto_ = Foto{};
}

void LoudnessPrograma::alimentar(const float* const* datos, int canales, std::uint32_t cuadros) {
    if (pedirReset_.exchange(false)) resetInterno();
    const int nc = std::clamp(canales, 1, 2);
    for (std::uint32_t k = 0; k < cuadros; ++k) {
        for (int c = 0; c < 2; ++c) {
            const float x = datos[std::min(c, nc - 1)][k];
            const double y = pasaAltos_[c].paso(estante_[c].paso(x));
            suma_[c] += y * y;
            // True peak: la muestra y las 3 interpoladas entre la anterior y esta.
            hist_[c][posHist_] = x;
        }
        for (int c = 0; c < nc; ++c)
            for (int fase = 0; fase < 4; ++fase) {
                float acc = 0;
                for (int t = 0; t < kTaps; ++t) acc += fir_[fase][t] * hist_[c][(posHist_ - t + kTaps) % kTaps];
                const double a = std::abs(static_cast<double>(acc));
                if (a > tpVentana_) tpVentana_ = a;
            }
        posHist_ = (posHist_ + 1) % kTaps;
        if (++enBloque_ >= porBloque_) cerrarBloque();
    }
}

void LoudnessPrograma::cerrarBloque() {
    // Energía K del bloque de 100 ms (L + R, pesos 1).
    const double e = (suma_[0] + suma_[1]) / static_cast<double>(enBloque_);
    suma_[0] = suma_[1] = 0;
    enBloque_ = 0;
    segundos_ += 0.1;
    bloques_.push_back(e);
    while (bloques_.size() > 30) bloques_.pop_front();
    const auto media = [this](std::size_t n) {
        const std::size_t k = std::min(n, bloques_.size());
        double s = 0;
        for (std::size_t i = bloques_.size() - k; i < bloques_.size(); ++i) s += bloques_[i];
        return k > 0 ? s / static_cast<double>(k) : 0.0;
    };
    const double e400 = media(4), e3s = media(30);
    const double s = aLufs(e3s);
    const auto bin = [](double l) { return std::clamp(static_cast<int>((l + 70.0) * 10.0), 0, kBins - 1); };
    const auto centro = [](int b) { return -70.0 + (b + 0.5) / 10.0; };
    const auto energiaDe = [](double l) { return std::pow(10.0, (l + 0.691) / 10.0); };
    if (bloques_.size() >= 4 && aLufs(e400) > -70) ++hist400_[bin(aLufs(e400))];
    if (bloques_.size() >= 30 && s > -70) ++histCorto_[bin(s)];
    // Integrado: compuerta absoluta -70 LUFS (ya en el histograma) y relativa -10 LU.
    double integrado = -120;
    {
        double sum = 0;
        std::uint64_t n = 0;
        for (int b = 0; b < kBins; ++b)
            if (hist400_[b]) sum += hist400_[b] * energiaDe(centro(b)), n += hist400_[b];
        if (n > 0) {
            const double rel = aLufs(sum / static_cast<double>(n)) - 10.0;
            double s2 = 0;
            std::uint64_t n2 = 0;
            for (int b = 0; b < kBins; ++b)
                if (hist400_[b] && centro(b) > rel) s2 += hist400_[b] * energiaDe(centro(b)), n2 += hist400_[b];
            if (n2 > 0) integrado = aLufs(s2 / static_cast<double>(n2));
        }
    }
    // LRA: corto con compuerta relativa -20 LU; P95 - P10 (EBU Tech 3342).
    double lra = 0;
    {
        double sum = 0;
        std::uint64_t n = 0;
        for (int b = 0; b < kBins; ++b)
            if (histCorto_[b]) sum += histCorto_[b] * energiaDe(centro(b)), n += histCorto_[b];
        if (n > 1) {
            const double rel = aLufs(sum / static_cast<double>(n)) - 20.0;
            std::uint64_t tot = 0;
            for (int b = 0; b < kBins; ++b)
                if (centro(b) > rel) tot += histCorto_[b];
            if (tot > 1) {
                const auto valorEn = [&](double p) {
                    const auto objetivo = static_cast<std::uint64_t>(p * static_cast<double>(tot - 1));
                    std::uint64_t acc = 0;
                    for (int b = 0; b < kBins; ++b) {
                        if (centro(b) <= rel) continue;
                        acc += histCorto_[b];
                        if (acc > objetivo) return centro(b);
                    }
                    return centro(kBins - 1);
                };
                lra = valorEn(0.95) - valorEn(0.10);
            }
        }
    }
    tpMax_ = std::max(tpMax_, tpVentana_);
    std::lock_guard<std::mutex> l(mu_);
    foto_.m = aLufs(e400);
    foto_.s = s;
    foto_.i = integrado;
    foto_.tp = aDb(tpMax_);
    foto_.tpReciente = aDb(tpVentana_);
    foto_.lra = lra;
    foto_.segundos = segundos_;
    foto_.grDb = gr_.load();
    tpVentana_ *= 0.7; // el reciente decae (~400 ms)
}

LoudnessPrograma::Foto LoudnessPrograma::foto() const {
    std::lock_guard<std::mutex> l(mu_);
    Foto f = foto_;
    f.grDb = gr_.load();
    return f;
}

LoudnessPrograma::Objetivo LoudnessPrograma::objetivo() const {
    std::lock_guard<std::mutex> l(mu_);
    return obj_;
}

std::string LoudnessPrograma::ponerObjetivo(const nlohmann::json& o) {
    const std::string p = o.value("perfil", std::string("custom"));
    Objetivo n;
    n.perfil = p;
    if (p == "r128") n.lufs = -23, n.tp = -1;
    else if (p == "a85") n.lufs = -24, n.tp = -2;
    else if (p == "plataforma16") n.lufs = -16, n.tp = -1;
    else if (p == "plataforma14") n.lufs = -14, n.tp = -1;
    else if (p == "custom") {
        n.lufs = o.value("lufs", -23.0);
        n.tp = o.value("tp", -1.0);
        if (n.lufs > -5 || n.lufs < -40 || n.tp > 0 || n.tp < -10) return "fuera de rango (lufs -40..-5, tp -10..0)";
    } else {
        return "perfil: r128, a85, plataforma16, plataforma14 o custom";
    }
    std::lock_guard<std::mutex> l(mu_);
    obj_ = n;
    return {};
}

nlohmann::json LoudnessPrograma::json() const {
    const Foto f = foto();
    const Objetivo o = objetivo();
    const auto r = [](double x) { return std::round(x * 10.0) / 10.0; };
    return {{"m", r(f.m)},
            {"s", r(f.s)},
            {"i", r(f.i)},
            {"tp", r(f.tp)},
            {"tp_reciente", r(f.tpReciente)},
            {"lra", r(f.lra)},
            {"segundos", std::round(f.segundos)},
            {"gr_db", r(f.grDb)},
            {"objetivo", {{"perfil", o.perfil}, {"lufs", o.lufs}, {"tp", o.tp}}},
            {"fuera_de_objetivo", f.i > -70 && (std::abs(f.i - o.lufs) > 2.0 || f.tp > o.tp)}};
}

} // namespace ba::app::audio
