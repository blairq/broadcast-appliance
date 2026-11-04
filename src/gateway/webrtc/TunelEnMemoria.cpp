// ============================================================================
// src/gateway/webrtc/TunelEnMemoria.cpp — ver TunelEnMemoria.hpp
// ============================================================================
#include "webrtc/TunelEnMemoria.hpp"

#if BA_GATEWAY_HAVE_WEBRTC && BA_HAVE_LWIP

namespace ba::gateway::rtcred {

void TunelEnMemoria::entregar(const net::DatagramaTunel* v, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const net::DatagramaTunel& d = v[i];
        Cache& e = cache_[d.puertoRtc];
        if (!e.p || !e.p->vivo.load(std::memory_order_acquire)) {
            const std::uint64_t gen = c_->generacion();
            if (e.p || e.gen != gen) {
                e.p = c_->buscarPuerto(d.puertoRtc);
                e.gen = gen;
            }
        }
        if (!e.p) {
            ++sinDestino_;
            continue;
        }
        c_->mandar(externo_, e.p, reinterpret_cast<const std::uint8_t*>(d.datos), d.largo, d.ipRemota, d.puertoRemoto,
                   memoria::Via::Tunel);
    }
    c_->terminarTanda(externo_); // una despertada por hilo de red y por tanda
}

bool TunelEnMemoria::enviar(std::uint16_t puertoRtc, std::uint32_t ip, std::uint16_t puerto, const std::uint8_t* d,
                            std::size_t n) {
    pila_->enviarDesdeRtc(puertoRtc, ip, puerto, d, n);
    return true;
}

} // namespace ba::gateway::rtcred

#endif
