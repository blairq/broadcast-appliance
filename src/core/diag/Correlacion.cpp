// src/core/diag/Correlacion.cpp — ver Correlacion.hpp

#include "diag/Correlacion.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>

namespace ba::diag::correlacion {

namespace {

thread_local std::string tlActual;

/// La semilla del proceso (una vez) y un contador: dos órdenes del mismo proceso no
/// se repiten, y dos procesos que arrancan juntos casi seguro tampoco.
std::uint32_t siguiente() {
    static const std::uint32_t semilla = [] {
        std::random_device rd;
        const auto t = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return rd() ^ static_cast<std::uint32_t>(t) ^ static_cast<std::uint32_t>(t >> 32);
    }();
    static std::atomic<std::uint32_t> n{0};
    // Mezcla de Knuth: los ids seguidos no se parecen (se distinguen a ojo).
    return (semilla + n.fetch_add(1, std::memory_order_relaxed)) * 2654435761u;
}

} // namespace

std::string nueva(char origen) {
    char b[16];
    std::snprintf(b, sizeof(b), "%c-%08x", origen, static_cast<unsigned>(siguiente()));
    return b;
}

const std::string& actual() { return tlActual; }

bool valido(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

Ambito::Ambito(const std::string& id) {
    if (!valido(id)) return;
    anterior_ = tlActual;
    tlActual = id;
    puso_ = true;
}

Ambito::~Ambito() {
    if (puso_) tlActual = std::move(anterior_);
}

std::string sufijo(const std::string& texto) {
    if (tlActual.empty() || texto.find(tlActual) != std::string::npos) return {};
    return " [orden=" + tlActual + "]";
}

} // namespace ba::diag::correlacion
