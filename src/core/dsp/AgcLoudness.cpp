// src/core/dsp/AgcLoudness.cpp — ver AgcLoudness.hpp
#include "dsp/AgcLoudness.hpp"

#include <algorithm>
#include <cmath>

namespace ba::dsp {

void AgcLoudness::preparar(int canales, int tasa) {
    canales_ = std::max(1, canales);
    tasa_ = tasa > 0 ? tasa : 48000;
    k_.preparar(tasa_);
    bloques_.fill(0);
    nBloques_ = posBloque_ = 0;
    cortoLufs_ = -120;
    congelado_ = true;
    // La ganancia se conserva: preparar de nuevo (otras caps) no es un salto.
}

void AgcLoudness::procesar(float* datos, std::size_t frames) {
    // Apagado y ya en 0 dB: nada que hacer (ni medir: al prenderlo, en 3 s ya mide).
    if (!activo_ && gananciaDb_ == 0) {
        nBloques_ = 0;
        congelado_ = true;
        return;
    }
    // La ganancia se mueve de a tramos de 32 cuadros (0,67 ms): a 1 dB/s, el paso
    // de un tramo es de 0,0007 dB, inaudible, y el pow() queda fuera del bucle fino.
    constexpr std::size_t kTramo = 32;
    const double paso = o_.velocidadDbS * static_cast<double>(kTramo) / tasa_;
    for (std::size_t inicio = 0; inicio < frames; inicio += kTramo) {
        const std::size_t fin = std::min(frames, inicio + kTramo);
        // Medir lo que ENTRA (antes de la ganancia): el lazo es abierto y no se
        // persigue a sí mismo.
        for (std::size_t f = inicio; f < fin; ++f) {
            const float* x = datos + f * static_cast<std::size_t>(canales_);
            double e = 0;
            if (k_.muestra(x[0], x[canales_ > 1 ? 1 : 0], e)) {
                // Un bloque de 100 ms en silencio congela en el acto y NO entra a la
                // ventana (la compuerta de BS.1770, acá para el control): la pausa no
                // arrastra el corto hacia abajo, y al volver la voz se sigue desde
                // donde estaba, sin una subida de 3 s buscando el ruido de fondo.
                congelado_ = PonderacionK::aLufs(e) < o_.objetivoLufs - o_.silencioLu;
                if (congelado_) continue;
                bloques_[posBloque_] = e;
                posBloque_ = (posBloque_ + 1) % bloques_.size();
                nBloques_ = std::min(nBloques_ + 1, bloques_.size());
                if (nBloques_ == bloques_.size()) {
                    double s = 0;
                    for (double b : bloques_) s += b;
                    cortoLufs_ = PonderacionK::aLufs(s / static_cast<double>(bloques_.size()));
                    deseadaDb_ = std::clamp(o_.objetivoLufs - cortoLufs_, -o_.rangoDb, o_.rangoDb);
                }
            }
        }
        const double meta = activo_ ? deseadaDb_ : 0.0;
        const double antes = gananciaDb_;
        if (!activo_ || !congelado_) gananciaDb_ += std::clamp(meta - gananciaDb_, -paso, paso);
        if (antes == 0 && gananciaDb_ == 0) continue;
        // Del valor de antes al de ahora a lo largo del tramo (sin escalones).
        const double g0 = std::pow(10.0, antes / 20.0), g1 = std::pow(10.0, gananciaDb_ / 20.0);
        const double n = static_cast<double>(fin - inicio);
        for (std::size_t f = inicio; f < fin; ++f) {
            const auto g = static_cast<float>(g0 + (g1 - g0) * static_cast<double>(f - inicio + 1) / n);
            float* x = datos + f * static_cast<std::size_t>(canales_);
            for (int c = 0; c < canales_; ++c) x[c] *= g;
        }
    }
}

} // namespace ba::dsp
