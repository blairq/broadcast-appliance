// SecretsStore.cpp
//
// Implementación de la bóveda de secretos cifrada en disco (spec §12, D7).
//
//   save(): genera salt + nonce aleatorios (mbedtls entropy), deriva la clave
//           de 32 bytes con Argon2id(passphrase + salt), serializa el estado
//           como JSON {clave: valor} (ordered_json, orden de inserción) y lo
//           cifra con AES-256-GCM (mbedtls). Escribe {header, ciphertext}.
//   load(): lee el archivo, deriva la clave con el salt del header y
//           descifra con AES-256-GCM verificando el tag. Passphrase equivocada
//           -> tag no coincide -> false (fallo limpio, sin throw).
//
// El KDF es Argon2id de la implementación de referencia (P-H-C/
// phc-winner-argon2), elegido para honrar el "Argon2" del spec §12. El
// cifrado y el base64 son de mbedTLS (ya enlazada por ba_core): no se
// reimplementa criptografía a mano.
//
// RT: load()/save() son de control path (E/S de disco + KDF costoso); no se
// llaman en el thread de audio/RT.

#include "security/SecretsStore.hpp"

#include <argon2.h>

#include <mbedtls/base64.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace ba::security {

namespace {

// --- Constantes del formato y de la criptografía ---------------------------

/// Longitud del salt del KDF en bytes (16 = 128 bits, recomendado).
constexpr std::size_t kSaltLen = 16;
/// Longitud del nonce/IV de AES-GCM en bytes (12 = 96 bits, recomendado).
constexpr std::size_t kNonceLen = 12;
/// Longitud del tag de autenticación GCM en bytes (16 = 128 bits).
constexpr std::size_t kTagLen = 16;
/// Longitud de la clave AES-256 en bytes (32).
constexpr std::size_t kKeyLen = 32;

/// Parámetros de Argon2id (OWASP: ~19 MiB, 2 pasadas, 1 lane). Son de control
/// path, así que un costo moderado es aceptable y da buena resistencia.
constexpr std::uint32_t kArgon2TCost = 2;     // pasadas
constexpr std::uint32_t kArgon2MCost = 19456; // KiB (~19 MiB)
constexpr std::uint32_t kArgon2Lanes = 1;     // lanes
constexpr std::uint32_t kArgon2Threads = 1;   // threads máx
constexpr std::uint32_t kArgon2Version = ARGON2_VERSION_13;

/// Nombre del KDF que se escribe en el header (para portabilidad futura).
constexpr const char* kKdfName = "argon2id";

/// Genera `n` bytes aleatorios en `out` usando la entropía de mbedTLS.
///
/// :param out: recibe los bytes aleatorios (se redimensiona a `n`).
/// :param n: cantidad de bytes a generar.
/// :returns: true si se generaron; false si la fuente de entropía falla.
bool randomBytes(std::vector<std::uint8_t>& out, std::size_t n) {
    mbedtls_entropy_context ent;
    mbedtls_entropy_init(&ent);
    out.resize(n);
    const int rc = mbedtls_entropy_func(&ent, out.data(), n);
    mbedtls_entropy_free(&ent);
    return rc == 0;
}

/// Deriva la clave AES-256 de 32 bytes con Argon2id(passphrase + salt).
///
/// :param passphrase: passphrase maestra (no vacía; el caller lo garantiza).
/// :param salt: salt aleatorio de kSaltLen bytes.
/// :param key: recibe la clave derivada de kKeyLen bytes.
/// :returns: true si el KDF terminó con ARGON2_OK; false en caso contrario.
bool deriveKey(const std::string& passphrase, const std::vector<std::uint8_t>& salt,
               std::array<std::uint8_t, kKeyLen>& key) {
    argon2_context ctx{};
    ctx.out = key.data();
    ctx.outlen = static_cast<std::uint32_t>(key.size());
    ctx.pwd = reinterpret_cast<std::uint8_t*>(const_cast<char*>(passphrase.data()));
    ctx.pwdlen = static_cast<std::uint32_t>(passphrase.size());
    ctx.salt = const_cast<std::uint8_t*>(salt.data());
    ctx.saltlen = static_cast<std::uint32_t>(salt.size());
    ctx.secret = nullptr;
    ctx.secretlen = 0;
    ctx.ad = nullptr;
    ctx.adlen = 0;
    ctx.t_cost = kArgon2TCost;
    ctx.m_cost = kArgon2MCost;
    ctx.lanes = kArgon2Lanes;
    ctx.threads = kArgon2Threads;
    ctx.version = kArgon2Version;
    ctx.allocate_cbk = nullptr;
    ctx.free_cbk = nullptr;
    ctx.flags = ARGON2_DEFAULT_FLAGS;

    const int rc = argon2id_ctx(&ctx);
    return rc == ARGON2_OK;
}

/// Codifica `data` en base64 estándar (mbedtls) y lo guarda en `out`.
///
/// :param data: puntero a los bytes.
/// :param len: cantidad de bytes.
/// :param out: recibe la representación base64 (se limpia antes).
/// :returns: true si se codificó; false si mbedtls falla.
bool base64Encode(const std::uint8_t* data, std::size_t len, std::string& out) {
    std::size_t olen = 0;
    // mbedtls_base64_encode necesita un buffer de (len*4/3 + 1) como tope.
    const std::size_t cap = (len * 4) / 3 + 8;
    std::vector<char> buf(cap);
    const int rc =
        mbedtls_base64_encode(reinterpret_cast<unsigned char*>(buf.data()), buf.size(), &olen, data, len);
    if (rc != 0) {
        out.clear();
        return false;
    }
    out.assign(buf.data(), olen);
    return true;
}

/// Decodifica una cadena base64 estándar (mbedtls) a bytes.
///
/// :param in: cadena base64 a decodificar.
/// :param out: recibe los bytes decodificados (se redimensiona).
/// :returns: true si todos los caracteres son válidos; false si hay un
///           carácter fuera del alfabeto base64.
bool base64Decode(const std::string& in, std::vector<std::uint8_t>& out) {
    std::size_t olen = 0;
    // Primer pasada: obtener el tamaño de destino con dst=null. mbedtls
    // devuelve MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL (no 0) en esa pasada, con
    // *olen ya lleno: es el resultado esperado, no un error.
    const int rcSize = mbedtls_base64_decode(nullptr, 0, &olen,
                                             reinterpret_cast<const unsigned char*>(in.data()), in.size());
    if (rcSize != 0 && rcSize != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        out.clear();
        return false;
    }
    out.resize(olen);
    const int rc = mbedtls_base64_decode(out.data(), out.size(), &olen,
                                         reinterpret_cast<const unsigned char*>(in.data()), in.size());
    if (rc != 0) {
        out.clear();
        return false;
    }
    out.resize(olen);
    return true;
}

/// Cifra `plaintext` con AES-256-GCM y produce `ciphertext` + `tag`.
///
/// :param key: clave AES-256 de kKeyLen bytes.
/// :param nonce: nonce/IV de kNonceLen bytes.
/// :param plaintext: datos a cifrar (puede estar vacío).
/// :param ciphertext: recibe el texto cifrado (mismo tamaño que plaintext).
/// :param tag: recibe el tag de autenticación de kTagLen bytes.
/// :returns: true si el cifrado terminó bien; false si mbedtls falla.
bool aesGcmEncrypt(const std::array<std::uint8_t, kKeyLen>& key, const std::vector<std::uint8_t>& nonce,
                   const std::string& plaintext, std::vector<std::uint8_t>& ciphertext,
                   std::vector<std::uint8_t>& tag) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc =
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), static_cast<unsigned int>(kKeyLen * 8));
    if (rc == 0) {
        ciphertext.resize(plaintext.size());
        tag.resize(kTagLen);
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintext.size(), nonce.data(),
                                       nonce.size(), nullptr, 0, // sin associated data
                                       reinterpret_cast<const unsigned char*>(plaintext.data()),
                                       ciphertext.data(), kTagLen, tag.data());
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        ciphertext.clear();
        tag.clear();
        return false;
    }
    return true;
}

/// Descifra `ciphertext` con AES-256-GCM y verifica el `tag`.
///
/// :param key: clave AES-256 de kKeyLen bytes.
/// :param nonce: nonce/IV de kNonceLen bytes.
/// :param ciphertext: texto cifrado a descifrar.
/// :param tag: tag de autenticación de kTagLen bytes a verificar.
/// :param plaintext: recibe el texto plano descifrado.
/// :returns: true si el descifrado y la verificación del tag tuvieron éxito;
///           false si el tag no coincide (passphrase equivocada o archivo
///           alterado) o si mbedtls falla.
bool aesGcmDecrypt(const std::array<std::uint8_t, kKeyLen>& key, const std::vector<std::uint8_t>& nonce,
                   const std::vector<std::uint8_t>& ciphertext, const std::vector<std::uint8_t>& tag,
                   std::string& plaintext) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc =
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), static_cast<unsigned int>(kKeyLen * 8));
    if (rc == 0) {
        plaintext.resize(ciphertext.size());
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, ciphertext.size(), nonce.data(), nonce.size(), nullptr, 0, // sin associated data
            tag.data(), tag.size(), ciphertext.data(), reinterpret_cast<unsigned char*>(plaintext.data()));
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        plaintext.clear();
        return false;
    }
    return true;
}

} // namespace

// --- SecretsStore -----------------------------------------------------------

SecretsStore::SecretsStore() = default;

bool SecretsStore::save(const std::string& path, const std::string& masterPassphrase) {
    // Precondición: passphrase no vacía.
    if (masterPassphrase.empty()) {
        return false;
    }

    // 1) Salt y nonce aleatorios.
    std::vector<std::uint8_t> salt, nonce;
    if (!randomBytes(salt, kSaltLen) || !randomBytes(nonce, kNonceLen)) {
        return false;
    }

    // 2) Derivar la clave con Argon2id(passphrase + salt).
    std::array<std::uint8_t, kKeyLen> key{};
    if (!deriveKey(masterPassphrase, salt, key)) {
        return false;
    }

    // 3) Serializar el estado como JSON {clave: valor} en orden de inserción.
    nlohmann::ordered_json plain = nlohmann::ordered_json::object();
    for (const auto& [k, v] : entries_) {
        plain[k] = v;
    }
    const std::string plaintext = plain.dump();

    // 4) Cifrar el plaintext con AES-256-GCM.
    std::vector<std::uint8_t> ciphertext, tag;
    if (!aesGcmEncrypt(key, nonce, plaintext, ciphertext, tag)) {
        return false;
    }

    // 5) Empaquetar: blob = ciphertext || tag, todo en base64.
    std::vector<std::uint8_t> blob = ciphertext;
    blob.insert(blob.end(), tag.begin(), tag.end());
    std::string saltB64, nonceB64, blobB64;
    if (!base64Encode(salt.data(), salt.size(), saltB64) ||
        !base64Encode(nonce.data(), nonce.size(), nonceB64) ||
        !base64Encode(blob.data(), blob.size(), blobB64)) {
        return false;
    }

    // 6) Armar el documento {header, ciphertext} con ordered_json.
    nlohmann::ordered_json header = nlohmann::ordered_json::object();
    header["salt"] = saltB64;
    header["nonce"] = nonceB64;
    header["kdf"] = kKdfName;
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["t_cost"] = kArgon2TCost;
    params["m_cost"] = kArgon2MCost;
    params["lanes"] = kArgon2Lanes;
    params["threads"] = kArgon2Threads;
    params["version"] = kArgon2Version;
    header["params"] = params;

    nlohmann::ordered_json doc = nlohmann::ordered_json::object();
    doc["header"] = header;
    doc["ciphertext"] = blobB64;

    // 7) Escribir a disco (modo binario, truncando el archivo previo).
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return false;
    }
    ofs << doc.dump(2);
    ofs.close();
    return ofs.good();
}

bool SecretsStore::load(const std::string& path, const std::string& masterPassphrase) {
    // Precondición: passphrase no vacía.
    if (masterPassphrase.empty()) {
        return false;
    }

    // 1) Leer el archivo completo.
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // 2) Parsear el JSON del documento (no-throw: catch cualquier excepción).
    nlohmann::ordered_json doc;
    try {
        doc = nlohmann::ordered_json::parse(raw);
    } catch (...) {
        return false;
    }
    if (!doc.is_object() || !doc.contains("header") || !doc.contains("ciphertext")) {
        return false;
    }
    const auto& header = doc["header"];
    if (!header.is_object() || !header.contains("salt") || !header.contains("nonce") ||
        !header.contains("kdf") || !header.contains("params")) {
        return false;
    }

    // 3) Extraer y decodificar salt, nonce y blob (ciphertext || tag).
    std::string saltB64, nonceB64, blobB64;
    try {
        saltB64 = header["salt"].get<std::string>();
        nonceB64 = header["nonce"].get<std::string>();
        blobB64 = doc["ciphertext"].get<std::string>();
    } catch (...) {
        return false;
    }
    std::vector<std::uint8_t> salt, nonce, blob;
    if (!base64Decode(saltB64, salt) || !base64Decode(nonceB64, nonce) || !base64Decode(blobB64, blob)) {
        return false;
    }
    if (blob.size() < kTagLen) {
        return false; // el blob no alcanza para contener el tag
    }

    // 4) Separar ciphertext y tag del blob.
    const std::size_t ctLen = blob.size() - kTagLen;
    std::vector<std::uint8_t> ciphertext(blob.begin(), blob.begin() + ctLen);
    std::vector<std::uint8_t> tag(blob.end() - kTagLen, blob.end());

    // 5) Derivar la clave con el salt del header y la passphrase.
    std::array<std::uint8_t, kKeyLen> key{};
    if (!deriveKey(masterPassphrase, salt, key)) {
        return false;
    }

    // 6) Descifrar y verificar el tag. Passphrase equivocada -> tag no
    //    coincide -> false (fallo limpio, sin throw).
    std::string plaintext;
    if (!aesGcmDecrypt(key, nonce, ciphertext, tag, plaintext)) {
        return false;
    }

    // 7) Parsear el plaintext como JSON {clave: valor} y llenar el estado.
    nlohmann::ordered_json plain;
    try {
        plain = nlohmann::ordered_json::parse(plaintext);
    } catch (...) {
        return false;
    }
    if (!plain.is_object()) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> next;
    next.reserve(plain.size());
    for (auto it = plain.begin(); it != plain.end(); ++it) {
        if (!it.value().is_string()) {
            return false; // formato inesperado (el valor no es string)
        }
        next.emplace_back(it.key(), it.value().get<std::string>());
    }
    entries_ = std::move(next);
    return true;
}

bool SecretsStore::set(const std::string& key, const std::string& value) {
    // Precondición: clave no vacía.
    if (key.empty()) {
        return false;
    }
    // Si la clave ya existe, actualizar el valor en su posición original
    // (no mover la clave al final, para preservar el orden de inserción).
    for (auto& e : entries_) {
        if (e.first == key) {
            e.second = value;
            return true;
        }
    }
    entries_.emplace_back(key, value);
    return true;
}

bool SecretsStore::get(const std::string& key, std::string& out) const {
    for (const auto& e : entries_) {
        if (e.first == key) {
            out = e.second;
            return true;
        }
    }
    out.clear();
    return false;
}

std::size_t SecretsStore::size() const { return entries_.size(); }

} // namespace ba::security
