// ============================================================================
// src/gateway/turn/Bucle.hpp — el bucle de un hilo de red, visto desde el TURN
//
// El TURN no tiene hilo propio: cada fragmento (Servidor.hpp) corre en un Bucle. En el
// gateway el bucle es el hilo de red de WebRTC (rtc/BucleRtc.hpp: los sockets del TURN en el
// mismo PhysicalSocketServer que los de WebRTC, F1 de PLAN-GATEWAY-FRAGMENTOS.md); sin
// libwebrtc (tests de lógica) es un hilo con poll() (turn/BuclePoll.hpp).
//
// Todo lo de un Bucle (vigilar, cambiar, dejar, los avisos y las tareas) corre en su hilo,
// menos enHilo/enHiloEsperando, que se llaman de cualquiera.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>

namespace ba::gateway::turn {

#ifdef _WIN32
using Fd = std::uintptr_t; // SOCKET
#else
using Fd = int;
#endif

class Bucle {
   public:
    enum : int { kLeer = 1, kEscribir = 2 };
    /// Lo que pasó en el descriptor (kLeer: hay para leer, conexión nueva o se cerró;
    /// kEscribir: hay lugar para mandar).
    using Aviso = std::function<void(int eventos)>;

    virtual ~Bucle() = default;

    /// Vigilar `fd` (no bloqueante) por `eventos`. En el hilo del bucle.
    virtual void vigilar(Fd fd, int eventos, Aviso aviso) = 0;
    /// Cambiar lo que se vigila (p. ej. sumar kEscribir con cola de salida).
    virtual void cambiar(Fd fd, int eventos) = 0;
    /// Dejar de vigilarlo (antes de cerrarlo). Se puede llamar desde su propio aviso.
    virtual void dejar(Fd fd) = 0;

    /// Correr `f` en el hilo del bucle (desde cualquier hilo).
    virtual void enHilo(std::function<void()> f) = 0;
    /// Correr `f` en el hilo del bucle dentro de `ms` milisegundos.
    virtual void enHiloLuego(int ms, std::function<void()> f) = 0;
    /// Correr `f` en el hilo del bucle y esperar a que termine (ahí mismo si ya es su hilo).
    virtual void enHiloEsperando(const std::function<void()>& f) = 0;
    virtual bool enEsteHilo() const = 0;
};

} // namespace ba::gateway::turn
