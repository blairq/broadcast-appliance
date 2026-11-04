// ============================================================================
// src/gateway/webrtc/HilosLote.hpp — los hilos de red y de trabajo de libwebrtc con la frontera #4
// en lote (LoteTrabajo.hpp), sin tocar libwebrtc
//
// Lo que M123 deja enganchar desde afuera (rtc_base/thread.h, socket_server.h):
//   - el socket server de cada rtc::Thread (Wait / WakeUp): PostTask encola y después llama a
//     WakeUp() del socket server del destino, SIEMPRE (esté dormido o no);
//   - rtc::Thread::Run() (virtual): el hilo dueño, al empezar y al terminar;
//   - PostTaskImpl / PostDelayedTaskImpl / BlockingCallImpl (virtuales, protegidos).
// Get() y Dispatch() son privados: no hay «después de cada tarea» salvo envolviendo la tarea.
//
//  - `HiloTrabajo`: su socket server es `EsperaTrabajo` (sin sockets, como NullSocketServer)
//    que duerme y despierta con un lote::Destino. BlockingCall hacia él: urgente (en el acto).
//  - `HiloRed`: su socket server es `ServidorRed` (un PhysicalSocketServer: BucleRtc y las
//    fábricas lo usan como siempre) con el lote::Remitente del hilo. Suelta lo anotado:
//      · al entrar a esperar (antes de dormirse: siempre);
//      · al salir de la espera (la vuelta de E/S: lo que llegó por sockets), si venció el lote;
//      · al terminar cada tarea (las tareas van envueltas: lo que llega por el TURN en memoria
//        o el conmutador), si venció el lote;
//      · antes de un BlockingCall a otro hilo.
//    Si anota algo DENTRO de la espera (procesando E/S), se despierta a sí mismo (sin cambio
//    de contexto: el PhysicalSocketServer sale de su vuelta) para soltar al terminar la vuelta.
//    Además, un PostTask a sí mismo desde afuera de la espera no llama al WakeUp de verdad
//    (escribir en el pipe o el evento de Windows): el hilo está corriendo y mira su cola antes
//    de volver a esperar. Es lo que gana `juntos` (red y trabajo en el mismo hilo: libwebrtc
//    sigue haciendo un PostTask por paquete al «hilo de trabajo», que es él mismo).
//
// Solo con libwebrtc.
// ============================================================================
#pragma once

#if BA_GATEWAY_HAVE_WEBRTC

#include <cstdint>
#include <memory>

#include "rtc/LoteTrabajo.hpp"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"

namespace ba::diag {
class Frontera;
}

namespace ba::gateway::rtcred {

/// El socket server del hilo de trabajo: sin sockets; espera con el Destino.
class EsperaTrabajo final : public rtc::SocketServer {
   public:
    explicit EsperaTrabajo(std::shared_ptr<lote::Destino> d) : d_(std::move(d)) {}
    bool Wait(webrtc::TimeDelta max_wait_duration, bool process_io) override;
    void WakeUp() override { d_->avisar(); }
    rtc::Socket* CreateSocket(int family, int type) override;

   private:
    std::shared_ptr<lote::Destino> d_;
};

class HiloTrabajo final : public rtc::Thread {
   public:
    explicit HiloTrabajo(std::shared_ptr<lote::Destino> d);
    ~HiloTrabajo() override;
    void Run() override;
    const lote::Destino& destino() const { return *d_; }

   protected:
    void BlockingCallImpl(rtc::FunctionView<void()> functor, const webrtc::Location& location) override;

   private:
    std::shared_ptr<lote::Destino> d_;
};

/// El socket server del hilo de red (ver arriba).
class ServidorRed final : public rtc::PhysicalSocketServer {
   public:
    /// `saltarPropio`: no despertarse de verdad a sí mismo fuera de la espera (lote y juntos).
    /// `propias` (o nullptr): cuenta esos PostTask a sí mismo.
    ServidorRed(bool enLote, std::int64_t loteNs, bool saltarPropio, ba::diag::Frontera* propias);
    bool Wait(webrtc::TimeDelta max_wait_duration, bool process_io) override;
    void WakeUp() override;

    lote::Remitente& remitente() { return remitente_; }
    /// En el hilo dueño, al empezar.
    void soyElDueno();
    /// Para Remitente::instalar: la tanda pasó de vacía a tener algo.
    static void alPrimero(void* ctx);

   private:

    lote::Remitente remitente_;
    const bool saltarPropio_;
    ba::diag::Frontera* const propias_;
    bool dentro_ = false; ///< el dueño está en PhysicalSocketServer::Wait (solo lo toca el dueño)
};

class HiloRed final : public rtc::Thread {
   public:
    /// En `Juntos` no manda a nadie en lote (él es su propio hilo de trabajo); en `Separados`
    /// solo cuenta (despierta en el acto, como libwebrtc).
    HiloRed(lote::Modo modo, std::int64_t loteNs);
    ~HiloRed() override;
    void Run() override;
    ServidorRed& servidor() { return *static_cast<ServidorRed*>(socketserver()); }

   protected:
    void PostTaskImpl(absl::AnyInvocable<void() &&> task, const PostTaskTraits& traits,
                      const webrtc::Location& location) override;
    void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task, webrtc::TimeDelta delay,
                             const PostDelayedTaskTraits& traits, const webrtc::Location& location) override;
    void BlockingCallImpl(rtc::FunctionView<void()> functor, const webrtc::Location& location) override;

   private:
    const lote::Modo modo_;
};

} // namespace ba::gateway::rtcred

#endif // BA_GATEWAY_HAVE_WEBRTC
