// ============================================================================
// src/gateway/RetornoCompartido.cpp — ver RetornoCompartido.hpp (qué y por qué). La parte con
// libwebrtc (flujos, capas, el codificador de paso) es webrtc/RetornoRtc.cpp.
// ============================================================================

#include "RetornoCompartido.hpp"

#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "diag/Registro.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace ba::gateway {

// ============================================================================
// Capas, selector y agregador (test_retorno_compartido)
// ============================================================================

namespace {
const std::vector<CapaRetorno>& capasPorOmision() {
    static const std::vector<CapaRetorno> c{{720, 1500}, {360, 600}, {180, 200}};
    return c;
}
} // namespace

std::vector<CapaRetorno> leerCapasRetorno(const std::string& texto) {
    std::vector<CapaRetorno> out;
    std::stringstream ss(texto);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto dos = item.find(':');
        if (dos == std::string::npos) continue;
        const int alto = std::atoi(item.substr(0, dos).c_str());
        const int kbps = std::atoi(item.substr(dos + 1).c_str());
        if (alto < 90 || alto > 2160 || kbps < 50 || kbps > 20000) continue;
        if (std::any_of(out.begin(), out.end(), [alto](const CapaRetorno& c) { return c.alto == alto; })) continue;
        out.push_back({alto, kbps});
    }
    std::sort(out.begin(), out.end(), [](const CapaRetorno& a, const CapaRetorno& b) { return a.alto > b.alto; });
    if (out.size() > 4) out.resize(4); // más capas son más codificadores por flujo
    return out.empty() ? capasPorOmision() : out;
}

std::vector<CapaRetorno> capasRetorno() {
    return leerCapasRetorno(ba::ajustes::texto("retorno.capas", "720:1500,360:600,180:200"));
}

std::vector<CapaRetorno> capasPara(int altoFuente, const std::vector<CapaRetorno>& todas) {
    std::vector<CapaRetorno> out;
    for (const CapaRetorno& c : todas)
        if (c.alto <= altoFuente) out.push_back(c);
    if (out.empty() && !todas.empty() && altoFuente > 0) out.push_back({altoFuente, todas.back().kbps});
    return out;
}

int indiceTope(const std::vector<CapaRetorno>& capas, int topeAlto) {
    if (topeAlto <= 0 || capas.empty()) return 0;
    for (size_t i = 0; i < capas.size(); ++i)
        if (capas[i].alto <= topeAlto) return static_cast<int>(i);
    return static_cast<int>(capas.size()) - 1;
}

int topeDePedido(const std::string& pedido) {
    if (pedido == "calidad-max") return 0;
    if (pedido == "calidad-720p") return 720;
    if (pedido == "calidad-360p") return 360;
    return -1;
}

void SelectorRetorno::reiniciar() {
    actual_ = -1;
    bajaDesde_ = -1;
    subeDesde_ = -1;
}

int SelectorRetorno::elegir(const std::vector<int>& kbps, std::int64_t objetivoBps, int tope, std::int64_t ahoraMs) {
    const int n = static_cast<int>(kbps.size());
    if (n == 0) return actual_ = -1;
    tope = std::clamp(tope, 0, n - 1);
    const auto entra = [&](int i, double factor) {
        return static_cast<double>(objetivoBps) >= factor * 1000.0 * static_cast<double>(kbps[static_cast<size_t>(i)]);
    };
    if (actual_ < 0 || actual_ >= n) {
        // La primera vez: la más alta que entra (sin saber nada todavía, la más alta permitida;
        // el control de congestión arranca bajo y sondea enseguida).
        int i = tope;
        if (objetivoBps > 0)
            while (i < n - 1 && !entra(i, 1.0)) ++i;
        bajaDesde_ = subeDesde_ = -1;
        return actual_ = i;
    }
    if (actual_ < tope) { // el operador bajó el tope: ya
        bajaDesde_ = subeDesde_ = -1;
        return actual_ = tope;
    }
    if (objetivoBps <= 0) return actual_;
    // ¿Bajar? Por debajo de la capa durante un rato: a la más alta que entra.
    if (actual_ < n - 1 && !entra(actual_, u_.bajarSi)) {
        subeDesde_ = -1;
        if (bajaDesde_ < 0) bajaDesde_ = ahoraMs;
        if (ahoraMs - bajaDesde_ >= u_.bajarMs) {
            int i = actual_ + 1;
            while (i < n - 1 && !entra(i, 1.0)) ++i;
            bajaDesde_ = -1;
            return actual_ = i;
        }
        return actual_;
    }
    bajaDesde_ = -1;
    // ¿Subir? La de arriba entra con margen durante un rato: de a una.
    if (actual_ > tope && entra(actual_ - 1, u_.subirSi)) {
        if (subeDesde_ < 0) subeDesde_ = ahoraMs;
        if (ahoraMs - subeDesde_ >= u_.subirMs) {
            subeDesde_ = -1;
            return --actual_;
        }
        return actual_;
    }
    subeDesde_ = -1;
    return actual_;
}

bool AgregadorClaves::tocaAhora(std::int64_t ahoraMs, std::int64_t minMs) {
    if (!pendiente_) return false;
    if (hubo_ && ahoraMs - ultimaMs_ < minMs) return false; // ya va a llegar la hora
    pendiente_ = false;
    ++atendidos_;
    return true;
}

void AgregadorClaves::salioClave(std::int64_t ahoraMs) {
    hubo_ = true;
    ultimaMs_ = ahoraMs;
    pendiente_ = false; // una clave atiende a todos los que la esperaban
}

} // namespace ba::gateway
