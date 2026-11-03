// src/gateway/PruebaSala.cpp — la prueba de sala (ver el .hpp)

#include "PruebaSala.hpp"

#include <iterator>

#include "security/Comparar.hpp"

namespace ba::gateway {

bool PruebaSala::abrir(const std::string& credencial, int64_t ahoraMs, int64_t venceMs) {
    if (credencial.size() < 16 || venceMs <= ahoraMs || venceMs - ahoraMs > kDuracionMaxMs) return false;
    std::lock_guard<std::mutex> l(m_);
    if (credencial_.empty()) {
        // Prueba nueva: nada de una anterior.
        desdeMs_ = ahoraMs;
        ids_.clear();
        directa_ = false;
        entradas_ = 0;
    }
    // Una credencial nueva invalida los tokens de la anterior que nadie canjeó.
    if (credencial_ != credencial) tokens_.clear();
    credencial_ = credencial;
    venceMs_ = venceMs;
    return true;
}

std::vector<std::string> PruebaSala::cerrar() {
    std::lock_guard<std::mutex> l(m_);
    std::vector<std::string> out(ids_.begin(), ids_.end());
    credencial_.clear();
    desdeMs_ = venceMs_ = 0;
    directa_ = false;
    tokens_.clear();
    ids_.clear();
    entradas_ = 0;
    medida_ = nullptr;
    medidaMs_ = 0;
    ips_.clear();
    return out;
}

bool PruebaSala::activaLocked(int64_t ahoraMs) const { return !credencial_.empty() && ahoraMs < venceMs_; }

bool PruebaSala::activa(int64_t ahoraMs) const {
    std::lock_guard<std::mutex> l(m_);
    return activaLocked(ahoraMs);
}

bool PruebaSala::vencida(int64_t ahoraMs) const {
    std::lock_guard<std::mutex> l(m_);
    return !credencial_.empty() && ahoraMs >= venceMs_;
}

bool PruebaSala::credencialValida(const std::string& pin, int64_t ahoraMs) const {
    std::lock_guard<std::mutex> l(m_);
    if (!activaLocked(ahoraMs) || pin.empty()) return false;
    return ba::security::igualesEnTiempoConstante(pin, credencial_);
}

void PruebaSala::anotarToken(const std::string& token, int64_t ahoraMs, int64_t venceTokenMs) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> l(m_);
    if (credencial_.empty()) return;
    // Poda: los vencidos no se canjean más (el token tampoco valdría).
    for (auto it = tokens_.begin(); it != tokens_.end();) it = it->second <= ahoraMs ? tokens_.erase(it) : std::next(it);
    tokens_[token] = venceTokenMs;
}

bool PruebaSala::tomarToken(const std::string& token, int64_t ahoraMs) {
    std::lock_guard<std::mutex> l(m_);
    if (!activaLocked(ahoraMs)) return false;
    const auto it = tokens_.find(token);
    if (it == tokens_.end()) return false;
    const bool vigente = ahoraMs < it->second;
    tokens_.erase(it);
    if (vigente) ++entradas_;
    return vigente;
}

void PruebaSala::anotarParticipante(const std::string& id) {
    std::lock_guard<std::mutex> l(m_);
    if (!credencial_.empty() && !id.empty()) ids_.insert(id);
}

void PruebaSala::olvidarParticipante(const std::string& id) {
    std::lock_guard<std::mutex> l(m_);
    ids_.erase(id);
}

bool PruebaSala::esDePrueba(const std::string& id) const {
    std::lock_guard<std::mutex> l(m_);
    return ids_.count(id) > 0;
}

std::vector<std::string> PruebaSala::participantes() const {
    std::lock_guard<std::mutex> l(m_);
    return {ids_.begin(), ids_.end()};
}

bool PruebaSala::setDirecta(bool on, int64_t ahoraMs) {
    std::lock_guard<std::mutex> l(m_);
    if (on && !activaLocked(ahoraMs)) return false;
    directa_ = on;
    return true;
}

bool PruebaSala::directa() const {
    std::lock_guard<std::mutex> l(m_);
    return directa_;
}

void PruebaSala::ponerMedida(const nlohmann::json& medida, int64_t ahoraMs) {
    std::lock_guard<std::mutex> l(m_);
    if (credencial_.empty() || !medida.is_object()) return;
    medida_ = medida;
    medidaMs_ = ahoraMs;
}

nlohmann::json PruebaSala::medida(int64_t ahoraMs) const {
    std::lock_guard<std::mutex> l(m_);
    if (!activaLocked(ahoraMs) || medida_.is_null()) return nullptr;
    return nlohmann::json{{"medida", medida_}, {"edadMs", ahoraMs - medidaMs_}};
}

void PruebaSala::anotarIp(std::uint32_t ip) {
    std::lock_guard<std::mutex> l(m_);
    if (!credencial_.empty() && ip != 0 && ips_.size() < 64) ips_.insert(ip);
}

bool PruebaSala::ipDePrueba(std::uint32_t ip, int64_t ahoraMs) const {
    std::lock_guard<std::mutex> l(m_);
    return activaLocked(ahoraMs) && ips_.count(ip) > 0;
}

nlohmann::json PruebaSala::estado(int64_t ahoraMs, bool conCredencial) const {
    std::lock_guard<std::mutex> l(m_);
    nlohmann::json j{{"activa", activaLocked(ahoraMs)},
                     {"desdeMs", desdeMs_},
                     {"venceMs", venceMs_},
                     {"directa", directa_},
                     {"participantes", static_cast<int>(ids_.size())},
                     {"entradas", entradas_}};
    if (conCredencial && !credencial_.empty()) j["credencial"] = credencial_;
    return j;
}

} // namespace ba::gateway
