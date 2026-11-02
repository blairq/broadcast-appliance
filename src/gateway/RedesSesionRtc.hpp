// ============================================================================
// src/gateway/RedesSesionRtc.hpp — las redes de UNA PeerConnection sobre la lista común
//
// El problema (ráfaga por el TURN, 2026-11-02): todas las PeerConnection comparten un
// rtc::BasicNetworkManager (la lista de interfaces sin las virtuales). Cada sesión que
// empieza a juntar candidatos llama StartUpdating(), y libwebrtc, si la lista ya estaba,
// avisa SignalNetworksChanged a TODOS los que escuchan, no solo al nuevo. Cada sesión que
// todavía no conectó (BasicPortAllocatorSession sin parar) toma ese aviso como un cambio
// de red y vuelve a juntar: un puerto UDP NUEVO del rango de WebRTC en cada red, sin
// soltar los anteriores. Por el camino directo las sesiones conectan en ~100 ms y casi no
// les llegan avisos; por el TURN tardan más, y con 40 entrando a la vez cada una juntaba
// varias veces: 40 sesiones agotaban los 100 puertos de IPv4 (con 10, 35 puertos en vez
// de 10). Las últimas se quedaban solo con IPv6, el relay del invitado es IPv4, y quedaban
// «conectando» para siempre (sin un solo par de ICE, nunca llegan a «falló»).
//
// La solución: cada PeerConnection ve la lista común a través de RedesDeSesion, que
// reenvía el aviso solo cuando a ESA sesión le importa: la primera vez después de su
// StartUpdating y cuando la lista de redes cambió de verdad (otra red, otra dirección).
// AsignadorDeSesion es el BasicPortAllocator que la trae (y la suelta después de él).
//
// Todo corre en el hilo de red de libwebrtc (como el NetworkManager común), menos los
// constructores. Solo con libwebrtc (BA_GATEWAY_HAVE_WEBRTC).
// ============================================================================
#pragma once

#include "p2p/client/basic_port_allocator.h"
#include "rtc_base/network.h"
#include "rtc_base/third_party/sigslot/sigslot.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ba::gateway {

class RedesDeSesion : public rtc::NetworkManager, public sigslot::has_slots<> {
   public:
    explicit RedesDeSesion(rtc::NetworkManager* comun) : comun_(comun) {}
    ~RedesDeSesion() override {
        // Las sesiones del asignador ya se fueron (AsignadorDeSesion lo suelta después).
        while (cuenta_ > 0) StopUpdating();
    }

    void Initialize() override { comun_->Initialize(); }

    void StartUpdating() override {
        if (cuenta_++ == 0) {
            comun_->SignalNetworksChanged.connect(this, &RedesDeSesion::alCambiarComun);
            comun_->SignalError.connect(this, &RedesDeSesion::alErrorComun);
        }
        // Una sesión nueva (de esta conexión) necesita su primer aviso: la lista común lo
        // manda (a todos) apenas puede; acá se deja pasar el próximo aunque no haya cambios.
        avisarSiempre_ = true;
        comun_->StartUpdating();
    }

    void StopUpdating() override {
        if (cuenta_ == 0) return;
        comun_->StopUpdating();
        if (--cuenta_ == 0) {
            comun_->SignalNetworksChanged.disconnect(this);
            comun_->SignalError.disconnect(this);
        }
    }

    std::vector<const rtc::Network*> GetNetworks() const override { return comun_->GetNetworks(); }
    std::vector<const rtc::Network*> GetAnyAddressNetworks() override { return comun_->GetAnyAddressNetworks(); }
    EnumerationPermission enumeration_permission() const override { return comun_->enumeration_permission(); }
    bool GetDefaultLocalAddress(int family, rtc::IPAddress* ip) const override {
        return comun_->GetDefaultLocalAddress(family, ip);
    }
    webrtc::MdnsResponderInterface* GetMdnsResponder() const override { return comun_->GetMdnsResponder(); }
    void DumpNetworks() override { comun_->DumpNetworks(); }

    /// Cuántos avisos de la lista común se reenviaron y cuántos se tragaron (para el test).
    int reenviados() const { return reenviados_; }
    int tragados() const { return tragados_; }

   private:
    /// Lo que distingue una lista de otra para una sesión: cada red (su clave) con su
    /// mejor dirección.
    std::vector<std::string> foto() const {
        std::vector<std::string> f;
        for (const rtc::Network* n : comun_->GetNetworks())
            if (n != nullptr) f.push_back(n->key() + "|" + n->GetBestIP().ToString());
        return f;
    }

    void alCambiarComun() {
        std::vector<std::string> ahora = foto();
        if (!avisarSiempre_ && ahora == ultima_) {
            ++tragados_; // el aviso era para otra sesión que recién empezaba
            return;
        }
        avisarSiempre_ = false;
        ultima_ = std::move(ahora);
        ++reenviados_;
        SignalNetworksChanged();
    }
    void alErrorComun() { SignalError(); }

    rtc::NetworkManager* comun_;
    int cuenta_ = 0;
    bool avisarSiempre_ = false;
    std::vector<std::string> ultima_;
    int reenviados_ = 0, tragados_ = 0;
};

namespace detalle {
/// Base que se construye ANTES (y se destruye DESPUÉS) que el BasicPortAllocator.
struct ConRedesDeSesion {
    explicit ConRedesDeSesion(rtc::NetworkManager* comun) : redesSesion(std::make_unique<RedesDeSesion>(comun)) {}
    std::unique_ptr<RedesDeSesion> redesSesion;
};
} // namespace detalle

/// El asignador de puertos de una PeerConnection, con sus redes propias sobre la lista común.
class AsignadorDeSesion : private detalle::ConRedesDeSesion, public cricket::BasicPortAllocator {
   public:
    AsignadorDeSesion(rtc::NetworkManager* comun, rtc::PacketSocketFactory* sockets)
        : detalle::ConRedesDeSesion(comun), cricket::BasicPortAllocator(redesSesion.get(), sockets) {}
    RedesDeSesion* redes() const { return redesSesion.get(); }
};

} // namespace ba::gateway
