// ============================================================================
// src/gateway/webrtc/BucleRtc.cpp — ver BucleRtc.hpp
//
// El PhysicalSocketServer de M123 espera por cada rtc::Dispatcher anotado: en macOS con
// select() (WaitSelect), en Linux con epoll y en Windows con WSAEventSelect sobre un evento
// común (le pide GetSocket() y GetRequestedEvents() en cada vuelta). Los avisos llegan en
// OnEvent con DE_READ/DE_ACCEPT/DE_CLOSE (hay algo para leer, aceptar o un cierre: el TURN
// lee y se entera) o DE_WRITE/DE_CONNECT (hay lugar para escribir).
// ============================================================================
#include "webrtc/BucleRtc.hpp"

#if BA_GATEWAY_HAVE_WEBRTC

#include <utility>

#include "api/units/time_delta.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/thread.h"

namespace ba::gateway::rtcred {

class BucleRtc::Despacho final : public rtc::Dispatcher {
   public:
    Despacho(turn::Fd fd, int eventos, Aviso aviso) : fd_(fd), eventos_(eventos), aviso_(std::move(aviso)) {}

    void cambiar(int eventos) { eventos_ = eventos; }
    void apagar() { vivo_ = false; }

    uint32_t GetRequestedEvents() override {
        if (!vivo_) return 0;
        uint32_t e = 0;
        if (eventos_ & kLeer) e |= rtc::DE_READ | rtc::DE_ACCEPT;
        if (eventos_ & kEscribir) e |= rtc::DE_WRITE;
        return e;
    }
    void OnEvent(uint32_t ff, int err) override {
        if (!vivo_ || !aviso_) return;
        int ev = 0;
        if (ff & (rtc::DE_READ | rtc::DE_ACCEPT | rtc::DE_CLOSE)) ev |= kLeer;
        if (ff & (rtc::DE_WRITE | rtc::DE_CONNECT)) ev |= kEscribir;
        if (err != 0) ev |= kLeer; // el recv dirá qué pasó
        if (ev == 0) return;
        // Copia: el aviso puede dejar este descriptor (y el despacho se borra después).
        Aviso a = aviso_;
        a(ev);
    }
#if defined(WEBRTC_WIN)
    WSAEVENT GetWSAEvent() override { return WSA_INVALID_EVENT; }
    SOCKET GetSocket() override { return vivo_ ? static_cast<SOCKET>(fd_) : INVALID_SOCKET; }
    bool CheckSignalClose() override { return false; }
#elif defined(WEBRTC_POSIX)
    int GetDescriptor() override { return static_cast<int>(fd_); }
    bool IsDescriptorClosed() override { return false; } // el TURN ve el fin con recv() == 0
#endif

   private:
    turn::Fd fd_;
    int eventos_;
    Aviso aviso_;
    bool vivo_ = true;
};

BucleRtc::BucleRtc(rtc::Thread* hilo)
    : hilo_(hilo), pss_(static_cast<rtc::PhysicalSocketServer*>(hilo->socketserver())) {}

BucleRtc::~BucleRtc() {
    if (hilo_ == nullptr) return;
    enHiloEsperando([this] {
        for (auto& [fd, d] : despachos_) pss_->Remove(d.get());
        despachos_.clear();
    });
}

void BucleRtc::vigilar(turn::Fd fd, int eventos, Aviso aviso) {
    dejar(fd); // un descriptor reusado: el anterior ya se cerró
    auto d = std::make_unique<Despacho>(fd, eventos, std::move(aviso));
    pss_->Add(d.get());
    despachos_[fd] = std::move(d);
}

void BucleRtc::cambiar(turn::Fd fd, int eventos) {
    auto it = despachos_.find(fd);
    if (it == despachos_.end()) return;
    it->second->cambiar(eventos);
    pss_->Update(it->second.get()); // epoll lo necesita; select y Windows lo leen en cada vuelta
}

void BucleRtc::dejar(turn::Fd fd) {
    auto it = despachos_.find(fd);
    if (it == despachos_.end()) return;
    std::unique_ptr<Despacho> d = std::move(it->second);
    despachos_.erase(it);
    d->apagar();
    pss_->Remove(d.get());
    // Se puede estar adentro de su propio OnEvent: se borra en otra vuelta del hilo (la tarea
    // se lo lleva; si el hilo se para antes, se borra con la cola).
    std::shared_ptr<Despacho> luego(std::move(d));
    hilo_->PostTask([luego]() {});
}

void BucleRtc::enHilo(std::function<void()> f) { hilo_->PostTask(std::move(f)); }

void BucleRtc::enHiloLuego(int ms, std::function<void()> f) {
    hilo_->PostDelayedTask(std::move(f), webrtc::TimeDelta::Millis(ms));
}

void BucleRtc::enHiloEsperando(const std::function<void()>& f) {
    if (hilo_->IsCurrent()) {
        f();
        return;
    }
    hilo_->BlockingCall([&f] { f(); });
}

bool BucleRtc::enEsteHilo() const { return hilo_->IsCurrent(); }

} // namespace ba::gateway::rtcred

#endif // BA_GATEWAY_HAVE_WEBRTC
