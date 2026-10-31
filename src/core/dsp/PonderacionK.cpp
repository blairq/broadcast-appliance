// src/core/dsp/PonderacionK.cpp — ver PonderacionK.hpp
#include "dsp/PonderacionK.hpp"

#include <cmath>

namespace ba::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

void PonderacionK::preparar(double tasa) {
    // K-weighting (BS.1770-4) para cualquier tasa: estante alto y pasa altos.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / tasa), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
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
        const double K = std::tan(kPi * f0 / tasa);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0;
        b.b1 = -2.0;
        b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        pasaAltos_ = {b, b};
    }
    porBloque_ = static_cast<std::uint32_t>(tasa / 10.0);
    reiniciar();
}

void PonderacionK::reiniciar() {
    for (auto& b : estante_) b.z1 = b.z2 = 0;
    for (auto& b : pasaAltos_) b.z1 = b.z2 = 0;
    enBloque_ = 0;
    suma_ = 0;
}

double PonderacionK::aLufs(double energia) { return energia > 0 ? -0.691 + 10.0 * std::log10(energia) : -120.0; }
double PonderacionK::aEnergia(double lufs) { return std::pow(10.0, (lufs + 0.691) / 10.0); }

} // namespace ba::dsp
