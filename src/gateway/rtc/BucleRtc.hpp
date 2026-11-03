// ============================================================================
// src/gateway/rtc/BucleRtc.hpp — un turn::Bucle sobre el hilo de red de libwebrtc
//
// F1 de PLAN-GATEWAY-FRAGMENTOS.md: los sockets del TURN (la escucha TCP, el UDP de pinggy,
// las conexiones y los relays) en el MISMO PhysicalSocketServer que los de WebRTC, no en un
// poll() de otro hilo. Cada descriptor se anota como un rtc::Dispatcher propio (los sockets
// siguen siendo los nuestros, crudos y no bloqueantes: el TURN lee y escribe como siempre;
// solo cambia quién espera por ellos). Las tareas van por PostTask del hilo.
//
// Solo con libwebrtc. Todo (menos enHilo*) en el hilo de red.
// ============================================================================
#pragma once

#if BA_GATEWAY_HAVE_WEBRTC

#include <map>
#include <memory>
#include <vector>

#include "turn/Bucle.hpp"

namespace rtc {
class Thread;
class PhysicalSocketServer;
} // namespace rtc

namespace ba::gateway::rtcred {

class BucleRtc final : public turn::Bucle {
   public:
    /// `hilo` tiene que haberse creado con rtc::Thread::CreateWithSocketServer() (su socket
    /// server es un PhysicalSocketServer) y vivir más que este bucle.
    explicit BucleRtc(rtc::Thread* hilo);
    ~BucleRtc() override;
    BucleRtc(const BucleRtc&) = delete;
    BucleRtc& operator=(const BucleRtc&) = delete;

    void vigilar(turn::Fd fd, int eventos, Aviso aviso) override;
    void cambiar(turn::Fd fd, int eventos) override;
    void dejar(turn::Fd fd) override;
    void enHilo(std::function<void()> f) override;
    void enHiloLuego(int ms, std::function<void()> f) override;
    void enHiloEsperando(const std::function<void()>& f) override;
    bool enEsteHilo() const override;

    class Despacho;

   private:
    rtc::Thread* hilo_;
    rtc::PhysicalSocketServer* pss_;
    std::map<turn::Fd, std::unique_ptr<Despacho>> despachos_;
};

} // namespace ba::gateway::rtcred

#endif // BA_GATEWAY_HAVE_WEBRTC
