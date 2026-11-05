// ============================================================================
// src/gateway/webrtc/SocketsEnMemoria.cpp — ver SocketsEnMemoria.hpp
// ============================================================================
#include "webrtc/SocketsEnMemoria.hpp"


#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rtc/Conmutador.hpp"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/time_utils.h"

namespace ba::gateway::rtcred {

namespace {

uint64_t claveDir(uint32_t ip, uint16_t puerto) { return (uint64_t(ip) << 16) | puerto; }

class SocketHibrido final : public rtc::AsyncPacketSocket, public memoria::Receptor {
   public:
    SocketHibrido(std::unique_ptr<rtc::AsyncPacketSocket> real, memoria::Conmutador* c, int hilo,
                  std::shared_ptr<EnlaceTunel> tunel)
        : real_(std::move(real)), c_(c), hilo_(hilo), tunel_(std::move(tunel)) {
        real_->RegisterReceivedPacketCallback(
            [this](rtc::AsyncPacketSocket*, const rtc::ReceivedPacket& p) { NotifyPacketReceived(p); });
        real_->SignalSentPacket.connect(this, &SocketHibrido::alEnviado);
        real_->SignalReadyToSend.connect(this, &SocketHibrido::alListo);
        real_->SubscribeCloseEvent(this, [this](rtc::AsyncPacketSocket*, int err) { NotifyClosed(err); });
        const rtc::SocketAddress local = real_->GetLocalAddress();
        // Solo los enlazados a una IP concreta: el TURN tiene que saber desde qué IP le llega
        // cada paquete (sus permisos son por IP), y uno en 0.0.0.0 no lo dice.
        if (local.family() == AF_INET && !rtc::IPIsAny(local.ipaddr())) {
            ipLocal_ = local.ipaddr().v4AddressAsHostOrderInteger();
            puertoLocal_ = static_cast<uint16_t>(local.port());
            punto_ = c_->abrir(ipLocal_, puertoLocal_, hilo_, memoria::Clase::WebrtcUdp, this);
        }
    }
    ~SocketHibrido() override {
        if (punto_) c_->cerrar(punto_);
        real_->DeregisterReceivedPacketCallback();
        real_->UnsubscribeCloseEvent(this);
    }

    rtc::SocketAddress GetLocalAddress() const override { return real_->GetLocalAddress(); }
    rtc::SocketAddress GetRemoteAddress() const override { return real_->GetRemoteAddress(); }
    int Send(const void* pv, size_t cb, const rtc::PacketOptions& options) override { return real_->Send(pv, cb, options); }

    int SendTo(const void* pv, size_t cb, const rtc::SocketAddress& addr, const rtc::PacketOptions& options) override {
        if (punto_ && addr.family() == AF_INET) {
            const uint32_t ip = addr.ipaddr().v4AddressAsHostOrderInteger();
            const uint16_t puerto = static_cast<uint16_t>(addr.port());
            const auto* d = static_cast<const uint8_t*>(pv);
            // #19: a alguien que llegó por el túnel, vuelve por el túnel.
            if (!porTunel_.empty() && porTunel_.count(claveDir(ip, puerto)) != 0 && tunel_ &&
                tunel_->enviar(puertoLocal_, ip, puerto, d, cb)) {
                enviadoEnMemoria(cb, options);
                return static_cast<int>(cb);
            }
            // #14: al relay de una asignación del TURN, en memoria.
            const uint32_t ipRelay = c_->ipRelay();
            if (ipRelay != 0 && ip == ipRelay) {
                if (memoria::PuntoPtr p = relay(ip, puerto)) {
                    c_->mandar(hilo_, p, d, cb, ipLocal_, puertoLocal_);
                    enviadoEnMemoria(cb, options);
                    return static_cast<int>(cb);
                }
            }
        }
        return real_->SendTo(pv, cb, addr, options);
    }

    int Close() override { return real_->Close(); }
    State GetState() const override { return real_->GetState(); }
    int GetOption(rtc::Socket::Option opt, int* value) override { return real_->GetOption(opt, value); }
    int SetOption(rtc::Socket::Option opt, int value) override { return real_->SetOption(opt, value); }
    int GetError() const override { return real_->GetError(); }
    void SetError(int error) override { real_->SetError(error); }

    // #3/#18: lo que entrega el TURN (o el túnel) en memoria, en este hilo.
    void recibirEnMemoria(const uint8_t* d, size_t n, uint32_t ip, uint16_t puerto, memoria::Via via) override {
        if (via == memoria::Via::Tunel) porTunel_.insert(claveDir(ip, puerto));
        const rtc::SocketAddress desde(ip, puerto);
        NotifyPacketReceived(rtc::ReceivedPacket::CreateFromLegacy(d, n, rtc::TimeMicros(), desde));
    }

   private:
    memoria::PuntoPtr relay(uint32_t ip, uint16_t puerto) {
        Cache& e = cache_[puerto];
        if (e.p && e.p->vivo.load(std::memory_order_acquire)) return e.p;
        const uint64_t gen = c_->generacion();
        if (!e.p && e.gen == gen) return nullptr; // ya se buscó y no había
        e.p = c_->buscar(ip, puerto);
        if (e.p && e.p->clase != memoria::Clase::RelayTurn) e.p = nullptr;
        e.gen = gen;
        return e.p;
    }
    /// Lo mismo que avisa AsyncUDPSocket después de un sendto.
    void enviadoEnMemoria(size_t cb, const rtc::PacketOptions& options) {
        rtc::SentPacket sp(options.packet_id, rtc::TimeMillis(), options.info_signaled_after_sent);
        rtc::CopySocketInformationToPacketInfo(cb, *this, true, &sp.info);
        SignalSentPacket(this, sp);
    }
    void alEnviado(rtc::AsyncPacketSocket*, const rtc::SentPacket& sp) { SignalSentPacket(this, sp); }
    void alListo(rtc::AsyncPacketSocket*) { SignalReadyToSend(this); }

    struct Cache {
        memoria::PuntoPtr p;
        uint64_t gen = 0;
    };
    std::unique_ptr<rtc::AsyncPacketSocket> real_;
    memoria::Conmutador* const c_;
    const int hilo_;
    std::shared_ptr<EnlaceTunel> tunel_;
    memoria::PuntoPtr punto_;
    uint32_t ipLocal_ = 0;
    uint16_t puertoLocal_ = 0;
    std::unordered_map<uint16_t, Cache> cache_;
    std::unordered_set<uint64_t> porTunel_;
};

} // namespace

FabricaEnMemoria::FabricaEnMemoria(rtc::SocketFactory* ss, memoria::Conmutador* c, int hilo, std::shared_ptr<EnlaceTunel> tunel)
    : base_(ss), c_(c), hilo_(hilo), tunel_(std::move(tunel)) {}

rtc::AsyncPacketSocket* FabricaEnMemoria::CreateUdpSocket(const rtc::SocketAddress& address, uint16_t min_port,
                                                          uint16_t max_port) {
    rtc::AsyncPacketSocket* s = base_.CreateUdpSocket(address, min_port, max_port);
    if (s == nullptr || c_ == nullptr) return s;
    return new SocketHibrido(std::unique_ptr<rtc::AsyncPacketSocket>(s), c_, hilo_, tunel_);
}

rtc::AsyncListenSocket* FabricaEnMemoria::CreateServerTcpSocket(const rtc::SocketAddress& local_address,
                                                                uint16_t min_port, uint16_t max_port, int opts) {
    return base_.CreateServerTcpSocket(local_address, min_port, max_port, opts);
}

rtc::AsyncPacketSocket* FabricaEnMemoria::CreateClientTcpSocket(const rtc::SocketAddress& local_address,
                                                                const rtc::SocketAddress& remote_address,
                                                                const rtc::ProxyInfo& proxy_info,
                                                                const std::string& user_agent,
                                                                const rtc::PacketSocketTcpOptions& tcp_options) {
    return base_.CreateClientTcpSocket(local_address, remote_address, proxy_info, user_agent, tcp_options);
}

std::unique_ptr<webrtc::AsyncDnsResolverInterface> FabricaEnMemoria::CreateAsyncDnsResolver() {
    return base_.CreateAsyncDnsResolver();
}

} // namespace ba::gateway::rtcred

