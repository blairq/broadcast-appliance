// src/app/sala/CupoSala.cpp — el cupo de invitados de la sala (ver el .hpp)

#include "sala/CupoSala.hpp"

#include "arranque/AppRuntime.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonObject>

namespace ba::app::sala {

CupoSala::CupoSala(AppRuntime& rt, ConferenceClient& sala) : rt_(rt), sala_(sala) {}

std::string CupoSala::cambiar(int n, Listo listo) {
    // El tope que informa la sala; sin foto todavía, el del gateway.
    const int tope = sala_.snapshot().value("reserva").toInt(kMax);
    if (n < 1 || n > tope) return "el cupo va de 1 a " + std::to_string(tope);
    const int antes = rt_.config().guests;
    // Aunque los eventos de la sala estén reconectando: es otro pedido HTTP.
    sala_.post("/api/v1/room", QJsonObject{{"cupo", n}}, [this, n, antes, listo](bool ok, const QJsonObject& r) {
        std::string error;
        if (!ok && r.contains("error")) {
            error = r.value("error").toString().toStdString(); // p. ej. gente en los lugares que se irían
        } else {
            // Aceptado, o la sala no contesta (no está andando): queda guardado y
            // rige cuando arranque (lee el cupo de la configuración).
            rt_.ajustarInvitados(n);
            if (cambio_) cambio_(n);
        }
        if (listo) listo(error);
        if (avisar_) {
            if (!error.empty())
                avisar_("Cupo: " + error, true);
            else if (!ok)
                avisar_("Cupo de invitados: " + std::to_string(n) + " (la conferencia no contesta: rige cuando arranque).", false);
            else
                avisar_("Cupo de invitados: " + std::to_string(antes) + " → " + std::to_string(n) + ", sin reiniciar la conferencia.",
                        false);
        }
    });
    return {};
}

} // namespace ba::app::sala
