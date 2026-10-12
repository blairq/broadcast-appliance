// ============================================================================
// src/app/emision/RetornosInvitados.hpp — lo que oye cada invitado, guardado (modelo)
//
// Por lugar: "mix_minus" (el programa sin su propia voz, de fábrica: no se
// escucha con retardo ni hace eco) o "programa" (el programa entero). Solo
// para los lugares que hay (1..cupo). Queda en Config/retornos.json para el
// próximo arranque. Lo piden la GUI (tarjeta Invitados) y el MCP
// (invitado_retorno), los dos por acá.
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::emision {

class RetornosInvitados {
   public:
    explicit RetornosInvitados(AppRuntime& rt);

    /// "" o el error (modo que no es mix_minus/programa, o un lugar que no hay).
    std::string poner(int slot, const std::string& modo);
    std::string modo(int slot) const;
    /// {"1": modo, …} de los lugares que hay.
    nlohmann::json todos() const;
    /// Pone lo guardado en retornos.json (al arrancar). Sin archivo, mix-minus para todos.
    void cargar();

   private:
    AppRuntime& rt_;
};

} // namespace ba::app::emision
