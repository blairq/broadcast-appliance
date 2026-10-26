// ============================================================================
// src/core/platform/Energia.hpp — que la máquina no se duerma con el programa abierto
//
// Un estudio no puede apagar la pantalla del multiview a los diez minutos ni
// suspender la notebook en medio de una transmisión. Mientras el programa está
// abierto se le pide al sistema, con sus propias funciones (nada instalado
// aparte), que no se duerma:
//
//   Windows  PowerCreateRequest + PowerSetRequest (sistema y pantalla): se ve en
//            `powercfg /requests` con el motivo. Y a cada proceso pesado,
//            fuera del «modo eficiencia» (EcoQoS), que en las notebooks le baja
//            la velocidad a lo que no tiene la ventana adelante.
//   macOS    una aserción de IOPM (sin reposo del sistema ni de la pantalla) y
//            una actividad de NSProcessInfo (sin App Nap).
//   Linux    el bloqueo «sleep:idle» de systemd-logind por D-Bus (libdbus-1, que
//            es de la base del sistema: se carga al usarla, no se enlaza).
//
// Lo mantiene el lanzador, que vive mientras el programa está abierto; al
// cerrarse, el sistema lo suelta solo (los pedidos son del proceso).
// Ajustes: energia.mantenerDespierta (true), energia.mantenerPantalla (true).
// ============================================================================
#pragma once

#include <memory>
#include <string>

namespace ba::platform {

/// Mientras exista, el sistema no se duerme (y la pantalla tampoco, con `pantalla`).
class Despierta {
   public:
    Despierta(const std::string& motivo, bool pantalla);
    ~Despierta();
    Despierta(const Despierta&) = delete;
    Despierta& operator=(const Despierta&) = delete;

    /// Si el sistema aceptó el pedido.
    bool activa() const;
    /// Qué se pidió o por qué no se pudo, para el registro.
    const std::string& detalle() const;

    struct Impl;

   private:
    std::unique_ptr<Impl> d_;
};

/// Para el proceso que llama: fuera del ahorro de energía por proceso (EcoQoS en
/// Windows, App Nap en macOS; en Linux no hay). Devuelve qué hizo, para el registro.
std::string sinAhorroDeEnergia();

} // namespace ba::platform
