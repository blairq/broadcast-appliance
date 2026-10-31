// src/app/arranque/InterfazConfig.cpp — ver el .hpp

#include "arranque/InterfazConfig.hpp"

#include "arranque/ArchivoConfig.hpp"

namespace ba::app::config {

InterfazConfig::InterfazConfig(std::filesystem::path dataRoot)
    : archivo_(std::move(dataRoot) / "Config" / "interfaz.json"), datos_(leerObjeto(archivo_)) {}

nlohmann::json InterfazConfig::valor(const std::string& clave) const {
    const auto it = datos_.find(clave);
    return it == datos_.end() ? nlohmann::json() : *it;
}

void InterfazConfig::guardar(const std::string& clave, const nlohmann::json& valor) {
    if (const auto it = datos_.find(clave); it != datos_.end() && *it == valor) return; // nada que escribir
    datos_[clave] = valor;
    config::guardar(archivo_, datos_); // lo encola el escritor de la persistencia (no se escribe acá)
}

} // namespace ba::app::config
