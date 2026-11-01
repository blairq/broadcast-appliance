// test_sse_epoch.cpp — el flujo de eventos de la sala hacia la app (G-7, G-9, G-10)
//
//   1. El primer `room` de cada conexión dice de qué arranque es la sala (`epoch`), la
//      versión del contrato (`protocolo`) y si pudo reenviar lo perdido (`reanudado`).
//   2. Last-Event-ID de esta misma sala: se reenvían los eventos de un solo disparo que
//      se perdieron mientras la app no estaba (una corrección, acá) y `reanudado` es true.
//   3. De otra sala (otro epoch): nada se reenvía y `reanudado` es false.
//   4. Si la cola corta ya no tiene todo lo perdido: `reanudado` false (la app republica).
//   5. Una app que no lee no traba a quien emite: los pedidos que generan eventos
//      terminan enseguida aunque haya una conexión de eventos que no lee nada.
//   6. Las claves de las superficies son del evento (G-1): la app las pone y valen
//      enseguida (la vieja deja de valer); una mal formada no cambia nada.
//   8. Una sala que arranca usa las claves del evento en curso que dejó la app en el almacén
//      (web 2): un QR que llega antes de que la app reconecte ya vale.
//   9. Cambiar una clave corta a las superficies que habían entrado con la de antes: al
//      volver con esa, «clave_mal»; con la nueva, reciben lo suyo (web 3).
//  10. «Sin evento»: claves nuevas al azar y se olvidan la urna y la página del público (web 3).
//   7. Los eventos de estado no se pierden al reanudar (web 1): con la app desconectada
//      cambia la urna (se abre una votación y llega un voto); la app vuelve REANUDANDO y
//      recibe, después de la foto, el último «votacion» (con el voto), sin id propio.

#include "Log.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"
#include "ipc/ProtocoloSala.hpp"
#include "platform/Paths.hpp"
#include "security/CredentialStore.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static constexpr TestFd kFdInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using namespace ba::gateway;
using json = nlohmann::json;

namespace {

int g_fallas = 0;
#define CHECK(c, m)                                                       \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);   \
            ++g_fallas;                                                   \
        } else {                                                          \
            std::printf("ok: %s\n", m);                                   \
        }                                                                 \
    } while (0)

void cerrar(TestFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

TestFd conectar(uint16_t puerto) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return fd;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        cerrar(fd);
        return kFdInvalid;
    }
#ifdef _WIN32
    DWORD ms = 300;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{0, 300000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    return fd;
}

/// Un pedido entero y la respuesta hasta que el servidor cierra.
std::string pedir(uint16_t puerto, const std::string& pedido) {
    TestFd fd = conectar(puerto);
    if (fd == kFdInvalid) return "";
    std::string out;
    if (send(fd, pedido.data(), static_cast<int>(pedido.size()), 0) == static_cast<int>(pedido.size())) {
        char buf[4096];
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < hasta) {
            const int n = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
            if (n == 0) break;
            if (n > 0) out.append(buf, static_cast<size_t>(n));
        }
    }
    cerrar(fd);
    return out;
}

std::string con(const std::string& metodo, const std::string& ruta, const std::string& cuerpo,
                const std::string& cabeceras = {}) {
    return metodo + " " + ruta + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" + cabeceras +
           "Content-Type: application/json\r\nContent-Length: " + std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
}

/// Abre el flujo de eventos (con Last-Event-ID si viene) y lee lo que llegue en `ms`.
struct Flujo {
    TestFd fd = kFdInvalid;
    std::string leido;
    void leer(int ms) {
        char buf[8192];
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < hasta) {
            const int n = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
            if (n == 0) break;
            if (n > 0) leido.append(buf, static_cast<size_t>(n));
        }
    }
    /// Los eventos leídos: [{id, event, data}].
    std::vector<json> eventos() const {
        std::vector<json> out;
        std::size_t p = leido.find("\r\n\r\n");
        p = p == std::string::npos ? 0 : p + 4;
        while (true) {
            const std::size_t fin = leido.find("\n\n", p);
            if (fin == std::string::npos) break;
            json e = json::object();
            const std::string bloque = leido.substr(p, fin - p);
            std::size_t q = 0;
            while (q < bloque.size()) {
                std::size_t eol = bloque.find('\n', q);
                if (eol == std::string::npos) eol = bloque.size();
                const std::string l = bloque.substr(q, eol - q);
                if (l.rfind("id: ", 0) == 0) e["id"] = l.substr(4);
                if (l.rfind("event: ", 0) == 0) e["event"] = l.substr(7);
                if (l.rfind("data: ", 0) == 0) e["data"] = json::parse(l.substr(6), nullptr, false);
                q = eol + 1;
            }
            if (e.contains("event")) out.push_back(e);
            p = fin + 2;
        }
        return out;
    }
    ~Flujo() {
        if (fd != kFdInvalid) cerrar(fd);
    }
};

void abrir(Flujo& f, uint16_t admin, const std::string& token, const std::string& ultimo) {
    f.fd = conectar(admin);
    const std::string p = "GET /api/v1/events HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token + "\r\n" +
                          (ultimo.empty() ? std::string() : "Last-Event-ID: " + ultimo + "\r\n") + "\r\n";
    send(f.fd, p.data(), static_cast<int>(p.size()), 0);
}

// --- Un WebSocket de superficie (como comun/canal.js: texto JSON, docs/arquitectura/06-canal-web.md) ---
TestFd wsAbrir(uint16_t puerto, const std::string& query) {
    TestFd fd = conectar(puerto);
    const std::string p = "GET /ws?" + query + " HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    send(fd, p.data(), static_cast<int>(p.size()), 0);
    std::string r;
    char c;
    while (r.find("\r\n\r\n") == std::string::npos && recv(fd, &c, 1, 0) == 1) r.push_back(c);
    return fd;
}
void wsMandar(TestFd fd, const json& m) {
    const std::string carga = m.dump();
    std::string f;
    f.push_back(static_cast<char>(0x81));
    f.push_back(static_cast<char>(0x80 | carga.size())); // < 126
    f.append(4, '\0');                                   // máscara 0
    f += carga;
    send(fd, f.data(), static_cast<int>(f.size()), 0);
}
/// Los mensajes que llegan en `ms` (frames del servidor, sin máscara).
std::vector<json> wsLeer(TestFd fd, int ms) {
    std::string b;
    char buf[8192];
    const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < hasta) {
        const int n = static_cast<int>(recv(fd, buf, sizeof(buf), 0));
        if (n == 0) break;
        if (n > 0) b.append(buf, static_cast<size_t>(n));
    }
    std::vector<json> out;
    std::size_t p = 0;
    while (p + 2 <= b.size()) {
        std::size_t len = static_cast<unsigned char>(b[p + 1]) & 0x7F, ini = p + 2;
        if (len == 126) {
            len = (static_cast<std::size_t>(static_cast<unsigned char>(b[p + 2])) << 8) | static_cast<unsigned char>(b[p + 3]);
            ini = p + 4;
        } else if (len == 127) {
            break;
        }
        if (ini + len > b.size()) break;
        out.push_back(json::parse(b.substr(ini, len), nullptr, false));
        p = ini + len;
    }
    return out;
}
bool llego(const std::vector<json>& ms, const std::string& tipo) {
    for (const json& m : ms)
        if (m.is_object() && m.value("type", "") == tipo) return true;
    return false;
}

std::string corregir(uint16_t invitados, int id, const std::string& texto) {
    return pedir(invitados,
                 con("POST", "/api/correccion", json{{"k", "clave-corr"}, {"id", id}, {"texto", texto}}.dump()));
}

} // namespace

int main() {
    std::printf("== test_sse_epoch ==\n");
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    // Un almacén de credenciales propio (las claves del evento en curso, el 8).
    const auto datos = std::filesystem::temp_directory_path() / "ba-test-sse-epoch-datos";
    std::filesystem::remove_all(datos);
    std::filesystem::create_directories(datos);
#ifdef _WIN32
    _putenv_s(ba::platform::kDataRootEnv, datos.string().c_str());
#else
    setenv(ba::platform::kDataRootEnv, datos.string().c_str(), 1);
#endif
    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182");
    Room::Cfg rcfg;
    rcfg.name = "sala-test";
    Room room(rcfg);
    const auto dir = std::filesystem::temp_directory_path() / "ba-test-sse-epoch";
    std::filesystem::remove_all(dir);
    ShareStore shares(dir, 1u << 20);
    SignalingServer::Cfg c;
    c.room = "sala-test";
    c.port = 0;
    c.adminPort = 0;
    c.correccionKey = "clave-corr";
    const std::string kToken = "token-de-la-app";
    c.adminToken = kToken;
    SignalingServer srv(c, auth, hub, room, shares, log);
    CHECK(srv.start(), "el servidor arranco");
    const uint16_t admin = srv.adminPort();
    const uint16_t invitados = srv.guestPort();

    // --- 1) El primer room ----------------------------------------------------------
    std::string ultimo;
    {
        Flujo f;
        abrir(f, admin, kToken, "");
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && ev[0].value("event", "") == "room", "lo primero es la foto");
        const json d = ev.empty() ? json() : ev[0].value("data", json());
        CHECK(d.value("epoch", "") == srv.epoch() && !srv.epoch().empty(), "con el epoch de este arranque");
        CHECK(d.value("protocolo", 0) == ba::ipc::kProtocoloSala, "y la versión del contrato");
        CHECK(d.value("reanudado", true) == false, "sin Last-Event-ID no hay nada que reanudar");
        ultimo = ev.empty() ? "" : ev[0].value("id", "");
        CHECK(ultimo.rfind(srv.epoch() + ":", 0) == 0, "el id es «<epoch>:<n>»");
        // Uno que llega con la conexión abierta, con su id.
        corregir(invitados, 1, "uno");
        f.leer(400);
        const auto ev2 = f.eventos();
        CHECK(ev2.size() >= 2 && ev2.back().value("event", "") == "correccion", "un evento en vivo llega");
        if (ev2.size() >= 2) ultimo = ev2.back().value("id", "");
    }

    // --- 2) Lo que se perdió, de esta misma sala --------------------------------------
    corregir(invitados, 7, "siete"); // con la app desconectada
    {
        Flujo f;
        abrir(f, admin, kToken, ultimo);
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && ev[0].value("data", json()).value("reanudado", false), "misma sala: reanudado");
        bool siete = false, uno = false;
        for (const auto& e : ev) {
            if (e.value("event", "") != "correccion") continue;
            const int id = e.value("data", json()).value("id", 0);
            siete = siete || id == 7;
            uno = uno || id == 1;
        }
        CHECK(siete, "la corrección perdida se reenvía");
        CHECK(!uno, "la que ya había llegado, no");
        for (const auto& e : ev)
            if (e.contains("id")) ultimo = e.value("id", "");
    }

    // --- 7) La urna cambia mientras la app no está; vuelve reanudando ----------------
    {
        const std::string auth = "Authorization: Bearer " + kToken + "\r\n";
        Flujo f;
        abrir(f, admin, kToken, ultimo);
        f.leer(300);
        // Con la app conectada se abre la votación (la app lo ve)…
        std::string r = pedir(admin, con("POST", "/api/v1/votacion",
                                         json{{"accion", "abrir"}, {"pregunta", "¿Sí?"}, {"opciones", {"sí", "no"}},
                                              {"evento", "ev-prueba"}}.dump(), auth));
        CHECK(r.find("200 OK") != std::string::npos, "se abre una votación");
        f.leer(300);
        for (const auto& e : f.eventos())
            if (e.contains("id")) ultimo = e.value("id", "");
        cerrar(f.fd);
        f.fd = kFdInvalid;
        // …y con la app afuera llega un voto (cambia la urna).
        pedir(admin, con("POST", "/api/v1/claves", json{{"publico", "1234567890abcdef1234567890abcdef"}}.dump(), auth));
        r = pedir(invitados, con("POST", "/api/votacion", json{{"opcion", 0}, {"yo", "tel-1"}}.dump(),
                                 "X-BA-Clave: " + srv.claves().publico + "\r\n"));
        CHECK(r.find("\"ok\":true") != std::string::npos, "llega un voto con la app desconectada");
        Flujo g;
        abrir(g, admin, kToken, ultimo);
        g.leer(400);
        const auto ev = g.eventos();
        CHECK(!ev.empty() && ev[0].value("data", json()).value("reanudado", false), "la app reanuda");
        int votos = -1;
        bool conId = false;
        for (const auto& e : ev) {
            if (e.value("event", "") != "votacion") continue;
            votos = e.value("data", json()).value("votacion", json()).value("votaron", -1);
            conId = conId || e.contains("id");
        }
        CHECK(votos == 1, "y recibe la urna como está ahora (con el voto)");
        CHECK(!conId, "sin id: no mueve el Last-Event-ID de la foto");
        CHECK(!ev.empty() && ev.back().value("event", "") != "room", "después de la foto");
    }

    // --- 3) De otra sala -----------------------------------------------------------------
    {
        Flujo f;
        abrir(f, admin, kToken, "otraSala:1");
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && !ev[0].value("data", json()).value("reanudado", true), "otro epoch: no reanudado");
        int correcciones = 0;
        for (const auto& e : ev) correcciones += e.value("event", "") == "correccion";
        CHECK(correcciones == 0, "y no se reenvía nada");
    }

    // --- 5) Una app que no lee no traba a quien emite (antes que el 4: llena la cola) ---
    {
        Flujo mudo;
        abrir(mudo, admin, kToken, ""); // nunca lee
        const std::string grande(1500, 'x');
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1200; ++i) corregir(invitados, 100 + i, grande);
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("   1200 eventos con una conexión que no lee: %lld ms\n", static_cast<long long>(ms));
        CHECK(ms < 8000, "quien emite no espera a la app que no lee");
    }

    // --- 4) La cola corta ya no tiene todo ---------------------------------------------
    {
        Flujo f;
        abrir(f, admin, kToken, ultimo);
        f.leer(400);
        const auto ev = f.eventos();
        CHECK(!ev.empty() && !ev[0].value("data", json()).value("reanudado", true),
              "si se perdió más de lo que se guarda: no reanudado (la app republica todo)");
    }

    // --- 6) Las claves del evento ---------------------------------------------------
    {
        const std::string auth = "Authorization: Bearer " + kToken + "\r\n";
        const std::string vieja = srv.claves().publico;
        const std::string nueva = "0123456789abcdef0123456789abcdef";
        std::string r = pedir(admin, con("POST", "/api/v1/claves", json{{"publico", nueva}}.dump(), auth));
        CHECK(r.find("200 OK") != std::string::npos, "la app pone la clave del público");
        r = pedir(invitados, con("GET", "/api/publico-info?k=" + nueva, ""));
        CHECK(r.find("200 OK") != std::string::npos, "y vale enseguida");
        r = pedir(invitados, con("GET", "/api/publico-info?k=" + vieja, ""));
        CHECK(r.find("403") != std::string::npos, "la de este arranque ya no");
        r = pedir(admin, con("POST", "/api/v1/claves", json{{"rotulos", "corta"}, {"publico", "f00f00f00f00f00f00"}}.dump(), auth));
        CHECK(r.find("400") != std::string::npos && srv.claves().publico == nueva, "una mal formada: 400 y no cambia nada");
        r = pedir(admin, con("GET", "/api/v1/publico-link?target=local", "", auth));
        CHECK(r.find(nueva) != std::string::npos || r.find("\"ok\":false") != std::string::npos, "el enlace del QR lleva la del evento");
    }

    // --- 9) Cambiar una clave desautoriza a las superficies de antes -------------------
    {
        const std::string auth = "Authorization: Bearer " + kToken + "\r\n";
        const auto entrar = [&](const std::string& k) {
            TestFd ws = wsAbrir(invitados, "superficie");
            wsMandar(ws, {{"v", 1}, {"type", "auth"}, {"tipo", "rotulos"}, {"id", "rot-1"}, {"k", k}});
            return ws;
        };
        const std::string vieja = srv.claves().rotulos;
        TestFd ws = entrar(vieja);
        CHECK(llego(wsLeer(ws, 400), "rotulos"), "una superficie de rótulos con la clave buena recibe lo suyo");
        const std::string otra = "abcdefabcdefabcdefabcdefabcdef01";
        pedir(admin, con("POST", "/api/v1/claves", json{{"rotulos", otra}}.dump(), auth));
        wsLeer(ws, 300);
        char c;
        CHECK(recv(ws, &c, 1, 0) <= 0, "otra clave (otro evento): se le corta el canal");
        cerrar(ws);
        ws = entrar(vieja);
        const std::vector<json> r = wsLeer(ws, 400);
        bool mal = false;
        for (const json& m : r) mal = mal || (m.value("type", "") == "auth" && m.value("error_codigo", "") == "clave_mal");
        CHECK(mal && !llego(r, "rotulos"), "al volver con la de antes: «clave_mal» y nada de lo suyo");
        cerrar(ws);
        ws = entrar(otra);
        CHECK(llego(wsLeer(ws, 400), "rotulos"), "con la nueva (la página reintenta sola): vuelve, sin recargar");
        cerrar(ws);
    }

    // --- 10) Sin evento -------------------------------------------------------------------
    {
        const std::string auth = "Authorization: Bearer " + kToken + "\r\n";
        const std::string antes = srv.claves().publico;
        pedir(admin, con("POST", "/api/v1/publico-info", json{{"info", {{"evento", "El de ayer"}}}}.dump(), auth));
        std::string r = pedir(admin, con("POST", "/api/v1/claves", json{{"sinEvento", true}}.dump(), auth));
        CHECK(r.find("200 OK") != std::string::npos && srv.claves().publico != antes && srv.claves().publico.size() == 32,
              "sin evento: claves nuevas al azar");
        r = pedir(admin, con("GET", "/api/v1/votacion", "", auth));
        CHECK(r.find("\"hubo\":false") != std::string::npos, "y la urna del evento anterior se olvida");
        r = pedir(invitados, con("GET", "/api/publico-info", "", "X-BA-Clave: " + srv.claves().publico + "\r\n"));
        CHECK(r.find("200 OK") != std::string::npos && r.find("El de ayer") == std::string::npos,
              "y la página del público también");
    }

    srv.stop();

    // --- 8) Una sala que arranca, con las claves del evento en curso ------------------------
    {
        const std::string publico = "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f";
        ba::security::CredentialStore::bundle().write(ba::ipc::kClavesEnCurso, json{{"publico", publico}}.dump());
        SignalingServer otra(c, auth, hub, room, shares, log);
        CHECK(otra.claves().publico == publico, "arranca con la clave del evento en curso (sin esperar a la app)");
        CHECK(otra.start(), "y arranca");
        const std::string r =
            pedir(otra.guestPort(), con("GET", "/api/publico-info", "", "X-BA-Clave: " + publico + "\r\n"));
        CHECK(r.find("200 OK") != std::string::npos, "un QR impreso que llega antes que la app: vale");
        otra.stop();
    }
    std::filesystem::remove_all(datos);
    std::filesystem::remove_all(dir);
    std::printf(g_fallas == 0 ? "== OK ==\n" : "== %d FALLAS ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
