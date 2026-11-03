// ============================================================================
// src/app/agent/SesionesEvento.cpp — ver SesionesEvento.hpp
// ============================================================================
#include "agent/SesionesEvento.hpp"

#include "arranque/ArchivoConfig.hpp"

#include <nlohmann/json.hpp>

namespace ba::app::agent {

using json = nlohmann::json;

SesionesEvento::SesionesEvento(std::filesystem::path archivo, std::string evento)
    : archivo_(std::move(archivo)), evento_(std::move(evento)) {}

SesionesEvento SesionesEvento::delEventoAbierto(const std::filesystem::path& raizEvento) {
    // La carpeta del evento (sin la barra final, que deja el nombre vacío).
    std::filesystem::path r = raizEvento;
    if (!r.empty() && r.filename().empty()) r = r.parent_path();
    return SesionesEvento(raizEvento / "Config" / "asistente-sesiones.json", r.filename().string());
}

std::string SesionesEvento::de(const std::string& instancia, const std::string& motor) const {
    const json j = config::leerObjeto(archivo_);
    // De otro evento (una copia): no es la de este.
    if (j.value("evento", std::string()) != evento_) return {};
    const json s = j.value("sesiones", json::object());
    if (!s.is_object() || !s.contains(instancia) || !s[instancia].is_object()) return {};
    const json& m = s[instancia];
    return m.contains(motor) && m[motor].is_string() ? m[motor].get<std::string>() : std::string();
}

void SesionesEvento::poner(const std::string& instancia, const std::string& motor, const std::string& id) {
    json j = config::leerObjeto(archivo_);
    if (j.value("evento", std::string()) != evento_) j = json::object(); // la de otro evento se descarta
    j["evento"] = evento_;
    if (!j.contains("sesiones") || !j["sesiones"].is_object()) j["sesiones"] = json::object();
    json& s = j["sesiones"];
    if (!s.contains(instancia) || !s[instancia].is_object()) s[instancia] = json::object();
    if (id.empty())
        s[instancia].erase(motor);
    else
        s[instancia][motor] = id;
    if (j == config::leerObjeto(archivo_)) return; // nada nuevo: no se escribe
    config::guardar(archivo_, j);
}

void SesionesEvento::olvidar(const std::string& instancia) {
    json j = config::leerObjeto(archivo_);
    if (j.value("evento", std::string()) != evento_) return;
    if (!j.contains("sesiones") || !j["sesiones"].is_object() || !j["sesiones"].contains(instancia)) return;
    j["sesiones"].erase(instancia);
    config::guardar(archivo_, j);
}

} // namespace ba::app::agent
