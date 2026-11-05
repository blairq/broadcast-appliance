// ============================================================================
// src/gateway/webrtc/TunelEnMemoria.hpp — WireGuard ↔ red de WebRTC en memoria (F5 #18/#19)
//
// El sumidero de la pila IP del túnel (net::IpStack::setSumidero, lo de HL): en vez de un
// socket local por invitado y un sendto a 127.0.0.1:P, cada tanda de datagramas que trae
// WireGuard llega acá (en el hilo de WireGuard), se busca el socket de WebRTC del puerto P y
// se le pasa al hilo de red de esa sesión por el Conmutador (cola SPSC; una despertada por
// hilo y por tanda). La vuelta (SalidaTunel) es net::IpStack::enviarDesdeRtc, que encola en
// tanda para el hilo de WireGuard.
//
// Solo con libwebrtc y lwIP.
// ============================================================================
#pragma once

#if BA_HAVE_LWIP

#include <cstdint>
#include <unordered_map>

#include "net/IpStack.hpp"
#include "rtc/Conmutador.hpp"
#include "webrtc/SocketsEnMemoria.hpp"

namespace ba::gateway::rtcred {

class TunelEnMemoria final : public net::SumideroRtc, public SalidaTunel {
   public:
    /// `externo`: el índice de productor externo en el conmutador (hilos de red + 0).
    TunelEnMemoria(memoria::Conmutador* c, int externo, net::IpStack* pila) : c_(c), externo_(externo), pila_(pila) {}

    /// En el hilo de WireGuard, una vez por tanda.
    void entregar(const net::DatagramaTunel* v, size_t n) override;
    /// En el hilo de red del socket que manda.
    bool enviar(std::uint16_t puertoRtc, std::uint32_t ip, std::uint16_t puerto, const std::uint8_t* d,
                std::size_t n) override;

    std::uint64_t sinDestino() const { return sinDestino_; }

   private:
    struct Cache {
        memoria::PuntoPtr p;
        std::uint64_t gen = 0;
    };
    memoria::Conmutador* const c_;
    const int externo_;
    net::IpStack* const pila_;
    std::unordered_map<std::uint16_t, Cache> cache_; // solo en el hilo de WireGuard
    std::uint64_t sinDestino_ = 0;
};

} // namespace ba::gateway::rtcred

#endif
