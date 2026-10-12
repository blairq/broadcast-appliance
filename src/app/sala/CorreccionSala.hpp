// ============================================================================
// src/app/sala/CorreccionSala.hpp — la cola del corrector de subtítulos, del lado de la sala (modelo)
//
// La página /correccion/ que sirve la sala muestra las frases que esperan
// salir (agent::FlujosTexto) y las pide cada 500 ms; acá se le pasan. Lo que
// el corrector decide vuelve por el SSE (ConferenceClient::correccionEvento) y
// el enlace con su clave lo da EnlacesSala::correccion. Lo usa el puesto del
// corrector (ui/PuestoCorreccion).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class CorreccionSala {
   public:
    explicit CorreccionSala(ConferenceClient& sala);

    /// Las frases pendientes, enteras (las que ya no están se van de la
    /// página). Sin la sala andando no hay a quién pasárselas: no hace nada
    /// (al volver se le pasan de nuevo).
    void publicarPendientes(const nlohmann::json& pendientes);

   private:
    ConferenceClient& sala_;
};

} // namespace ba::app::sala
