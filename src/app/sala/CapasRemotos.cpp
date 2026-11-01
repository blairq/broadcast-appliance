// src/app/sala/CapasRemotos.cpp — qué cámaras de remotos tiene la app al aire (ver el .hpp)

#include "sala/CapasRemotos.hpp"

#include "emision/FotoMotor.hpp"
#include "sala/Pedido.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

CapasRemotos::CapasRemotos(ConferenceClient& sala) : sala_(sala) {
    // Una sala reiniciada no sabe qué está al aire: hasta que se lo digan, pide la alta
    // a todos los del programa (como siempre).
    sala_.registrarRepublicador("capas", &vida_, [this]() {
        if (!ultima_.is_null()) pedir(sala_, "POST", "/api/v1/capas", ultima_, {});
    });
}

void CapasRemotos::actualizar(const FotoMotor& f) {
    nlohmann::json c = capasNecesarias(f);
    if (c == ultima_) return;
    ultima_ = std::move(c);
    pedir(sala_, "POST", "/api/v1/capas", ultima_, {});
}

} // namespace ba::app::sala
