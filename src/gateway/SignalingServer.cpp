// ============================================================================
// src/gateway/SignalingServer.cpp — implementacion del servidor HTTP + WebSocket
//
// Proposito:
//   Implementa SignalingServer (ver el header). Capas:
//     - capa de socket dual-plataforma (Win winsock2 / POSIX) con RAII simple;
//     - servidor HTTP (estatico + API join/verify/healthz);
//     - servidor WebSocket (handshake 101 + flujo de senalizacion WebRTC);
//     - framing de los mensajes de senalizacion con ba_core::ipc::Frame
//       (encodeFrame/decodeFrame, spec §4.1) sobre frames binarios WS.
//
// Modelo de threading:
//   - acceptLoop (1 thread) acepta y lanza un thread por conexion.
//   - handleConnection lee la peticion y despacha a HTTP o WS.
//   - wsLoop lee frames WS y despacha a la sesion; los callbacks de sesion
//     (onLocalAnswer/onLocalIce) pueden venir de otro thread (backend real) y
//     hacen send bajo el lock de la conexion.
//   - stop() cierra el socket de escucha y senala parada.
//
// Estrategia de errores:
//   No lanza. Fallos de parseo/IO cierran la conexion (logueado) sin tumbar al
//   servidor. Token invalido / slot ocupado → 401 antes del 101.
// ============================================================================

#include "SignalingServer.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "ipc/ControlProtocol.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"             // tipo completo: se invoca forwardAudio/forwardVideo
#include "security/TokenService.hpp" // nowUnix() para el rate-limit por IP

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Fd;
#define FD_INVALID INVALID_SOCKET
static Fd toFd(long long f) { return static_cast<Fd>(f); }
static long long toLL(Fd f) { return static_cast<long long>(f); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h> // select() del bucle de accept con timeout
#include <sys/socket.h>
#include <unistd.h>
typedef int Fd;
#define FD_INVALID (-1)
static Fd toFd(long long f) { return static_cast<Fd>(f); }
static long long toLL(Fd f) { return static_cast<long long>(f); }
#endif

namespace ba::gateway {

namespace {

using json = nlohmann::json;
using namespace ba::ipc;

/// Tamano maximo de la cabecera de una peticion HTTP/WS (64 KiB).
constexpr size_t kMaxHeaderBytes = 64 * 1024;
/// Tamano maximo de un cuerpo HTTP (1 MiB, coherente con Frame::kMaxPayloadBytes).
constexpr size_t kMaxBodyBytes = 1u << 20;

// --- SHA-1 (para el handshake WebSocket, RFC 6455) -------------------------
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t len = 0;
    uint8_t buf[64];
    size_t bufLen = 0;

    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void process(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            // Paréntesis explícitos en cada ronda: `&`/`|`/`^` tienen
            // precedencias distintas y el compilador avisa (`-Wparentheses`)
            // al mezclarlas sin agrupar. La aritmética no cambia.
            // Las cuatro rondas de SHA-1 (FIPS 180-4 §6.1.2) duran 20 pasos
            // cada una: 0-19, 20-39, 40-59, 60-79. Los limites estaban en
            // 16/32/48, que es el tamaño del message schedule (16 palabras),
            // no el de las rondas; con eso el digest salia mal para CUALQUIER
            // entrada y, como este SHA-1 solo alimenta el Sec-WebSocket-Accept
            // del handshake, ningun cliente que valide ese campo (todos los
            // navegadores lo hacen) podia completar la conexion.
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = (b ^ c) ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = (b ^ c) ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    void update(const uint8_t* data, size_t n) {
        len += n;
        while (n > 0) {
            const size_t take = std::min(n, size_t(64) - bufLen);
            std::memcpy(buf + bufLen, data, take);
            bufLen += take;
            data += take;
            n -= take;
            if (bufLen == 64) {
                process(buf);
                bufLen = 0;
            }
        }
    }

    void finish(uint8_t out[20]) {
        const uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (bufLen != 56)
            update(&zero, 1);
        uint8_t lenBuf[8];
        for (int i = 0; i < 8; ++i)
            lenBuf[i] = uint8_t(bits >> (56 - i * 8));
        update(lenBuf, 8);
        for (int i = 0; i < 5; ++i) {
            out[i * 4] = uint8_t(h[i] >> 24);
            out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8);
            out[i * 4 + 3] = uint8_t(h[i]);
        }
    }
};

/// SHA-1 de una cadena → 20 bytes.
std::array<uint8_t, 20> sha1(const std::string& s) {
    Sha1 ctx;
    ctx.update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    std::array<uint8_t, 20> out{};
    ctx.finish(out.data());
    return out;
}

/// Base64 (estandar, con padding) de `n` bytes.
std::string base64(const uint8_t* data, size_t n) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= n) {
        const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    const size_t rem = n - i;
    if (rem == 1) {
        const uint32_t v = uint32_t(data[i]) << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

// Forward declarations de los frames WebSocket (definidos al final del archivo,
// tras las metodos de la clase que los usan).
bool readWsFrame(long long fd, std::string& payload, int& opcode);
bool writeWsFrame(long long fd, int opcode, const std::string& payload);

// --- Utilidades de socket ---------------------------------------------------
bool socketInit() {
#ifdef _WIN32
    WSADATA wsa{};
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void socketCleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeFdImpl(Fd s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

long long makeListen(uint16_t port) {
    Fd s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == FD_INVALID) return -1;
    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 (spec §5.6)
#else
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 (spec §5.6)
#endif
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeFdImpl(s);
        return -1;
    }
    if (listen(s, 16) != 0) {
        closeFdImpl(s);
        return -1;
    }
    return toLL(s);
}

/// Espera a que `fd` tenga algo que aceptar/leer, con timeout en ms.
///
/// Existe para que el bucle de accept NO se quede bloqueado indefinidamente:
/// en POSIX cerrar el descriptor desde otro thread no despierta a un accept()
/// bloqueado (no esta garantizado por el estandar), asi que stop() nunca
/// lograba hacer join del thread de accept y el proceso quedaba colgado al
/// cerrar. Con un select acotado, el bucle vuelve cada `ms` y ve `stop_`.
///
/// :returns: 1 si hay actividad, 0 si vencio el timeout, -1 en error.
int waitReadable(long long fd, int ms) {
    if (fd < 0) return -1;
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(toFd(fd), &rd);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
#ifdef _WIN32
    // En winsock el primer argumento se ignora.
    return select(0, &rd, nullptr, nullptr, &tv);
#else
    return select(static_cast<int>(fd) + 1, &rd, nullptr, nullptr, &tv);
#endif
}

/// Fija (o quita, con ms == 0) el timeout de recepcion y envio de un socket.
///
/// Acota la fase HTTP/handshake: una conexion que abre y no manda nada dejaria
/// su thread bloqueado en recv para siempre, y con el a stop(), que hace join.
/// Una vez promovida a WebSocket el timeout se quita: un invitado puede estar
/// legitimamente en silencio, y ahi el desbloqueo lo da stop() cerrando el fd.
void setSocketTimeout(long long fd, int ms) {
#ifdef _WIN32
    DWORD v = static_cast<DWORD>(ms);
    setsockopt(toFd(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
    setsockopt(toFd(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&v), sizeof(v));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(toFd(fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(toFd(fd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

bool setNoDelay(long long fd) {
    int yes = 1;
#ifdef _WIN32
    return setsockopt(toFd(fd), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)) ==
           0;
#else
    return setsockopt(toFd(fd), IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == 0;
#endif
}

/// Lee exactamente `n` bytes (maneja lecturas parciales). true si se leyeron.
bool readExact(long long fd, uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = recv(toFd(fd), reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
        if (r <= 0) return false; // EOF o error
        got += static_cast<size_t>(r);
    }
    return true;
}

bool sendAllImpl(long long fd, const void* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        const int r = send(toFd(fd), reinterpret_cast<const char*>(static_cast<const uint8_t*>(data) + sent),
                           static_cast<int>(n - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

/// Lee hasta encontrar `\r\n\r\n` (fin de cabecera). Devuelve la cabecera.
bool readHeaders(long long fd, std::string& headers) {
    headers.clear();
    uint8_t c;
    while (headers.size() < kMaxHeaderBytes) {
        const int r = recv(toFd(fd), reinterpret_cast<char*>(&c), 1, 0);
        if (r <= 0) return false;
        headers.push_back(static_cast<char>(c));
        if (headers.size() >= 4 && headers.compare(headers.size() - 4, 4, "\r\n\r\n") == 0) return true;
    }
    return false;
}

/// Extrae el valor de una cabecera (case-insensitive).
///
/// La BUSQUEDA es case-insensitive (RFC 7230 §3.2: los nombres de cabecera no
/// distinguen mayusculas), pero el VALOR se recorta del buffer original: hay
/// valores que si distinguen mayusculas y bajarlos romperia el protocolo
/// (`Sec-WebSocket-Key` es base64 y su SHA-1 entra en el accept del handshake).
/// Por eso se mantienen dos buffers alineados indice a indice: `hay` (copia en
/// minusculas, donde se busca) y `orig` (el original, de donde se recorta).
std::string getHeader(const std::string& headers, const std::string& name) {
    const std::string orig = "\r\n" + headers;
    std::string hay = orig;
    for (auto& ch : hay)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    std::string needle = "\r\n" + name + ":";
    for (auto& ch : needle)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    const size_t pos = hay.find(needle);
    if (pos == std::string::npos) return "";
    const size_t start = pos + needle.size();
    // Saltar espacios (OWS, RFC 7230 §3.2.3).
    size_t i = start;
    while (i < hay.size() && (hay[i] == ' ' || hay[i] == '\t'))
        ++i;
    size_t e = i;
    while (e < hay.size() && hay[e] != '\r' && hay[e] != '\n')
        ++e;
    // Recorte del ORIGINAL: `hay` solo sirvio para localizar los indices.
    return orig.substr(i, e - i);
}

/// Parsea la query string a un map.
std::map<std::string, std::string> parseQuery(const std::string& q) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < q.size()) {
        const size_t amp = q.find('&', i);
        const std::string pair = (amp == std::string::npos) ? q.substr(i) : q.substr(i, amp - i);
        const size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            out[pair] = "";
        } else {
            out[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return out;
}

/// Parsea un entero decimal de forma NO-lanzante (devuelve `fallback` si la
/// cadena no es un entero valido).
///
/// `std::stoi`/`std::stoll` lanzan `std::invalid_argument` (no numerico) y
/// `std::out_of_range` (excede el rango); ambos cruzarian el borde del thread
/// de conexion y tumbarian al proceso (contrato: no-throw cruzando threads).
/// Primero se valida que solo haya [+-]?digitos (descarta invalid_argument) y
/// se captura out_of_range.
long long parseLong(const std::string& s, long long fallback) {
    if (s.empty()) return fallback;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') i = 1;
    if (i >= s.size()) return fallback;
    for (size_t k = i; k < s.size(); ++k) {
        if (s[k] < '0' || s[k] > '9') return fallback;
    }
    try {
        return std::stoll(s);
    } catch (...) {
        return fallback;
    }
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (s[i] == '+') {
            out += ' ';
            continue;
        }
        out += s[i];
    }
    return out;
}

} // namespace

// ============================================================================
// SignalingServer
// ============================================================================
SignalingServer::SignalingServer(const Cfg& cfg, TokenAuth& auth, WebrtcHub& hub, Log& log)
    : cfg_(cfg), auth_(auth), hub_(hub), log_(log) {
    slotBusy_.assign(static_cast<size_t>(cfg_.maxGuests) + 1, false);
    slotReservedUntil_.assign(static_cast<size_t>(cfg_.maxGuests) + 1, 0);
    slotReservedIp_.assign(static_cast<size_t>(cfg_.maxGuests) + 1, 0u);
}

SignalingServer::~SignalingServer() { stop(); }

bool SignalingServer::start() {
    if (!socketInit()) {
        log_.error("senalizacion: fallo WSAStartup", -1);
        return false;
    }
    listenFd_ = makeListen(cfg_.port);
    if (listenFd_ < 0) {
        log_.error(
            "senalizacion: no se pudo bind 127.0.0.1:" + std::to_string(cfg_.port) + " (puerto en uso?)", -1);
        socketCleanup();
        return false;
    }
    stop_.store(false);
    acceptThread_ = std::thread(&SignalingServer::acceptLoop, this);
    log_.info("senalizacion: escuchando en 127.0.0.1:" + std::to_string(cfg_.port) +
              " (webroot=" + cfg_.webroot + ")");
    return true;
}

void SignalingServer::stop() {
    // Idempotente: el destructor llama a stop() y main() tambien.
    if (listenFd_ < 0 && activeConns_.load() == 0 && !acceptThread_.joinable()) {
        return;
    }
    stop_.store(true);

    // 1) El thread de accept sale por `stop_` (su espera es acotada), no por el
    //    cierre del descriptor: cerrarlo no despierta a un accept() bloqueado.
    if (acceptThread_.joinable()) acceptThread_.join();
    if (listenFd_ >= 0) {
        closeFdImpl(toFd(listenFd_));
        listenFd_ = -1;
    }

    // 2) Las conexiones WS pueden estar en un recv sin timeout (un invitado en
    //    silencio es legitimo). Se les cierra el descriptor para que el recv
    //    retorne y el thread salga.
    closeActiveConns();

    // 3) Espera acotada a que los threads detachados terminen. Si alguno no
    //    sale, se avisa en vez de bloquear el cierre del proceso para siempre.
    constexpr int kDrainTimeoutMs = 3000;
    constexpr int kDrainStepMs = 20;
    int waited = 0;
    while (activeConns_.load(std::memory_order_relaxed) > 0 && waited < kDrainTimeoutMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kDrainStepMs));
        waited += kDrainStepMs;
    }
    const int leftover = activeConns_.load(std::memory_order_relaxed);
    if (leftover > 0) {
        log_.warn("senalizacion: " + std::to_string(leftover) + " conexiones sin cerrar tras " +
                      std::to_string(kDrainTimeoutMs) + " ms",
                  -1);
    }

    socketCleanup();
    log_.info("senalizacion: detenida");
}

bool SignalingServer::ipAttemptAllowed(std::uint32_t ip) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    auto it = ipAttempts_.find(ip);
    if (it == ipAttempts_.end()) {
        return true;
    }
    if (it->second.lockedUntilUnix != 0) {
        if (now < it->second.lockedUntilUnix) {
            return false;
        }
        // Vencio la ventana: presupuesto fresco para esa IP.
        ipAttempts_.erase(it);
    }
    return true;
}

void SignalingServer::ipRecordFailure(std::uint32_t ip) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    // Poda oportunista: sin esto el mapa crece con cada IP que alguna vez
    // fallo, y es memoria que un atacante puede hacer crecer a voluntad.
    for (auto it = ipAttempts_.begin(); it != ipAttempts_.end();) {
        if (it->second.lockedUntilUnix != 0 && now >= it->second.lockedUntilUnix) {
            it = ipAttempts_.erase(it);
        } else {
            ++it;
        }
    }
    auto& a = ipAttempts_[ip];
    ++a.failures;
    if (a.failures >= kPinMaxAttemptsPerIp) {
        a.lockedUntilUnix = now + kPinLockoutSecPerIp;
    }
}

void SignalingServer::ipRecordSuccess(std::uint32_t ip) {
    std::lock_guard<std::mutex> lock(ipAttemptsLock_);
    ipAttempts_.erase(ip);
}

void SignalingServer::trackFd(long long fd) {
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    activeFds_.insert(fd);
}

void SignalingServer::untrackFd(long long fd) {
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    activeFds_.erase(fd);
}

void SignalingServer::closeActiveConns() {
    // shutdown(), no close(): el descriptor es propiedad del thread de conexion,
    // que lo cerrara al salir. Cerrarlo desde aqui abriria una ventana en la que
    // ese numero de fd se reutiliza y el thread opera sobre un socket ajeno.
    // shutdown() solo rompe la conexion: el recv retorna 0 y el thread sale.
    std::lock_guard<std::mutex> lock(activeFdsLock_);
    for (long long fd : activeFds_) {
        if (fd < 0) continue;
#ifdef _WIN32
        shutdown(toFd(fd), SD_BOTH);
#else
        ::shutdown(toFd(fd), SHUT_RDWR);
#endif
    }
}

bool SignalingServer::running() const { return listenFd_ >= 0; }

void SignalingServer::acceptLoop() {
    // Periodo con el que el bucle vuelve a mirar `stop_` si no entra nadie.
    // Es tambien la latencia maxima del cierre ordenado.
    constexpr int kAcceptPollMs = 200;

    while (!stop_.load(std::memory_order_relaxed)) {
        // Espera acotada en vez de accept() bloqueante: asi el cierre no
        // depende de que llegue una conexion mas (ver waitReadable).
        const long long listenFd = listenFd_;
        if (listenFd < 0) break;
        const int ready = waitReadable(listenFd, kAcceptPollMs);
        if (ready == 0) continue; // timeout: solo re-chequea stop_
        if (ready < 0) {          // error del selector
            if (stop_.load(std::memory_order_relaxed)) break;
            // No se reintenta en caliente: un error persistente aqui haria
            // girar el bucle al 100% de CPU. Se cede el turno.
            std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptPollMs));
            continue;
        }

        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        Fd c = accept(toFd(listenFd), reinterpret_cast<sockaddr*>(&cli), &len);
        if (c == FD_INVALID) {
            if (stop_.load(std::memory_order_relaxed)) break;
            continue;
        }
        const long long fd = toLL(c);

        // Tope de concurrencia: por encima se responde 503 y se cierra, en vez
        // de lanzar un thread mas. Sin esto, abrir conexiones en bucle agota
        // threads y memoria sin necesidad de autenticarse.
        if (activeConns_.load(std::memory_order_relaxed) >= kMaxConcurrentConns) {
            sendResponse(fd, 503, "Service Unavailable", "text/plain", "servidor saturado\n");
            closeFdImpl(toFd(fd));
            log_.warn("senalizacion: conexion rechazada, tope de " + std::to_string(kMaxConcurrentConns) +
                          " conexiones",
                      -1);
            continue;
        }

        setNoDelay(fd);
        // Acota la fase HTTP/handshake (una conexion muda no puede retener un
        // thread para siempre). wsHandshake lo quita al promover a WebSocket.
        setSocketTimeout(fd, kHandshakeTimeoutMs);

        // El contador sube y el fd se registra ANTES de lanzar el thread: si se
        // hiciera dentro, stop() podria ver 0 conexiones (o no conocer el fd) y
        // volver mientras el thread todavia arranca.
        activeConns_.fetch_add(1, std::memory_order_relaxed);
        trackFd(fd);
        // La IP de origen alimenta el rate-limit de PIN por cliente.
#ifdef _WIN32
        const std::uint32_t clientIp = static_cast<std::uint32_t>(cli.sin_addr.S_un.S_addr);
#else
        const std::uint32_t clientIp = static_cast<std::uint32_t>(cli.sin_addr.s_addr);
#endif
        std::thread(&SignalingServer::handleConnection, this, fd, clientIp).detach();
    }
}

void SignalingServer::handleConnection(long long fd, std::uint32_t clientIp) {
    // El thread esta detachado: el contador de conexiones vivas DEBE bajar por
    // cualquier salida de la funcion (hay varios return), porque stop() espera
    // a que llegue a 0 antes de dejar morir al objeto. Un guard de ambito lo
    // garantiza sin depender de acordarse en cada rama.
    struct ConnGuard {
        SignalingServer* self;
        long long fd;
        ~ConnGuard() {
            self->untrackFd(fd);
            // El contador baja EL ULTIMO: stop() lo usa como senal de "ya no
            // queda nadie tocando el objeto", asi que no debe llegar a 0 antes
            // de que este thread termine de usar `self`.
            self->activeConns_.fetch_sub(1, std::memory_order_relaxed);
        }
    } guard{this, fd};

    std::string headers;
    if (!readHeaders(fd, headers)) {
        closeFdImpl(toFd(fd));
        return;
    }

    // Linea de peticion: METHOD PATH HTTP/1.1
    const size_t eol = headers.find("\r\n");
    const std::string line = headers.substr(0, eol);
    const size_t sp1 = line.find(' ');
    const size_t sp2 = line.rfind(' ');
    const std::string method = line.substr(0, sp1);
    std::string target =
        (sp1 == std::string::npos || sp2 == std::string::npos) ? "" : line.substr(sp1 + 1, sp2 - sp1 - 1);

    // Separa path y query.
    std::string path, query;
    const size_t qpos = target.find('?');
    if (qpos == std::string::npos) {
        path = target;
    } else {
        path = target.substr(0, qpos);
        query = target.substr(qpos + 1);
    }

    // Cuerpo (POST).
    std::string body;
    const std::string cl = getHeader(headers, "Content-Length");
    if (!cl.empty()) {
        // Content-Length hostil (no numerico / enorme): parseLong no lanza y
        // devuelve 0, por lo que se descarta el cuerpo (conexion que no es WS
        // ni POST valido).
        const long long n = parseLong(cl, 0);
        if (n > 0 && n < static_cast<long long>(kMaxBodyBytes)) {
            body.resize(static_cast<size_t>(n));
            if (!readExact(fd, reinterpret_cast<uint8_t*>(&body[0]), body.size())) {
                closeFdImpl(toFd(fd));
                return;
            }
        }
    }

    // El valor de Upgrade es un token case-insensitive (RFC 6455 §4.2.1: "un
    // valor que sea un match ASCII case-insensitive de 'websocket'"), y cada
    // cliente lo escribe a su manera ("websocket", "WebSocket"). Se compara
    // bajando el valor, no enumerando capitalizaciones.
    std::string upgrade = getHeader(headers, "Upgrade");
    for (auto& ch : upgrade)
        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    const bool isWs = (upgrade == "websocket");

    if (isWs && path == "/ws") {
        if (wsHandshake(fd, headers, query)) {
            // wsHandshake ya reservo el slot; lo recupera de la query.
            const auto q = parseQuery(query);
            const int slot = static_cast<int>(
                (q.count("slot") && !q.at("slot").empty()) ? parseLong(urlDecode(q.at("slot")), 1) : 1);
            registerConn(slot, fd);
            wsLoop(fd, slot);
            unregisterConn(slot);
            hub_.session(slot)->close();
            releaseSlot(slot);
        }
        closeFdImpl(toFd(fd));
        return;
    }

    handleHttp(fd, method, path, query, body, clientIp);
    closeFdImpl(toFd(fd));
}

// --- HTTP -------------------------------------------------------------------
void SignalingServer::handleHttp(long long fd, const std::string& method, const std::string& path,
                                 const std::string& query, const std::string& body, std::uint32_t clientIp) {
    if (path == "/" || path == "/index.html") {
        serveStatic(fd, "index.html");
        return;
    }
    if (path == "/healthz") {
        sendResponse(fd, 200, "OK", "text/plain", "ok\n");
        return;
    }
    if (path == "/api/join" && method == "POST") {
        apiJoin(fd, body, clientIp);
        return;
    }
    if (path == "/api/verify") {
        apiVerify(fd, query);
        return;
    }
    // Estático: /app.js, /style.css, etc.
    if (!path.empty() && path[0] == '/') {
        if (serveStatic(fd, path.substr(1))) return;
    }
    sendResponse(fd, 404, "Not Found", "text/plain", "no encontrado\n");
}

bool SignalingServer::serveStatic(long long fd, const std::string& relPath) {
    // Guarda contra path traversal por CANONICALIZACION, no por buscar "..".
    // La busqueda de subcadena funcionaba solo por accidente (el path nunca se
    // URL-decodifica), rechazaba nombres legitimos que contuvieran dos puntos
    // seguidos, y se romperia en silencio el dia que alguien agregue el decode
    // que el resto del codigo ya aplica a la query. Comparar la ruta resuelta
    // contra el webroot resuelto es la comprobacion que de verdad importa.
    std::error_code ec;
    const std::filesystem::path rootCanon = std::filesystem::weakly_canonical(cfg_.webroot, ec);
    if (ec) {
        sendResponse(fd, 404, "Not Found", "text/plain", "no encontrado\n");
        return true;
    }
    const std::filesystem::path target =
        std::filesystem::weakly_canonical(std::filesystem::path(cfg_.webroot) / relPath, ec);
    if (ec) {
        sendResponse(fd, 400, "Bad Request", "text/plain", "ruta invalida\n");
        return true;
    }
    // `target` debe quedar DENTRO de rootCanon.
    const auto rootStr = rootCanon.generic_string();
    const auto targetStr = target.generic_string();
    if (targetStr.size() < rootStr.size() || targetStr.compare(0, rootStr.size(), rootStr) != 0 ||
        (targetStr.size() > rootStr.size() && targetStr[rootStr.size()] != '/')) {
        sendResponse(fd, 400, "Bad Request", "text/plain", "ruta invalida\n");
        return true;
    }
    const std::string full = target.string();
    std::ifstream f(full, std::ios::binary);
    if (!f) {
        sendResponse(fd, 404, "Not Found", "text/plain", "no encontrado\n");
        return true;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string content = ss.str();

    std::string ct = "application/octet-stream";
    if (relPath.size() > 5 && relPath.compare(relPath.size() - 5, 5, ".html") == 0)
        ct = "text/html; charset=utf-8";
    else if (relPath.size() > 3 && relPath.compare(relPath.size() - 3, 3, ".js") == 0)
        ct = "text/javascript; charset=utf-8";
    else if (relPath.size() > 4 && relPath.compare(relPath.size() - 4, 4, ".css") == 0)
        ct = "text/css; charset=utf-8";
    else if (relPath.size() > 4 && relPath.compare(relPath.size() - 4, 4, ".svg") == 0)
        ct = "image/svg+xml";

    sendResponse(fd, 200, "OK", ct, content);
    return true;
}

void SignalingServer::apiJoin(long long fd, const std::string& body, std::uint32_t clientIp) {
    // Rate-limit por IP ANTES de tocar el validador de PIN: quien agoto sus
    // intentos recibe 429 y no consume presupuesto del lockout global.
    if (!ipAttemptAllowed(clientIp)) {
        sendResponse(fd, 429, "Too Many Requests", "application/json", "{\"ok\":false,\"error\":\"rate\"}");
        return;
    }
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        sendResponse(fd, 400, "Bad Request", "application/json", "{\"ok\":false,\"error\":\"json\"}");
        return;
    }
    const std::string room = req.value("room", cfg_.room);
    const std::string pin = req.value("pin", "");
    const std::string token = auth_.issue(room, pin);
    if (token.empty()) {
        ipRecordFailure(clientIp);
        sendResponse(fd, 401, "Unauthorized", "application/json", "{\"ok\":false,\"error\":\"pin\"}");
        return;
    }
    ipRecordSuccess(clientIp);
    // El SERVIDOR elige el slot: es quien conoce la ocupacion. El cliente web
    // lo tenia codificado a 1, de modo que el segundo invitado siempre chocaba
    // con un 409 y la mesa de hasta 4 invitados era imposible.
    const int slot = reserveSlotFor(clientIp);
    if (slot == 0) {
        sendResponse(fd, 503, "Service Unavailable", "application/json", "{\"ok\":false,\"error\":\"full\"}");
        return;
    }

    json resp;
    resp["ok"] = true;
    resp["token"] = token;
    resp["room"] = room;
    resp["slot"] = slot;
    resp["maxGuests"] = cfg_.maxGuests;
    resp["ttlSec"] = auth_.ttlSec();
    sendResponse(fd, 200, "OK", "application/json", resp.dump());
}

void SignalingServer::apiVerify(long long fd, const std::string& query) {
    const auto q = parseQuery(query);
    const std::string token = q.count("token") ? urlDecode(q.at("token")) : "";
    const std::string room = q.count("room") ? urlDecode(q.at("room")) : cfg_.room;
    const int code = auth_.verify(token, room);
    if (code == 0) {
        sendResponse(fd, 200, "OK", "application/json", "{\"ok\":true}");
    } else {
        json resp;
        resp["ok"] = false;
        resp["code"] = code;
        sendResponse(fd, 401, "Unauthorized", "application/json", resp.dump());
    }
}

// --- WebSocket --------------------------------------------------------------
bool SignalingServer::wsHandshake(long long fd, const std::string& req, const std::string& query) {
    const std::string key = getHeader(req, "Sec-WebSocket-Key");
    if (key.empty()) {
        sendResponse(fd, 400, "Bad Request", "text/plain", "falta Sec-WebSocket-Key\n");
        return false;
    }

    const auto q = parseQuery(query);
    const std::string token = q.count("token") ? urlDecode(q.at("token")) : "";
    const std::string room = q.count("room") ? urlDecode(q.at("room")) : cfg_.room;
    const int slot = static_cast<int>(
        (q.count("slot") && !q.at("slot").empty()) ? parseLong(urlDecode(q.at("slot")), 1) : 1);

    // Auth: CANJEA el token contra la sala (spec §8.3). Se usa redeem() y no
    // verify() porque este es el punto donde el token se cambia por una sesion
    // real: el nonce se consume y una segunda presentacion del mismo token
    // (replay) se rechaza aunque siga dentro de su TTL.
    const int vcode = auth_.redeem(token, room);
    if (vcode != 0) {
        log_.fmt("warn", vcode, "ws: token invalido (slot=%d, code=%d)", slot, vcode);
        sendResponse(fd, 401, "Unauthorized", "text/plain", "token invalido\n");
        return false;
    }
    // Slot: debe ser valido y libre.
    if (slot < 1 || slot > cfg_.maxGuests || !tryClaimSlot(slot)) {
        log_.warn("ws: slot ocupado o invalido: " + std::to_string(slot), -1);
        sendResponse(fd, 409, "Conflict", "text/plain", "slot ocupado\n");
        return false;
    }

    // Handshake RFC 6455: accept = base64(SHA1(key + GUID)).
    const std::string accept = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20);
    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: " +
                       accept + "\r\n\r\n";
    if (!sendAll(fd, resp.data(), resp.size())) {
        releaseSlot(slot);
        return false;
    }
    // Promovida a WebSocket: se quita el timeout de la fase de handshake. Un
    // invitado puede estar legitimamente callado minutos entre mensajes de
    // senalizacion, y un recv que venciera cerraria su sesion. El desbloqueo en
    // el cierre lo da stop(), que hace shutdown() de este mismo descriptor.
    setSocketTimeout(fd, 0);
    log_.fmt("info", 0, "ws: invitado conectado (slot=%d, room=%s)", slot, room.c_str());
    return true;
}

void SignalingServer::wsLoop(long long fd, int slot) {
    WebrtcSession* sess = hub_.session(slot);
    if (!sess) return;

    // Configura la sesion con los callbacks que envian por ESTA conexion.
    SessionCallbacks cb;
    cb.onLocalAnswer = [this, slot](const std::string& sdp) {
        json j;
        j["type"] = "answer";
        j["sdp"] = sdp;
        sendSignaling(slot, j.dump());
    };
    cb.onLocalIce = [this, slot](const std::string& cand) {
        json j;
        j["type"] = "ice";
        j["candidate"] = cand;
        sendSignaling(slot, j.dump());
    };
    cb.onStateChange = [this, slot](bool up) {
        log_.fmt("info", 0, "ws: sesion slot=%d %s", slot, up ? "conectada" : "cerrada");
    };
    // onRemoteAudio/onRemoteVideo: el uplink del invitado se publica en los
    // canales B→A (ring BA.Audio.Guest.<slot> + sender Spout BA.Guest.<slot>)
    // via el MediaPump (spec §8.5/§8.6). En el backend real estos callbacks se
    // disparan desde los threads de libwebrtc; el pump toma sus locks internos.
    if (pump_) {
        cb.onRemoteAudio = [this, slot](const float* samples, size_t n, int rate) {
            if (pump_) pump_->forwardAudio(slot, samples, n, rate);
        };
        cb.onRemoteVideo = [this, slot](const uint8_t* pixels, size_t bytes, int w, int h) {
            if (pump_) pump_->forwardVideo(slot, pixels, bytes, w, h);
        };
    }
    sess->configure(cb, hub_.rtcFactory());

    // Bucle de lectura de frames WS.
    std::string payload;
    int opcode = 0;
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!readWsFrame(fd, payload, opcode)) break; // EOF/error
        switch (opcode) {
            case 0x8: // close
                return;
            case 0x9: // ping → pong
                writeWsFrame(fd, 0xA, payload);
                continue;
            case 0xA: // pong
                continue;
            case 0x1: // text
            case 0x2: // binary
                break;
            default: continue;
        }

        // El payload es un frame (Frame::encodeFrame): prefijo LE + JSON.
        std::string jsonStr;
        size_t consumed = 0;
        if (!Frame::decodeFrame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), jsonStr,
                                consumed)) {
            log_.warn("ws: frame corrupto (slot=" + std::to_string(slot) + ")", -1);
            continue;
        }
        json msg;
        try {
            msg = json::parse(jsonStr);
        } catch (...) {
            continue;
        }
        const std::string type = msg.value("type", "");
        if (type == "offer") {
            const std::string sdp = msg.value("sdp", "");
            if (!sess->setRemoteOffer(sdp)) {
                log_.warn("ws: setRemoteOffer fallo (slot=" + std::to_string(slot) + ")", -1);
            }
        } else if (type == "ice") {
            const std::string cand = msg.value("candidate", "");
            sess->addRemoteIce(cand);
        } else if (type == "bye") {
            return;
        }
    }
}

// --- Send por conexion (thread-safe) ---------------------------------------
void SignalingServer::sendSignaling(int slot, const std::string& jsonStr) {
    WsConn* conn = connForSlot(slot);
    if (!conn) return;
    const auto frame = Frame::encodeFrame(jsonStr);
    std::lock_guard<std::mutex> lock(conn->sendLock);
    writeWsFrame(conn->fd, 0x2, std::string(frame.begin(), frame.end()));
}

SignalingServer::WsConn* SignalingServer::connForSlot(int slot) {
    std::lock_guard<std::mutex> lock(connsLock_);
    const auto it = conns_.find(slot);
    return (it == conns_.end()) ? nullptr : it->second.get();
}

void SignalingServer::registerConn(int slot, long long fd) {
    std::lock_guard<std::mutex> lock(connsLock_);
    // WsConn contiene un std::mutex (no movible / no asignable), asi que se
    // construye in-place detras del unique_ptr: `conns_[slot] = WsConn{fd}`
    // invocaria el move-assign borrado de WsConn.
    auto conn = std::make_unique<WsConn>();
    conn->fd = fd;
    conns_[slot] = std::move(conn);
}

void SignalingServer::unregisterConn(int slot) {
    std::lock_guard<std::mutex> lock(connsLock_);
    conns_.erase(slot);
}

// --- Slots ------------------------------------------------------------------
void SignalingServer::expireReservationsLocked(int nowUnix) {
    for (size_t i = 1; i < slotReservedUntil_.size(); ++i) {
        if (slotReservedUntil_[i] != 0 && nowUnix >= slotReservedUntil_[i]) {
            // Pidio token y nunca abrio el WebSocket: el slot vuelve al pool.
            slotReservedUntil_[i] = 0;
            slotReservedIp_[i] = 0u;
            slotBusy_[i] = false;
        }
    }
}

int SignalingServer::reserveSlotFor(std::uint32_t ip) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(slotLock_);
    expireReservationsLocked(now);

    // Si este cliente ya tiene una reserva viva, se la refresca en vez de
    // quemar otro slot: es el caso de recargar la pagina de ingreso.
    for (size_t i = 1; i < slotReservedUntil_.size(); ++i) {
        if (slotReservedUntil_[i] != 0 && slotReservedIp_[i] == ip) {
            slotReservedUntil_[i] = now + auth_.ttlSec();
            return static_cast<int>(i);
        }
    }

    for (size_t i = 1; i < slotBusy_.size(); ++i) {
        if (!slotBusy_[i]) {
            slotBusy_[i] = true;
            slotReservedUntil_[i] = now + auth_.ttlSec();
            slotReservedIp_[i] = ip;
            return static_cast<int>(i);
        }
    }
    return 0; // sala llena
}

bool SignalingServer::tryClaimSlot(int slot) {
    const int now = static_cast<int>(ba::security::nowUnix());
    std::lock_guard<std::mutex> lock(slotLock_);
    expireReservationsLocked(now);
    if (slot < 1 || slot >= static_cast<int>(slotBusy_.size())) return false;
    const size_t i = static_cast<size_t>(slot);
    if (slotBusy_[i]) {
        // Ocupado: solo se admite si es una RESERVA pendiente. Quien llega aca
        // ya presento un token valido y de un solo uso, asi que es su dueño.
        if (slotReservedUntil_[i] == 0) {
            return false; // hay una sesion activa de verdad
        }
        slotReservedUntil_[i] = 0; // reserva -> ocupacion activa
        slotReservedIp_[i] = 0u;
        return true;
    }
    slotBusy_[i] = true;
    return true;
}

void SignalingServer::releaseSlot(int slot) {
    std::lock_guard<std::mutex> lock(slotLock_);
    if (slot < 1 || slot >= static_cast<int>(slotBusy_.size())) return;
    slotBusy_[static_cast<size_t>(slot)] = false;
    slotReservedUntil_[static_cast<size_t>(slot)] = 0;
    slotReservedIp_[static_cast<size_t>(slot)] = 0u;
}

// --- Socket send ------------------------------------------------------------
bool SignalingServer::sendAll(long long fd, const void* data, size_t len) const {
    return sendAllImpl(fd, data, len);
}

bool SignalingServer::sendResponse(long long fd, int code, const std::string& status,
                                   const std::string& contentType, const std::string& body) const {
    std::string resp =
        "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n" + "Content-Type: " + contentType + "\r\n" +
        "Content-Length: " + std::to_string(body.size()) + "\r\n" + "Connection: close\r\n\r\n" + body;
    return sendAll(fd, resp.data(), resp.size());
}

// --- Frames WebSocket (RFC 6455) -------------------------------------------
namespace {
/// Lee un frame WS (cliente→servidor, enmascarado). Devuelve true si se leyo.
bool readWsFrame(long long fd, std::string& payload, int& opcode) {
    payload.clear();
    opcode = 0;
    // Opcode del mensaje EN CURSO cuando se esta reensamblando una secuencia
    // fragmentada (0 = todavia no empezo ninguno).
    int messageOpcode = 0;

    // Un mensaje WebSocket puede llegar partido en varios frames (RFC 6455
    // §5.4): el primero lleva el opcode real y FIN=0, los siguientes llevan
    // opcode 0 (continuacion) y el ultimo FIN=1. Antes se ignoraba el bit FIN
    // y cada fragmento se trataba como un mensaje completo, asi que un SDP
    // grande -- que los navegadores parten con naturalidad -- llegaba en trozos
    // y el parseo JSON fallaba. Este bucle reensambla hasta ver FIN.
    for (;;) {
        uint8_t h[2];
        if (!readExact(fd, h, 2)) return false;
        const bool fin = (h[0] & 0x80) != 0;
        const int op = h[0] & 0x0F;
        const bool masked = (h[1] & 0x80) != 0;

        // RFC 6455 §5.1: todo frame de cliente a servidor DEBE venir
        // enmascarado. Un frame sin mascara es un error de protocolo y la
        // conexion se cierra en vez de procesarlo.
        if (!masked) return false;

        uint64_t len = h[1] & 0x7F;
        if (len == 126) {
            uint8_t ext[2];
            if (!readExact(fd, ext, 2)) return false;
            len = (uint64_t(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (!readExact(fd, ext, 8)) return false;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | ext[i];
        }
        // El tope se aplica al mensaje REENSAMBLADO, no a cada fragmento: si no,
        // bastaba con partir un payload enorme para saltarse el limite.
        if (len > Frame::kMaxPayloadBytes || payload.size() + len > Frame::kMaxPayloadBytes) {
            return false;
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (!readExact(fd, mask, 4)) return false;

        const size_t base = payload.size();
        payload.resize(base + static_cast<size_t>(len));
        if (len > 0 && !readExact(fd, reinterpret_cast<uint8_t*>(&payload[base]), len)) return false;
        for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
            payload[base + i] = static_cast<char>(static_cast<uint8_t>(payload[base + i]) ^ mask[i % 4]);
        }

        // Los frames de CONTROL (>= 0x8: close, ping, pong) nunca se fragmentan
        // y pueden intercalarse en medio de un mensaje fragmentado. Se entregan
        // tal cual al caller, sin tocar el reensamblado en curso.
        if (op >= 0x8) {
            if (base != 0) {
                // Habia un mensaje a medias: no se puede devolver el control y
                // conservar el parcial con esta API de una sola salida, asi que
                // se prioriza el frame de control y se descarta el parcial.
                payload.erase(0, base);
            }
            opcode = op;
            return true;
        }

        if (op != 0x0) {
            // Frame inicial de un mensaje: fija el opcode real.
            messageOpcode = op;
        } else if (messageOpcode == 0) {
            // Continuacion sin mensaje empezado: error de protocolo.
            return false;
        }

        if (fin) {
            opcode = messageOpcode;
            return true;
        }
        // FIN=0: seguir leyendo fragmentos.
    }
}

/// Escribe un frame WS (servidor→cliente, sin mascara).
bool writeWsFrame(long long fd, int opcode, const std::string& payload) {
    std::string out;
    out.push_back(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN + opcode
    const size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n < 65536) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
    }
    out += payload;
    return sendAllImpl(fd, out.data(), out.size());
}
} // namespace

} // namespace ba::gateway
