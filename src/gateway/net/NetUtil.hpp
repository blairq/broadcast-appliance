// ============================================================================
// src/gateway/net/NetUtil.hpp — Direcciones locales
//
// Las direcciones de las interfaces de la máquina, con cuáles son virtuales (Hyper-V,
// VPN, WSL, Docker, el puente de las máquinas virtuales de la Mac…): la mejor IPv4 va
// en el QR (localIPv4s) y WebRTC no abre puertos en las que no hacen falta
// (redesRtc).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::gateway::net {

/// Una dirección de una interfaz activa (sin loopback).
struct DireccionLocal {
    std::string interfaz; ///< el nombre con que la conoce libwebrtc (Windows: AdapterName, el GUID)
    std::string ip;       ///< en texto
    bool v6 = false;
    bool virtual_ = false;     ///< un adaptador virtual (ver esVirtual)
    bool enlaceLocal = false;  ///< 169.254/16 o fe80::/10
    int puntos = 0;            ///< mejor primero: con puerta de enlace, Ethernet o Wi-Fi
};

/// Las direcciones de las interfaces activas, IPv4 e IPv6 (sin loopback).
std::vector<DireccionLocal> direccionesLocales();

/// ¿Un adaptador virtual? Por su nombre (y en Windows su descripción), en minúsculas:
/// Hyper-V, VMware, VirtualBox, WSL, Docker, VPN (tun/utun, Tailscale, ZeroTier, TAP),
/// los puentes de las máquinas virtuales (bridge100…, vmenet, virbr, lxcbr…).
bool esVirtual(const std::string& nombreMinusculas);

/// IPv4 de las interfaces activas (sin loopback), en texto, la mejor primero.
std::vector<std::string> localIPv4s();

/// Las redes en que WebRTC abre un puerto por sesión (una por interfaz y familia, como
/// libwebrtc) y las interfaces que se le dicen que ignore (las virtuales, si no se
/// usan). `conVirtuales`: también las virtuales (Configuración: red.rtcVirtuales).
struct RedesRtc {
    int redes = 0;                     ///< puertos por sesión (UDP), sin contar el loopback
    std::vector<std::string> ignorar;  ///< nombres de interfaz para libwebrtc
};
RedesRtc redesRtc(const std::vector<DireccionLocal>& dirs, bool conVirtuales);

/// Parsea "a.b.c.d/nn" -> ip (orden de host) y prefijo. false si no es valida.
bool parseCidr(const std::string& cidr, uint32_t& ip, int& prefix);

/// Separa "host:puerto" (puerto por defecto si falta).
void splitHostPort(const std::string& s, std::string& host, uint16_t& port, uint16_t defPort);

} // namespace ba::gateway::net
