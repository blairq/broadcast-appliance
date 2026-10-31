// ============================================================================
// src/app/emision/SeguridadAire.hpp — el fundido a negro (FTB) y la placa de seguridad (modelo)
//
// Encima de todo lo que sale, sin tocar las escenas. Lo que queda puesto va al
// documento de aire (F3-04: Runtime/al-aire.json, la `placa` de cada salida; antes
// era Runtime/placa.json aparte): si la app se cae con la placa o el negro al aire,
// al volver AppRuntime::restaurarAire lo pone igual. Lo piden la GUI
// (BotonesSeguridad) y el MCP (aire_ftb, aire_placa), los dos por acá; quien
// muestre el estado se entera con alCambiar.
//
// Cada salida tiene su negro y su placa (PLAN-SALIDAS): `salida` "" = la por
// omisión; cada una vuelve igual (si la salida existe al restaurar).
//
// Desde la interfaz o el plano de control (F3-03: aire_ftb y aire_placa con la interfaz
// trabada): toma el turno del motor y alCambiar llega siempre en el hilo dueño.
// ============================================================================
#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::emision {

class SeguridadAire {
   public:
    explicit SeguridadAire(AppRuntime& rt);

    /// Cuando cambió el FTB o la placa (para mostrarlo): en el hilo dueño.
    void alCambiar(std::function<void()> f) { cambio_ = std::move(f); }

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
    std::function<void()> cambio_;
};

} // namespace ba::app::emision
