// src/app/arranque/InterfazConfig.cpp — ver el .hpp

#include "arranque/InterfazConfig.hpp"

#include "arranque/ArchivoConfig.hpp"

namespace ba::app::config {

InterfazConfig::InterfazConfig(std::filesystem::path dataRoot)
    : archivo_(std::move(dataRoot) / "Config" / "interfaz.json") {}

nlohmann::json InterfazConfig::valor(const std::string& clave) const {
    const nlohmann::json j = leerObjeto(archivo_);
    const auto it = j.find(clave);
    return it == j.end() ? nlohmann::json() : *it;
}

void InterfazConfig::guardar(const std::string& clave, const nlohmann::json& valor) {
    nlohmann::json j = leerObjeto(archivo_);
    j[clave] = valor;
    config::guardar(archivo_, j);
}

} // namespace ba::app::config
