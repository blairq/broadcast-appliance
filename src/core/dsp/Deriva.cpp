// src/core/dsp/Deriva.cpp — ver Deriva.hpp
#include "dsp/Deriva.hpp"

#include <algorithm>
#include <cmath>

namespace ba::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Deriva::configurar(const Opciones& o) {
    o_ = o;
    o_.frecuencia = std::max(1, o_.frecuencia);
    o_.canales = std::max(1, o_.canales);
    rs_.configure(o_.frecuencia, o_.frecuencia, o_.canales, o_.calidad);
    periodo_ = 1.0 / o_.frecuencia;
    reiniciar();
}

void Deriva::reiniciar() {
    iniciado_ = enganchado_ = faseFijada_ = corteProximo_ = false;
    desde_ = salidas_ = fase0_ = desfaseS_ = 0;
    razon_ = 1;
    rs_.setRatio(1);
    rs_.reset();
}

std::size_t Deriva::capacidadPara(std::size_t n) const {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(n) * (1.0 + o_.topePpm * 2e-6))) + 64;
}

void Deriva::empezar(std::size_t n, double llegadaS) {
    // El período estimado se conserva (una placa que se reancla sigue siendo la
    // misma placa): solo se vuelve a contar desde acá.
    fin_ = llegadaS;
    t0_ = llegadaS - static_cast<double>(n) * periodo_;
    salidas_ = 0;
    faseFijada_ = false;
    desfaseS_ = 0;
    rs_.reset();
    iniciado_ = true;
}

std::size_t Deriva::procesar(const float* in, std::size_t n, double llegadaS, float* out, std::size_t cap, double& ptsS,
                             bool& corte) {
    corte = corteProximo_;
    corteProximo_ = false;
    if (n == 0) return 0;
    const double R = o_.frecuencia;
    if (!iniciado_) {
        empezar(n, llegadaS);
        corte = true;
    } else {
        // El lazo: dónde tendría que terminar este bloque según lo estimado, y
        // cuánto se equivocó. Un error enorme es un corte (la placa se trabó o se
        // reconectó): ahí no se filtra nada, se vuelve a empezar.
        const double pred = fin_ + static_cast<double>(n) * periodo_;
        const double e = llegadaS - pred;
        if (std::fabs(e) > o_.huecoS) {
            ++reanclajes_;
            empezar(n, llegadaS);
            corte = true;
        } else {
            // Ancho de banda: de 1 Hz al enganchar a 0,02 Hz en unos segundos.
            const double ancho =
                o_.anchoHz + (o_.anchoInicialHz - o_.anchoHz) * std::exp(-desde_ / std::max(0.1, o_.engancheS / 5));
            const double w = 2 * kPi * ancho * static_cast<double>(n) * periodo_;
            const double b = std::sqrt(2.0) * w, c = w * w;
            fin_ = pred + b * e;
            periodo_ += c * e / static_cast<double>(n);
            // Una estimación absurda (un reloj que saltó) no se acepta.
            const double nominal = 1.0 / R;
            periodo_ = std::clamp(periodo_, nominal * (1 - o_.topePpm * 1e-6), nominal * (1 + o_.topePpm * 1e-6));
        }
    }
    desde_ += static_cast<double>(n) / R;
    enganchado_ = desde_ >= o_.engancheS;

    // La razón: la del lazo (entregar R cuadros por segundo del estudio) más la
    // corrección lenta de la fase que se fue juntando.
    const double base = periodo_ * R;
    const double correccion = enganchado_ && faseFijada_ ? -desfaseS_ / std::max(1.0, o_.faseS) : 0.0;
    razon_ = std::clamp(base * (1 + correccion), 1 - o_.topePpm * 1e-6, 1 + o_.topePpm * 1e-6);
    ppm_ = (1.0 / base - 1.0) * 1e6;

    rs_.setRatio(razon_);
    std::size_t hechos = 0;
    if (!rs_.resample(in, n, out, cap, &hechos)) hechos = 0;
    // El sello: el cuadro s de la salida es la hora t0 + s/R del estudio (el
    // remuestreador pone lo de la placa sobre esa grilla).
    ptsS = t0_ + salidas_ / R;
    salidas_ += static_cast<double>(hechos);

    // La fase: lo que salió contra lo que pasó en el reloj del estudio. Al
    // enganchar se fija (incluye lo que el remuestreador retiene: es latencia,
    // fija); después, lo que se aparte de ahí es error de la razón y se corrige.
    const double fase = salidas_ / R - (fin_ - t0_);
    if (!faseFijada_) {
        fase0_ = fase;
        faseFijada_ = enganchado_;
    }
    desfaseS_ = fase - fase0_;
    if (faseFijada_ && std::fabs(desfaseS_) > o_.reanclarS) {
        // La red de seguridad: algo que el lazo no pudo seguir. El bloque que
        // sigue empieza de nuevo.
        ++reanclajes_;
        empezar(0, llegadaS);
        corteProximo_ = true;
    }
    return hechos;
}

double Deriva::latenciaS() const { return faseFijada_ ? std::max(0.0, -fase0_) : 0.0; }

} // namespace ba::dsp
