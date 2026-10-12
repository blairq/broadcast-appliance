// src/app/sala/CorreccionSala.cpp — la cola del corrector, del lado de la sala (ver el .hpp)

#include "sala/CorreccionSala.hpp"

#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

CorreccionSala::CorreccionSala(ConferenceClient& sala) : sala_(sala) {}

void CorreccionSala::publicarPendientes(const nlohmann::json& pendientes) {
    if (!sala_.online()) return;
    pedir(sala_, "POST", "/api/v1/correccion/pendientes",
          {{"pendientes", pendientes.is_array() ? pendientes : nlohmann::json::array()}}, {});
}

} // namespace ba::app::sala
