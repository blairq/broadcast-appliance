// ============================================================================
// src/core/security/CredentialStore.hpp — Credenciales del equipo, cifradas
//
// Proposito:
//   Todas las credenciales (tokens de YouTube/Twitch/Facebook y sus apps,
//   token MCP, clave WireGuard, clave del certificado HTTPS) viven en UNA
//   carpeta propia, `<datos>/Credenciales/`, aparte de Config/ y del
//   programa: el actualizador nunca la pisa (la respalda antes) y se puede
//   copiar sola para mudar el equipo a otro pendrive.
//
//   Cada credencial es un archivo `<nombre>.cred` cifrado con AES-256-GCM
//   (mbedTLS) con la clave de `Credenciales/clave-equipo` (32 bytes al azar,
//   se crea la primera vez). El nombre va como dato asociado: un archivo
//   renombrado no descifra. Sin contraseña (decision del usuario: el equipo
//   arranca solo): el cifrado evita lecturas casuales en un pendrive FAT/exFAT
//   (sin permisos Unix), pero quien tenga el pendrive entero tiene la clave.
//
//   Formato: "BACRED1\n" (8) | nonce (12) | tag (16) | texto cifrado.
//
// Threading / procesos:
//   A (interfaz) y B (gateway) la usan a la vez. Las escrituras son atomicas
//   (archivo temporal + rename) y la clave se crea con creacion exclusiva
//   (dos procesos que arrancan juntos terminan con la misma). Una instancia
//   no guarda estado mutable: se puede usar desde cualquier thread.
//
// Errores: no lanza; false ante E/S fallida, archivo alterado o clave
// equivocada.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ba::security {

class CredentialStore {
   public:
    /// Almacen en `dir` (se crea si falta).
    explicit CredentialStore(std::filesystem::path dir);

    /// El del equipo: `platform::credentialsDir()`.
    static CredentialStore bundle();

    const std::filesystem::path& dir() const { return dir_; }

    /// Lee y descifra `name`. false si no existe o no descifra.
    bool read(const std::string& name, std::string& out) const;
    /// Cifra y guarda `name` (reemplaza de forma atomica).
    bool write(const std::string& name, const std::string& data) const;
    bool remove(const std::string& name) const;
    bool exists(const std::string& name) const;
    /// Nombres guardados (sin extension), ordenados.
    std::vector<std::string> list() const;

    /// Si `name` no existe y hay un archivo en claro heredado de una version
    /// anterior (`legacy`), lo importa cifrado y borra el original. Devuelve
    /// true si importo algo.
    bool migrate(const std::string& name, const std::filesystem::path& legacy, bool overwrite = false) const;

    /// Importa (y borra) todo lo que versiones anteriores guardaban en claro en
    /// Config/ (tokens de plataformas, mcp-token, wireguard.key, claves TLS), y
    /// el certificado propio que la persona deje en Credenciales/ (cert.pem +
    /// key.pem). Idempotente: lo llaman el launcher y cada proceso al arrancar.
    /// Devuelve los nombres importados.
    std::vector<std::string> importLegacy(const std::filesystem::path& dataRoot) const;

    /// Nombre valido: [a-z0-9._-], 1..64, sin empezar con punto.
    static bool validName(const std::string& name);

   private:
    bool key(unsigned char out[32]) const;
    std::filesystem::path pathOf(const std::string& name) const;

    std::filesystem::path dir_;
};

} // namespace ba::security
