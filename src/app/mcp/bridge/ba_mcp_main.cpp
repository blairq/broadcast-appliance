// ============================================================================
// src/app/mcp/bridge/ba_mcp_main.cpp — ba-mcp: puente MCP stdio <-> HTTP
//
// Para clientes MCP que lanzan un proceso (Claude Desktop, etc.): lee mensajes
// JSON-RPC de stdin (uno por linea), los reenvia al servidor MCP de la app
// (http://127.0.0.1:<puerto>/mcp con su token) y escribe cada respuesta en
// stdout, una por linea. stderr solo lleva diagnosticos.
//
// Sin Qt y con el runtime de C++ estatico: solo depende de la libc, asi arranca
// en cualquier maquina aunque la lance un cliente externo (que no pasa por el
// launcher). El HTTP es el minimo que habla McpHttp (127.0.0.1, POST con
// Content-Length, respuesta con Content-Length y Connection: close).
//
// De donde saca puerto y token (en este orden):
//   1. BA_MCP_URL y BA_MCP_TOKEN
//   2. Runtime/mcp.json (dentro del bundle, lo publica la app al arrancar; se
//      relee en cada mensaje: si la app se reinicia, el puente sigue andando) y
//      el token cifrado de Credenciales/.
// `ba-mcp --token` imprime el token (para configurar un cliente por HTTP).
// `ba-mcp --rol <rol>` se presenta con ese rol en cada pedido (cabecera
// X-BA-Rol): el servidor le muestra solo el juego de herramientas del rol.
// Es como lanza la app a sus agentes automaticos (recepcion, tecnico...).
// ============================================================================

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
#endif

#include "platform/Paths.hpp"
#include "security/CredentialStore.hpp"

namespace {

using json = nlohmann::json;

struct Target {
    std::string host = "127.0.0.1";
    int port = 0;
    std::string path = "/mcp";
    std::string token;
};

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

bool parseUrl(const std::string& url, Target& t) {
    const std::string pre = "http://";
    if (url.rfind(pre, 0) != 0) return false;
    const std::string rest = url.substr(pre.size());
    const auto slash = rest.find('/');
    const std::string hostport = rest.substr(0, slash);
    t.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = hostport.find(':');
    t.host = hostport.substr(0, colon);
    t.port = colon == std::string::npos ? 80 : std::atoi(hostport.c_str() + colon + 1);
    return t.port > 0;
}

std::string storedToken() {
    std::string token;
    ba::security::CredentialStore::bundle().read("mcp-token", token);
    return trim(token);
}

bool findTarget(Target& t) {
    const char* envUrl = std::getenv("BA_MCP_URL");
    const char* envToken = std::getenv("BA_MCP_TOKEN");
    if (envUrl != nullptr && *envUrl != '\0' && envToken != nullptr && *envToken != '\0') {
        t.token = envToken;
        return parseUrl(envUrl, t);
    }
    const auto dir = ba::platform::stateDir(); // <bundle>/Runtime (el puente vive en el mismo bundle)
    if (dir.empty()) return false;
    std::ifstream in(dir / "mcp.json", std::ios::binary);
    if (!in) return false;
    const json o = json::parse(in, nullptr, false);
    if (!o.is_object() || !o.contains("url")) return false;
    t.token = storedToken();
    return parseUrl(o.value("url", std::string()), t) && !t.token.empty();
}

std::mutex g_out;

void writeLine(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_out);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void writeError(const json& id, const std::string& message) {
    if (id.is_null()) return; // notificacion: no se contesta
    writeLine(
        json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32000}, {"message", message}}}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void closeSock(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

struct HttpReply {
    int status = 0;
    std::string sessionId;
    std::string body;
    std::string error;
};

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

HttpReply post(const Target& t, const std::string& session, const std::string& rol, const std::string& body) {
    HttpReply r;
    const socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        r.error = "socket";
        return r;
    }
    // capture_sequence puede tardar: hasta 5 minutos por respuesta.
#ifdef _WIN32
    DWORD tv = 300000;
#else
    timeval tv{300, 0};
#endif
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<unsigned short>(t.port));
    if (::inet_pton(AF_INET, t.host == "localhost" ? "127.0.0.1" : t.host.c_str(), &a.sin_addr) != 1 ||
        ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closeSock(s);
        r.error = "no se pudo conectar a " + t.host + ":" + std::to_string(t.port);
        return r;
    }
    std::string req = "POST " + t.path + " HTTP/1.1\r\nHost: " + t.host + ":" + std::to_string(t.port) +
                      "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\n"
                      "Authorization: Bearer " +
                      t.token + "\r\n";
    if (!session.empty()) req += "Mcp-Session-Id: " + session + "\r\n";
    if (!rol.empty()) req += "X-BA-Rol: " + rol + "\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    for (std::size_t sent = 0; sent < req.size();) {
        const auto n = ::send(s, req.data() + sent, static_cast<int>(req.size() - sent), 0);
        if (n <= 0) {
            closeSock(s);
            r.error = "no se pudo enviar";
            return r;
        }
        sent += static_cast<std::size_t>(n);
    }
    std::string in;
    char buf[16384];
    std::size_t headerEnd = std::string::npos;
    long long need = -1;
    for (;;) {
        const auto n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        in.append(buf, static_cast<std::size_t>(n));
        if (headerEnd == std::string::npos) {
            headerEnd = in.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::string head = lower(in.substr(0, headerEnd));
                const auto cl = head.find("\ncontent-length:");
                if (cl != std::string::npos) need = std::atoll(head.c_str() + cl + 16);
            }
        }
        if (headerEnd != std::string::npos && need >= 0 &&
            static_cast<long long>(in.size() - headerEnd - 4) >= need)
            break;
    }
    closeSock(s);
    if (headerEnd == std::string::npos) {
        r.error = in.empty() ? "sin respuesta" : "respuesta invalida";
        return r;
    }
    const std::string head = in.substr(0, headerEnd);
    if (head.size() > 12) r.status = std::atoi(head.c_str() + 9); // "HTTP/1.1 200"
    const std::string lh = lower(head);
    if (const auto p = lh.find("\nmcp-session-id:"); p != std::string::npos) {
        std::size_t b = p + 16;
        while (b < head.size() && head[b] == ' ')
            ++b;
        const std::size_t e = head.find("\r\n", b);
        r.sessionId = head.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    r.body = in.substr(headerEnd + 4);
    if (need >= 0 && static_cast<long long>(r.body.size()) > need)
        r.body.resize(static_cast<std::size_t>(need));
    return r;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--token") {
        const std::string token = storedToken();
        if (token.empty()) {
            std::fprintf(stderr, "ba-mcp: todavia no hay token (abri la app una vez con el MCP activado)\n");
            return 1;
        }
        std::printf("%s\n", token.c_str());
        return 0;
    }
    std::string rol;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--rol") rol = argv[i + 1];
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    std::mutex sessionLock;
    std::string session;
    std::mutex pendingLock;
    std::condition_variable pendingCv;
    int pending = 0;

    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty()) continue;
        {
            std::lock_guard<std::mutex> lk(pendingLock);
            ++pending;
        }
        // Un thread por pedido: una captura lenta no frena a los demas.
        std::thread([line, &session, &sessionLock, &pending, &pendingLock, &pendingCv, &rol]() {
            const json doc = json::parse(line, nullptr, false);
            const json id = doc.is_object() && doc.contains("id") ? doc["id"] : json();
            Target t;
            if (!findTarget(t)) {
                writeError(
                    id, "Broadcast Appliance no esta abierto (no encuentro Runtime/mcp.json dentro del "
                        "bundle). Abri la app (con el servidor MCP activado en Configuracion -> Asistente) "
                        "y reintenta.");
            } else {
                std::string sid;
                {
                    std::lock_guard<std::mutex> lk(sessionLock);
                    sid = session;
                }
                const HttpReply r = post(t, sid, rol, line);
                if (!r.sessionId.empty()) {
                    std::lock_guard<std::mutex> lk(sessionLock);
                    session = r.sessionId;
                }
                const std::string body = trim(r.body);
                if (r.status == 200 && !body.empty()) {
                    writeLine(body);
                } else if (r.status == 202) {
                    // notificacion aceptada
                } else if (r.status == 401) {
                    writeError(id, "Token rechazado por la app (cambio Credenciales/mcp-token.cred?).");
                } else {
                    writeError(id, "La app no respondio (" +
                                       (r.status == 0 ? r.error : "HTTP " + std::to_string(r.status)) + ").");
                }
            }
            std::lock_guard<std::mutex> lk(pendingLock);
            --pending;
            pendingCv.notify_all();
        }).detach();
    }
    std::unique_lock<std::mutex> lk(pendingLock);
    pendingCv.wait(lk, [&]() { return pending == 0; });
    return 0;
}
