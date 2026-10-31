// test_control_borde.cpp — el borde de red del control web (ControlWeb.hpp):
// de fábrica solo la red local; por el túnel solo con el acceso habilitado y el
// PIN; emparejar nunca desde afuera; Origin igual a Host; el token se canjea una
// vez; la página /control/ con su redirección; lo de la app llega a las conexiones.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "ControlWeb.hpp"
#include "Log.hpp"

using ba::gateway::BotoneraBorde;
using ba::gateway::ControlWebBorde;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                              \
    do {                                                         \
        if (!(c)) {                                              \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);  \
            ++g_fallas;                                          \
        } else {                                                 \
            std::printf("ok: %s\n", m);                          \
        }                                                        \
    } while (0)

/// Un frame de texto de cliente (con máscara, como manda un navegador).
std::string frameCliente(const std::string& texto, int op = 0x1) {
    std::string f;
    f += static_cast<char>(0x80 | op);
    const unsigned char masc[4] = {0x11, 0x22, 0x33, 0x44};
    if (texto.size() < 126) {
        f += static_cast<char>(0x80 | texto.size());
    } else {
        f += static_cast<char>(0x80 | 126);
        f += static_cast<char>((texto.size() >> 8) & 0xFF);
        f += static_cast<char>(texto.size() & 0xFF);
    }
    f.append(reinterpret_cast<const char*>(masc), 4);
    for (std::size_t i = 0; i < texto.size(); ++i) f += static_cast<char>(texto[i] ^ masc[i % 4]);
    return f;
}

std::uint32_t ip(unsigned a, unsigned b, unsigned c, unsigned d) {
    const unsigned char x[4] = {static_cast<unsigned char>(a), static_cast<unsigned char>(b),
                                static_cast<unsigned char>(c), static_cast<unsigned char>(d)};
    std::uint32_t r;
    std::memcpy(&r, x, 4);
    return r;
}
} // namespace

int main() {
    // --- De dónde viene ---------------------------------------------------------------
    CHECK(!ControlWebBorde::deAfuera(ip(192, 168, 1, 20), "Host: x", false), "red local: adentro");
    CHECK(ControlWebBorde::deAfuera(ip(192, 168, 1, 20), "Host: x", true), "la escucha del túnel: afuera");
    CHECK(ControlWebBorde::deAfuera(ip(8, 8, 8, 8), "Host: x", false), "IP pública: afuera");
    CHECK(ControlWebBorde::deAfuera(ip(127, 0, 0, 1), "Host: x\r\nCF-Ray: abc", false), "Cloudflare: afuera");
    CHECK(ControlWebBorde::mismoOrigen("Host: 192.168.1.5:8787\r\nOrigin: http://192.168.1.5:8787"), "mismo origen");
    CHECK(!ControlWebBorde::mismoOrigen("Host: 192.168.1.5:8787\r\nOrigin: http://malo.example"), "origen ajeno");
    CHECK(ControlWebBorde::mismoOrigen("Host: 192.168.1.5:8787"), "sin Origin");

    ba::gateway::Log log;
    std::vector<json> eventos;
    ControlWebBorde b("", log, [&](const json& e) { eventos.push_back(e); });

    // --- Autenticación ------------------------------------------------------------------
    const std::string token(64, 'a'), sal = "0011", salPin = "99";
    const auto credenciales = [&](bool tunel) {
        b.ponerCredenciales({{"aparatos", {{{"id", "cw-1"}, {"hash", BotoneraBorde::sha256Hex(sal + token)}, {"sal", sal}}}},
                             {"tunel", {{"habilitado", tunel}, {"sal", salPin}, {"hash_pin", BotoneraBorde::sha256Hex(salPin + "4321")}}}});
    };
    credenciales(false);
    CHECK(b.autenticar(token, "", false) == "cw-1", "token bueno en la red local");
    CHECK(b.autenticar(std::string(64, 'b'), "", false).empty(), "otro token: no");
    CHECK(b.autenticar("corto", "", false).empty(), "token mal formado: no");
    CHECK(b.autenticar(token, "4321", true).empty(), "desde afuera con el túnel apagado: no (aunque traiga el PIN)");
    credenciales(true);
    CHECK(b.tunelHabilitado(), "túnel habilitado");
    CHECK(b.autenticar(token, "", true).empty(), "desde afuera sin PIN: no");
    CHECK(b.autenticar(token, "1111", true).empty(), "desde afuera con otro PIN: no");
    CHECK(b.autenticar(token, "4321", true) == "cw-1", "desde afuera con token y PIN: sí");
    credenciales(false);

#ifndef _WIN32
    const auto pedir = [&](const std::string& metodo, const std::string& ruta, const std::string& query,
                           const std::string& cabeceras, const std::string& cuerpo, std::uint32_t desde, bool externo,
                           bool admin) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return std::string();
        if (admin) b.admin(sv[0], metodo, ruta, cuerpo);
        else b.atender(sv[0], metodo, ruta, query, cabeceras, cuerpo, desde, externo);
        ::close(sv[0]);
        std::string r;
        char buf[4096];
        for (ssize_t n; (n = ::read(sv[1], buf, sizeof buf)) > 0;) r.append(buf, static_cast<size_t>(n));
        ::close(sv[1]);
        return r;
    };
    const auto cuerpoDe = [](const std::string& r) {
        const auto p = r.find("\r\n\r\n");
        return p == std::string::npos ? json() : json::parse(r.substr(p + 4), nullptr, false);
    };
    const std::uint32_t lan = ip(192, 168, 1, 30);

    // La página.
    std::string r = pedir("GET", "/control", "", "Host: x", "", lan, false, false);
    CHECK(r.find(" 301 ") != std::string::npos && r.find("Location: /control/") != std::string::npos, "/control -> /control/");
    r = pedir("GET", "/control/", "", "Host: x", "", ip(8, 8, 8, 8), false, false);
    CHECK(r.find(" 403 ") != std::string::npos, "la página desde afuera con el túnel apagado: 403");
    r = pedir("GET", "/control/", "", "Host: x\r\nOrigin: http://malo.example", "", lan, false, false);
    CHECK(r.find(" 403 ") != std::string::npos, "origen ajeno: 403");

    // Emparejar: pedido -> la app resuelve -> el token se entrega UNA vez, a la misma IP.
    const std::string codigo(32, 'c');
    r = pedir("POST", "/api/control/emparejar", "", "Host: x", json{{"codigo", codigo}, {"nombre", "iPad"}}.dump(), lan, false, false);
    const json pedido = cuerpoDe(r);
    const std::string sol = pedido.is_object() ? pedido.value("solicitud", "") : "";
    CHECK(!sol.empty(), "emparejar: devuelve la solicitud");
    CHECK(!eventos.empty() && eventos.back().value("tipo", "") == "solicitud" && eventos.back().value("codigo", "") == codigo &&
              eventos.back().value("nombre", "") == "iPad",
          "emparejar: la app se entera con el código y el nombre");
    r = pedir("GET", "/api/control/emparejar", "solicitud=" + sol, "Host: x", "", lan, false, false);
    CHECK(cuerpoDe(r).value("estado", "") == "pendiente", "pendiente hasta que una persona decide");
    pedir("POST", "/api/v1/control/resolver", "", "",
          json{{"solicitud", sol}, {"estado", "aprobada"}, {"aparato", "cw-1"}, {"token", token}}.dump(), 0, false, true);
    r = pedir("GET", "/api/control/emparejar", "solicitud=" + sol, "Host: x", "", ip(192, 168, 1, 31), false, false);
    CHECK(cuerpoDe(r).value("estado", "") == "desconocida", "otra IP no se lleva el token");
    r = pedir("GET", "/api/control/emparejar", "solicitud=" + sol, "Host: x", "", lan, false, false);
    CHECK(cuerpoDe(r).value("estado", "") == "aprobada" && cuerpoDe(r).value("token", "") == token, "aprobada: el token");
    r = pedir("GET", "/api/control/emparejar", "solicitud=" + sol, "Host: x", "", lan, false, false);
    CHECK(cuerpoDe(r).value("estado", "") == "desconocida", "el token se entrega una sola vez");

    // Emparejar nunca desde afuera, ni con el túnel habilitado.
    credenciales(true);
    const std::size_t antes = eventos.size();
    r = pedir("POST", "/api/control/emparejar", "", "Host: x", json{{"codigo", codigo}}.dump(), lan, true, false);
    CHECK(r.find(" 403 ") != std::string::npos && eventos.size() == antes, "emparejar por el túnel: 403 y la app ni se entera");
    r = pedir("GET", "/control/", "", "Host: x", "", lan, true, false);
    CHECK(r.find(" 403 ") == std::string::npos, "la página por el túnel habilitado: se sirve");
    credenciales(false);

    // Código mal formado.
    r = pedir("POST", "/api/control/emparejar", "", "Host: x", json{{"codigo", "123"}}.dump(), ip(10, 0, 0, 9), false, false);
    CHECK(r.find(" 400 ") != std::string::npos, "código inválido: 400");

    // El WebSocket: auth con el token -> la app se entera de la conexión (con su id);
    // lo que manda el aparato le llega limpio; lo que no entiende, no; la app le
    // contesta por /api/v1/control/enviar; al cerrar, la app se entera.
    {
        eventos.clear();
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
        std::thread servidor([&] {
            b.atender(sv[0], "GET", "/ws", "control=1", "Host: x\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "", lan, false);
        });
        const auto mandarWs = [&](const std::string& t, int op = 0x1) {
            const std::string f = frameCliente(t, op);
            (void)::write(sv[1], f.data(), f.size());
        };
        const auto leer = [&](std::size_t minimo) {
            std::string r;
            char buf[4096];
            for (int i = 0; i < 50 && r.size() < minimo; ++i) {
                const ssize_t n = ::read(sv[1], buf, sizeof buf);
                if (n <= 0) break;
                r.append(buf, static_cast<size_t>(n));
            }
            return r;
        };
        CHECK(leer(20).find(" 101 ") != std::string::npos, "ws: 101 Switching Protocols");
        mandarWs(json{{"type", "auth"}, {"token", token}}.dump());
        for (int i = 0; i < 100 && eventos.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const std::string conexion = eventos.empty() ? "" : eventos[0].value("conexion", "");
        CHECK(!eventos.empty() && eventos[0].value("tipo", "") == "conexion" && eventos[0].value("aparato", "") == "cw-1" &&
                  eventos[0].value("conectado", false) && !conexion.empty() && eventos[0].value("lugar", "") == "sala",
              "ws: token bueno -> la app se entera (aparato, id de conexión, lugar)");
        CHECK(b.conectados() == 1, "ws: una conexión viva");
        mandarWs(json{{"type", "llamar"}, {"id", 7}, {"herramienta", "preview_al_aire"}, {"args", {{"x", 1}}}, {"confirmado", true}, {"basura", 1}}.dump());
        mandarWs(json{{"type", "cualquiera"}}.dump());
        mandarWs(json{{"type", "mirar"}, {"temas", {"escenas", 3, "audio"}}}.dump());
        for (int i = 0; i < 100 && eventos.size() < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(eventos.size() == 3, "ws: lo que no se entiende no pasa");
        if (eventos.size() == 3) {
            const json m1 = eventos[1]["msg"];
            CHECK(eventos[1].value("conexion", "") == conexion && m1.value("type", "") == "llamar" && m1.value("id", 0) == 7 &&
                      m1.value("herramienta", "") == "preview_al_aire" && m1["args"].value("x", 0) == 1 && m1.value("confirmado", false) &&
                      !m1.contains("basura"),
                  "ws: llamar llega limpio, con la conexión");
            CHECK(eventos[2]["msg"]["temas"] == json::array({"escenas", "audio"}), "ws: mirar solo con temas de texto");
        }
        r = pedir("POST", "/api/v1/control/enviar", "", "", json{{"conexiones", {conexion}}, {"msg", {{"type", "resultado"}, {"id", 7}}}}.dump(), 0, false, true);
        CHECK(cuerpoDe(r).value("entregado", 0) == 1, "enviar: entregado a la conexión");
        CHECK(leer(10).find("resultado") != std::string::npos, "ws: el aparato recibe lo que manda la app");
        // La alarma de la interfaz trabada (F1-13): la app no sabe quién está; va a todas.
        r = pedir("POST", "/api/v1/control/enviar", "", "", json{{"todas", true}, {"msg", {{"type", "alarma"}, {"id", "ui_trabada"}}}}.dump(), 0, false, true);
        CHECK(cuerpoDe(r).value("entregado", 0) == 1, "enviar a todas: entregado");
        CHECK(leer(10).find("ui_trabada") != std::string::npos, "ws: el aparato recibe la alarma");
        r = pedir("POST", "/api/v1/control/cortar", "", "", json{{"todas", true}}.dump(), 0, false, true);
        CHECK(cuerpoDe(r).value("entregado", 1) == 0, "cortar a todas: no (solo enviar)");
        mandarWs("", 0x8);
        servidor.join();
        ::close(sv[0]);
        ::close(sv[1]);
        CHECK(!eventos.empty() && eventos.back().value("tipo", "") == "conexion" && !eventos.back().value("conectado", true),
              "ws: al cerrar, la app se entera");
        CHECK(b.conectados() == 0, "ws: ninguna conexión viva");
    }
    {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
        const std::size_t antes = eventos.size();
        std::thread servidor([&] {
            b.atender(sv[0], "GET", "/ws", "control=1", "Host: x\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", "", ip(10, 1, 1, 1), false);
        });
        const std::string f = frameCliente(json{{"type", "auth"}, {"token", std::string(64, 'e')}}.dump());
        (void)::write(sv[1], f.data(), f.size());
        servidor.join();
        std::string todo;
        char buf[4096];
        for (ssize_t n; (n = ::read(sv[1], buf, sizeof buf)) > 0;) {
            todo.append(buf, static_cast<size_t>(n));
            if (todo.find("emparejado") != std::string::npos) break;
        }
        ::close(sv[0]);
        ::close(sv[1]);
        CHECK(todo.find("\"ok\":false") != std::string::npos && eventos.size() == antes, "ws: token malo -> auth no y la app ni se entera");
    }

    // Estado (API de control).
    r = pedir("GET", "/api/v1/control/estado", "", "", "", 0, false, true);
    CHECK(cuerpoDe(r).value("credenciales", 0) == 1, "estado: las credenciales cargadas");
#endif

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
