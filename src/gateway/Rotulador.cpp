// ============================================================================
// src/gateway/Rotulador.cpp — ver Rotulador.hpp
// ============================================================================
#include "Rotulador.hpp"

#include <chrono>

namespace ba::gateway {
namespace {

using nlohmann::json;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

void Rotulador::publicar(const json& estado) {
    std::lock_guard<std::mutex> g(m_);
    estado_ = estado.is_object() ? estado : json::object();
    publicadoMs_ = ahoraMs();
}

json Rotulador::estado() const {
    std::lock_guard<std::mutex> g(m_);
    json e = estado_;
    e["publicadoMs"] = publicadoMs_;
    e["ahoraMs"] = ahoraMs();
    // Sin esto, un celular con la app caida mostraria para siempre la escena
    // que habia, y el que rotula escribiria contra un fantasma.
    e["viva"] = publicadoMs_ > 0 && (ahoraMs() - publicadoMs_) < 8000;
    e["pendientes"] = encargos_.size();
    return e;
}

bool Rotulador::encargar(const json& orden) {
    if (!orden.is_object() || !orden.contains("id")) return false;
    std::lock_guard<std::mutex> g(m_);
    json o = orden;
    o.erase("clave"); // no se guarda ni se le devuelve a nadie
    o["enMs"] = ahoraMs();
    // La cola se recorta por el principio: lo viejo es lo que ya no importa.
    if (encargos_.size() >= kMaxEncargos) encargos_.erase(encargos_.begin());
    encargos_.push_back(std::move(o));
    return true;
}

json Rotulador::tomarEncargos() {
    std::lock_guard<std::mutex> g(m_);
    json out = json::array();
    for (const json& e : encargos_)
        out.push_back(e);
    encargos_.clear();
    return out;
}

std::size_t Rotulador::pendientes() const {
    std::lock_guard<std::mutex> g(m_);
    return encargos_.size();
}

} // namespace ba::gateway
