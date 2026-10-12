// ============================================================================
// src/gateway/turn/Ice.hpp — credenciales efímeras y la respuesta de /api/ice
//
// El navegador pide GET /api/ice antes de crear cada RTCPeerConnection
// (assets/web/comun/ice.js) y recibe:
//   {"iceServers":[{"urls":[...],"username":"…","credential":"…"}],
//    "politica":"all"|"relay", "ttl":<segundos>}
// Las credenciales son «TURN REST»: usuario "<vence_unix>:<id>", clave =
// base64(HMAC-SHA1(secreto, usuario)). El servidor (turn/Servidor) las
// verifica sin guardar nada. Contrato completo: docs/funciones/TURN.md.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "turn/Servidor.hpp"

namespace ba::gateway::turn {

struct Credencial {
    std::string usuario;
    std::string clave;
    int64_t vence = 0; // unix
};

/// base64(HMAC-SHA1(secreto, usuario)).
std::string claveDeUsuario(const std::string& secreto, const std::string& usuario);
/// Credencial que vence en `ttlS` segundos. `id` vacío = uno al azar.
/// `ahoraUnix` < 0 = el reloj del sistema (los tests fijan uno).
Credencial credencialEfimera(const std::string& secreto, int ttlS, const std::string& id = {},
                             int64_t ahoraUnix = -1);
/// 32 bytes al azar en hex (64 caracteres): el secreto de un arranque.
std::string secretoAleatorio();

/// El arreglo `iceServers` de RTCPeerConnection, con una sola entrada con
/// credencial. Lo que falte se omite; sin nada, devuelve [].
///   - turns:<hostPublico>:<puertoTls>?transport=tcp  (pinggy tlstcp; 443)
///   - turn:<hostPublico>:<puertoTcp>?transport=tcp   (pinggy tcp; 0 = no hay)
///   - turn:<udpPublico>?transport=udp                ("host:puerto" o "")
nlohmann::json iceServersPara(const Servidor::Config& cfg, const std::string& hostPublico, uint16_t puertoTls,
                              uint16_t puertoTcp, const std::string& udpPublico, int ttlS);

/// Por dónde se llega al TURN.
struct ExtremosIce {
    // Desde afuera (los forwardings de pinggy).
    std::string hostPublico;   // p. ej. "sala.ejemplo.com" (sin esquema ni puerto)
    uint16_t puertoTls = 0;    // tlstcp: 443; 0 = no hay
    uint16_t puertoTcp = 0;    // tcp plano con puerto fijo; 0 = no hay
    std::string tcpPublico;    // si el TCP va por otro host: "host:puerto" (gana sobre hostPublico:puertoTcp)
    std::string udpPublico;    // "host:puerto"; "" = no hay
    // Desde la LAN: solo si el TURN escucha en esa IP (Config::direccion
    // 0.0.0.0 o la IP de la LAN). Sirve para que un navegador de la sala y uno
    // de afuera se vean en la sesión remota (P2P): el de la sala consigue un
    // candidato relay en la caja, el único destino que el de afuera puede usar.
    std::string hostLan;       // "" = sin TURN para la LAN
    uint16_t puertoLanTcp = 0;
    uint16_t puertoLanUdp = 0;
};

/// Cuerpo JSON de GET /api/ice.
///   - turnActivo false → {"iceServers":[],"politica":"all","ttl":0} (lo de hoy).
///   - externa (entró por el túnel) → los extremos públicos y política "relay":
///     sus candidatos host/srflx no le sirven a nadie (el gateway no tiene STUN
///     ni IP pública por pinggy) y con "relay" el navegador no prueba contra
///     IPs privadas del gateway que pueden coincidir con la red de su casa
///     (y no anuncia sus IP privadas). Si no hay ningún extremo público, cae a
///     [] y "all".
///   - LAN → el TURN de la LAN (si hay `hostLan`) con política "all" (los host
///     siguen ganando por prioridad), o [] y "all".
/// `ttlS` < 0 = ajuste "turn.credencialS" (3600). Solo cuenta para asignaciones
/// NUEVAS: una asignación viva se refresca aunque la credencial venza.
std::string respuestaIce(const Servidor::Config& cfg, bool turnActivo, const ExtremosIce& ext, bool externa,
                         int ttlS = -1);

} // namespace ba::gateway::turn
