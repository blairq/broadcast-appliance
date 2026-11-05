// ============================================================================
// src/gateway/EstadoCapas.hpp — las dos capas de la cámara de una sesión, con su candado
//
// Lo usan los dos backends de WebrtcSession (webrtc/Webrtc.cpp y nulo/WebrtcNulo.cpp): el
// mismo SelectorCapas (Capas.hpp) con los mids que declaró el navegador. Los cuadros llegan
// por los hilos de libwebrtc y los pedidos por el del servidor: candado.
// ============================================================================
#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include "Capas.hpp"

namespace ba::gateway {

struct EstadoCapas {
    mutable std::mutex mu;
    SelectorCapas sel;
    std::string midAlta, midBaja;

    bool conLocked() const { return !midAlta.empty() && !midBaja.empty(); }
    bool con() const {
        std::lock_guard<std::mutex> l(mu);
        return conLocked();
    }
    /// Conexion nueva (configure): el navegador arranca con las dos prendidas.
    void reiniciar() {
        std::lock_guard<std::mutex> l(mu);
        sel.reiniciar();
        midAlta.clear();
        midBaja.clear();
    }
    /// Los mids que declaró el navegador. Otros que los de antes (o los primeros): es otro
    /// juego de capas, que arranca con las dos prendidas.
    void ponerMids(const std::string& alta, const std::string& baja) {
        std::lock_guard<std::mutex> l(mu);
        if (alta == midAlta && baja == midBaja) return;
        midAlta = alta;
        midBaja = baja;
        sel.reiniciar();
    }
    /// La capa de un transceptor por su mid (Nada: no es una capa, o no hay capas).
    Capa deMid(const std::string& mid) const {
        std::lock_guard<std::mutex> l(mu);
        if (!conLocked() || mid.empty()) return Capa::Nada;
        return mid == midAlta ? Capa::Alta : mid == midBaja ? Capa::Baja : Capa::Nada;
    }
    /// true (con `a`) si hay que decirle al navegador qué capas prender.
    bool pedir(Capa c, CapasActivas& a) {
        std::lock_guard<std::mutex> l(mu);
        if (!conLocked()) return false; // una sola cámara: no hay nada que elegir
        const bool cambio = sel.pedir(c);
        a = sel.activas();
        return cambio;
    }
    bool llego(Capa c, bool& pasa, bool& anterior, bool& empieza, int& fundidoMs, CapasActivas& a) {
        std::lock_guard<std::mutex> l(mu);
        const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        const bool cambio = sel.llego(c, pasa, anterior, empieza, ahora);
        fundidoMs = sel.fundidoMs();
        a = sel.activas();
        return cambio;
    }
    void setFundidoMs(int ms) {
        std::lock_guard<std::mutex> l(mu);
        sel.setFundidoMs(ms);
    }
    Capa sale() const {
        std::lock_guard<std::mutex> l(mu);
        return sel.sale();
    }
    Capa pedida() const {
        std::lock_guard<std::mutex> l(mu);
        return sel.pedida();
    }
    CapasActivas activas() const {
        std::lock_guard<std::mutex> l(mu);
        return sel.activas();
    }
};

} // namespace ba::gateway
