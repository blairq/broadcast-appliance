// ============================================================================
// src/app/arranque/Invitados.hpp — los lugares de los invitados y del integrante remoto
//
// Pieza de AppRuntime (ver AppRuntime.hpp). El audio de cada lugar (global, con su retorno
// mix-minus), su cámara y su pantalla en las escenas (ocultas en la primera hasta que el
// operador las muestre), los dos canales del integrante remoto con la palabra (R3) y el cupo
// que cambia en caliente (T33).
//
// Hilos: EL DUEÑO (cfg_.scenes, cfg_.guests y el motor, con el turno). Sin estado propio:
// la verdad es la configuración (cfg_) y el motor.
// ============================================================================
#pragma once

namespace ba::app {

class AppRuntime;

class Invitados {
   public:
    explicit Invitados(AppRuntime& rt) : rt_(rt) {}
    /// Al arrancar: el audio de cada lugar y el video que falta en las escenas (antes de
    /// crearlas en el motor: va a la configuración).
    void agregar();
    /// El cupo cambió en caliente (AppRuntime::ajustarInvitados).
    void ajustar(int n);

   private:
    AppRuntime& rt_;
};

} // namespace ba::app
