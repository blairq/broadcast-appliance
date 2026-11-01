// src/core/platform/Relanzador.cpp — ver Relanzador.hpp

#include "platform/Relanzador.hpp"

#include <algorithm>

namespace ba::platform {

std::function<std::uint64_t(int)> PoliticaRelanzar::duplicando(std::uint64_t primeroMs,
                                                                std::function<std::uint64_t()> topeMs) {
    return [primeroMs, topeMs = std::move(topeMs)](int n) {
        const std::uint64_t tope = topeMs ? topeMs() : primeroMs;
        std::uint64_t e = primeroMs;
        for (int i = 0; i < n && e < tope; ++i) e *= 2;
        return std::min(e, std::max(tope, primeroMs));
    };
}

std::uint64_t Relanzador::caida(std::uint64_t ahoraMs) {
    ++caidas_;
    const std::uint64_t espera = p_.esperaMs ? p_.esperaMs(consecutivas_) : 0;
    ++consecutivas_;
    if (p_.cortarTras > 0 && consecutivas_ >= p_.cortarTras) cortado_ = true;
    const std::uint64_t e = cortado_ ? std::max(espera, p_.esperaCortadoMs) : espera;
    agendar(ahoraMs, e);
    return e;
}

void Relanzador::agendar(std::uint64_t ahoraMs, std::uint64_t esperaMs) {
    relanzarEn_ = ahoraMs + esperaMs;
    lanzadoEn_.reset();
}

void Relanzador::lanzado(std::uint64_t ahoraMs) {
    relanzarEn_.reset();
    lanzadoEn_ = ahoraMs;
}

bool Relanzador::vigilar(std::uint64_t ahoraMs, bool sano, std::uint64_t estableMs) {
    if (consecutivas_ == 0 || !lanzadoEn_ || !sano || ahoraMs - *lanzadoEn_ < estableMs) return false;
    consecutivas_ = 0;
    cortado_ = false;
    return true;
}

void Relanzador::olvidar() {
    consecutivas_ = 0;
    cortado_ = false;
    relanzarEn_.reset();
    lanzadoEn_.reset();
}

} // namespace ba::platform
