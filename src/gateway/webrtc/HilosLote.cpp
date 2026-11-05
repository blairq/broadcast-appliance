// ============================================================================
// src/gateway/webrtc/HilosLote.cpp — ver HilosLote.hpp
// ============================================================================
#include "webrtc/HilosLote.hpp"


#include <utility>

#include "diag/Frontera.hpp"

namespace ba::gateway::rtcred {

namespace {
thread_local const ServidorRed* t_servidor = nullptr;

/// rtc::Thread pasa el plazo de su próxima diferida (en ms) o kForever.
std::int64_t aNs(webrtc::TimeDelta d) {
    if (d.IsPlusInfinity()) return -1;
    const std::int64_t ns = d.ns();
    return ns < 0 ? 0 : ns;
}

/// Al terminar una tarea del hilo de red: suelta lo anotado si venció el lote.
void alTerminarTarea() {
    if (lote::Remitente* r = lote::Remitente::delHilo()) r->soltarSiVencio();
}
} // namespace

// ---------------------------------------------------------------------------- trabajo

bool EsperaTrabajo::Wait(webrtc::TimeDelta max_wait_duration, bool /*process_io*/) {
    d_->esperar(aNs(max_wait_duration));
    return true;
}

rtc::Socket* EsperaTrabajo::CreateSocket(int /*family*/, int /*type*/) { return nullptr; }

HiloTrabajo::HiloTrabajo(std::shared_ptr<lote::Destino> d)
    : rtc::Thread(std::make_unique<EsperaTrabajo>(d), /*do_init=*/false), d_(std::move(d)) {
    DoInit();
}

HiloTrabajo::~HiloTrabajo() { Stop(); }

void HiloTrabajo::Run() {
    d_->soyElDueno();
    rtc::Thread::Run();
}

void HiloTrabajo::BlockingCallImpl(rtc::FunctionView<void()> functor, const webrtc::Location& location) {
    // El que llama se queda esperando: que lo que tenía anotado no espere con él, y que esta
    // tarea despierte en el acto (no al terminar una tanda que no va a terminar).
    if (lote::Remitente* r = lote::Remitente::delHilo()) r->soltar();
    lote::Remitente::Urgente urgente;
    rtc::Thread::BlockingCallImpl(functor, location);
}

// ---------------------------------------------------------------------------- red

ServidorRed::ServidorRed(bool enLote, std::int64_t loteNs, bool saltarPropio, ba::diag::Frontera* propias)
    : remitente_(enLote, loteNs), saltarPropio_(saltarPropio), propias_(propias) {}

void ServidorRed::soyElDueno() { t_servidor = this; }

void ServidorRed::alPrimero(void* ctx) {
    auto* s = static_cast<ServidorRed*>(ctx);
    // Anotó algo procesando E/S adentro de la espera: el PhysicalSocketServer no sale de su
    // vuelta hasta que lo despierten. Se despierta él (sin cambio de contexto) para soltar al
    // terminar ESTA vuelta.
    if (s->dentro_) s->rtc::PhysicalSocketServer::WakeUp();
}

bool ServidorRed::Wait(webrtc::TimeDelta max_wait_duration, bool process_io) {
    t_servidor = this;
    remitente_.soltar(); // por dormirse (o por mirar la E/S): nada anotado espera con él
    dentro_ = true;
    const bool ok = rtc::PhysicalSocketServer::Wait(max_wait_duration, process_io);
    dentro_ = false;
    remitente_.soltarSiVencio(); // lo que anotó la vuelta de E/S
    return ok;
}

void ServidorRed::WakeUp() {
    // El dueño avisándose fuera de la espera: está corriendo; rtc::Thread::Get mira la cola (y
    // las diferidas, y si está saliendo) antes de volver a llamar a Wait.
    if (saltarPropio_ && t_servidor == this && !dentro_) {
        if (propias_ != nullptr) propias_->pase(0);
        return;
    }
    rtc::PhysicalSocketServer::WakeUp();
}

HiloRed::HiloRed(lote::Modo modo, std::int64_t loteNs)
    : rtc::Thread(std::make_unique<ServidorRed>(modo == lote::Modo::Lote, loteNs, modo != lote::Modo::Separados,
                                                &ba::diag::frontera("rtc.red.propias", 4,
                                                                    "red → sí mismo (PostTask sin despertar; con «juntos», "
                                                                    "el paso a trabajo)")),
                  /*do_init=*/false),
      modo_(modo) {
    DoInit();
}

HiloRed::~HiloRed() { Stop(); }

void HiloRed::Run() {
    ServidorRed& s = servidor();
    s.soyElDueno();
    // Con «juntos» no hay a quién mandarle en lote (el trabajo es este mismo hilo).
    const bool remite = modo_ != lote::Modo::Juntos;
    if (remite) s.remitente().instalar(modo_ == lote::Modo::Lote ? &ServidorRed::alPrimero : nullptr, &s);
    rtc::Thread::Run();
    if (remite) s.remitente().desinstalar();
}

void HiloRed::PostTaskImpl(absl::AnyInvocable<void() &&> task, const PostTaskTraits& traits,
                           const webrtc::Location& location) {
    if (modo_ != lote::Modo::Lote) {
        rtc::Thread::PostTaskImpl(std::move(task), traits, location);
        return;
    }
    rtc::Thread::PostTaskImpl(
        [t = std::move(task)]() mutable {
            std::move(t)();
            alTerminarTarea();
        },
        traits, location);
}

void HiloRed::PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task, webrtc::TimeDelta delay,
                                  const PostDelayedTaskTraits& traits, const webrtc::Location& location) {
    if (modo_ != lote::Modo::Lote) {
        rtc::Thread::PostDelayedTaskImpl(std::move(task), delay, traits, location);
        return;
    }
    rtc::Thread::PostDelayedTaskImpl(
        [t = std::move(task)]() mutable {
            std::move(t)();
            alTerminarTarea();
        },
        delay, traits, location);
}

void HiloRed::BlockingCallImpl(rtc::FunctionView<void()> functor, const webrtc::Location& location) {
    if (lote::Remitente* r = lote::Remitente::delHilo()) r->soltar();
    rtc::Thread::BlockingCallImpl(functor, location);
}

} // namespace ba::gateway::rtcred

