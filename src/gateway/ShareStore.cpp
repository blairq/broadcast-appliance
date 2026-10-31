// ============================================================================
// src/gateway/ShareStore.cpp — implementacion de ShareStore (ver el header)
// ============================================================================

#include "ShareStore.hpp"

#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

#include "security/SecretsStore.hpp" // randomSecretHex

namespace ba::gateway {

namespace fs = std::filesystem;

namespace {

bool at(const uint8_t* p, size_t n, size_t off, const char* sig) {
    const size_t len = std::strlen(sig);
    return n >= off + len && std::memcmp(p + off, sig, len) == 0;
}

bool contains(const uint8_t* p, size_t n, const char* needle) {
    const size_t len = std::strlen(needle);
    for (size_t i = 0; i + len <= n; ++i) {
        if (std::memcmp(p + i, needle, len) == 0) return true;
    }
    return false;
}

/// Bytes que hacen falta para decidir el tipo (MPEG-TS mira el byte 188).
constexpr size_t kSniffBytes = 256;

} // namespace

ShareStore::ShareStore(fs::path dir, uint64_t maxBytes) : dir_(std::move(dir)), maxBytes_(maxBytes) {}

void ShareStore::clear() {
    std::error_code ec;
    if (!fs::exists(dir_, ec)) return;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        fs::remove_all(e.path(), ec);
    }
}

bool ShareStore::sniff(const uint8_t* p, size_t n, Kind& out) {
    static const uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 8 && std::memcmp(p, kPng, 8) == 0) {
        out = {"image", "image/png", ".png"};
        return true;
    }
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) {
        out = {"image", "image/jpeg", ".jpg"};
        return true;
    }
    if (at(p, n, 0, "GIF87a") || at(p, n, 0, "GIF89a")) {
        out = {"image", "image/gif", ".gif"};
        return true;
    }
    if (at(p, n, 0, "RIFF")) {
        if (at(p, n, 8, "WEBP")) {
            out = {"image", "image/webp", ".webp"};
            return true;
        }
        if (at(p, n, 8, "WAVE")) {
            out = {"audio", "audio/wav", ".wav"};
            return true;
        }
        if (at(p, n, 8, "AVI ")) {
            out = {"video", "video/x-msvideo", ".avi"};
            return true;
        }
        return false;
    }
    if (at(p, n, 4, "ftyp")) {
        // Familia ISO-BMFF: la marca decide. HEIC/AVIF no los decodifica la
        // fuente de imagen del motor, asi que se rechazan en vez de fallar
        // despues en el aire.
        if (at(p, n, 8, "M4A ") || at(p, n, 8, "M4B ")) {
            out = {"audio", "audio/mp4", ".m4a"};
            return true;
        }
        if (at(p, n, 8, "heic") || at(p, n, 8, "heix") || at(p, n, 8, "mif1") || at(p, n, 8, "avif")) {
            return false;
        }
        if (at(p, n, 8, "qt  ")) {
            out = {"video", "video/quicktime", ".mov"};
            return true;
        }
        out = {"video", "video/mp4", ".mp4"};
        return true;
    }
    if (n >= 4 && p[0] == 0x1A && p[1] == 0x45 && p[2] == 0xDF && p[3] == 0xA3) {
        if (contains(p, n, "webm")) {
            out = {"video", "video/webm", ".webm"};
        } else {
            out = {"video", "video/x-matroska", ".mkv"};
        }
        return true;
    }
    if (at(p, n, 0, "OggS")) {
        out = {"audio", "audio/ogg", ".ogg"};
        return true;
    }
    if (at(p, n, 0, "fLaC")) {
        out = {"audio", "audio/flac", ".flac"};
        return true;
    }
    if (at(p, n, 0, "ID3") || (n >= 2 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0)) {
        out = {"audio", "audio/mpeg", ".mp3"};
        return true;
    }
    if (n > 188 && p[0] == 0x47 && p[188] == 0x47) {
        out = {"video", "video/mp2t", ".ts"};
        return true;
    }
    return false;
}

ShareStore::Result ShareStore::save(uint64_t total, const std::function<int(uint8_t*, size_t)>& read) {
    Result res;
    if (total == 0) {
        res.err = kShort;
        return res;
    }
    if (total > maxBytes_.load()) {
        res.err = kTooLarge;
        return res;
    }

    // Cabecera para decidir el tipo ANTES de escribir nada.
    std::vector<uint8_t> head(static_cast<size_t>(std::min<uint64_t>(total, kSniffBytes)));
    size_t got = 0;
    while (got < head.size()) {
        const int r = read(head.data() + got, head.size() - got);
        if (r <= 0) {
            res.err = kShort;
            return res;
        }
        got += static_cast<size_t>(r);
    }
    if (!sniff(head.data(), head.size(), res.kind)) {
        res.err = kUnsupported;
        return res;
    }

    std::string name;
    if (!ba::security::randomSecretHex(16, name)) {
        res.err = kIo;
        return res;
    }
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const fs::path part = dir_ / (name + ".part");
    const fs::path final = dir_ / (name + res.kind.ext);

    bool ok = false;
    {
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        if (f) {
            f.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
            uint64_t left = total - head.size();
            std::vector<uint8_t> buf(256 * 1024);
            ok = static_cast<bool>(f);
            while (ok && left > 0) {
                const int r = read(buf.data(), static_cast<size_t>(std::min<uint64_t>(left, buf.size())));
                if (r <= 0) {
                    res.err = kShort;
                    ok = false;
                    break;
                }
                f.write(reinterpret_cast<const char*>(buf.data()), r);
                left -= static_cast<uint64_t>(r);
                ok = static_cast<bool>(f);
            }
            f.close();
            ok = ok && static_cast<bool>(f);
        }
    }
    if (!ok) {
        fs::remove(part, ec);
        if (res.err == kOk) res.err = kIo;
        return res;
    }
    fs::rename(part, final, ec);
    if (ec) {
        fs::remove(part, ec);
        res.err = kIo;
        return res;
    }
    res.path = final;
    res.bytes = total;
    return res;
}

} // namespace ba::gateway
