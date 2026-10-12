// ============================================================================
// src/gateway/proxy/Certificados.hpp — claves, autofirmados, CSR y vencimientos
//
// Lo que necesitan el proxy por nombre (T61) y el cliente ACME, con mbedtls
// (vendoreado): una clave ECDSA P-256 por certificado, el autofirmado «de la
// casa» para la red local (con todos los nombres en el SAN), el pedido de firma
// (CSR) para Let's Encrypt y la fecha en que vence un certificado.
//
// Todo en PEM (texto): así se guarda cifrado en Credenciales/ con el
// CredentialStore, como el resto de los secretos.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::gateway::proxy {

/// Una clave ECDSA P-256 nueva, en PEM. "" si falló.
std::string claveNueva();

/// Un autofirmado para `nombres` (DNS o IP; el primero va de CN), válido 10 años.
/// :returns: el certificado en PEM, o "" si falló.
std::string autofirmado(const std::string& clavePem, const std::vector<std::string>& nombres);

/// El pedido de firma (CSR) en DER para `nombres` (el primero de CN). Vacío si falló.
std::vector<unsigned char> csrDer(const std::string& clavePem, const std::vector<std::string>& nombres);

/// Cuándo vence el (primer) certificado del PEM, en segundos desde 1970 (UTC). 0 si no se pudo leer.
std::int64_t vence(const std::string& certPem);

/// Los nombres DNS del SAN del primer certificado del PEM.
std::vector<std::string> nombresDe(const std::string& certPem);

/// JOSE (ACME): firma ES256 de `datos` con la clave, en crudo (r‖s, 64 bytes). false si falló.
bool firmarEs256(const std::string& clavePem, const std::string& datos, std::string& firma);
/// Las coordenadas de la clave pública (32 bytes cada una) para la JWK.
bool coordenadas(const std::string& clavePem, std::string& x, std::string& y);
/// SHA-256 (32 bytes crudos).
std::string sha256(const std::string& datos);

/// base64url sin relleno (lo que usa ACME/JOSE).
std::string base64url(const unsigned char* p, size_t n);
inline std::string base64url(const std::string& s) {
    return base64url(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

} // namespace ba::gateway::proxy
