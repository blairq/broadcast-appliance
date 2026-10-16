// ============================================================================
// src/gateway/Direcciones.hpp — LA fuente de las direcciones de los servicios
//
// Proposito (docs/planes/PLAN-DIRECCIONES.md):
//   Todo enlace que se muestra, se manda a un aparato o va en un QR sale de acá:
//   la app, los *-link de la API de control, el QR de ingreso, el MCP. Antes se
//   armaban en ~13 lugares con reglas distintas y terminaban en 127.0.0.1 o en
//   el dominio del túnel para gente que estaba en la sala.
//
//   Reglas:
//     - Cada servicio tiene su dirección de la RED LOCAL y, aparte, la DE AFUERA
//       (túnel). Nunca se da 127.0.0.1 como enlace: si un servicio no llega desde
//       la red, `local` es null con el motivo.
//     - Los servicios de la sala (tablets, presidencia, integrantes, televisores,
//       teleprompter…) van por la PUERTA DE LA SALA (HTTPS / HTTP propios, en la
//       red aunque la sala de invitados esté cerrada). Los invitados, por la
//       escucha de la sala (solo si «Acceso» la abre a la red).
//     - Lo de afuera, solo con un túnel listo de verdad y solo para los servicios
//       que lo atienden por el túnel.
//
//   `armar` es pura (entradas → catálogo): los tests la recorren con todas las
//   combinaciones. Quien junta las entradas (main.cpp) las toma frescas: las IP
//   de la máquina se releen, el túnel se mira cada vez.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::gateway::direcciones {

struct Entradas {
    std::vector<std::string> ips; ///< IPv4 de la máquina, la mejor primero (net::localIPv4s)
    std::string urlHost;          ///< Configuración → «Dirección para el QR» ("" = la elige sola)
    std::string bind;             ///< Configuración → «Acceso»: 127.0.0.1 | 0.0.0.0 | una IP
    int puertaHttps = 0;          ///< la puerta de la sala, HTTPS (0 = no abrió)
    int puertaHttp = 0;           ///< la puerta de la sala, HTTP (televisores, Companion)
    int invitadosHttps = 0;       ///< HTTPS de la sala de invitados en la red (0 = no escucha en la red)
    int invitadosHttp = 0;        ///< HTTP de la sala en la red (sin HTTPS: sin cámara)
    std::string remoto;           ///< origen de afuera ("https://x.a.pinggy.link"), "" = no hay
    std::string via;              ///< pinggy | cloudflare | wireguard
    std::string estadoRemoto;     ///< listo | reconectando
    std::string motivoRemoto;     ///< por qué no hay remoto (si no hay)
    bool controlRemoto = false;   ///< el control web habilitado desde afuera (con PIN)
    bool eventoWeb = false;       ///< hay una página del evento publicada (la raíz)
    std::string huella;           ///< SHA-256 del certificado de la puerta (el mismo de la sala)
};

/// El host de la red local para los enlaces: el elegido (si no es de esta misma
/// máquina), la IP de «Acceso» si es una sola, o la mejor de la máquina. "" = ninguna.
std::string hostLocal(const Entradas& e);

/// El catálogo (ver PLAN-DIRECCIONES.md):
///   {local: {ip, ips, https, http} | null, motivo_local, remoto: {url, via, estado} | null,
///    motivo_remoto, servicios: [{id, nombre, local, remoto, motivo_local, motivo_remoto}]}
nlohmann::json armar(const Entradas& e);

/// El ORIGEN (esquema://host:puerto, sin ruta) de un servicio. `donde`: "local" |
/// "remote". {origen} o {error: "local"|"remote", message}.
nlohmann::json origenDe(const nlohmann::json& catalogo, const std::string& servicio, const std::string& donde);

} // namespace ba::gateway::direcciones
