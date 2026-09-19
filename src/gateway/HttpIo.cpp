// ============================================================================
// src/gateway/HttpIo.cpp — implementacion de HttpIo.hpp (ver el header)
// ============================================================================

#include "HttpIo.hpp"

#include <algorithm>
#include <cstring>

#include "ipc/ControlProtocol.hpp" // Frame::kMaxPayloadBytes (tope de un mensaje WS)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Fd;
#define FD_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Fd;
#define FD_INVALID (-1)
#endif

namespace ba::gateway::http {

using ba::ipc::Frame;

namespace {
Fd toFd(long long f) { return static_cast<Fd>(f); }
long long toLL(Fd f) { return static_cast<long long>(f); }
} // namespace

// --- SHA-1 (para el handshake WebSocket, RFC 6455) -------------------------
namespace {
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

} // namespace

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

static void closeFdRaw(Fd s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

long long makeListen(const std::string& bindAddress, uint16_t port) {
    Fd s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == FD_INVALID) return -1;
    int yes = 1;
#ifdef _WIN32
    // En Windows SO_REUSEADDR deja que OTRO proceso se enganche al mismo
    // puerto y reciba conexiones: se pide el puerto en exclusiva.
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // 127.0.0.1 por defecto (spec §5.6: lo externo entra por el tunel). La
    // red local ("0.0.0.0" o una IP) es una eleccion explicita del operador.
    if (inet_pton(AF_INET, bindAddress.empty() ? "127.0.0.1" : bindAddress.c_str(), &addr.sin_addr) != 1) {
        closeFdRaw(s);
        return -1;
    }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeFdRaw(s);
        return -1;
    }
    if (listen(s, 16) != 0) {
        closeFdRaw(s);
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
#ifdef SO_NOSIGPIPE
    setsockopt(toFd(fd), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)); // macOS: ver sendAll
#endif
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

bool sendAll(long long fd, const void* data, size_t n) {
    // Escribir a un par que ya cerro NO debe matar el proceso con SIGPIPE: es
    // lo normal cuando un invitado recarga la pagina en medio de un envio.
    // Linux: MSG_NOSIGNAL por llamada. macOS no lo tiene: SO_NOSIGPIPE en el
    // socket (setNoDelay). Windows no tiene SIGPIPE.
#ifdef MSG_NOSIGNAL
    constexpr int kFlags = MSG_NOSIGNAL;
#else
    constexpr int kFlags = 0;
#endif
    size_t sent = 0;
    while (sent < n) {
        const int r = send(toFd(fd), reinterpret_cast<const char*>(static_cast<const uint8_t*>(data) + sent),
                           static_cast<int>(n - sent), kFlags);
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
    return sendAll(fd, out.data(), out.size());
}
void closeFd(long long fd) {
    if (fd >= 0) closeFdRaw(toFd(fd));
}

void shutdownFd(long long fd) {
    if (fd < 0) return;
#ifdef _WIN32
    ::shutdown(toFd(fd), SD_BOTH);
#else
    ::shutdown(toFd(fd), SHUT_RDWR);
#endif
}

uint16_t localPort(long long fd) {
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    if (getsockname(toFd(fd), reinterpret_cast<sockaddr*>(&a), &len) != 0) return 0;
    return ntohs(a.sin_port);
}

int recvSome(long long fd, uint8_t* out, size_t n) {
    const int want = static_cast<int>(std::min<size_t>(n, 1u << 30));
    return static_cast<int>(recv(toFd(fd), reinterpret_cast<char*>(out), want, 0));
}

const char* statusText(int code) {
    switch (code) {
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

bool sendResponse(long long fd, int code, const std::string& contentType, const std::string& body,
                  const std::string& extraHeaders) {
    std::string resp = "HTTP/1.1 " + std::to_string(code) + " " + statusText(code) + "\r\n";
    if (!contentType.empty()) resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    // Nada de cache para la API ni de sniffing de tipos en el navegador.
    resp += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    resp += extraHeaders;
    resp += "Connection: close\r\n\r\n";
    resp += body;
    return sendAll(fd, resp.data(), resp.size());
}

} // namespace ba::gateway::http
