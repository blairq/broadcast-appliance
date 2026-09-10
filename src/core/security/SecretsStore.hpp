// SecretsStore.hpp
//
// Proposito:
//   Bóveda de secretos cifrada en disco (spec §12, decisión D7): guarda pares
//   clave->valor (stream key, token del túnel, credenciales TURN, refresh
//   token de OAuth, ...) en `Config/secrets.json` cifrados con AES-256-GCM
//   (mbedTLS) bajo una clave derivada de la passphrase maestra del ingeniero
//   con Argon2id (implementación de referencia P-H-C/phc-winner-argon2).
//   El plaintext es un objeto JSON {clave: valor} (nlohmann ordered_json), de
//   modo que el orden de inserción se conserva y el formato es legible para
//   herramientas. No depende de Qt ni de libobs.
//
//   Formato en disco (nlohmann ordered_json, un único objeto):
//     {
//       "header": {
//         "salt":   "<base64 16 bytes>",   // salt aleatorio del KDF
//         "nonce":  "<base64 12 bytes>",   // IV/nonce de AES-GCM
//         "kdf":    "argon2id",            // identificador del KDF usado
//         "params": { "t_cost":.., "m_cost":.., "lanes":..,
//                     "threads":.., "version":.. }
//       },
//       "ciphertext": "<base64>"           // AES-256-GCM(plaintext)
//     }
//   La clave de 32 bytes se deriva de (passphrase + salt) con Argon2id; con
//   esa clave y el nonce se cifra el plaintext JSON. Un `load` con la
//   passphrase equivocada falla LIMPIAMENTE (el tag de autenticación GCM no
//   coincide) y devuelve false, sin lanzar.
//
// Modelo de threading:
//   Una instancia NO es thread-safe por sí misma: la usa un solo owner. El
//   estado (mapa de secretos en memoria) es mutable sin lock. load()/save()
//   son de CONTROL PATH: hacen E/S de disco y KDF (Argon2id, costoso) y por
//   tanto NO se llaman en el thread de audio/RT; se llaman al arrancar o al
//   persistir, desde el thread de control. set()/get()/size() solo tocan la
//   memoria en RAM y son baratas, pero igualmente se asume un solo owner.
//
// Seguridad / RT:
//   - Cifrado en reposo: los valores NUNCA aparecen en claro en el archivo
//     (lo comprueba test_secrets sobre los bytes crudos del archivo).
//   - Autenticación: AES-GCM aporta integridad; cualquier alteración del
//     ciphertext o del header (salt/nonce) o una passphrase distinta hacen
//     que `load` devuelva false.
//   - No-throw: ninguna función lanza; los fallos se reportan con false.
//
// Estrategia de errores:
//   No lanza. load()/save() devuelven false ante: passphrase vacía, E/S de
//   disco fallida, JSON mal formado, base64 inválido, KDF fallido o tag GCM
//   no coincidente. set() devuelve false si la clave está vacía. get()
//   devuelve false si la clave no existe. size() devuelve el número de
//   secretos en memoria.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ba::security {

/// Bóveda de secretos cifrada en disco (spec §12, D7).
///
/// Guarda pares clave->valor en memoria y los persiste cifrados
/// (AES-256-GCM + Argon2id) a un archivo JSON. Es de control path (E/S de
/// disco + KDF), no del thread de audio/RT. No lanza: los fallos se reportan
/// con false.
class SecretsStore {
public:
    /// Construye una bóveda vacía (sin secretos en memoria).
    ///
    /// No hace E/S ni KDF: el estado se carga con load() o se llena con
    /// set() y se persiste con save().
    SecretsStore();

    /// Carga y descifra la bóveda desde `path` con `masterPassphrase`.
    ///
    /// Lee el archivo, deriva la clave con Argon2id(passphrase + salt) y
    /// descifra el ciphertext con AES-256-GCM verificando el tag. Si la
    /// passphrase es la equivocada, el tag no coincide y la función devuelve
    /// false (fallo limpio, sin throw). Reemplaza el estado en memoria por el
    /// descifrado.
    ///
    /// :param path: ruta del archivo `secrets.json` a leer.
    /// :param masterPassphrase: passphrase maestra (no vacía).
    /// :returns: true si se leyó y descifró con éxito; false si la passphrase
    ///           es vacía, el archivo no existe / no se puede leer, el JSON o
    ///           el base64 están mal formados, el KDF falla o el tag GCM no
    ///           coincide (passphrase equivocada o archivo alterado).
    bool load(const std::string& path, const std::string& masterPassphrase);

    /// Cifra el estado en memoria y lo escribe a `path` con `masterPassphrase`.
    ///
    /// Genera un salt y un nonce aleatorios, deriva la clave con
    /// Argon2id(passphrase + salt), serializa el estado como JSON
    /// {clave: valor} (ordered) y lo cifra con AES-256-GCM. Escribe el
    /// objeto {header, ciphertext} a `path`. Es de control path (E/S + KDF).
    ///
    /// :param path: ruta del archivo `secrets.json` a escribir.
    /// :param masterPassphrase: passphrase maestra (no vacía).
    /// :returns: true si se cifró y escribió con éxito; false si la passphrase
    ///           es vacía, el KDF falla, el cifrado falla o la escritura a
    ///           disco falla.
    bool save(const std::string& path, const std::string& masterPassphrase);

    /// Inserta o actualiza un secreto en memoria.
    ///
    /// :param key: clave del secreto (no vacía).
    /// :param value: valor (puede ser vacío; se guarda tal cual).
    /// :returns: true si se guardó; false si `key` está vacío.
    bool set(const std::string& key, const std::string& value);

    /// Lee un secreto de la memoria.
    ///
    /// :param key: clave del secreto a buscar.
    /// :param out: recibe el valor (se limpia antes de escribir).
    /// :returns: true si la clave existe (y `out` queda con su valor); false
    ///           si no existe.
    bool get(const std::string& key, std::string& out) const;

    /// Número de secretos guardados en memoria.
    ///
    /// :returns: cantidad de claves distintas en la bóveda.
    std::size_t size() const;

private:
    /// Estado en memoria: lista de pares (clave, valor) en ORDEN DE
    /// INSERCIÓN. Se usa un `std::vector<std::pair<...>>` (no un `std::map`)
    /// para preservar el orden en que se llamaron set(), de modo que save()
    /// pueda serializarlo con `ordered_json` y el archivo refleje ese orden.
    /// `set()` sobre una clave existente actualiza el valor en su posición
    /// original (no mueve la clave al final).
    std::vector<std::pair<std::string, std::string>> entries_;
};

} // namespace ba::security
