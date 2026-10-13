// ============================================================================
// src/app/sala/SeguridadSala.hpp — lo que protege los accesos a la sala y al MCP (modelo)
//
// T47, en Config/seguridad.json:
//   - pin_por_sesion (de fábrica sí): al arrancar una sesión en vivo, la sala
//     cambia de PIN si cambió el día. El PIN es uno por día (los reinicios del
//     mismo día lo conservan) y lo guarda el gateway: acá solo se le pide.
//   - rotar_token_mcp (de fábrica sí): un token del MCP nuevo en cada arranque.
//
// Los ajustes no se cambian en vivo. Los usan Studio (al arrancar la sesión) y
// el MCP (al arrancar el servidor y en seguridad_estado), todos por acá.
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class SeguridadSala {
   public:
    /// ¿Estamos en vivo? (entonces los ajustes no se tocan).
    using EnVivo = std::function<bool()>;
    /// error ("" = bien), si la sala cambió de PIN y cuál quedó.
    using ListoPin = std::function<void(const std::string& error, bool cambio, const std::string& pin)>;

    SeguridadSala(std::filesystem::path dataRoot, ConferenceClient& sala, EnVivo enVivo);

    bool pinPorSesion() const;
    bool rotarTokenMcp() const;
    /// "" o por qué no (en vivo no se cambia).
    std::string setPinPorSesion(bool si);
    std::string setRotarTokenMcp(bool si);
    /// {rotar_token_mcp, pin_por_sesion}.
    nlohmann::json estado() const;

    /// Al arrancar una sesión en vivo: si pin_por_sesion, le pide a la sala un PIN
    /// nuevo solo si el vigente no es el de hoy. Sin pin_por_sesion no pide nada
    /// (listo("", false, "")).
    void rotarPinSiCambioElDia(ListoPin listo);

   private:
    std::string poner(const char* clave, bool si);
    std::filesystem::path archivo_;
    ConferenceClient& sala_;
    EnVivo enVivo_;
};

} // namespace ba::app::sala
