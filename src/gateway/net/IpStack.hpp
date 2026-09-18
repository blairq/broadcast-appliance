// ============================================================================
// src/gateway/net/IpStack.hpp — Pila IP (lwIP) sobre el tunel WireGuard
//
// Proposito:
//   Hacer que el gateway ATIENDA en la IP del tunel sin dispositivo TUN ni
//   privilegios: los paquetes que trae WgDevice los procesa lwIP en espacio de
//   usuario, y lo que llega a los puertos publicados se reenvia a los
//   servicios locales de siempre (que no se enteran del tunel):
//
//     TCP  tunel:puerto  ->  127.0.0.1:puertoLocal   con cabecera PROXY v1
//          (HTTP/HTTPS de       (el servidor de la sala ve la IP REAL del
//           los invitados)       invitado, no 127.0.0.1)
//     UDP  tunel:P       ->  127.0.0.1:P  (mismo puerto; P en el rango de
//          (medios WebRTC)      WebRTC). NAT por invitado: cada origen remoto
//                               usa su propio socket local.
//
//   El servidor WireGuard (el que da la IP publica) redirige esos puertos a
//   nuestra IP del tunel; los invitados se conectan a la IP publica.
//
// Threading: lwIP es global y NO es thread-safe: un unico thread (el de la
// pila) la toca. Los demas le mandan trabajo por una cola. Una sola IpStack
// por proceso.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ba::gateway::net {

class WgDevice;

class IpStack {
   public:
    using LogFn = std::function<void(const std::string&)>;

    struct Config {
        uint32_t tunnelIp = 0; ///< orden de host (p. ej. 10.99.0.2)
        int prefix = 24;
        int mtu = 1420;
    };

    struct Stats {
        uint64_t packetsIn = 0;
        uint64_t packetsOut = 0;
        int tcpBridges = 0; ///< conexiones TCP reenviadas vivas
        int udpFlows = 0;   ///< flujos UDP (invitado x puerto) vivos
    };

    IpStack(WgDevice& wg, Config cfg, LogFn log = {});
    ~IpStack();
    IpStack(const IpStack&) = delete;
    IpStack& operator=(const IpStack&) = delete;

    /// Reenvios: llamar ANTES de start().
    void forwardTcp(uint16_t tunnelPort, uint16_t localPort, bool proxyHeader);
    void forwardUdpRange(uint16_t first, uint16_t last);

    bool start();
    void stop();

    Stats stats() const;

    // --- Cliente (diagnostico y pruebas) ----------------------------------------
    /// Abre TCP a ip:port por el tunel, manda `request` y junta la respuesta
    /// hasta que el otro lado cierra. nullopt si no conecta a tiempo.
    std::optional<std::string> tcpRequest(uint32_t ip, uint16_t port, const std::string& request,
                                          int timeoutMs);
    /// Manda un datagrama y espera UNO de respuesta.
    std::optional<std::string> udpRequest(uint32_t ip, uint16_t port, const std::string& payload,
                                          int timeoutMs);

    static std::string ipToString(uint32_t hostOrderIp);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ba::gateway::net
