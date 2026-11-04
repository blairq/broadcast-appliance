// ============================================================================
// src/gateway/rtc/SocketsEnMemoria.hpp — los sockets UDP de WebRTC que hablan en memoria con
// el TURN y con el túnel (PLAN-GATEWAY-FRAGMENTOS.md F1 #3/#14 y F5 #18/#19)
//
// FabricaEnMemoria es el rtc::PacketSocketFactory de las PeerConnection (AsignadorDeSesion):
// cada socket UDP es uno de verdad (BasicPacketSocketFactory: los invitados directos siguen
// entrando por el kernel) envuelto en un SocketHibrido que además:
//   - se anota en el Conmutador con su dirección local y su hilo de red: el TURN le entrega
//     en memoria lo que mandó el cliente por el relay (llega «desde» la dirección del relay,
//     la misma que el candidato que el invitado señaló);
//   - lo que manda a un relay del TURN (la IP de relay y un puerto anotado) va en memoria al
//     fragmento del TURN de esa asignación, no al kernel;
//   - lo que manda a una dirección que le llegó por el túnel de WireGuard vuelve por el túnel
//     (EnlaceTunel), sin sockets locales por invitado.
// Para libwebrtc es un socket más: avisa SignalSentPacket igual (lo usa el control de
// congestión) y entrega lo recibido por NotifyPacketReceived.
//
// Todo en el hilo de red de la fábrica. Solo con libwebrtc.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace ba::gateway::memoria {
class Conmutador;
}

namespace ba::gateway::rtcred {

/// La vuelta por el túnel (la implementa TunelEnMemoria sobre net::IpStack).
class SalidaTunel {
   public:
    virtual ~SalidaTunel() = default;
    /// Desde el socket de WebRTC en `puertoRtc` hacia ip:puerto (orden de host) por el túnel.
    virtual bool enviar(std::uint16_t puertoRtc, std::uint32_t ip, std::uint16_t puerto, const std::uint8_t* d,
                        std::size_t n) = 0;
};

/// Dónde está la salida por el túnel ahora (se conecta cuando arranca WireGuard y se suelta
/// antes de pararlo). Lo comparten todos los sockets; lo leen muchos hilos de red a la vez sin
/// esperarse (candado compartido).
class EnlaceTunel {
   public:
    void poner(std::shared_ptr<SalidaTunel> s) {
        std::unique_lock<std::shared_mutex> l(m_);
        s_ = std::move(s);
    }
    bool enviar(std::uint16_t puertoRtc, std::uint32_t ip, std::uint16_t puerto, const std::uint8_t* d, std::size_t n) {
        std::shared_lock<std::shared_mutex> l(m_);
        return s_ && s_->enviar(puertoRtc, ip, puerto, d, n);
    }

   private:
    std::shared_mutex m_;
    std::shared_ptr<SalidaTunel> s_;
};

} // namespace ba::gateway::rtcred

#if BA_GATEWAY_HAVE_WEBRTC

#include "api/packet_socket_factory.h"
#include "p2p/base/basic_packet_socket_factory.h"

namespace ba::gateway::rtcred {

class FabricaEnMemoria final : public rtc::PacketSocketFactory {
   public:
    /// `ss`: el socket server del hilo de red `hilo` (el índice en el conmutador). Sin
    /// conmutador es una BasicPacketSocketFactory y nada más.
    FabricaEnMemoria(rtc::SocketFactory* ss, memoria::Conmutador* c, int hilo, std::shared_ptr<EnlaceTunel> tunel);

    rtc::AsyncPacketSocket* CreateUdpSocket(const rtc::SocketAddress& address, uint16_t min_port,
                                            uint16_t max_port) override;
    rtc::AsyncListenSocket* CreateServerTcpSocket(const rtc::SocketAddress& local_address, uint16_t min_port,
                                                  uint16_t max_port, int opts) override;
    rtc::AsyncPacketSocket* CreateClientTcpSocket(const rtc::SocketAddress& local_address,
                                                  const rtc::SocketAddress& remote_address,
                                                  const rtc::ProxyInfo& proxy_info, const std::string& user_agent,
                                                  const rtc::PacketSocketTcpOptions& tcp_options) override;
    std::unique_ptr<webrtc::AsyncDnsResolverInterface> CreateAsyncDnsResolver() override;

   private:
    rtc::BasicPacketSocketFactory base_;
    memoria::Conmutador* c_;
    const int hilo_;
    std::shared_ptr<EnlaceTunel> tunel_;
};

} // namespace ba::gateway::rtcred

#endif // BA_GATEWAY_HAVE_WEBRTC
