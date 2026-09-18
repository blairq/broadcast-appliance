// ============================================================================
// src/gateway/net/NetUtil.hpp — Direcciones IPv4 locales
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::gateway::net {

/// IPv4 de las interfaces activas (sin loopback), en texto.
std::vector<std::string> localIPv4s();

/// Parsea "a.b.c.d/nn" -> ip (orden de host) y prefijo. false si no es valida.
bool parseCidr(const std::string& cidr, uint32_t& ip, int& prefix);

/// Separa "host:puerto" (puerto por defecto si falta).
void splitHostPort(const std::string& s, std::string& host, uint16_t& port, uint16_t defPort);

} // namespace ba::gateway::net
