// ============================================================================
// src/gateway/net/TlsTerminator.hpp — HTTPS para los invitados (mbedtls)
//
// Proposito:
//   Los navegadores solo dan camara/microfono en paginas SEGURAS (https o
//   localhost). Un invitado que entra por la IP de la red local o por la IP
//   publica del tunel necesita HTTPS. En vez de meter TLS en el servidor de la
//   sala, se pone ADELANTE: acepta TLS, descifra y reenvia en claro a
//   127.0.0.1:<puerto del servidor> con cabecera PROXY v1 (la IP real del
//   invitado). El servidor de la sala no cambia.
//
//   Certificado:
//     - uno propio si esta en Credenciales/ (tls-propio-cert/-clave; se
//       importa dejando cert.pem + key.pem en esa carpeta, p. ej. uno de
//       Let's Encrypt para el dominio que apunta a la IP publica): sin avisos
//       en el navegador;
//     - si no, uno autofirmado (ECDSA P-256) generado una vez y reutilizado
//       (tls-autofirmado-*), con los nombres/IPs pedidos. El navegador avisa
//       la primera vez; aceptado, la camara funciona.
//   Todo cifrado en Credenciales/ (security/CredentialStore).
//
//   Si la conexion viene del puente del tunel (127.0.0.1) con su propia
//   cabecera PROXY, se respeta la IP original que trae.
//
// Threading: un thread de accept y uno por conexion.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ba::gateway::net {

class TlsTerminator {
   public:
    using LogFn = std::function<void(const std::string&)>;

    struct Config {
        std::string bindAddress = "127.0.0.1";
        uint16_t port = 8789;
        uint16_t backendPort = 8787;    ///< servidor HTTP de la sala (127.0.0.1)
        std::string credentialsDir;     ///< <datos>/Credenciales
        std::vector<std::string> names; ///< SAN del autofirmado (DNS o IP)
    };

    explicit TlsTerminator(Config cfg, LogFn log = {});
    ~TlsTerminator();
    TlsTerminator(const TlsTerminator&) = delete;
    TlsTerminator& operator=(const TlsTerminator&) = delete;

    bool start();
    void stop();
    uint16_t port() const { return boundPort_; }
    /// Huella SHA-256 del certificado en uso (para mostrarla al operador).
    const std::string& fingerprint() const { return fingerprint_; }
    bool selfSigned() const { return selfSigned_; }

   private:
    struct Impl;
    void acceptLoop();
    void serve(long long fd, uint32_t peerIp, uint16_t peerPort);

    Config cfg_;
    LogFn log_;
    std::unique_ptr<Impl> impl_;
    long long listenFd_ = -1;
    uint16_t boundPort_ = 0;
    std::string fingerprint_;
    bool selfSigned_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<int> active_{0};
    std::thread accept_;
};

} // namespace ba::gateway::net
