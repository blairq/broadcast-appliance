// ============================================================================
// src/gateway/IpCliente.hpp — LA IP del cliente (una sola regla para todo el gateway)
//
// El tope de intentos por IP de la sesión remota, el bloqueo de PIN de la sala, los
// cupos de conexiones por IP, «en sala» o «afuera», «solo red local» de la botonera y
// del control web: todo se decide con la IP que da `resolver`, nunca con una cabecera
// leída en cada borde. Las cabeceras de un proxy (PROXY v1, X-Forwarded-For,
// CF-Connecting-IP) las escribe cualquiera; solo valen cuando el que las manda es un
// proceso nuestro de esta máquina, y cada una solo en la escucha que le corresponde:
//
//   1. La conexión NO viene de 127.0.0.1: la IP es la del socket, punto. Ninguna
//      cabecera cambia nada (desde la red local nadie se pone otra IP ni se hace «de
//      afuera»). Una línea PROXY desde ahí ni se acepta (SignalingServer corta).
//   2. Viene de 127.0.0.1 con PROXY v1 (ba-web, el terminador HTTPS, el proxy por
//      nombre, el puente de WireGuard, ba-pinggy sin TURN): la IP es la de la línea
//      PROXY, que armó ese proceso con lo que vio. Las cabeceras de IP del pedido se
//      ignoran (son del visitante).
//   3. Escucha del túnel de pinggy, 127.0.0.1 sin PROXY v1 (con el TURN prendido
//      ba-pinggy va sin PROXY): la IP es el ÚLTIMO elemento de la ÚLTIMA línea
//      X-Forwarded-For, el que agregó pinggy; lo de antes lo pudo escribir el visitante
//      y no cuenta. Sin un último válido, 127.0.0.1 (todos los del túnel juntos: se
//      frenan entre ellos, nunca se escapan).
//   4. Escucha común, y lo que quedó todavía es 127.0.0.1 (cloudflared se conecta
//      directo, o por ba-web y su PROXY con 127.0.0.1): si trae cabeceras de Cloudflare
//      es Cloudflare, «afuera»; la IP es CF-Connecting-IP si viene una sola y es válida
//      (si no, 127.0.0.1). Sin esas cabeceras es un programa de la misma máquina.
//
//   X-Real-IP y Forwarded no se usan nunca. La escucha del túnel es «afuera» siempre,
//   venga la IP que venga. IPv6 → una seudo-IP 240.x.y.z por /64 (ver ipDeTexto).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::gateway::ipcliente {

/// Por dónde llegó (para el registro y los tests; las decisiones usan ip/porTunel).
enum class Via { Directa, ProxyLocal, Pinggy, Cloudflare, Maquina };

struct Origen {
    std::uint32_t ip = 0;  ///< la IP que cuenta (orden de red)
    bool porTunel = false; ///< llegó por un túnel (pinggy o Cloudflare): «afuera» aunque la IP sea privada
    Via via = Via::Directa;
};

struct Entrada {
    std::uint32_t ipSocket = 0;  ///< la del accept (orden de red)
    bool escuchaTunel = false;   ///< la escucha del túnel de pinggy (127.0.0.1, puerto propio)
    bool conProxyV1 = false;     ///< llegó una línea PROXY v1 válida (solo se acepta desde 127.0.0.1)
    std::uint32_t ipProxyV1 = 0; ///< la IP de esa línea
    const std::string* cabeceras = nullptr; ///< el pedido HTTP (sin la línea PROXY)
};

/// La regla de arriba. Pura: sin red ni estado.
Origen resolver(const Entrada& e);

/// ¿En la sala? La red local (ipLocal) y no por un túnel.
bool enSala(const Origen& o);

/// Una IP en texto (IPv4, o IPv6: la IPv4 mapeada o una seudo-IP 240.x.y.z estable por
/// /64, que nunca es la red local y separa a los clientes para los límites).
bool ipDeTexto(const std::string& texto, std::uint32_t& ipRed);
/// La IP de una línea PROXY v1 ("PROXY TCP4|TCP6 <src> <dst> <sp> <dp>"). false si no se entiende.
bool ipDeProxyV1(const std::string& linea, std::uint32_t& ipRed);
/// El último elemento de una lista X-Forwarded-For (el que agregó el proxy de adelante).
bool ipDeXff(const std::string& valor, std::uint32_t& ipRed);
/// Todas las líneas de la cabecera `nombre`, en orden (sin distinguir mayúsculas).
std::size_t lineasDe(const std::string& cabeceras, const std::string& nombre, std::string* ultima);

} // namespace ba::gateway::ipcliente
