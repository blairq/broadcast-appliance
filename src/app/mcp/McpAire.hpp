// SPDX-License-Identifier: GPL-2.0-or-later
//
// Herramientas "de aire": las que cambian lo que sale (cortar, cambiar de
// escena, sacar a alguien, parar la transmisión…). Mientras hay aire
// (transmitiendo, grabando o la sesión en vivo), una de estas pedida por algo
// AUTOMÁTICO (el asistente, un plugin, una receta, un disparador, la voz)
// espera SIEMPRE la confirmación de una persona, sin importar la política de
// aprobación, el rol ni si la sesión leyó contenido de invitados.
//
// La lista fija no se puede bajar; se pueden sumar otras (Config/mcp-aire.json).
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace ba::app::mcp {

enum class ClaseAire { Normal, Aire, AireSi };

class McpAire {
   public:
    /// Lee los agregados de `config` (Config/mcp-aire.json).
    void cargar(const std::filesystem::path& config);

    /// La clase de `herramienta` (fija o agregada).
    ClaseAire clase(const std::string& herramienta) const;
    /// ¿Esta llamada cambia el aire? (resuelve las "aire_si" con sus argumentos).
    bool esDeAire(const std::string& herramienta, const nlohmann::json& args) const;

    /// [{herramienta, clase, fija, cuando}] (las que no son normales).
    nlohmann::json lista() const;
    /// Agrega (clase "aire") o quita (clase "normal") una herramienta. Las fijas
    /// no bajan. "" o el error.
    std::string ajustar(const std::string& herramienta, const std::string& clase);

    /// Lo que hace que una herramienta "aire_si" cuente (p. ej. ptz_* sobre la
    /// cámara que está al aire): lo pone la app.
    void setCondicion(std::function<bool(const std::string&, const nlohmann::json&)> fn) {
        condicion_ = std::move(fn);
    }

   private:
    std::filesystem::path archivo_;
    mutable std::mutex mu_;
    std::set<std::string> agregadas_;
    std::function<bool(const std::string&, const nlohmann::json&)> condicion_;
};

/// Lo que se pide sin confirmación por la voz o un disparador estando al aire
/// (reversible: mute, volumen, prompter).
bool reversibleEnVivo(const std::string& herramienta);

} // namespace ba::app::mcp
