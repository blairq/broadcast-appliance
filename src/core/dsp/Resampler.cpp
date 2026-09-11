// Resampler.cpp
//
// Implementacion del re-muestreador de audio sobre libsamplerate (Secret
// Rabbit Code). Es un envoltorio RAII fino: `configure()` crea el estado
// (camino de control, unica asignacion de memoria) y `resample()` lo recorre
// en estado estacionario (camino de RT, sin asignaciones).
//
// Mapeo de calidad (Resampler::Quality -> SRC_CONVERSION_QUALITY):
//   Linear     -> SRC_LINEAR
//   SincMedium -> SRC_SINC_MEDIUM_QUALITY
//   SincBest   -> SRC_SINC_BEST_QUALITY
//
// Formato de buffer: INTERLEAVED (canal 0, canal 1, ... por frame). Con
// `channels == 1` es plano, que es el caso comun de los tests.

#include "Resampler.hpp"

#include <samplerate.h>

namespace ba::dsp {

namespace {

constexpr int kDefaultRate = 48000;

/// Mapea la calidad de la API al valor de interpolador de libsamplerate.
/// <samplerate.h> declara el enum de forma anónima (sin typedef), por eso se
/// devuelve `int` (los valores SRC_* son constantes enteras).
int toSrcQuality(Quality q) {
    switch (q) {
        case Quality::Linear: return SRC_LINEAR;
        case Quality::SincMedium: return SRC_SINC_MEDIUM_QUALITY;
        case Quality::SincBest: return SRC_SINC_BEST_QUALITY;
    }
    // Nunca se llega aqui (Quality solo tiene esos tres valores); se devuelve
    // la mejor calidad como default defensivo.
    return SRC_SINC_BEST_QUALITY;
}

} // namespace

Resampler::Resampler() = default;

Resampler::~Resampler() {
    if (state_ != nullptr) {
        src_delete(state_);
        state_ = nullptr;
    }
}

void Resampler::configure(int inRate, int outRate, int channels, Quality q) {
    // Clamps defensivos (ver docstring): las frecuencias deben ser > 0 y los
    // canales >= 1. Si no, se corrigen a valores validos para que el objeto
    // quede siempre en un estado util.
    if (inRate <= 0) {
        inRate = kDefaultRate;
    }
    if (outRate <= 0) {
        outRate = kDefaultRate;
    }
    if (channels < 1) {
        channels = 1;
    }
    channels_ = channels;

    // Si ya habia un estado previo (re-configuracion), se destruye antes de
    // crear el nuevo para no fugar memoria.
    if (state_ != nullptr) {
        src_delete(state_);
        state_ = nullptr;
    }

    // Crea el estado de libsamplerate. `src_new` toma (tipo, canales, error)
    // y devuelve nullptr si no puede (p. ej. memoria insuficiente); en ese caso
    // se deja state_ en nullptr y resample() fallara de forma segura. El ratio
    // no se fija aqui: viaja en cada SRC_DATA.src_ratio (ver resample()).
    ratio_ = static_cast<double>(outRate) / static_cast<double>(inRate);
    int err = 0;
    state_ = src_new(toSrcQuality(q), channels, &err);
    if (state_ == nullptr) {
        // src_new fallo: se deja el objeto en estado vacio (resample() -> false).
        (void)err;
    }
}

bool Resampler::resample(const float* in, size_t inFrames, float* out, size_t maxOutFrames,
                         size_t* outFrames) {
    // Pre-condiciones de la API: sin estado o sin buffers no hay nada que
    // re-muestrear. Se deja *outFrames en 0 para que el llamador sepa que no
    // se escribio nada.
    if (outFrames != nullptr) {
        *outFrames = 0;
    }
    if (state_ == nullptr || in == nullptr || out == nullptr || inFrames == 0) {
        return false;
    }

    SRC_DATA data;
    data.data_in = in;
    data.input_frames = static_cast<long>(inFrames);
    data.data_out = out;
    data.output_frames = static_cast<long>(maxOutFrames);
    // El ratio viaja en cada SRC_DATA (src_new no lo toma como argumento).
    data.src_ratio = ratio_;
    // end_of_input: 0 = hay mas datos de entrada despues de este bloque
    // (estado estacionario). 1 = es el ultimo bloque; aqui siempre 0.
    data.end_of_input = 0;

    const int err = src_process(state_, &data);
    if (err != 0) {
        // Error de libsamplerate (p. ej. buffer de salida insuficiente).
        // `data.output_frames` queda con la cantidad de frames que HUBIERA
        // escrito, de modo que el llamador sepa cuanto ampliar el buffer.
        if (outFrames != nullptr) {
            *outFrames = static_cast<size_t>(data.output_frames);
        }
        return false;
    }

    if (outFrames != nullptr) {
        *outFrames = static_cast<size_t>(data.output_frames);
    }
    return true;
}

} // namespace ba::dsp
