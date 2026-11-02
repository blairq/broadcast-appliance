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
// Threading: un thread propio (socket + timers), o el de quien tome el bucle (cederBucle:
// IpStack, F5 de docs/planes/PLAN-GATEWAY-FRAGMENTOS.md: recibir, descifrar y lwIP en el
// mismo hilo, sin cola ni despertar en el medio). send() y stats() son thread-safe;
// onPacket se llama desde el hilo que corre el bucle, sin el candado tomado.
//
// En tanda (F5, #15 #16 #20): se recibe de a tandas (recvmmsg en Linux; en los demás,
// recvfrom hasta vaciar), los datos de la tanda se descifran juntos (en este hilo, o en
// paralelo si alguien enchufa un DescifradorParalelo: #16, preparado, sin usar mientras no
// se mida como cuello) y se entregan en el orden en que llegaron; lo que se cifra entre
// empezarTanda() y terminarTanda() sale junto (sendmmsg en Linux). Los búferes se reusan:
// nada de un std::vector por paquete.
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

/// #16 (preparado): un mensaje de datos de una tanda, para descifrarlo. `plano` tiene lugar
/// para `largo` - 16 bytes. Quien descifra pone `ok`.
struct DescifradoWg {
    const uint8_t* cifrado = nullptr; ///< lo cifrado (sin la cabecera de 16 bytes)
    size_t largo = 0;                 ///< largo de `cifrado` (con la etiqueta de 16)
    uint64_t contador = 0;
    Key32 clave{};
    uint8_t* plano = nullptr;
    bool ok = false;
    /// Lo que hace cada uno (ChaCha20-Poly1305): lo llama el descifrador, en cualquier hilo.
    void descifrar();
};

/// #16 (preparado, sin implementación: solo si F0 muestra el descifrado como cuello). Con un
/// solo par (el VPS) todo cae en un hilo; si satura, un descifrador reparte la tanda entre
/// varios hilos (cada DescifradoWg es independiente) y vuelve cuando terminaron todos: el
/// orden lo mantiene WgDevice (entrega en el orden de llegada; el anti-replay se marca al
/// entregar).
class DescifradorParalelo {
   public:
    virtual ~DescifradorParalelo() = default;
    virtual void descifrar(DescifradoWg* v, size_t n) = 0;
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

    // --- F5: el bucle en el hilo de otro (IpStack) --------------------------------------
    /// Para el hilo propio: desde ahora el bucle lo corre quien llama, con servir(). Devuelve
    /// el socket (para esperarlo con poll junto a lo suyo); -1 si no arrancó.
    long long cederBucle();
    /// Vuelve el hilo propio (al parar IpStack).
    void retomarBucle();
    /// Una vuelta del bucle: si `legible`, vacía el socket en tanda, descifra y entrega
    /// (onPacket, en este hilo); y los temporizadores. Desde el hilo que tomó el bucle.
    void servir(bool legible);
    /// Lo que se cifra entre una y otra sale junto al final (sendmmsg en Linux).
    void empezarTanda();
    void terminarTanda();
    /// #16: descifrar las tandas en paralelo (nullptr = en el hilo del bucle, de fábrica).
    void setDescifradorParalelo(std::shared_ptr<DescifradorParalelo> d);

   private:
    /// Un datagrama recibido en la tanda (en rxBuf_).
    struct Rx {
        size_t off = 0, n = 0;
        uint32_t ip = 0;
        uint16_t port = 0;
    };
    /// Un mensaje de datos de la tanda, con lo que hace falta para entregarlo.
    struct DatoTanda {
        uint32_t localIndex = 0;
        uint32_t ip = 0;
        uint16_t port = 0;
        size_t planoOff = 0;
    };
    int recibirTanda();
    void prepararDatoLocked(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void procesarTandaLocked(std::unique_lock<std::mutex>& lock, int64_t now);
    void enviarSalidaLocked();
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
    void handleInitiation(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void handleResponse(const uint8_t* d, size_t n, uint32_t ip, uint16_t port, int64_t now);
    void handleCookie(const uint8_t* d, size_t n);
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
    bool externo_ = false; ///< el bucle lo corre otro (cederBucle)
    int64_t lastResolve_ = 0;

    // F5: búferes que se reusan (sin un std::vector por paquete).
    static constexpr size_t kTanda = 64;     ///< datagramas por tanda
    static constexpr size_t kRxMax = 2048;   ///< un mensaje de WireGuard con MTU 1420 entra holgado
    std::vector<uint8_t> rxBuf_;             ///< kTanda * kRxMax (solo el hilo del bucle)
    std::vector<Rx> rx_;                     ///< (solo el hilo del bucle)
    std::vector<DescifradoWg> descifrar_;    ///< la tanda de datos (con m_)
    std::vector<DatoTanda> datos_;           ///< (con m_)
    std::vector<uint8_t> planoTanda_;        ///< los descifrados de la tanda (con m_)
    std::vector<uint8_t> planoEnvio_;        ///< lo que se cifra (con m_)
    std::vector<uint8_t> msgEnvio_;          ///< (con m_)
    bool enTanda_ = false;                   ///< sendRawLocked junta en vez de mandar (con m_)
    std::vector<uint8_t> salida_;            ///< lo cifrado de la tanda, uno detrás del otro (con m_)
    std::vector<size_t> salidaLargos_;       ///< (con m_)
    std::shared_ptr<DescifradorParalelo> paralelo_; ///< (con m_)
};

} // namespace ba::gateway::net
