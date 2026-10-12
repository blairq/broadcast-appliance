// ============================================================================
// src/gateway/tests/test_pinggy.cpp — acceso remoto por pinggy.io
//
//   1. Argumentos de ba-pinggy: forwardings (http siempre; tlstcp/tcp/udp con el
//      TURN), puertos prohibidos (8788, 8790, la señalización común) y que el
//      token NUNCA vaya en la línea de comandos.
//   2. Eventos JSON de ba-pinggy -> estado (listo, TURN público, reconectando,
//      error no recuperable, líneas que no son JSON).
//   3. URL pública (host configurado primero) y la forma del JSON de /api/remoto.
//   4. PROXY v1: TCP4, TCP6 (mapeada y seudo-IP 240.x), basura.
//   5. La escucha del túnel: todo lo que entra es EXTERNO (la botonera, que solo
//      atiende a la red local, la rechaza aunque la IP sea 127.0.0.1 o de la LAN
//      por PROXY v1) y los límites de PIN van por la IP real del PROXY v1.
//   6. GET /api/remoto en la API de control (sin proveedor y con uno).
//   7. Con un ba-pinggy falso (script de shell, solo POSIX): el supervisor llega
//      a «listo», el token va por el entorno, stop() lo apaga; un error no
//      recuperable no se reintenta.
// ============================================================================
#include "Log.hpp"
#include "Pinggy.hpp"
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using TestFd = SOCKET;
static constexpr TestFd kFdInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using TestFd = int;
static constexpr TestFd kFdInvalid = -1;
#endif

using ba::gateway::PinggySupervisor;
using json = nlohmann::json;

namespace {

int g_fallas = 0;

#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

bool contiene(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::string unir(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += x + " ";
    return s;
}

std::uint32_t ip4(unsigned a, unsigned b, unsigned c, unsigned d) {
    const unsigned char x[4] = {static_cast<unsigned char>(a), static_cast<unsigned char>(b),
                                static_cast<unsigned char>(c), static_cast<unsigned char>(d)};
    std::uint32_t r;
    std::memcpy(&r, x, 4);
    return r;
}

// --- Sockets de prueba (como test_gateway_http) ------------------------------

void cerrarFd(TestFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

std::string ida(uint16_t puerto, const std::string& pedido) {
    TestFd fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kFdInvalid) return "";
#ifdef _WIN32
    DWORD tv = 3000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        cerrarFd(fd);
        return "";
    }
    size_t enviado = 0;
    while (enviado < pedido.size()) {
        const int n = send(fd, pedido.data() + enviado, static_cast<int>(pedido.size() - enviado), 0);
        if (n <= 0) break;
        enviado += static_cast<size_t>(n);
    }
    std::string out;
    char buf[4096];
    for (int i = 0; i < 64; ++i) {
        const int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    cerrarFd(fd);
    return out;
}

int codigo(const std::string& resp) {
    if (resp.size() < 12) return 0;
    return std::atoi(resp.substr(9, 3).c_str());
}

std::string cuerpo(const std::string& resp) {
    const auto p = resp.find("\r\n\r\n");
    return p == std::string::npos ? std::string() : resp.substr(p + 4);
}

std::string proxyV1(const std::string& ip) { return "PROXY TCP4 " + ip + " 127.0.0.1 40000 8786\r\n"; }

std::string postJoin(const std::string& pin) {
    const std::string b = R"({"room":"sala-test","pin":")" + pin + R"("})";
    return "POST /api/join HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(b.size()) + "\r\n\r\n" + b;
}

// --- Casos -------------------------------------------------------------------

void argumentos() {
    PinggySupervisor::Cfg c;
    c.token = "TOKEN-SECRETO";
    std::string err;
    auto a = PinggySupervisor::argumentos(c, &err);
    CHECK(!a.empty() && err.empty(), "argumentos básicos");
    CHECK(contiene(a, "http=127.0.0.1:8786"), "http hacia la escucha del túnel");
    CHECK(!contiene(a, "tlstcp=127.0.0.1:3478"), "sin TURN no hay forwarding del TURN");
    CHECK(contiene(a, "--reconectar") && contiene(a, "--haproxy") && contiene(a, "--xff") && contiene(a, "auto"),
          "reconectar/haproxy/xff/región");
    CHECK(unir(a).find("TOKEN-SECRETO") == std::string::npos, "el token nunca en la línea de comandos");
    CHECK(!contiene(a, "--host") && !contiene(a, "--listablanca"), "sin host ni lista blanca si están vacíos");

    c.turn = true;
    c.turnTcpPuerto = 40123;
    c.turnUdp = true;
    c.host = "sala.ejemplo.com.ar";
    c.listaBlanca = "200.1.2.0/24";
    c.region = "us";
    a = PinggySupervisor::argumentos(c, &err);
    CHECK(!contiene(a, "tlstcp=127.0.0.1:3478"), "sin tlstcp (en el plan gratis tira la sesión; en Pro choca con el 443)");
    CHECK(contiene(a, "tcp=127.0.0.1:3478@:40123"), "TURN por tcp con el puerto reservado");
    CHECK(contiene(a, "udp=127.0.0.1:3478"), "TURN por udp");
    CHECK(contiene(a, "sala.ejemplo.com.ar") && contiene(a, "200.1.2.0/24") && contiene(a, "us"),
          "host, lista blanca y región");

    for (uint16_t p : {8788, 8790}) {
        PinggySupervisor::Cfg m;
        m.puertoTunel = p;
        err.clear();
        CHECK(PinggySupervisor::argumentos(m, &err).empty() && !err.empty(), "8788/8790 prohibidos por defecto");
    }
    {
        PinggySupervisor::Cfg m;
        m.puertoTunel = 8787;
        m.prohibidos.insert(8787);
        CHECK(PinggySupervisor::argumentos(m).empty(), "la señalización común prohibida (main la suma)");
        m.puertoTunel = 8786;
        m.turn = true;
        m.turnLocal = 8790;
        CHECK(PinggySupervisor::argumentos(m).empty(), "tampoco el TURN hacia un puerto prohibido");
    }
}

void eventos() {
    PinggySupervisor::Estado e;
    CHECK(e.estado == "apagado", "arranca apagado");
    CHECK(!PinggySupervisor::aplicarLinea("libpinggy: algo que no es JSON", e), "lo que no es JSON se ignora");
    CHECK(!PinggySupervisor::aplicarLinea("{\"sin\":\"ev\"}", e), "JSON sin ev se ignora");
    CHECK(!PinggySupervisor::aplicarLinea("{\"ev\":\"listo\",", e), "JSON cortado se ignora");
    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"conectando"})", e) && e.estado == "conectando", "conectando");
    const std::string listo =
        R"({"ev":"listo","urls":["http://abc.a.pinggy.link","https://abc.a.pinggy.link"],"forwardings":[)"
        R"({"tipo":"http","local":"127.0.0.1:8786","publico":"https://abc.a.pinggy.link"},)"
        R"({"tipo":"tcp","local":"127.0.0.1:3478","publico":"tcp://abc.a.pinggy.link:40123"},)"
        R"({"tipo":"tlstcp","local":"127.0.0.1:3478","publico":"tls://abc.a.pinggy.link:443"},)"
        R"({"tipo":"udp","local":"127.0.0.1:3478","publico":"udp://abc.a.pinggy.link:40124"}]})";
    CHECK(PinggySupervisor::aplicarLinea(listo, e), "listo");
    CHECK(e.estado == "listo" && e.urls.size() == 2, "listo con sus URLs");
    CHECK(e.urlHttp == "https://abc.a.pinggy.link", "la URL del forwarding http");
    CHECK(e.turnPublico == "abc.a.pinggy.link:443" && e.turnTipo == "tlstcp", "TURN: tlstcp antes que tcp");
    CHECK(e.turnUdp == "abc.a.pinggy.link:40124", "TURN por udp");
    CHECK(PinggySupervisor::urlPublicaDe(e, "") == "https://abc.a.pinggy.link", "URL pública sin host");
    CHECK(PinggySupervisor::urlPublicaDe(e, "sala.ejemplo.com.ar") == "https://sala.ejemplo.com.ar",
          "el host configurado primero");

    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"reconectando"})", e) && e.estado == "reconectando",
          "reconectando");
    CHECK(PinggySupervisor::urlPublicaDe(e, "") == "https://abc.a.pinggy.link", "reconectando: sigue la URL");
    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"reconectado","urls":["https://xyz.a.pinggy.link"]})", e),
          "reconectado");
    CHECK(e.estado == "listo" && e.urls.size() == 1 && e.turnTipo == "tlstcp", "reconectado: URLs nuevas, TURN igual");

    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"aviso","texto":"quedan 5 minutos"})", e) &&
              e.aviso == "quedan 5 minutos",
          "aviso");
    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"error","error":"red caída","recuperable":true})", e) &&
              e.estado == "error" && !e.fatal,
          "error recuperable");
    CHECK(PinggySupervisor::urlPublicaDe(e, "x.com").empty(), "con error no hay URL pública");
    CHECK(PinggySupervisor::aplicarLinea(R"({"ev":"error","error":"token inválido","recuperable":false})", e) &&
              e.fatal && e.error == "token inválido",
          "error no recuperable");

    // Solo TCP (sin tlstcp): el TURN es tcp. Un forwarding de otro puerto no es el TURN.
    PinggySupervisor::Estado t;
    PinggySupervisor::aplicarLinea(
        R"({"ev":"listo","urls":["https://q.pinggy.link"],"forwardings":[)"
        R"({"tipo":"tcp","local":"127.0.0.1:9999","publico":"tcp://q.pinggy.link:1"},)"
        R"({"tipo":"tcp","local":"127.0.0.1:3478","publico":"tcp://q.pinggy.link:2"}]})",
        t);
    CHECK(t.turnPublico == "q.pinggy.link:2" && t.turnTipo == "tcp", "TURN solo por tcp");
    CHECK(t.urlHttp == "https://q.pinggy.link", "sin forwarding http: la https de urls");
}

void formaJson() {
    PinggySupervisor::Estado e;
    PinggySupervisor::aplicarLinea(
        R"({"ev":"listo","urls":["https://abc.a.pinggy.link"],"forwardings":[)"
        R"({"tipo":"tlstcp","local":"127.0.0.1:3478","publico":"abc.a.pinggy.link:443"}]})",
        e);
    const json j = PinggySupervisor::estadoJsonDe(true, e, "");
    CHECK(j.at("activo").is_boolean() && j.at("activo").get<bool>(), "activo");
    CHECK(j.at("estado") == "listo", "estado");
    CHECK(j.at("urls").is_array() && j.at("urls").size() == 1, "urls");
    CHECK(j.at("urlPublica") == "https://abc.a.pinggy.link", "urlPublica");
    CHECK(j.at("error").is_string(), "error");
    CHECK(j.at("turn").at("publico") == "abc.a.pinggy.link:443" && j.at("turn").at("tipo") == "tlstcp" &&
              j.at("turn").at("udp") == "",
          "turn {publico, tipo, udp}");
    const json apagado = PinggySupervisor::estadoJsonDe(false, PinggySupervisor::Estado{}, "");
    CHECK(apagado.at("estado") == "apagado" && apagado.at("urlPublica") == "", "apagado sin URL");
}

void proxyV1Ips() {
    using ba::gateway::SignalingServer;
    std::uint32_t ip = 0;
    CHECK(SignalingServer::ipDeProxyV1("PROXY TCP4 200.45.1.2 127.0.0.1 5555 8786", ip) &&
              ip == ip4(200, 45, 1, 2),
          "PROXY TCP4");
    CHECK(SignalingServer::ipDeProxyV1("PROXY TCP6 ::ffff:181.1.2.3 ::1 5555 8786", ip) && ip == ip4(181, 1, 2, 3),
          "PROXY TCP6 mapeada");
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    CHECK(SignalingServer::ipDeProxyV1("PROXY TCP6 2800:810:1:2::1 ::1 5555 8786", a) &&
              SignalingServer::ipDeProxyV1("PROXY TCP6 2800:810:1:2::99 ::1 5555 8786", b) &&
              SignalingServer::ipDeProxyV1("PROXY TCP6 2800:810:1:3::1 ::1 5555 8786", c),
          "PROXY TCP6");
    CHECK(reinterpret_cast<const unsigned char*>(&a)[0] == 240, "TCP6 -> 240.x (nunca local)");
    CHECK(a == b && a != c, "una seudo-IP por /64");
    CHECK(!SignalingServer::ipDeProxyV1("PROXY UNKNOWN", ip), "UNKNOWN no da IP");
    CHECK(!SignalingServer::ipDeProxyV1("PROXY TCP4 no-es-ip 127.0.0.1 1 2", ip), "basura no da IP");
    CHECK(SignalingServer::ipDeXff("1.2.3.4, 190.5.6.7", ip) && ip == ip4(190, 5, 6, 7), "XFF: el último");
    CHECK(SignalingServer::ipDeXff(" 190.5.6.8 ", ip) && ip == ip4(190, 5, 6, 8), "XFF: uno solo");
    CHECK(!SignalingServer::ipDeXff("", ip) && !SignalingServer::ipDeXff("1.2.3.4, basura", ip), "XFF: basura");
}

void escuchaTunel() {
    using namespace ba::gateway;
    Log log;
    WebrtcHub hub(2, 640, 360, 48000, 2);
    TokenAuth auth("secreto-de-prueba", "9182", 100); // el tope global alto: se prueba el de por IP
    Room::Cfg rcfg;
    rcfg.name = "sala-test";
    rcfg.maxGuests = 2;
    Room room(rcfg);
    const auto sharesDir = std::filesystem::temp_directory_path() / "ba-test-pinggy-shares";
    std::filesystem::remove_all(sharesDir);
    ShareStore shares(sharesDir, 1u << 20);
    SignalingServer::Cfg s;
    s.room = "sala-test";
    s.port = 0;
    s.adminPort = 0;
    s.webroot = BA_TEST_WEBROOT;
    s.botoneraWebroot = (std::filesystem::path(BA_TEST_WEBROOT).parent_path() / "botonera").string();
    s.adminToken = "token-operador";
    s.pinIntentos = 3;
    s.tunel = true;
    s.tunnelPort = 0;
    SignalingServer srv(s, auth, hub, room, shares, log);
    CHECK(srv.start(), "arrancó");
    const uint16_t gp = srv.guestPort();
    const uint16_t tp = srv.tunnelPort();
    const uint16_t ap = srv.adminPort();
    CHECK(tp != 0 && tp != gp && tp != ap, "la escucha del túnel en su propio puerto");

    const std::string botonera = "GET /botonera/ HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    CHECK(codigo(ida(gp, botonera)) == 200, "la botonera desde 127.0.0.1 por la común: red local");
    CHECK(codigo(ida(gp, proxyV1("192.168.0.20") + botonera)) == 200, "común + PROXY LAN: red local");
    CHECK(codigo(ida(gp, proxyV1("8.8.8.8") + botonera)) == 403, "común + PROXY pública: afuera");
    CHECK(codigo(ida(tp, botonera)) == 403, "por el túnel: afuera aunque sea 127.0.0.1");
    CHECK(codigo(ida(tp, proxyV1("192.168.0.20") + botonera)) == 403, "por el túnel con PROXY LAN: afuera igual");
    CHECK(codigo(ida(tp, "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n")) == 200, "el túnel sirve lo mismo que la común");
    CHECK(codigo(ida(tp, "GET / HTTP/1.1\r\nHost: x\r\n\r\n")) == 200, "la página del invitado por el túnel");
    // La sesión remota acepta la página por el túnel (es para los de afuera).
    {
        const int c = codigo(ida(tp, "GET /sesion/ HTTP/1.1\r\nHost: x\r\n\r\n"));
        CHECK(c == 200 || c == 404, "la sesión remota no se rechaza por el túnel");
    }
    // Límite de PIN por la IP real del PROXY v1: uno se bloquea, el otro no.
    for (int i = 0; i < 3; ++i) ida(tp, proxyV1("200.1.1.1") + postJoin("0000"));
    CHECK(codigo(ida(tp, proxyV1("200.1.1.1") + postJoin("0000"))) == 429, "la IP que erró se bloquea");
    CHECK(codigo(ida(tp, proxyV1("200.2.2.2") + postJoin("9182"))) == 200, "otra IP por el mismo túnel entra");
    // Sin PROXY v1: la IP del X-Forwarded-For (el último), solo en la escucha del túnel.
    const auto conXff = [](const std::string& xff, const std::string& pin) {
        std::string p = postJoin(pin);
        return p.insert(p.find("\r\n") + 2, "X-Forwarded-For: " + xff + "\r\n");
    };
    for (int i = 0; i < 3; ++i) ida(tp, conXff("9.9.9.9, 200.3.3.3", "0000"));
    CHECK(codigo(ida(tp, conXff("200.3.3.3", "0000"))) == 429, "XFF: la IP que erró se bloquea");
    CHECK(codigo(ida(tp, conXff("200.3.3.3, 200.4.4.4", "9182"))) == 200, "XFF: cuenta el último, no el primero");
    // En la común el XFF no vale: 127.0.0.1 sigue sin bloquear.
    CHECK(codigo(ida(gp, conXff("200.1.1.1", "9182"))) == 200, "en la común el XFF se ignora");

    // GET /api/remoto (API de control).
    const std::string bearer = "Authorization: Bearer token-operador\r\n";
    {
        const std::string r = ida(ap, "GET /api/remoto HTTP/1.1\r\nHost: 127.0.0.1\r\n" + bearer + "\r\n");
        const json j = json::parse(cuerpo(r), nullptr, false);
        CHECK(codigo(r) == 200 && j.is_object() && j.contains("pinggy"), "/api/remoto sin proveedor");
        CHECK(j.is_object() && j["pinggy"].value("estado", "") == "apagado" && j["pinggy"].contains("turn"),
              "sin proveedor: apagado con la forma completa");
        CHECK(codigo(ida(ap, "GET /api/remoto HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")) == 401, "pide credenciales");
        CHECK(codigo(ida(gp, "GET /api/remoto HTTP/1.1\r\nHost: x\r\n" + bearer + "\r\n")) != 200,
              "no existe en la superficie de invitados");
    }
    PinggySupervisor::Estado e;
    PinggySupervisor::aplicarLinea(R"({"ev":"listo","urls":["https://abc.a.pinggy.link"]})", e);
    srv.setRemotoStatus([&] { return json{{"pinggy", PinggySupervisor::estadoJsonDe(true, e, "")}}; });
    {
        const std::string r = ida(ap, "GET /api/remoto HTTP/1.1\r\nHost: 127.0.0.1\r\n" + bearer + "\r\n");
        const json j = json::parse(cuerpo(r), nullptr, false);
        CHECK(j.is_object() && j["pinggy"].value("estado", "") == "listo" &&
                  j["pinggy"].value("urlPublica", "") == "https://abc.a.pinggy.link",
              "/api/remoto con proveedor");
    }
    srv.stop();
    std::filesystem::remove_all(sharesDir);
}

#if !defined(_WIN32)
std::filesystem::path script(const std::filesystem::path& dir, const std::string& nombre, const std::string& cuerpoSh) {
    const auto p = dir / nombre;
    std::ofstream out(p);
    out << "#!/bin/sh\n" << cuerpoSh;
    out.close();
    ::chmod(p.c_str(), 0755);
    return p;
}

std::string leer(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void supervisorFalso() {
    using namespace std::chrono_literals;
    const auto dir = std::filesystem::temp_directory_path() / ("ba_pinggy_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    ba::gateway::Log log;

    {
        const std::string d = dir.string();
        const auto exe = script(
            dir, "ba-pinggy-ok.sh",
            "echo \"$@\" > '" + d + "/args.txt'\n"
            "echo \"$BA_PINGGY_TOKEN\" > '" + d + "/env.txt'\n"
            "trap 'exit 0' TERM\n"
            "echo '{\"ev\":\"conectando\"}'\n"
            "echo 'ruido de libpinggy'\n"
            "echo '{\"ev\":\"listo\",\"urls\":[\"https://abc.a.free.pinggy.link\"],\"forwardings\":["
            "{\"tipo\":\"http\",\"local\":\"127.0.0.1:8786\",\"publico\":\"https://abc.a.free.pinggy.link\"},"
            "{\"tipo\":\"tlstcp\",\"local\":\"127.0.0.1:3478\",\"publico\":\"tls://abc.a.free.pinggy.link:443\"}]}'\n"
            "while :; do sleep 1; done\n");
        PinggySupervisor::Cfg c;
        c.exe = exe.string();
        c.token = "tok-de-prueba";
        c.logPath = (dir / "pinggy.log").string();
        c.turn = true;
        // Una corrida anterior en el mismo log no debe contar.
        {
            std::ofstream viejo(c.logPath);
            viejo << R"({"ev":"error","error":"de ayer","recuperable":false})" << "\n";
        }
        PinggySupervisor sup(c, log);
        CHECK(sup.start(), "ba-pinggy falso lanzado");
        std::string estado;
        for (int i = 0; i < 40 && (estado = sup.estado().estado) != "listo"; ++i) std::this_thread::sleep_for(100ms);
        CHECK(estado == "listo", "el supervisor llega a listo leyendo el log");
        CHECK(sup.urlPublica() == "https://abc.a.free.pinggy.link", "urlPublica del supervisor");
        CHECK(sup.turnPublico() == "abc.a.free.pinggy.link:443", "turnPublico() del supervisor");
        CHECK(sup.estadoJson().at("activo") == true, "estadoJson activo");
        CHECK(leer(dir / "env.txt").find("tok-de-prueba") != std::string::npos, "el token llega por el entorno");
        CHECK(leer(dir / "args.txt").find("tok-de-prueba") == std::string::npos, "y no por argv");
        CHECK(leer(dir / "args.txt").find("tcp=127.0.0.1:3478") != std::string::npos, "forwardings en argv");
        sup.stop();
        CHECK(!sup.running() && sup.estado().estado == "apagado" && sup.urlPublica().empty(), "stop lo apaga");
        CHECK(sup.restartCount() == 0, "sin reinicios");
    }
    {
        const auto exe = script(dir, "ba-pinggy-fatal.sh",
                                "echo '{\"ev\":\"error\",\"error\":\"token inválido\",\"recuperable\":false}'\nexit 1\n");
        PinggySupervisor::Cfg c;
        c.exe = exe.string();
        c.logPath = (dir / "fatal.log").string();
        PinggySupervisor sup(c, log);
        sup.start();
        std::this_thread::sleep_for(4s);
        CHECK(sup.restartCount() == 0 && sup.estado().estado == "error" && sup.estado().fatal,
              "error no recuperable: no se relanza");
        sup.stop();
    }
    {
        const auto exe = script(dir, "ba-pinggy-cae.sh", "echo '{\"ev\":\"conectando\"}'\nexit 1\n");
        PinggySupervisor::Cfg c;
        c.exe = exe.string();
        c.logPath = (dir / "cae.log").string();
        PinggySupervisor sup(c, log);
        sup.start();
        std::this_thread::sleep_for(3s);
        CHECK(sup.restartCount() >= 1, "una caída común se relanza");
        sup.stop();
    }
    {
        PinggySupervisor::Cfg c;
        c.exe = "/no/existe";
        c.puertoTunel = 8788;
        c.logPath = (dir / "x.log").string();
        PinggySupervisor sup(c, log);
        CHECK(!sup.start() && sup.estado().estado == "error", "puerto prohibido: no se lanza nada");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
#endif

} // namespace

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#else
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    argumentos();
    eventos();
    formaJson();
    proxyV1Ips();
    escuchaTunel();
#if !defined(_WIN32)
    supervisorFalso();
#else
    std::printf("(el ba-pinggy falso es un script de shell: solo POSIX)\n");
#endif
    if (g_fallas == 0) std::printf("test_pinggy: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
