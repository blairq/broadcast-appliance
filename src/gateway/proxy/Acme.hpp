// ============================================================================
// src/gateway/proxy/Acme.hpp — certificados automáticos (ACME, RFC 8555) para el proxy
//
// Pide a Let's Encrypt (o a cualquier CA ACME) el certificado de los nombres del
// proxy con el desafío HTTP-01: la CA pide http://<nombre>/.well-known/
// acme-challenge/<token> y el proxy contesta por el puerto 80 (ProxyNombres::
// ponerDesafio). Hace falta que el nombre apunte a esta máquina y que el 80 llegue
// desde internet (o por el túnel); para una red local sin puerto abierto queda
// el certificado de la casa.
//
// La cuenta ACME es una clave ECDSA P-256 que se guarda (cifrada) y se reusa. Todo
// el pedido es bloqueante (decenas de segundos): se corre en un hilo aparte.
//
// JOSE: JWS con ES256 (firma r‖s), «jwk» para crear la cuenta y «kid» después;
// la autorización del desafío es token.base64url(SHA-256 de la JWK canónica).
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ba::gateway::proxy {

class ClienteAcme {
   public:
    using LogFn = std::function<void(const std::string&)>;
    /// Poner (true) o sacar (false) la respuesta de un desafío HTTP-01.
    using Desafio = std::function<void(const std::string& token, const std::string& respuesta, bool poner)>;

    static constexpr const char* kLetsEncrypt = "https://acme-v02.api.letsencrypt.org/directory";
    static constexpr const char* kLetsEncryptPruebas = "https://acme-staging-v02.api.letsencrypt.org/directory";

    struct Resultado {
        bool ok = false;
        std::string cert;  ///< PEM con la cadena
        std::string clave; ///< PEM (nueva para cada pedido)
        std::string error;
    };

    /// `cuentaPem`: la clave de la cuenta (vacía = se crea una; ver cuentaPem()).
    ClienteAcme(std::string directorio, std::string email, std::string cuentaPem, LogFn log = {});

    /// Pide un certificado para `nombres`. Bloqueante.
    Resultado pedir(const std::vector<std::string>& nombres, const Desafio& desafio);
    /// La clave de la cuenta (para guardarla si se creó).
    const std::string& cuentaPem() const { return cuenta_; }
    /// token.huella: lo que contesta el desafío (público para las pruebas).
    std::string autorizacion(const std::string& token) const;

   private:
    struct Http {
        int status = 0;
        std::string cuerpo, nonce, location, error;
    };
    bool directorio(std::string& err);
    Http firmado(const std::string& url, const std::string& payload, bool conJwk);
    std::string jwk() const;

    std::string dirUrl_, email_, cuenta_;
    LogFn log_;
    std::string nuevoNonce_, nuevaCuenta_, nuevaOrden_;
    std::string nonce_, kid_;
};

} // namespace ba::gateway::proxy
