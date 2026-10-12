// ============================================================================
// src/gateway/proxy/ProxyNombres.hpp — el proxy por nombre: un DNS por servicio (T61)
//
// «Una especie de Traefik propio, muy rápido.» Atiende el 443 (TLS) y el 80:
//
//   - 443: lee el nombre que pide el navegador (SNI), le presenta el certificado
//     de ESE nombre (el de Let's Encrypt si ya lo hay; si no, el autofirmado de la
//     casa), descifra y pasa los bytes tal cual al servicio de adentro
//     (127.0.0.1:<puerto>), con la cabecera PROXY v1 (la IP real) cuando el
//     servicio la entiende. HTTP/1.1 y WebSocket pasan sin tocarse (es un caño de
//     bytes después del primer pedido). Un servicio «solo red local» rechaza lo
//     que venga de afuera. El primer pedido a «/» de un servicio con su propia
//     ruta (sesion.<dominio> → /sesion/) se redirige.
//   - 80: contesta los desafíos HTTP-01 de ACME (/.well-known/acme-challenge/) y
//     todo lo demás lo manda a https.
//
// RÁPIDO: un solo hilo con poll() para todas las conexiones (sin un hilo por
// conexión), sockets no bloqueantes, TLS de mbedtls no bloqueante, buffers por
// dirección con contrapresión (256 KB). Va bien con cientos o miles de
// conexiones; para muchas más, el mismo bucle se pasa a kqueue/epoll/IOCP.
//
// Los certificados se cambian en caliente (ponerCertificado): las conexiones
// nuevas toman el nuevo; las que están andando siguen con el suyo.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway::proxy {

class ProxyNombres {
   public:
    using LogFn = std::function<void(const std::string&)>;

    struct Servicio {
        std::string nombre;       ///< "sesion" (el host es nombre.dominio)
        uint16_t destino = 8787;  ///< puerto en 127.0.0.1
        std::string ruta = "/";   ///< a dónde va «/» (p. ej. /sesion/)
        bool soloLan = false;     ///< rechazar lo que no venga de la red local
        bool cabeceraProxy = true; ///< el servicio entiende PROXY v1 (el de la sala sí)
    };
    struct Config {
        std::string bind = "0.0.0.0";
        uint16_t puertoTls = 443;
        uint16_t puertoHttp = 80; ///< 0 = no atender el 80
        std::string dominio;      ///< "midominio.org": los hosts son <servicio>.<dominio>
        std::vector<Servicio> servicios;
        /// Qué servicio atiende si el nombre no coincide con ninguno (o se entra por IP).
        std::string porDefecto = "sala";
    };

    ProxyNombres(Config cfg, LogFn log = {});
    ~ProxyNombres();
    ProxyNombres(const ProxyNombres&) = delete;
    ProxyNombres& operator=(const ProxyNombres&) = delete;

    /// Abre los puertos y arranca el hilo. `autofirmadoPem`/`clavePem`: el certificado
    /// de la casa (para cualquier nombre que no tenga el suyo).
    bool start(const std::string& autofirmadoPem, const std::string& clavePem);
    void stop();

    /// El certificado de `host` (PEM, con la cadena). "" en el cert lo saca (vuelve al de la casa).
    bool ponerCertificado(const std::string& host, const std::string& certPem, const std::string& clavePem,
                          const std::string& origen = "acme");
    /// Desafíos HTTP-01 que contesta el 80: token -> autorización de la clave.
    void ponerDesafio(const std::string& token, const std::string& respuesta);
    void quitarDesafio(const std::string& token);

    /// {activo, puertos, dominio, servicios: [{nombre, host, destino, ruta, soloLan, certificado: {origen, vence}}],
    ///  conexiones, total, rechazadas}.
    nlohmann::json estado() const;
    /// Los hosts de todos los servicios (para pedir los certificados).
    std::vector<std::string> hosts() const;
    const Config& config() const { return cfg_; }

    static bool ipLocal(uint32_t ipOrdenRed);

   private:
    struct Cert;
    struct Conn;
    struct Impl;
    void bucle();
    std::shared_ptr<Cert> certPara(const std::string& host) const;
    const Servicio* servicioPara(const std::string& host) const;

    Config cfg_;
    LogFn log_;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> stop_{false};
    std::thread hilo_;
    mutable std::mutex certMu_;
    std::map<std::string, std::shared_ptr<Cert>> certs_; ///< host -> el suyo
    std::shared_ptr<Cert> casa_;
    std::mutex desafioMu_;
    std::map<std::string, std::string> desafios_;
    std::atomic<int> conexiones_{0};
    std::atomic<std::uint64_t> total_{0}, rechazadas_{0};
};

} // namespace ba::gateway::proxy
