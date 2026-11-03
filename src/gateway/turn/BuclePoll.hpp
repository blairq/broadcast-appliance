// ============================================================================
// src/gateway/turn/BuclePoll.hpp — un Bucle con hilo propio y poll() (sin libwebrtc)
//
// El del TURN cuando no hay red de WebRTC donde correr: los tests de lógica (test_turn) y el
// gateway sin libwebrtc. Se despierta para las tareas de otros hilos con un UDP propio en
// 127.0.0.1 (anda igual en Windows, donde no hay pipe para WSAPoll).
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "turn/Bucle.hpp"

namespace ba::gateway::turn {

class BuclePoll final : public Bucle {
   public:
    /// Arranca el hilo (con ese nombre).
    explicit BuclePoll(std::string nombre);
    ~BuclePoll() override;
    BuclePoll(const BuclePoll&) = delete;
    BuclePoll& operator=(const BuclePoll&) = delete;

    void vigilar(Fd fd, int eventos, Aviso aviso) override;
    void cambiar(Fd fd, int eventos) override;
    void dejar(Fd fd) override;
    void enHilo(std::function<void()> f) override;
    void enHiloLuego(int ms, std::function<void()> f) override;
    void enHiloEsperando(const std::function<void()>& f) override;
    bool enEsteHilo() const override;

    /// Para el hilo (corre lo que ya estaba listo y sale). Lo llama el destructor.
    void parar();

   private:
    void bucle();
    void despertarHilo();

    struct Vigilado {
        int eventos = 0;
        std::shared_ptr<Aviso> aviso;
    };
    std::string nombre_;
    std::map<Fd, Vigilado> vigilados_; // solo en el hilo
    std::mutex m_;
    std::deque<std::function<void()>> tareas_;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> luego_;
    Fd despertador_; // UDP en 127.0.0.1: se manda un byte a sí mismo
    std::atomic<bool> despierto_{false};
    std::atomic<bool> corriendo_{true};
    std::thread::id id_;
    std::thread hilo_;
};

} // namespace ba::gateway::turn
