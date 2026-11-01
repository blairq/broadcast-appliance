// ============================================================================
// src/app/arranque/SaludProceso.hpp — el proceso A ante el launcher y el gateway
//
// Pieza de AppRuntime (ver AppRuntime.hpp). El latido en BA.Health (el del arranque, el de
// la interfaz y el de los cuadros: LatidoAire), el canal de órdenes del launcher
// («control.app»), si el gateway (B) está vivo y el pedido de reiniciarlo.
//
// Hilos:
//   - El bloque de salud (health_) se escribe con healthMu_ desde cualquier hilo: el que
//     arranca (y el hilo «latido-arranque» mientras GStreamer revisa los plugins), el reloj
//     de la interfaz (latirInterfaz) y el hilo de LatidoAire.
//   - El canal de control (control_) es del plano de control (atenderCanal, en ticControl);
//     se abre en el arranque y se cierra en stop, en el hilo que es dueño en ese momento.
//   - El reinicio del gateway corre en su hilo («sala-reinicio», uno a la vez); el resultado
//     vuelve al dueño encolado (AppRuntime::despuesEnDueno).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "emision/LatidoAire.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"

namespace ba::app {

class AppRuntime;

class SaludProceso {
   public:
    explicit SaludProceso(AppRuntime& rt) : rt_(rt) {}
    ~SaludProceso();

    /// Su lugar en BA.Health (sin launcher, en desarrollo, crea el bloque): identidad,
    /// «andando» y el primer latido.
    void abrir();
    /// Un latido (con el candado del bloque). Desde cualquier hilo.
    void latir();
    /// El proceso terminó (o no pudo arrancar).
    void terminado();
    bool valido() const { return health_.valid(); }

    /// EL LATIDO DEL ARRANQUE: mientras el motor se arma, late desde su hilo (hasta que el de
    /// los cuadros toma la posta, con un tope de 2 minutos). Parar es idempotente.
    void empezarLatidoArranque();
    void pararLatidoArranque();
    /// El latido que mira los cuadros del motor (LatidoAire, hilo propio): `cuadros` es el
    /// contador del programa. Sin bloque de salud no hace nada.
    void iniciarLatidoAire(const std::filesystem::path& root, std::function<std::uint64_t()> cuadros);
    /// El latido de la interfaz (en su reloj): el supervisor sabe si se trabó.
    void latirInterfaz(bool arrancado, bool hayAire);
    LatidoAire& latido() { return latido_; }

    /// El canal de órdenes del launcher: abrirlo (avisa si no se pudo), atenderlo (el cierre
    /// pedido) y cerrarlo.
    void abrirCanal();
    void atenderCanal();
    void cerrarCanal() { control_.stop(); }

    bool gatewayAlive() const;
    std::uint32_t gatewayPid() const;
    static std::uint32_t pid();

    /// Ver AppRuntime::reiniciarSalaEnElFondo.
    void reiniciarSalaEnElFondo(std::function<void(const std::string& error)> listo);
    /// Espera el reinicio del gateway que esté en curso (al cerrar).
    void esperarReinicio();

   private:
    AppRuntime& rt_;
    ba::ipc::HealthBlock health_;
    std::mutex healthMu_; ///< latidos desde la UI, desde LatidoAire y desde el arranque
    LatidoAire latido_;
    ba::ipc::ControlService control_;
    std::atomic<bool> latidoArranqueSigue_{false};
    std::thread latidoArranque_;
    std::thread reinicioSala_; ///< reiniciarSalaEnElFondo
    std::atomic<bool> reiniciandoSala_{false};
};

} // namespace ba::app
