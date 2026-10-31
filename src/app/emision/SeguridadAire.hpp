// ============================================================================
// src/app/emision/SeguridadAire.hpp — el fundido a negro (FTB) y la placa de seguridad (modelo)
//
// Encima de todo lo que sale, sin tocar las escenas. Lo que queda puesto va al
// documento de aire (F3-04: Runtime/al-aire.json, la `placa` de cada salida; antes
// era Runtime/placa.json aparte): si la app se cae con la placa o el negro al aire,
// al volver AppRuntime::restaurarAire lo pone igual. Lo piden la GUI
// (BotonesSeguridad, por la ventanilla del motor) y el MCP (aire_ftb, aire_placa), los
// dos por acá; quien muestre el estado lo lee de la foto del motor (la placa de cada
// salida, FotoMotor::Salida::placa), que se rearma cuando cambia (AppRuntime::aireCambio).
//
// Cada salida tiene su negro y su placa (PLAN-SALIDAS): `salida` "" = la por
// omisión; cada una vuelve igual (si la salida existe al restaurar).
//
// En el dueño del motor (el plano de control, F3-03): toma el turno del motor.
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::emision {

class SeguridadAire {
   public:
    explicit SeguridadAire(AppRuntime& rt);

    /// `audio`: "bajar" (el audio baja con el negro) o "mantener". "" o el error.
    std::string ftb(bool activar, int ms, const std::string& audio, const std::string& salida = {});
    /// La placa con `texto` (y una imagen opcional). "" o el error.
    std::string placa(bool activar, const std::string& texto, const std::string& imagen, int ms,
                      const std::string& salida = {});
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen}, de la salida.
    nlohmann::json estado(const std::string& salida = {}) const;

   private:
    void guardar();
    AppRuntime& rt_;
};

} // namespace ba::app::emision
