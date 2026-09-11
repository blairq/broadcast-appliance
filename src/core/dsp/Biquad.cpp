// Biquad.cpp
//
// Implementacion de las formulas del "Audio EQ Cookbook" de Robert
// Bristow-Johnson (RBJ) para biquads de segundo orden. Todas las formulas
// estan en forma A0-normalizada (se divide por a0 para que a0 == 1) y se
// procesan en forma directa II transpuesta.
//
// Fuente de las formulas: RBJ, "Audio EQ Cookbook",
// https://www.musicdsp.org/en/latest/Filtering/149-Audio-EQ-Cookbook.html
// (bilinear transform, Q-based). Para los shelves se usa la variante Q-based
// (alpha = sw/(2*Q)), que es la forma estandar y numericamente estable; el
// cookbook tambien documenta una variante por "shelf slope" S, pero nuestra
// API expone Q, por lo que usamos Q.
//
// Nota de estabilidad: con los clamps de setParams (fs>0, 0<freq<fs/2, Q>0)
// los polos quedan siempre dentro del circulo unitario, de modo que el filtro
// es estable para cualquier senal de prueba.

#include "Biquad.hpp"

#include <cmath>

namespace ba::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultFs = 48000.0;
constexpr double kMinQ = 0.5;

/// Recorta `freq` a un rango seguro (0, fs/2) con un margen para que w0 no
/// se pegue a 0 ni a pi (donde el cookbook degenera). Devuelve la frecuencia
/// efectiva en Hz.
double clampFreq(double freq, double fs) {
    const double nyquist = fs * 0.5;
    // Margen de 1 Hz por debajo del Nyquist y 1 Hz por encima de 0.
    const double lo = 1.0;
    const double hi = nyquist - 1.0;
    if (freq < lo) {
        return lo;
    }
    if (freq > hi) {
        return hi;
    }
    return freq;
}

} // namespace

void Biquad::setParams(BiquadType type, double fs, double freq, double gainDb, double q) {
    // Clamps defensivos (ver docstring de la clase).
    if (fs <= 0.0) {
        fs = kDefaultFs;
    }
    if (freq <= 0.0) {
        freq = 1.0;
    }
    if (q <= 0.0) {
        q = kMinQ;
    }
    freq = clampFreq(freq, fs);

    // Variables comunes del cookbook.
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double t = 2.0 * std::sqrt(A) * alpha; // solo shelves

    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
        case BiquadType::LowPass:
            b0 = (1.0 - cw) / 2.0;
            b1 = 1.0 - cw;
            b2 = (1.0 - cw) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;

        case BiquadType::HighPass:
            b0 = (1.0 + cw) / 2.0;
            b1 = -(1.0 + cw);
            b2 = (1.0 + cw) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;

        case BiquadType::BandPass:
            // Pasa banda resonante (pico ~0 dB), alpha = sw/(2Q).
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha;
            break;

        case BiquadType::Peaking:
            b0 = 1.0 + A * alpha;
            b1 = -2.0 * cw;
            b2 = 1.0 - A * alpha;
            a0 = 1.0 + A * alpha;
            a1 = -2.0 * cw;
            a2 = 1.0 - A * alpha;
            break;

        case BiquadType::LowShelf:
            b0 = A * ((A + 1.0) - (A - 1.0) * cw + t);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            b2 = A * ((A + 1.0) - (A - 1.0) * cw - t);
            a0 = (A + 1.0) + (A - 1.0) * cw + t;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            a2 = (A + 1.0) + (A - 1.0) * cw - t;
            break;

        case BiquadType::HighShelf:
            b0 = A * ((A + 1.0) + (A - 1.0) * cw + t);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            b2 = A * ((A + 1.0) + (A - 1.0) * cw - t);
            a0 = (A + 1.0) - (A - 1.0) * cw + t;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
            a2 = (A + 1.0) - (A - 1.0) * cw - t;
            break;
    }

    // Normaliza por a0 (A0-normalizado) y guarda como float.
    const double invA0 = 1.0 / a0;
    b0_ = static_cast<float>(b0 * invA0);
    b1_ = static_cast<float>(b1 * invA0);
    b2_ = static_cast<float>(b2 * invA0);
    a1_ = static_cast<float>(a1 * invA0);
    a2_ = static_cast<float>(a2 * invA0);
}

float Biquad::process(float x) {
    // Forma directa II transpuesta (a0 == 1). Realiza exactamente
    //   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
    // con dos registros de estado:
    //   x1_ = w1[n-1]   (estado w1 atrasado)
    //   x2_ = w2[n-1]   (estado w2 atrasado)
    //
    //   y[n]    = b0*x[n] + w1[n-1]
    //   w1[n]   = b1*x[n] - a1*y[n] + w2[n-1]
    //   w2[n]   = b2*x[n] - a2*y[n]
    //
    // La contribucion de w2 entra a traves de la actualizacion de w1 (no
    // directamente en la suma de salida); asi el filtro reproduce la funcion
    // de transferencia con solo dos registros.
    const float y = b0_ * x + x1_;

    // Actualiza el estado DF2T.
    const float w1 = b1_ * x - a1_ * y + x2_;
    const float w2 = b2_ * x - a2_ * y;
    x1_ = w1;
    x2_ = w2;

    return y;
}

void Biquad::reset() {
    x1_ = 0.0f;
    x2_ = 0.0f;
}

} // namespace ba::dsp
