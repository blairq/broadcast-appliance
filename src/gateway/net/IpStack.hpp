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
//
// F5 (docs/planes/PLAN-GATEWAY-FRAGMENTOS.md, #15-#20): el hilo de la pila es TAMBIÉN el
// de WireGuard (WgDevice::cederBucle): recibe en tanda, descifra y le pasa cada paquete a
// lwIP ahí mismo, sin cola ni despertar por un UDP local en el medio (#17). Lo que lwIP
// manda sale cifrado en tanda al final de cada vuelta (#20). Para la red de WebRTC hay dos
// caminos:
//   - EN MEMORIA (#18/#19, `gateway.wgEnMemoria`, de fábrica): con un SumideroRtc
//     enchufado (lo pone el gateway: los sockets en memoria de rtc/, F1), lo que llega a
//     un puerto del rango de WebRTC se le entrega en UNA llamada por vuelta (la tanda
//     entera), y lo que WebRTC manda vuelve por enviarDesdeRtc (encola y despierta solo
//     si la cola estaba vacía). Sin un socket por invitado ni select sobre todos.
//   - POR EL KERNEL (el respaldo, mientras no haya sumidero o con el ajuste apagado): un
//     socket local por invitado y sendto a 127.0.0.1, como antes.
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

#include "net/EnlaceMedidor.hpp"

namespace ba::gateway::net {

class WgDevice;

/// F5: un datagrama del túnel para (o desde) la red de WebRTC. Direcciones en orden de host.
struct DatagramaTunel {
    uint32_t ipRemota = 0;     ///< el invitado (su IP pública, tal como llega por el túnel)
    uint16_t puertoRemoto = 0;
    uint16_t puertoRtc = 0;    ///< el puerto del rango de WebRTC al que va
    const uint8_t* datos = nullptr;
    size_t largo = 0;
};

/// F5 (#18): adónde entrega IpStack lo que llega por el túnel a los puertos de WebRTC. Lo
/// implementa el gateway con sus sockets en memoria (rtc/SocketsEnMemoria, F1): cada
/// datagrama al socket del puerto `puertoRtc`, como si viniera de ipRemota:puertoRemoto, en
/// el hilo de red del grupo de ese invitado (el pase entre hilos va en lote: una tanda por
/// llamada). Se llama en el hilo de la pila (el de WireGuard); los punteros valen solo
/// durante la llamada (hay que copiar). Que no se trabe.
class SumideroRtc {
   public:
    virtual ~SumideroRtc() = default;
    virtual void entregar(const DatagramaTunel* v, size_t n) = 0;
};

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
        int udpFlows = 0;   ///< flujos UDP (invitado x puerto) vivos (solo por el kernel)
        bool enMemoria = false;   ///< F5: hay sumidero y el ajuste lo deja
        uint64_t aRtc = 0;        ///< datagramas entregados en memoria a WebRTC
        uint64_t desdeRtc = 0;    ///< datagramas que volvieron de WebRTC en memoria
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

    // --- F5: la red de WebRTC en memoria ---------------------------------------------------
    /// Enchufa (o saca, con nullptr) el sumidero. Se puede llamar en cualquier momento y
    /// desde cualquier hilo: vale desde la vuelta siguiente. Sin sumidero, por el kernel.
    void setSumidero(std::shared_ptr<SumideroRtc> s);
    /// La vuelta (#19): WebRTC manda desde su puerto `puertoRtc` a ipRemota:puertoRemoto
    /// (orden de host). Thread-safe: copia, encola y despierta al hilo de la pila solo si la
    /// cola estaba vacía (una despertada por tanda). Sale por lwIP y WireGuard en esa vuelta.
    void enviarDesdeRtc(uint16_t puertoRtc, uint32_t ipRemota, uint16_t puertoRemoto, const uint8_t* d, size_t n);

    // --- Cliente (diagnostico y pruebas) ----------------------------------------
    /// Abre TCP a ip:port por el tunel, manda `request` y junta la respuesta
    /// hasta que el otro lado cierra. nullopt si no conecta a tiempo.
    std::optional<std::string> tcpRequest(uint32_t ip, uint16_t port, const std::string& request,
                                          int timeoutMs);
    /// Manda un datagrama y espera UNO de respuesta.
    std::optional<std::string> udpRequest(uint32_t ip, uint16_t port, const std::string& payload,
                                          int timeoutMs);

    /// Un UDP propio por el túnel hacia ip:port (el medidor del enlace). Lo que llega va a
    /// `alLlegar` en el hilo de la pila (que no se trabe). 0 si no se pudo abrir.
    using LlegadaFn = std::function<void(const uint8_t* d, size_t n)>;
    int udpAbrir(uint32_t ip, uint16_t port, LlegadaFn alLlegar);
    /// Manda un lote por ese UDP (una sola vuelta por la cola de la pila).
    void udpEnviar(int id, std::vector<std::string>&& lote);
    /// Lo cierra: después de esto `alLlegar` ya no se llama.
    void udpCerrar(int id);
    /// El transporte del medidor del enlace por el túnel (Enlace.hpp); nullptr si no abrió.
    std::unique_ptr<enlace::Transporte> transporteUdp(uint32_t ip, uint16_t port);

    static std::string ipToString(uint32_t hostOrderIp);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ba::gateway::net
