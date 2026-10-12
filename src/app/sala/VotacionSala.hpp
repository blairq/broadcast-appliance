// ============================================================================
// src/app/sala/VotacionSala.hpp — la votación (o encuesta) de la sala (modelo)
//
// Abrir, cerrar y mirar la votación del público. Es una operación del modelo:
// la pide la GUI (PanelVotacion) y el MCP (votacion_abrir, votacion_ver), todos
// por acá y con las mismas reglas:
//
//   - de dos a diez opciones (con una sola no hay nada que decidir);
//   - una FORMAL necesita el padrón del evento: votan los que tienen derecho y
//     llegaron (evento::Padron::votantes), y si no hay ninguno no se abre (una
//     votación donde nadie puede votar da un resultado que parece válido);
//   - una sola por vez: abrir otra reemplaza a la anterior.
//
// El resultado de la sala va a `listo` (error, y la votación como la devuelve
// la sala).
// ============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Padron.hpp"

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class VotacionSala {
   public:
    /// "" = hecho; si no, por qué no. `respuesta`: lo que contestó la sala ({votacion, …}).
    using Listo = std::function<void(const std::string& error, const nlohmann::json& respuesta)>;
    /// Los votantes del evento abierto, o nada si no hay evento (no hay padrón).
    using Votantes = std::function<std::optional<evento::Padron::Votantes>()>;

    VotacionSala(ConferenceClient& sala, Votantes votantes);

    /// `modo`: encuesta o formal. Devuelve el error inmediato o ""; lo que diga la sala va a `listo`.
    std::string abrir(const std::string& pregunta, const std::vector<std::string>& opciones, const std::string& modo,
                      bool secreta, bool enVivo, Listo listo);
    void cerrar(Listo listo);
    /// Mostrar el resultado mientras está abierta.
    void setEnVivo(bool si, Listo listo);
    /// Cómo va (con `historial`, también las cerradas del evento: van al acta).
    void ver(bool historial, Listo listo);

   private:
    void pedir(const char* metodo, const std::string& ruta, const nlohmann::json& cuerpo, Listo listo);

    ConferenceClient& sala_;
    Votantes votantes_;
};

} // namespace ba::app::sala
