// SnapshotStore.cpp
//
// Implementacion del writer/reader del snapshot con doble generacion
// alternada (spec §11.2). Tres ideas de diseno:
//
//   1) Atomicidad por plataforma: se escribe en un TEMP file en el MISMO
//      directorio (mismo fs) y se hace rename atomico sobre la generacion
//      destino. En Linux: open(O_WRONLY|O_CREAT|O_TRUNC) + write + fsync(fd) +
//      std::rename. En _WIN32: CreateFileW + WriteFile + FlushFileBuffers +
//      MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH). Un lector
//      nunca ve un archivo a medio escribir: o es la generacion anterior
//      completa o la nueva completa.
//
//   2) Debounce con steady_clock: maximo una escritura fisica cada 500 ms
//      (kDebounceMs). steady_clock es inmune a cambios de hora del sistema
//      (a diferencia de system_clock), por lo que el debounce no se ve
//      afectado si el usuario cambia la hora. Los eventos criticos
//      (immediate=true) bypasean el debounce.
//
//   3) No se pierde estado: ante un fallo de disco (o disco "ausente" por
//      fault-injection) el doc queda en memoria (pendingDoc_) y se marca un
//      reintento pendiente; no se pierde. El caller lo resuelve con flushNow()
//      cuando el disco vuelve.
//
// Ninguna funcion publica lanza: init()/readLatest() devuelven Status;
// scheduleWrite()/flushNow() son no-fallibles sobre un store inicializado.

#include "SnapshotStore.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>  // open, O_WRONLY, O_CREAT, O_TRUNC
#include <unistd.h> // write, close, fsync, ssize_t
#endif

namespace ba::persist {

namespace {

// --- SHA-256 (FIPS 180-4), autocontenido ------------------------------------
//
// Implementacion compacta y sin dependencias para el checksum del snapshot.
// No es thread-safe (cada instancia la usa un solo caller); aqui se crea una
// por hash. Solo se usa para hashear el body del snapshot (no para cripto).

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb40e69e, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b0d1f6b, 0x16891980, 0x8a2be1e9};
    unsigned char buf[64]{};
    std::size_t bufLen = 0;
    std::uint64_t totalLen = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const unsigned char* chunk) {
        static const std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int t = 0; t < 16; ++t) {
            w[t] = (static_cast<std::uint32_t>(chunk[t * 4]) << 24) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(chunk[t * 4 + 3]));
        }
        for (int t = 16; t < 64; ++t) {
            const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; ++t) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + K[t] + w[t];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    void update(const void* data, std::size_t len) {
        const auto* p = static_cast<const unsigned char*>(data);
        totalLen += len;
        while (len > 0) {
            const std::size_t space = 64 - bufLen;
            const std::size_t n = (len < space) ? len : space;
            std::memcpy(buf + bufLen, p, n);
            bufLen += n;
            p += n;
            len -= n;
            if (bufLen == 64) {
                compress(buf);
                bufLen = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> digest() const {
        Sha256 f = *this;
        const std::uint64_t bitLen = f.totalLen * 8ULL;
        unsigned char pad = 0x80;
        f.update(&pad, 1);
        const unsigned char zero = 0x00;
        while (f.bufLen % 64 != 56) {
            f.update(&zero, 1);
        }
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; ++i) {
            lenBytes[i] = static_cast<unsigned char>((bitLen >> (56 - 8 * i)) & 0xFF);
        }
        f.update(lenBytes, 8);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i + 0] = static_cast<unsigned char>((f.h[i] >> 24) & 0xFF);
            out[4 * i + 1] = static_cast<unsigned char>((f.h[i] >> 16) & 0xFF);
            out[4 * i + 2] = static_cast<unsigned char>((f.h[i] >> 8) & 0xFF);
            out[4 * i + 3] = static_cast<unsigned char>(f.h[i] & 0xFF);
        }
        return out;
    }
};

std::string toHexLower(const std::array<std::uint8_t, 32>& d) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned char b : d) {
        out.push_back(hexd[b >> 4]);
        out.push_back(hexd[b & 0xF]);
    }
    return out;
}

// Convierte una ruta UTF-8 a UTF-16 para las APIs Win32 (CreateFileW, etc.).
// En no-Windows no se usa (el branch _WIN32 es el unico que lo llama).
#if defined(_WIN32)
std::wstring toWide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    }
    return out;
}
#endif

// Convierte un hex (minuscula) a std::uint64_t; devuelve false si no es hex.
bool hexToU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20) {
        return false;
    }
    std::uint64_t v = 0;
    for (char c : s) {
        const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const int digit = (lc >= '0' && lc <= '9')   ? (lc - '0')
                          : (lc >= 'a' && lc <= 'f') ? (lc - 'a' + 10)
                                                     : -1;
        if (digit < 0) {
            return false;
        }
        v = (v << 4) | static_cast<std::uint64_t>(digit);
    }
    out = v;
    return true;
}

// --- formato de archivo -----------------------------------------------------
//
// Linea 1 (header): #BA-SNAP v1 seq=<uint64> sha256=<hex64>
// Linea 2..N (body): el JSON serializado de StateDoc.
//
// El checksum es SHA-256 sobre los bytes del BODY (sin el header).

constexpr const char* kHeaderPrefix = "#BA-SNAP v1 seq=";

// Construye el contenido completo del archivo (header + body).
std::string buildFileContent(const StateDoc& doc, const std::string& body, const std::string& shaHex) {
    std::string out;
    out.reserve(body.size() + 96);
    out += kHeaderPrefix;
    out += std::to_string(doc.sequence);
    out += " sha256=";
    out += shaHex;
    out += "\n";
    out += body;
    out += "\n";
    return out;
}

// Extrae el header (seq + sha256) de un contenido de archivo.
// Devuelve false si la primera linea no es un header valido.
struct HeaderInfo {
    std::uint64_t seq = 0;
    std::string shaHex;
};

bool parseHeader(const std::string& content, HeaderInfo& out) {
    // La primera linea debe empezar por kHeaderPrefix y terminar en '\n'.
    if (content.rfind(kHeaderPrefix, 0) != 0) {
        return false;
    }
    const std::size_t nl = content.find('\n');
    if (nl == std::string::npos) {
        return false;
    }
    const std::string line = content.substr(0, nl);
    // Formato: <prefix>seq sha256=<hex>
    const std::size_t seqStart = std::char_traits<char>::length(kHeaderPrefix);
    const std::size_t spacePos = line.find(' ', seqStart);
    if (spacePos == std::string::npos) {
        return false;
    }
    const std::string seqStr = line.substr(seqStart, spacePos - seqStart);
    if (!hexToU64(seqStr, out.seq)) {
        return false;
    }
    const std::string rest = line.substr(spacePos + 1);
    constexpr const char* shaKey = "sha256=";
    if (rest.rfind(shaKey, 0) != 0) {
        return false;
    }
    out.shaHex = rest.substr(std::char_traits<char>::length(shaKey));
    if (out.shaHex.size() != 64) {
        return false;
    }
    return true;
}

// Lee un archivo completo a memoria; devuelve false si no existe / no se lee.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

// --- RAII -------------------------------------------------------------------

SnapshotStore::~SnapshotStore() = default;

SnapshotStore::SnapshotStore(SnapshotStore&& other) noexcept
    : dir_(std::move(other.dir_)), pathA_(std::move(other.pathA_)), pathB_(std::move(other.pathB_)),
      initialized_(other.initialized_), nextIsA_(other.nextIsA_), lastWriteTime_(other.lastWriteTime_),
      pendingDoc_(std::move(other.pendingDoc_)), hasPending_(other.hasPending_),
      diskPresent_(other.diskPresent_), faultStep_(other.faultStep_), physicalWrites_(other.physicalWrites_) {
    other.initialized_ = false;
    other.hasPending_ = false;
}

SnapshotStore& SnapshotStore::operator=(SnapshotStore&& other) noexcept {
    if (this != &other) {
        dir_ = std::move(other.dir_);
        pathA_ = std::move(other.pathA_);
        pathB_ = std::move(other.pathB_);
        initialized_ = other.initialized_;
        nextIsA_ = other.nextIsA_;
        lastWriteTime_ = other.lastWriteTime_;
        pendingDoc_ = std::move(other.pendingDoc_);
        hasPending_ = other.hasPending_;
        diskPresent_ = other.diskPresent_;
        faultStep_ = other.faultStep_;
        physicalWrites_ = other.physicalWrites_;
        other.initialized_ = false;
        other.hasPending_ = false;
    }
    return *this;
}

// --- init -------------------------------------------------------------------

Status SnapshotStore::init(const std::string& dir, const std::string& fileA, const std::string& fileB) {
    if (dir.empty()) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (fileA.empty() || fileB.empty() || fileA == fileB) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    dir_ = dir;
    pathA_ = (std::filesystem::path(dir) / fileA).string();
    pathB_ = (std::filesystem::path(dir) / fileB).string();
    initialized_ = true;
    nextIsA_ = true;
    lastWriteTime_ = std::chrono::steady_clock::time_point{};
    hasPending_ = false;
    physicalWrites_ = 0;
    return Status::success();
}

// --- helpers ----------------------------------------------------------------

std::chrono::steady_clock::time_point SnapshotStore::now() const { return std::chrono::steady_clock::now(); }

bool SnapshotStore::debounceAllowsWrite() const {
    // Sin escritura previa: siempre permite (lastWriteTime_ == epoch).
    if (lastWriteTime_ == std::chrono::steady_clock::time_point{}) {
        return true;
    }
    const auto elapsed = now() - lastWriteTime_;
    return elapsed >= kDebounceMs;
}

// --- physicalWrite ----------------------------------------------------------

Status SnapshotStore::physicalWrite(const StateDoc& doc, bool targetIsA) {
    // Disco ausente (fault-injection): no toca el disco; el doc queda en
    // memoria (lo gestiona el caller) y se marca reintento pendiente.
    if (!diskPresent_) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    const std::string targetPath = targetIsA ? pathA_ : pathB_;
    Status st = atomicWrite(doc, targetPath);
    if (!st.ok) {
        // Fallo en Temp/Flush/Rename (o fault-injection en ese paso).
        // La generacion destino NO se actualizo; no se alterna.
        return st;
    }

    // Exito: alterna la generacion para la proxima escritura.
    nextIsA_ = !targetIsA;
    lastWriteTime_ = now();
    ++physicalWrites_;
    return Status::success();
}

// --- atomicWrite ------------------------------------------------------------

Status SnapshotStore::atomicWrite(const StateDoc& doc, const std::string& targetPath) {
    const std::string body = doc.serialize();
    const std::string shaHex = sha256Hex(body);
    const std::string content = buildFileContent(doc, body, shaHex);

    // Temp en el MISMO directorio (mismo fs) para que el rename sea atomico.
    const std::string tempPath = targetPath + ".tmp";

#if defined(_WIN32)
    // --- Windows: CreateFileW + WriteFile + FlushFileBuffers + MoveFileExW ---
    const std::wstring wTemp = toWide(tempPath);
    const std::wstring wTarget = toWide(targetPath);

    HANDLE hFile =
        CreateFileW(wTemp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (faultStep_ == Step::Temp) {
            return Status::fail(static_cast<int>(ErrorCode::kIo));
        }
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    const std::string& data = content;
    DWORD written = 0;
    const BOOL okWrite = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    if (!okWrite || written != static_cast<DWORD>(data.size())) {
        CloseHandle(hFile);
        DeleteFileW(wTemp.c_str());
        if (faultStep_ == Step::Temp) {
            return Status::fail(static_cast<int>(ErrorCode::kIo));
        }
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    if (faultStep_ == Step::Flush) {
        CloseHandle(hFile);
        DeleteFileW(wTemp.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    if (!FlushFileBuffers(hFile)) {
        CloseHandle(hFile);
        DeleteFileW(wTemp.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    CloseHandle(hFile);

    if (faultStep_ == Step::Rename) {
        DeleteFileW(wTemp.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    const BOOL okMove =
        MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!okMove) {
        DeleteFileW(wTemp.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    return Status::success();

#else
    // --- Linux: open + write + fsync + std::rename (atomico en mismo fs) ---
    int fd = ::open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (faultStep_ == Step::Temp) {
            return Status::fail(static_cast<int>(ErrorCode::kIo));
        }
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    const std::string& data = content;
    const std::size_t total = data.size();
    std::size_t off = 0;
    bool writeOk = true;
    while (off < total) {
        const ssize_t n = ::write(fd, data.data() + off, total - off);
        if (n < 0) {
            writeOk = false;
            break;
        }
        off += static_cast<std::size_t>(n);
    }
    if (!writeOk) {
        ::close(fd);
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        if (faultStep_ == Step::Temp) {
            return Status::fail(static_cast<int>(ErrorCode::kIo));
        }
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    if (faultStep_ == Step::Flush) {
        ::close(fd);
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    if (::fsync(fd) != 0) {
        ::close(fd);
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    ::close(fd);

    if (faultStep_ == Step::Rename) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    std::error_code ec;
    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    return Status::success();
#endif
}

// --- scheduleWrite / flushNow -----------------------------------------------

void SnapshotStore::scheduleWrite(const StateDoc& doc, bool immediate) {
    if (!initialized_) {
        return;
    }
    // Siempre actualiza el doc pendiente en memoria (no se pierde estado).
    pendingDoc_ = doc;
    hasPending_ = true;

    const bool shouldWrite = immediate || debounceAllowsWrite();
    if (!shouldWrite) {
        // Debounced: queda pendiente para la proxima oportunidad.
        return;
    }

    const bool targetIsA = nextIsA_;
    Status st = physicalWrite(doc, targetIsA);
    if (st.ok) {
        hasPending_ = false;
    }
    // Si fallo (disco ausente / fault), el doc sigue en memoria (hasPending_
    // sigue true) y se reintenta con flushNow() o el proximo scheduleWrite.
}

void SnapshotStore::flushNow() {
    if (!initialized_ || !hasPending_) {
        return;
    }
    const bool targetIsA = nextIsA_;
    Status st = physicalWrite(pendingDoc_, targetIsA);
    if (st.ok) {
        hasPending_ = false;
    }
}

// --- readLatest -------------------------------------------------------------

Status SnapshotStore::readLatest(StateDoc& out) {
    if (!initialized_) {
        return Status::fail(static_cast<int>(ErrorCode::kNotInitialized));
    }

    StateDoc docA, docB;
    const bool aValid = readGeneration(pathA_, docA);
    const bool bValid = readGeneration(pathB_, docB);

    if (!aValid && !bValid) {
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }

    // Toma la mas nueva (mayor secuencia) entre las validas.
    if (aValid && bValid) {
        out = (docA.sequence >= docB.sequence) ? docA : docB;
    } else if (aValid) {
        out = docA;
    } else {
        out = docB;
    }
    return Status::success();
}

bool SnapshotStore::readGeneration(const std::string& path, StateDoc& out) const {
    std::string content;
    if (!readFileAll(path, content)) {
        return false; // no existe
    }
    HeaderInfo hdr;
    if (!parseHeader(content, hdr)) {
        return false; // header malformado
    }
    // El body es todo lo despues del primer '\n'.
    const std::size_t nl = content.find('\n');
    if (nl == std::string::npos) {
        return false;
    }
    std::string body = content.substr(nl + 1);
    // Quita el '\n' final (lo agrega buildFileContent).
    if (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }
    // Valida el checksum: SHA-256 del body debe coincidir con el del header.
    const std::string actual = sha256Hex(body);
    if (actual != hdr.shaHex) {
        return false; // corrupto: checksum no coincide
    }
    // Deserializa el body.
    return StateDoc::deserialize(body, out);
}

// --- sha256Hex --------------------------------------------------------------

std::string SnapshotStore::sha256Hex(const std::string& body) {
    Sha256 sh;
    sh.update(body.data(), body.size());
    const auto d = sh.digest();
    return toHexLower(d);
}

// --- fault-injection --------------------------------------------------------

void SnapshotStore::setDiskPresent(bool present) { diskPresent_ = present; }

void SnapshotStore::setFaultAtStep(Step step) { faultStep_ = step; }

std::uint64_t SnapshotStore::physicalWrites() const { return physicalWrites_; }

} // namespace ba::persist
