// src/app/sala/ConfigSala.cpp — la configuración de la sala en caliente (ver el .hpp)

#include "sala/ConfigSala.hpp"

#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>

namespace ba::app::sala {

ConfigSala::ConfigSala(ConferenceClient& sala) : sala_(sala) {}

void ConfigSala::aplicar(const std::string& seccion, const nlohmann::json& valores, Listo listo) {
    pedir(sala_, "PUT", "/api/v1/config/" + seccion, valores,
          [listo](const std::string& error, const nlohmann::json&) {
              if (listo) listo(error);
          });
}

int ConfigSala::conectados() const {
    if (!sala_.online()) return -1;
    return static_cast<int>(sala_.snapshot().value("participants").toArray().size());
}

} // namespace ba::app::sala
