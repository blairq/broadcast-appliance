// src/app/sala/CupoSala.cpp — el cupo de invitados de la sala (ver el .hpp)

#include "sala/CupoSala.hpp"

#include "i18n/I18n.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/VentanillaMotor.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonObject>

namespace ba::app::sala {

CupoSala::CupoSala(AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceClient& sala)
    : rt_(rt), ventanilla_(ventanilla), sala_(sala) {}

std::string CupoSala::cambiar(int n, Listo listo) {
    // El tope que informa la sala; sin foto todavía, el del gateway.
    const int tope = sala_.snapshot().value("reserva").toInt(kMax);
    if (n < 1 || n > tope) return ba::i18n::llenar(ba::i18n::traducir("sala", "el cupo va de 1 a %1"), {std::to_string(tope)});
    const int antes = ventanilla_.foto().ajustes.invitados;
    // Aunque los eventos de la sala estén reconectando: es otro pedido HTTP.
    sala_.post("/api/v1/room", QJsonObject{{"cupo", n}}, [this, n, antes, listo](bool ok, const QJsonObject& r) {
        std::string error;
        if (!ok && r.contains("error")) {
            error = r.value("error").toString().toStdString(); // p. ej. gente en los lugares que se irían
        } else {
            // Aceptado, o la sala no contesta (no está andando): queda guardado y
            // rige cuando arranque (lee el cupo de la configuración).
            ventanilla_.pedir("sala_cupo", [&rt = rt_, n]() {
                rt.ajustarInvitados(n);
                rt.fotoSucia(FotoMotor::Ajustes | FotoMotor::Escenas | FotoMotor::Sala);
            });
            if (cambio_) cambio_(n);
        }
        if (listo) listo(error);
        if (avisar_) {
            if (!error.empty())
                avisar_(ba::i18n::llenar(ba::i18n::traducir("sala", "Cupo: %1"), {error}), true);
            else if (!ok)
                avisar_(ba::i18n::llenar(ba::i18n::traducir("sala", "Cupo de invitados: %1 (la conferencia no contesta: "
                                                                    "rige cuando arranque)."),
                                         {std::to_string(n)}),
                        false);
            else
                avisar_(ba::i18n::llenar(ba::i18n::traducir("sala", "Cupo de invitados: %1 → %2, sin reiniciar la conferencia."),
                                         {std::to_string(antes), std::to_string(n)}),
                        false);
        }
    });
    return {};
}

} // namespace ba::app::sala
