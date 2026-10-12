// ============================================================================
// src/app/arranque/InterfazConfig.hpp — lo que recuerda la interfaz, guardado (modelo)
//
// Config/interfaz.json: valores sueltos por clave (anchos de los divisores,
// pestañas…). Portable: al lado de la app, nunca en el sistema.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::config {

class InterfazConfig {
   public:
    explicit InterfazConfig(std::filesystem::path dataRoot);

    /// El valor guardado, o null si no hay.
    nlohmann::json valor(const std::string& clave) const;
    void guardar(const std::string& clave, const nlohmann::json& valor);

   private:
    std::filesystem::path archivo_;
};

} // namespace ba::app::config
