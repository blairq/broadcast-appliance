// ============================================================================
// src/app/sala/OradoresSala.hpp — el modo oradores de la sala (modelo)
//
// La fila de oradores con reloj: la configuración (tiempo general, uno o
// varios al aire, lugar, carrusel, margen) y lo que se le hace a cada uno (al
// aire, a la fila, moverlo, su tiempo, presentador). La GUI (la página Sala, la
// tarjeta de oradores, el cue «orador» del guion) y el MCP (speakers_*,
// speaker_*) pasan por acá, con las mismas reglas: un tiempo tiene que ser de
// más de cero segundos; el lugar en la fila empieza en 0.
//
// Cómo se escribe un tiempo ("3", "2:30", "90s") es de quien lo lee: acá
// llegan segundos.
// ============================================================================
#pragma once

#include <string>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class OradoresSala {
   public:
    explicit OradoresSala(ConferenceClient& sala);

    /// Cambia los campos presentes: enabled, defaultSec, multi, defaultSlot,
    /// carousel (manual|chain|time), graceSec. Devuelve el error inmediato o "".
    std::string configurar(const nlohmann::json& cambios, Listo listo = {});
    /// Al aire el siguiente de la fila (con «uno a la vez», el que estaba sale).
    void siguiente(Listo listo = {});
    void alAire(const std::string& id, bool on, Listo listo = {});
    /// Lugar en la fila (0 = primero).
    void mover(const std::string& id, int lugar, Listo listo = {});
    /// Segundos para hablar (más de cero). Devuelve el error inmediato o "".
    std::string tiempo(const std::string& id, int segundos, Listo listo = {});
    /// A la fila de oradores (o de vuelta a ella si ya habló).
    void aLaFila(const std::string& id, Listo listo = {});
    /// Presentador fijo: se queda al aire mientras rotan los oradores.
    void presentador(const std::string& id, bool on, Listo listo = {});

   private:
    ConferenceClient& sala_;
};

} // namespace ba::app::sala
