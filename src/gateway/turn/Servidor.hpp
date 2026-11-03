// ============================================================================
// src/gateway/turn/Servidor.hpp — TURN mínimo dentro de la caja
//
// Para qué: que un navegador de afuera (entrando por pinggy, docs/planes/
// PINGGY.md) llegue con el medio de WebRTC a los puertos del gateway sin abrir
// nada en el router. pinggy expone UN puerto TCP/TLS (tlstcp: termina el TLS y
// entrega TCP plano a 127.0.0.1:3478) y, si se puede, uno UDP.
//
// Qué hace (subconjunto de RFC 5766/8656, sin RFC 6062):
//   - transporte cliente↔servidor por TCP (marco STUN / ChannelData con relleno
//     a 4 bytes) y, opcional, por UDP;
//   - Binding, Allocate (relay UDP), Refresh, CreatePermission, ChannelBind,
//     indicaciones Send/Data y ChannelData;
//   - credencial larga con MESSAGE-INTEGRITY (HMAC-SHA1) y FINGERPRINT, nonce
//     sin estado con vencimiento, usuarios efímeros estilo «TURN REST»
//     ("<vence_unix>:<id>", clave = base64(HMAC-SHA1(secreto, usuario))).
//
// SEGURIDAD (no es un relay abierto): solo se relaya hacia las IP de
// `peersPermitidos` (las de la propia máquina) Y a puertos de [rtcMin, rtcMax]
// (los de WebRTC del gateway), o hacia el relay de otra asignación de este
// mismo servidor (navegador↔navegador de la sesión remota). Lo que llega al
// relay desde cualquier otro lado se descarta. Topes de asignaciones y
// conexiones, vida máxima por asignación. Ver docs/funciones/TURN.md.
//
// Hilos (PLAN-GATEWAY-FRAGMENTOS.md F1/F2): el TURN no tiene hilo propio. Está partido en
// FRAGMENTOS, uno por hilo de red (Config::bucles: en el gateway, los hilos de red de WebRTC,
// rtc/BucleRtc.hpp), cada uno con sus conexiones y sus asignaciones, todo en su hilo:
//   - el fragmento 0 atiende las escuchas: cada conexión TCP nueva se le entrega UNA vez al
//     fragmento menos cargado (como el accept de nginx); el UDP (un solo puerto, el de
//     pinggy) lo vacía de a tandas y reparte cada datagrama por su origen, una entrega por
//     fragmento y por tanda;
//   - con Config::memoria, lo que va del relay a la red de WebRTC (y vuelve) no pasa por el
//     kernel: se entrega en memoria (rtc/Conmutador.hpp), en el mismo hilo si la sesión de
//     WebRTC está en el hilo del fragmento. Los sockets de relay existen igual (el puerto es
//     de verdad y lo que llegue por el kernel se atiende como siempre).
// Sin bucles (tests, gateway sin libwebrtc): `fragmentosPropios` hilos con poll()
// (turn/BuclePoll.hpp) llamados «turn».
// start()/stop()/estado() se pueden llamar desde cualquier hilo.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "turn/Bucle.hpp"

namespace ba::gateway::memoria {
class Conmutador;
}

namespace ba::gateway::turn {

class BuclePoll;

class Servidor {
   public:
    struct Config {
        uint16_t puertoTcp = 3478;   // 0 = el que dé el sistema (tests)
        uint16_t puertoUdp = 0;      // 0 = sin UDP
        bool conUdp = false;         // true + puertoUdp 0 = UDP en un puerto del sistema (tests)
        std::string direccion = "127.0.0.1"; // dónde escucha (TCP y UDP)
        /// IP que se anuncia como dirección del relay (XOR-RELAYED-ADDRESS).
        /// Vacío = `direccion` (o 127.0.0.1 si `direccion` es 0.0.0.0). Conviene
        /// la IP de la LAN: los candidatos host del gateway son de la LAN.
        std::string direccionRelay;
        /// Dónde se enlazan los sockets de relay. Vacío = `direccionRelay`.
        /// "0.0.0.0" deja que el sistema elija el origen según el destino
        /// (llega igual a 127.0.0.1 y a la IP de la LAN); lo que entre de otro
        /// lado lo descartan los permisos.
        std::string bindRelay;
        std::string realm = "ba";
        std::string secreto; // por arranque, al azar (Ice.hpp: secretoAleatorio)
        std::vector<std::string> peersPermitidos; // IPv4 de la propia máquina (con 127.0.0.1)
        uint16_t rtcMin = 40000, rtcMax = 41599;  // únicos puertos de peer admitidos
        // Topes: 0 = el ajuste (ba::ajustes) con su respaldo.
        int maxAsignaciones = 0; // turn.maxAsignaciones (640)
        int maxConexiones = 0;   // turn.maxConexiones (1280)
        int vidaMaxS = 0;        // turn.vidaMaxS (3600)
        int nonceS = 0;          // turn.nonceS (600)
        /// Los hilos donde corre cada fragmento (el i-ésimo, en bucles[i]; tienen que vivir
        /// más que el servidor). Vacío = `fragmentosPropios` hilos propios con poll().
        std::vector<Bucle*> bucles;
        int fragmentosPropios = 1;
        /// Entrega en memoria hacia la red de WebRTC (el fragmento i manda como el hilo i del
        /// conmutador). nullptr = sendto por el kernel, como antes.
        memoria::Conmutador* memoria = nullptr;
    };

    explicit Servidor(Config cfg);
    ~Servidor();
    Servidor(const Servidor&) = delete;
    Servidor& operator=(const Servidor&) = delete;

    bool start(std::string* err = nullptr);
    void stop();
    bool activo() const { return corriendo_.load(); }
    nlohmann::json estado() const;
    const Config& config() const { return cfg_; }
    /// Puertos efectivos (útiles cuando se pidió 0).
    uint16_t puertoTcp() const { return puertoTcpReal_.load(); }
    uint16_t puertoUdp() const { return puertoUdpReal_.load(); }

    struct Comun;
    struct Fragmento;

   private:
    Config cfg_;
    std::vector<std::unique_ptr<BuclePoll>> propios_;
    mutable std::mutex comunM_; // comun_ (estado() se llama de cualquier hilo)
    std::shared_ptr<Comun> comun_;
    std::atomic<bool> corriendo_{false};
    std::atomic<uint16_t> puertoTcpReal_{0}, puertoUdpReal_{0};
};

} // namespace ba::gateway::turn
