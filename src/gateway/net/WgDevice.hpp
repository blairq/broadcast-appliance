// ============================================================================
// src/gateway/net/WgDevice.hpp — WireGuard en espacio de usuario (un peer)
//
// Proposito:
//   Tunel WireGuard sin dispositivo TUN ni privilegios (portable: Windows,
//   Linux, macOS): cifra/descifra paquetes IP y los intercambia por UDP con un
//   unico peer (el servidor que nos da la IP publica). Lo que entra por el
//   tunel se entrega por `onPacket`; lo que se quiere mandar va por send().
//   La pila IP que usa esos paquetes (lwIP) esta en IpStack.
//
// Protocolo (whitepaper de WireGuard, §5):
//   - Handshake Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s en los DOS roles: como
//     iniciador (lo normal: somos cliente detras de NAT) y como respondedor
//     (el servidor puede reiniciar la sesion si tiene trafico para nosotros).
//   - Mensajes de transporte con contador, ventana anti-replay de 2048,
//     relleno a multiplos de 16.
//   - Timers: rekey a los 120 s o ante 15 s sin respuesta, rechazo a los
//     180 s, reintento de handshake cada 5 s (+jitter), keepalive pasivo
//     (10 s) y persistente (configurable, para mantener abierto el NAT).
//   - Cookies (mensaje tipo 3): se guardan y se usan en MAC2 al reintentar.
//   - Roaming: el endpoint se actualiza con el origen de cada paquete
//     autenticado.
//
// Threading: un thread propio (socket + timers). send() y stats() son
// thread-safe; onPacket se llama desde el thread del dispositivo.
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "net/WgCrypto.hpp"

namespace ba::gateway::net {

struct WgConfig {
    Key32 privateKey{};
    Key32 peerPublicKey{};
    Key32 presharedKey{};     ///< ceros = sin PSK
    std::string endpointHost; ///< vacio = solo respondedor (esperamos al peer)
    uint16_t endpointPort = 51820;
    uint16_t listenPort = 0;         ///< 0 = efimero
    int persistentKeepaliveSec = 25; ///< 0 = apagado
};

class WgDevice {
   public:
    using PacketFn = std::function<void(const uint8_t* packet, size_t n)>;
    using LogFn = std::function<void(const std::string& line)>;

    struct Stats {
        bool up = false; ///< hay una sesion vigente
        uint64_t rxBytes = 0;
        uint64_t txBytes = 0;
        uint64_t handshakes = 0;
        int64_t lastHandshakeMs = 0; ///< reloj de pared (ms epoch); 0 = nunca
        std::string endpoint;        ///< ip:puerto del peer
    };

    explicit WgDevice(WgConfig cfg, LogFn log = {});
    ~WgDevice();
    WgDevice(const WgDevice&) = delete;
    WgDevice& operator=(const WgDevice&) = delete;

    void setOnPacket(PacketFn fn) { onPacket_ = std::move(fn); }
    bool start();
    void stop();

    /// Manda un paquete IP por el tunel. Sin sesion, lo encola (hasta 64) y
    /// dispara el handshake. :returns: false si no se pudo ni encolar.
    bool send(const uint8_t* packet, size_t n);

    Stats stats() const;
    uint16_t localPort() const { return localPort_; }
    const Key32& publicKey() const { return ourPub_; }

   private:
    struct Replay {
        uint64_t greatest = 0;
        bool any = false;
        std::bitset<2048> bits;
        bool ok(uint64_t c) const;
        void mark(uint64_t c);
    };
    struct Keypair {
        uint32_t localIndex = 0;
        uint32_t remoteIndex = 0;
        Key32 sendKey{};
        Key32 recvKey{};
        uint64_t sendCounter = 0;
        Replay replay;
        int64_t createdMs = 0;
        bool initiator = false;
    };
    struct Handshake {
        bool active = false;
        uint32_t localIndex = 0;
        Key32 ephPriv{};
        Key32 chaining{};
        Key32 hash{};
        int64_t startedMs = 0; ///< primer intento (para abandonar a los 90 s)
        int64_t sentMs = 0;
    };

    void loop();
    void tick(int64_t now);
    void handleDatagram(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void handleInitiation(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void handleResponse(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void handleCookie(const uint8_t* d, size_t n);
    void handleData(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void sendInitiationLocked(int64_t now);
    bool sendDataLocked(const uint8_t* p, size_t n, int64_t now);
    void flushQueueLocked(int64_t now);
    void sendRawLocked(const uint8_t* d, size_t n);
    Keypair* keypairFor(uint32_t localIndex);
    uint32_t newIndex();
    bool resolveEndpoint();

    WgConfig cfg_;
    LogFn log_;
    PacketFn onPacket_;
    Key32 ourPub_{};
    Key32 staticShared_{};  ///< DH(ourPriv, peerPub), fijo
    Key32 initChaining_{};  ///< HASH(CONSTRUCTION)
    Key32 initHash_{};      ///< HASH(initChaining || IDENTIFIER)
    Key32 mac1KeyPeer_{};   ///< HASH("mac1----" || peerPub)
    Key32 mac1KeyOurs_{};   ///< HASH("mac1----" || ourPub)
    Key32 cookieKeyPeer_{}; ///< HASH("cookie--" || peerPub)

    mutable std::mutex m_;
    long long sock_ = -1;
    uint16_t localPort_ = 0;
    uint32_t epIp_ = 0; ///< orden de red
    uint16_t epPort_ = 0;
    Handshake hs_;
    std::unique_ptr<Keypair> current_;
    std::unique_ptr<Keypair> previous_;
    std::unique_ptr<Keypair> next_; ///< respondedor: hasta el primer dato
    std::deque<std::vector<uint8_t>> queue_;
    std::array<uint8_t, 16> lastMac1_{};
    std::array<uint8_t, 16> cookie_{};
    int64_t cookieMs_ = 0;
    std::array<uint8_t, 12> lastTimestamp_{}; ///< anti-replay de iniciaciones
    int64_t lastSentMs_ = 0;
    int64_t lastRecvMs_ = 0;
    int64_t lastDataSentMs_ = 0;
    int64_t lastDataRecvMs_ = 0;
    int64_t unansweredSinceMs_ = 0; ///< primer dato enviado sin respuesta posterior
    Stats stats_;

    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace ba::gateway::net
