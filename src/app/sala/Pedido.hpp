// src/app/sala/Pedido.hpp — un pedido a la API de control de la sala (modelo)
//
// Lo común de los servicios de la sala (votación, preguntas…): manda el pedido
// y le pasa a `listo` el error ("" = bien) y la respuesta como JSON. Un 200 con
// {"ok": false} también es error (la sala no lo aceptó).
#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

using Listo = std::function<void(const std::string& error, const nlohmann::json& respuesta)>;

/// `metodo`: "GET" o "POST" (`cuerpo` solo para POST).
void pedir(ConferenceClient& cliente, const std::string& metodo, const std::string& ruta, const nlohmann::json& cuerpo,
           Listo listo);

} // namespace ba::app::sala
