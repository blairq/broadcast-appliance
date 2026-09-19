// ============================================================================
// src/core/security/CredentialStore.cpp — ver CredentialStore.hpp
// ============================================================================

#include "security/CredentialStore.hpp"

#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

#include "platform/Paths.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

namespace ba::security {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[8] = {'B', 'A', 'C', 'R', 'E', 'D', '1', '\n'};
constexpr std::size_t kNonce = 12;
constexpr std::size_t kTag = 16;
constexpr const char* kKeyFile = "clave-equipo";
constexpr const char* kExt = ".cred";

bool random(unsigned char* out, std::size_t n) {
    mbedtls_entropy_context ent;
    mbedtls_entropy_init(&ent);
    bool ok = true;
    while (ok && n > 0) {
        const std::size_t chunk = std::min<std::size_t>(n, 32);
        ok = mbedtls_entropy_func(&ent, out, chunk) == 0;
        out += chunk;
        n -= chunk;
    }
    mbedtls_entropy_free(&ent);
    return ok;
}

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

/// Escritura atomica: temporal en la misma carpeta + rename.
bool writeAtomic(const fs::path& p, const std::string& data) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseKey(const std::string& text, unsigned char out[32]) {
    if (text.size() < 64) return false;
    for (int i = 0; i < 32; ++i) {
        const int hi = hexVal(text[2 * i]);
        const int lo = hexVal(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>(hi * 16 + lo);
    }
    return true;
}

} // namespace

CredentialStore::CredentialStore(fs::path dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    fs::permissions(dir_, fs::perms::owner_all, fs::perm_options::replace, ec);
}

CredentialStore CredentialStore::bundle() { return CredentialStore(platform::credentialsDir()); }

bool CredentialStore::validName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name[0] == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

fs::path CredentialStore::pathOf(const std::string& name) const { return dir_ / (name + kExt); }

bool CredentialStore::key(unsigned char out[32]) const {
    const fs::path p = dir_ / kKeyFile;
    // Otro proceso puede estar creandola en este instante: se reintenta un
    // momento si esta a medio escribir.
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::string text;
        if (readAll(p, text) && parseKey(text, out)) return true;
        if (!fs::exists(p)) {
            unsigned char raw[32];
            if (!random(raw, sizeof(raw))) return false;
            static const char* kHex = "0123456789abcdef";
            std::string hex;
            for (unsigned char b : raw) {
                hex += kHex[b >> 4];
                hex += kHex[b & 15];
            }
            hex += "\n";
            mbedtls_platform_zeroize(raw, sizeof(raw));
            // Creacion exclusiva: si otro proceso gano, se usa la suya.
#ifdef _WIN32
            // El CRT de Microsoft no acepta "x" en fopen (aborta el proceso por
            // parametro invalido): O_CREAT|O_EXCL con _wopen y despues FILE*.
            FILE* f = nullptr;
            const int fd =
                _wopen(p.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
            if (fd >= 0 && (f = _fdopen(fd, "wb")) == nullptr) _close(fd);
#else
            FILE* f = std::fopen(p.c_str(), "wbx");
#endif
            if (f != nullptr) {
                const bool ok = std::fwrite(hex.data(), 1, hex.size(), f) == hex.size();
                std::fflush(f);
                std::fclose(f);
                std::error_code ec;
                fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace,
                                ec);
                if (ok && parseKey(hex, out)) return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool CredentialStore::read(const std::string& name, std::string& out) const {
    out.clear();
    if (!validName(name)) return false;
    std::string blob;
    if (!readAll(pathOf(name), blob)) return false;
    if (blob.size() < sizeof(kMagic) + kNonce + kTag || std::memcmp(blob.data(), kMagic, sizeof(kMagic)) != 0)
        return false;
    unsigned char k[32];
    if (!key(k)) return false;
    const auto* nonce = reinterpret_cast<const unsigned char*>(blob.data() + sizeof(kMagic));
    const auto* tag = nonce + kNonce;
    const auto* ct = tag + kTag;
    const std::size_t ctLen = blob.size() - sizeof(kMagic) - kNonce - kTag;
    std::string plain(ctLen, '\0');
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, k, 256);
    if (rc == 0)
        rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, nonce, kNonce,
                                      reinterpret_cast<const unsigned char*>(name.data()), name.size(), tag,
                                      kTag, ct, reinterpret_cast<unsigned char*>(plain.data()));
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(k, sizeof(k));
    if (rc != 0) {
        mbedtls_platform_zeroize(plain.data(), plain.size());
        return false;
    }
    out = std::move(plain);
    return true;
}

bool CredentialStore::write(const std::string& name, const std::string& data) const {
    if (!validName(name)) return false;
    unsigned char k[32];
    if (!key(k)) return false;
    std::string blob(sizeof(kMagic) + kNonce + kTag + data.size(), '\0');
    std::memcpy(blob.data(), kMagic, sizeof(kMagic));
    auto* nonce = reinterpret_cast<unsigned char*>(blob.data() + sizeof(kMagic));
    auto* tag = nonce + kNonce;
    auto* ct = tag + kTag;
    bool ok = random(nonce, kNonce);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    ok = ok && mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, k, 256) == 0;
    ok = ok &&
         mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, data.size(), nonce, kNonce,
                                   reinterpret_cast<const unsigned char*>(name.data()), name.size(),
                                   reinterpret_cast<const unsigned char*>(data.data()), ct, kTag, tag) == 0;
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(k, sizeof(k));
    return ok && writeAtomic(pathOf(name), blob);
}

bool CredentialStore::remove(const std::string& name) const {
    if (!validName(name)) return false;
    std::error_code ec;
    return fs::remove(pathOf(name), ec) || !fs::exists(pathOf(name));
}

bool CredentialStore::exists(const std::string& name) const {
    return validName(name) && fs::exists(pathOf(name));
}

std::vector<std::string> CredentialStore::list() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (e.path().extension() == kExt) out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool CredentialStore::migrate(const std::string& name, const fs::path& legacy, bool overwrite) const {
    std::error_code ec;
    if (legacy.empty() || !fs::is_regular_file(legacy, ec)) return false;
    if (overwrite || !exists(name)) {
        std::string data;
        if (!readAll(legacy, data) || !write(name, data)) return false;
        mbedtls_platform_zeroize(data.data(), data.size());
    }
    // El original en claro no queda: se pisa con ceros antes de borrarlo.
    if (const auto size = fs::file_size(legacy, ec); !ec && size > 0) {
        std::ofstream z(legacy, std::ios::binary | std::ios::in | std::ios::out);
        const std::string zeros(static_cast<std::size_t>(size), '\0');
        z.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    fs::remove(legacy, ec);
    return true;
}

std::vector<std::string> CredentialStore::importLegacy(const fs::path& dataRoot) const {
    struct Item {
        const char* legacy;
        const char* name;
        bool overwrite;
    };
    static const Item kItems[] = {
        {"Config/youtube-client.json", "youtube-client", false},
        {"Config/youtube-token.json", "youtube-token", false},
        {"Config/twitch-client.json", "twitch-client", false},
        {"Config/twitch-token.json", "twitch-token", false},
        {"Config/facebook-client.json", "facebook-client", false},
        {"Config/facebook-token.json", "facebook-token", false},
        {"Config/mcp-token", "mcp-token", false},
        {"Config/wireguard.key", "wireguard-key", false},
        {"Config/tls/selfsigned-cert.pem", "tls-autofirmado-cert", false},
        {"Config/tls/selfsigned-key.pem", "tls-autofirmado-clave", false},
        {"Config/tls/selfsigned-names.txt", "tls-autofirmado-nombres", false},
        {"Config/tls/cert.pem", "tls-propio-cert", false},
        {"Config/tls/key.pem", "tls-propio-clave", false},
        // Certificado propio nuevo: se deja en Credenciales/ y reemplaza al anterior.
        {"Credenciales/cert.pem", "tls-propio-cert", true},
        {"Credenciales/key.pem", "tls-propio-clave", true},
    };
    std::vector<std::string> out;
    if (dataRoot.empty()) return out;
    for (const Item& it : kItems) {
        if (migrate(it.name, dataRoot / it.legacy, it.overwrite)) out.emplace_back(it.name);
    }
    std::error_code ec;
    fs::remove(dataRoot / "Config" / "tls", ec); // solo si quedo vacia
    return out;
}

} // namespace ba::security
