// ============================================================================
// src/gateway/DestinoCamara.cpp — ver DestinoCamara.hpp
// ============================================================================
#include "DestinoCamara.hpp"

#include "MediaPump.hpp"
#include "ajustes/Ajustes.hpp"

#include <chrono>
#include <cmath>

namespace ba::gateway {

bool DestinoCamara::quiereEntero() const { return pump_ != nullptr && pump_->llegoCamara(sesion_); }

void DestinoCamara::entero(const std::uint8_t* bgra, int w, int h) {
    if (pump_ == nullptr || bgra == nullptr || w <= 0 || h <= 0) return;
    pump_->forwardVideo(sesion_, bgra, static_cast<std::size_t>(w) * h * 4u, w, h);
    BA_SONDA(ritmoEntero_.cuadro(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());)
}

Grillas::PedidoCelda DestinoCamara::pedirCelda(int w, int h, std::int64_t redUs, std::int64_t decoUs) {
    if (grillas_ == nullptr) return {};
    return grillas_->pedirCelda(persona_, w, h, Grillas::reloj(), redUs, decoUs);
}

void DestinoCamara::guardarCelda(Grillas::PedidoCelda&& p) {
    if (grillas_ != nullptr) grillas_->guardarCelda(std::move(p));
}

void DestinoCamara::soltarCelda(Grillas::PedidoCelda&& p) {
    if (grillas_ != nullptr) grillas_->soltarCelda(std::move(p));
}

nlohmann::json DestinoCamara::ritmo() const {
    nlohmann::json j = nlohmann::json::object();
    BA_SONDA(const auto uno = [](const ba::diag::RitmoCuadros& r) {
        const auto m = r.resumen();
        const auto z = r.resumen(static_cast<std::int64_t>(m.p50Ms * 1000));
        const auto r1 = [](double v) { return std::round(v * 10) / 10; };
        int n = 0;
        const auto s = r.serie(n);
        nlohmann::json serie = nlohmann::json::array();
        for (int k = 0; k < n; ++k) serie.push_back(s[k] / 1000);
        // `fin_us`: cuándo fue el último (para alinear las series de dos tramos).
        return nlohmann::json{{"cps", r1(z.cps)}, {"p50_ms", r1(z.p50Ms)}, {"p95_ms", r1(z.p95Ms)},
                              {"max_ms", r1(z.maxMs)}, {"huecos", z.huecos}, {"n", z.n},
                              {"serie_ms", std::move(serie)}, {"fin_us", r.ultimoUs()}};
    };
    j["red"] = uno(ritmoRed_); j["deco"] = uno(ritmoDeco_); j["entero"] = uno(ritmoEntero_);)
    return j;
}

bool DestinoCamara::celdaEnPlaca() { return ba::ajustes::siNo("gateway.celdaEnPlaca", true); }

} // namespace ba::gateway
