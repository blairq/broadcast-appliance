// test_registro_atiende.cpp — la atención remota del registro por tótem (RegistroAtiende.cpp):
// el link de atención (hash con sal; cambiarlo corta a quienes estaban), el nombre que pone
// quien atiende, mirar una conversación y atenderla (la ruta de medios: qué canal ve y si le
// habla, y que se rehace cuando la conversación suelta el canal o se va), lo que va a la app,
// la API de control y el WebSocket de punta a punta. Y lo de los medios que se puede probar
// sin WebRTC: las rutas (MediaRoute) y la mezcla de voces (ColaVoz.hpp), con señales sintéticas.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "ColaVoz.hpp"
#include "HttpIo.hpp"
#include "Log.hpp"
#include "MediaRoute.hpp"
#include "Registro.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <chrono>

using ba::gateway::BotoneraBorde;
using ba::gateway::MediaRoute;
using ba::gateway::RegistroBorde;
using nlohmann::json;
namespace colavoz = ba::gateway::colavoz;

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

json totem(const std::string& id, const std::string& sal, const std::string& token) {
    return {{"id", id}, {"nombre", id}, {"sal", sal}, {"hash_token", BotoneraBorde::sha256Hex(sal + token)}};
}
json credencial(const std::string& sal, const std::string& token) {
    return {{"sal", sal}, {"hash_token", BotoneraBorde::sha256Hex(sal + token)}};
}

struct Respuesta {
    int codigo = 0;
    std::string cuerpo;
};
#if defined(_WIN32)
using Sock = SOCKET;
#else
using Sock = int;
#endif
Respuesta pedir(RegistroBorde& r, const std::string& method, const std::string& path, const std::string& body) {
    namespace http = ba::gateway::http;
    Respuesta out;
    const long long lfd = http::makeListen("127.0.0.1", 0);
    if (lfd < 0) return out;
    const uint16_t puerto = http::localPort(lfd);
    const Sock cli = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(cli, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        http::closeFd(static_cast<long long>(cli));
        http::closeFd(lfd);
        return out;
    }
    const Sock srv = ::accept(static_cast<Sock>(lfd), nullptr, nullptr);
    r.admin(static_cast<long long>(srv), method, path, "", body);
    http::shutdownFd(static_cast<long long>(srv));
    std::string todo;
    char buf[4096];
    while (true) {
        const int n = static_cast<int>(::recv(cli, buf, sizeof(buf), 0));
        if (n <= 0) break;
        todo.append(buf, static_cast<size_t>(n));
    }
    http::closeFd(static_cast<long long>(srv));
    http::closeFd(static_cast<long long>(cli));
    http::closeFd(lfd);
    if (todo.size() > 12) out.codigo = std::atoi(todo.substr(9, 3).c_str());
    const auto fin = todo.find("\r\n\r\n");
    if (fin != std::string::npos) out.cuerpo = todo.substr(fin + 4);
    return out;
}
#ifndef _WIN32
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
#endif

/// Un tono sintético (nada por los parlantes): estéreo intercalado.
std::vector<float> tono(double hz, int rate, std::size_t cuadros, float amp) {
    std::vector<float> v(cuadros * 2);
    for (std::size_t i = 0; i < cuadros; ++i) {
        const float s = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / rate));
        v[2 * i] = s;
        v[2 * i + 1] = s;
    }
    return v;
}
} // namespace

int main() {
    ba::gateway::http::socketInit();
    ba::gateway::Log log;
    const std::string sal = "0123456789abcdef";
    const std::string token = std::string(64, 'b');
    const std::string tokenTotem = std::string(64, 'a');

    // --- El nombre que pone quien atiende ------------------------------------------------
    CHECK(RegistroBorde::nombreAtiende("  Laura   Pérez ") == "Laura Pérez", "nombre: sin espacios de más");
    CHECK(RegistroBorde::nombreAtiende("Ana\n\tGómez") == "Ana Gómez", "nombre: sin controles");
    CHECK(RegistroBorde::nombreAtiende("   ").empty(), "nombre: vacío queda vacío");
    {
        const std::string largo = std::string(39, 'x') + "ñandú";
        const std::string n = RegistroBorde::nombreAtiende(largo);
        CHECK(n.size() <= 40 && n == std::string(39, 'x'), "nombre: cortado sin dejar media letra");
    }

    // --- La credencial -------------------------------------------------------------------
    std::vector<json> eventos;
    std::mutex evMu;
    RegistroBorde r("", log, [&](const json& d) {
        std::lock_guard<std::mutex> l(evMu);
        eventos.push_back(d);
    });
    CHECK(!r.autenticarAtiende(token), "sin credencial no entra nadie");
    CHECK(r.ponerCredencialAtiende(credencial(sal, token)).empty(), "poner la credencial (nadie que cortar)");
    CHECK(r.autenticarAtiende(token), "el token bueno entra");
    CHECK(!r.autenticarAtiende(std::string(64, 'c')), "uno errado no");
    CHECK(!r.autenticarAtiende("corto"), "uno corto no");
    CHECK(!r.autenticarAtiende(tokenTotem), "el token de un tótem no sirve para atender");

    // --- Mirar y atender -------------------------------------------------------------------
    r.ponerTotems(json::array({totem("hall", sal, tokenTotem), totem("puerta2", sal, tokenTotem)}));
    const std::string c1 = r.alta("hall", "10.0.0.5", "sala");
    const std::string c2 = r.alta("puerta2", "10.0.0.6", "sala");
    const int k1 = r.tomarCanal(c1);
    CHECK(k1 == 1, "la conversación del hall tiene el canal 1");
    const std::string a1 = r.altaAtiende("Laura", "200.1.2.3", "afuera");
    const std::string a2 = r.altaAtiende("", "200.1.2.4", "afuera");
    CHECK(!a1.empty() && !a2.empty() && a1 != a2, "dos personas atendiendo");
    {
        std::lock_guard<std::mutex> l(evMu);
        const json& e = eventos.back();
        CHECK(e.value("tipo", "") == "atiende-conexion" && e.value("aconn", "") == a2 &&
                  e.value("nombre", "") == "Alguien del equipo" && e.value("conectado", false),
              "la app se entera (y sin nombre queda «Alguien del equipo»)");
    }
    CHECK(r.rutaAtiende(a1) == std::make_pair(0, false), "sin mirar nada: ninguna ruta");
    CHECK(!r.mirar(a1, "c999"), "no se mira una conversación que no existe");
    CHECK(r.mirar(a1, c1) && r.rutaAtiende(a1) == std::make_pair(k1, false), "mira el hall: ve y oye su canal, no habla");
    CHECK(r.mirar(a2, c2) && r.rutaAtiende(a2) == std::make_pair(0, false), "mira una sin canal: nada todavía");
    const int k2 = r.tomarCanal(c2);
    CHECK(k2 == 2 && r.rutaAtiende(a2) == std::make_pair(2, false), "toma canal: quien la miraba la ve desde ahora");

    CHECK(r.ponerAtendida(c1, a1) && r.atendidaPor(c1) == a1, "la app: a1 atiende el hall");
    CHECK(r.rutaAtiende(a1) == std::make_pair(k1, true), "a1 le habla al hall");
    CHECK(r.ponerAtendida(c1, a2) && r.rutaAtiende(a2) == std::make_pair(k1, true) &&
              r.rutaAtiende(a1) == std::make_pair(k1, false),
          "otra la toma: pasa a mirarla y hablarle; la primera ya no le habla (una a la vez)");
    CHECK(!r.ponerAtendida(c1, "a999"), "una página que no existe no atiende");
    CHECK(r.ponerAtendida(c1, "") && r.atendidaPor(c1).empty() && r.rutaAtiende(a2) == std::make_pair(k1, false),
          "vuelve al asistente: nadie le habla");
    r.ponerAtendida(c1, a1);
    r.soltarCanal(c1);
    CHECK(r.atendidaPor(c1).empty() && r.rutaAtiende(a1) == std::make_pair(0, false),
          "la conversación suelta el canal: ni se ve ni se le habla");
    r.tomarCanal(c1);
    r.ponerAtendida(c1, a1);
    r.bajaAtiende(a1);
    CHECK(r.atendidaPor(c1).empty(), "quien atendía se fue: la conversación queda sin su voz");
    r.mirar(a2, c1);
    r.baja(c1);
    CHECK(r.rutaAtiende(a2) == std::make_pair(0, false), "la conversación se fue: quien la miraba no ve nada");

    // --- Lo que va a la app ----------------------------------------------------------------
    {
        const json at = RegistroBorde::accionAtiende("a1", "Laura", {{"type", "atender"}, {"conn", "c7"}});
        CHECK(at.value("tipo", "") == "atiende" && at.value("accion", "") == "atender" && at.value("conn", "") == "c7" &&
                  at.value("nombre", "") == "Laura" && at.value("aconn", "") == "a1",
              "atender va a la app con quién y cuál");
        const json d = RegistroBorde::accionAtiende("a1", "Laura", {{"type", "dato"}, {"conn", "c7"}, {"clave", "correo"},
                                                                   {"valor", "ana@x.com"}});
        CHECK(d.value("clave", "") == "correo" && d.value("valor", "") == "ana@x.com", "un dato con su clave y su valor");
        CHECK(!RegistroBorde::accionAtiende("a1", "L", {{"type", "dato"}, {"conn", "c7"}, {"clave", "co rreo"}, {"valor", "x"}})
                   .is_object(),
              "una clave con espacios no");
        CHECK(RegistroBorde::accionAtiende("a1", "L", {{"type", "dato"}, {"conn", "c7"}, {"clave", "nota"}, {"valor", ""}})
                  .value("valor", "x")
                  .empty(),
              "un valor vacío (borrar el dato) sí");
        CHECK(RegistroBorde::accionAtiende("a1", "L", {{"type", "guardar"}, {"conn", "../x"}}).is_null(), "un conn raro no");
        CHECK(RegistroBorde::accionAtiende("a1", "L", {{"type", "borrar_todo"}, {"conn", "c7"}}).is_null(),
              "lo que no es una acción conocida no");
        const json lv = RegistroBorde::accionAtiende("a1", "L", {{"type", "dato"}, {"conn", "c7"}, {"clave", "x"},
                                                               {"valor", std::string(900, 'z')}});
        CHECK(lv.value("valor", "").size() == 500, "el valor, acotado");
    }

    // --- Cambiar el link corta a quienes estaban ---------------------------------------------
    {
        const auto cortar = r.ponerCredencialAtiende(credencial(sal, std::string(64, 'd')));
        CHECK(cortar.size() == 1 && cortar[0] == a2, "renovar el link: hay que cortar a quien estaba");
        CHECK(!r.autenticarAtiende(token) && r.autenticarAtiende(std::string(64, 'd')), "el viejo ya no vale; el nuevo sí");
        CHECK(r.ponerCredencialAtiende(credencial(sal, std::string(64, 'd'))).empty(), "la misma otra vez: no corta");
        r.ponerCredencialAtiende(json::object());
        CHECK(!r.autenticarAtiende(std::string(64, 'd')), "sin credencial (otro evento sin tótems): nadie");
    }

    // --- La API de control --------------------------------------------------------------------
    {
        std::vector<json> ev;
        RegistroBorde a("", log, [&](const json& d) { ev.push_back(d); });
        Respuesta t = pedir(a, "POST", "/api/v1/registro/atencion", credencial(sal, token).dump());
        CHECK(t.codigo == 200 && a.autenticarAtiende(token), "POST atencion pone la credencial");
        a.ponerTotems(json::array({totem("hall", sal, tokenTotem)}));
        const std::string c = a.alta("hall", "10.0.0.5", "sala");
        a.tomarCanal(c);
        const std::string x = a.altaAtiende("Laura", "1.2.3.4", "afuera");
        t = pedir(a, "POST", "/api/v1/registro/atendida", json{{"conn", c}, {"aconn", x}}.dump());
        CHECK(t.codigo == 200 && a.atendidaPor(c) == x && a.rutaAtiende(x) == std::make_pair(1, true),
              "POST atendida: la atiende y le habla");
        t = pedir(a, "POST", "/api/v1/registro/atendida", json{{"conn", "c999"}, {"aconn", x}}.dump());
        CHECK(t.codigo == 404, "atendida de una conversación que no existe: 404");
        t = pedir(a, "GET", "/api/v1/registro/estado", "");
        const json est = json::parse(t.cuerpo, nullptr, false);
        CHECK(est.contains("atienden") && est["atienden"].size() == 1 && est["atienden"][0].value("aconn", "") == x &&
                  est["atienden"][0].value("mira", "") == c && est["atienden"][0].value("habla", false) &&
                  est["atienden"][0].value("nombre", "") == "Laura",
              "el estado trae a quienes atienden (qué miran y si hablan)");
        t = pedir(a, "POST", "/api/v1/registro/enviar", json{{"atiende", "*"}, {"msg", {{"type", "conversaciones"}}}}.dump());
        CHECK(t.codigo == 200 && json::parse(t.cuerpo, nullptr, false).value("entregado", -1) == 1,
              "enviar a todas las páginas de atención");
        t = pedir(a, "POST", "/api/v1/registro/enviar", json{{"id", "hall"}, {"msg", {{"type", "reiniciar"}}}}.dump());
        CHECK(t.codigo == 200 && json::parse(t.cuerpo, nullptr, false).value("entregado", -1) == 1,
              "enviar a un tótem sigue andando como antes");
        t = pedir(a, "POST", "/api/v1/registro/atencion", "{}");
        CHECK(t.codigo == 200 && json::parse(t.cuerpo, nullptr, false).value("cortadas", 0) == 1 && !a.autenticarAtiende(token),
              "POST atencion vacía: sin atención remota, y se corta a quien estaba");
    }

    // --- Las rutas de los medios ----------------------------------------------------------------
    {
        const MediaRoute mira = MediaRoute::registroAtiende(3, false);
        const MediaRoute habla = MediaRoute::registroAtiende(3, true);
        CHECK(mira.miraRegistro(3) && !mira.hablaARegistro(3), "mirar el canal 3: ve y oye, no habla");
        CHECK(habla.miraRegistro(3) && habla.hablaARegistro(3), "atender el canal 3: además habla");
        CHECK(!habla.miraRegistro(2) && !habla.hablaARegistro(2), "a otro canal, nada");
        CHECK(!MediaRoute::registroAtiende(0, true).miraRegistro(0), "canal 0: nada");
        CHECK(!MediaRoute::registro(3).miraRegistro(3), "el tótem no se mira a sí mismo");
        CHECK(!habla.oyeVozRegistro(3) && !habla.oyeAlAtendedor(), "quien atiende no es el tótem ni el lobby");
        CHECK(!MediaRoute::registroAtiende(ba::gateway::kMaxTotems + 1, true).miraRegistro(ba::gateway::kMaxTotems + 1),
              "fuera de rango, nada");
        CHECK(ba::gateway::kMaxTotems == 8, "ocho conversaciones a la vez");
    }

    // --- La mezcla de voces (señales sintéticas) --------------------------------------------------
    {
        std::deque<float> cola;
        const auto voz = tono(440.0, 48000, 480, 0.5f); // 10 ms
        colavoz::encolar(cola, voz.data(), voz.size(), 48000 * 2 * 3 / 10);
        CHECK(cola.size() == voz.size(), "encolar 10 ms");
        std::vector<float> bloque(960, 0.0f);
        CHECK(colavoz::mezclar(cola, bloque.data(), bloque.size()) == 960 && cola.empty(), "mezclar saca lo que usa");
        bool igual = true;
        for (std::size_t i = 0; i < bloque.size(); ++i) igual = igual && std::fabs(bloque[i] - voz[i]) < 1e-6f;
        CHECK(igual, "sobre silencio, queda la voz tal cual");
        // Dos voces fuertes: se suman y se recortan (nunca pasan de 1).
        colavoz::encolar(cola, voz.data(), voz.size(), 1u << 20);
        std::vector<float> fuerte = tono(440.0, 48000, 480, 0.9f);
        colavoz::mezclar(cola, fuerte.data(), fuerte.size());
        float pico = 0;
        for (float s : fuerte) pico = std::max(pico, std::fabs(s));
        CHECK(pico <= 1.0f && pico > 0.99f, "dos voces fuertes: se recorta en 1");
        // Cola con menos de un bloque: suma lo que hay y el resto queda como estaba.
        std::vector<float> poco(100, 0.25f);
        colavoz::encolar(cola, poco.data(), poco.size(), 1u << 20);
        std::vector<float> b2(960, 0.0f);
        CHECK(colavoz::mezclar(cola, b2.data(), b2.size()) == 100 && b2[99] == 0.25f && b2[100] == 0.0f,
              "cola corta: suma lo que hay");
        // El tope: si el reloj se para no se acumula una frase vieja; queda lo último.
        for (int i = 0; i < 100; ++i) {
            std::vector<float> marca(960, static_cast<float>(i) / 100.0f);
            colavoz::encolar(cola, marca.data(), marca.size(), 28800);
        }
        CHECK(cola.size() == 28800 && std::fabs(cola.back() - 0.99f) < 1e-6f && std::fabs(cola.front() - 0.70f) < 1e-6f,
              "con tope (300 ms): se queda lo último");
        const auto a44 = tono(440.0, 44100, 441, 0.5f);
        const auto r48 = colavoz::remuestrear(a44.data(), a44.size(), 44100, 48000);
        CHECK(r48.size() == 960, "remuestrear 10 ms de 44,1 kHz a 48 kHz: 480 cuadros");
        CHECK(colavoz::remuestrear(voz.data(), voz.size(), 48000, 48000).size() == voz.size(), "a la misma: igual");
    }

#ifndef _WIN32
    // --- El WebSocket de punta a punta ------------------------------------------------------------
    {
        std::vector<json> ev;
        std::mutex mu;
        RegistroBorde w("", log, [&](const json& d) {
            std::lock_guard<std::mutex> l(mu);
            ev.push_back(d);
        });
        std::vector<std::string> ofertas, rutas;
        RegistroBorde::MediosAtiende ma;
        ma.oferta = [&](const std::string& aconn, int canal, bool habla, const std::string&,
                        std::function<void(const json&)> enviar) {
            ofertas.push_back(aconn + "@" + std::to_string(canal) + (habla ? "+habla" : ""));
            enviar({{"type", "respuesta"}, {"sdp", "v=0"}});
            return true;
        };
        ma.ruta = [&](const std::string& aconn, int canal, bool habla) {
            std::lock_guard<std::mutex> l(mu);
            rutas.push_back(aconn + "@" + std::to_string(canal) + (habla ? "+habla" : ""));
        };
        ma.cerrar = [](const std::string&) {};
        w.setMediosAtiende(ma);
        w.ponerCredencialAtiende(credencial(sal, token));
        w.ponerTotems(json::array({totem("hall", sal, tokenTotem)}));
        const std::string c = w.alta("hall", "10.0.0.5", "sala");
        w.tomarCanal(c);
        const unsigned char lanB[4] = {192, 168, 1, 50};
        std::uint32_t lan;
        std::memcpy(&lan, lanB, 4);
        const std::string cab = "Host: x\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==";
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
        std::thread servidor([&] { w.atender(sv[0], "GET", "/ws", "registro_atiende=1", cab, lan, false); });
        const auto mandar = [&](const json& j, int op = 0x1) {
            const std::string f = frameCliente(op == 0x1 ? j.dump() : std::string(), op);
            (void)::write(sv[1], f.data(), f.size());
        };
        std::string leido;
        const auto leerHasta = [&](const std::string& aguja) {
            char buf[4096];
            for (int i = 0; i < 50 && leido.find(aguja) == std::string::npos; ++i) {
                const ssize_t n = ::read(sv[1], buf, sizeof buf);
                if (n <= 0) break;
                leido.append(buf, static_cast<size_t>(n));
            }
            return leido.find(aguja) != std::string::npos;
        };
        const auto esperar = [&](const std::function<bool()>& f) {
            for (int i = 0; i < 200 && !f(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return f();
        };
        const auto cuantos = [&](const std::string& tipo) {
            std::lock_guard<std::mutex> l(mu);
            int n = 0;
            for (const auto& e : ev) n += e.value("tipo", std::string()) == tipo ? 1 : 0;
            return n;
        };
        CHECK(leerHasta(" 101 "), "ws: 101");
        mandar({{"type", "auth"}, {"t", token}, {"nombre", "Laura"}});
        CHECK(leerHasta("\"type\":\"hola\"") && leido.find("\"nombre\":\"Laura\"") != std::string::npos,
              "ws: entra con su nombre");
        const auto atiende = w.atiendenTodas();
        CHECK(atiende.size() == 1, "ws: queda anotada");
        const std::string x = atiende.empty() ? std::string() : atiende[0];
        mandar({{"type", "mirar"}, {"conn", c}});
        CHECK(leerHasta("\"type\":\"mirando\"") && leido.find("\"canal\":1") != std::string::npos, "ws: mira el canal 1");
        mandar({{"type", "oferta"}, {"sdp", "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\nm=application 9 x\r\n"}});
        CHECK(leerHasta("medios-error") && ofertas.empty(), "ws: una oferta con datos: no");
        mandar({{"type", "oferta"}, {"sdp", "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n"}});
        CHECK(leerHasta("\"type\":\"respuesta\"") && ofertas.size() == 1 && ofertas[0] == x + "@1",
              "ws: su WebRTC, por el canal que mira y sin hablar");
        mandar({{"type", "atender"}, {"conn", c}});
        CHECK(esperar([&] { return cuantos("atiende") == 1; }), "ws: «atender» va a la app");
        w.ponerAtendida(c, x); // la app dice que sí
        CHECK(esperar([&] {
                  std::lock_guard<std::mutex> l(mu);
                  return !rutas.empty() && rutas.back() == x + "@1+habla";
              }),
              "ws: la app la deja atender: su ruta pasa a hablarle al tótem");
        mandar({{"type", "dato"}, {"conn", c}, {"clave", "correo"}, {"valor", "ana@x.com"}});
        CHECK(esperar([&] { return cuantos("atiende") == 2; }), "ws: un dato va a la app");
        mandar({{"type", "ping"}});
        CHECK(leerHasta("pong"), "ws: ping -> pong");
        mandar({}, 0x8);
        servidor.join();
        ::close(sv[0]);
        ::close(sv[1]);
        CHECK(w.atiendenTodas().empty() && w.atendidaPor(c).empty(), "ws: al irse, deja de atender");
        json fin;
        {
            std::lock_guard<std::mutex> l(mu);
            fin = ev.back();
        }
        CHECK(fin.value("tipo", "") == "atiende-conexion" && !fin.value("conectado", true), "ws: la app se entera de que se fue");
        // Un token malo: error, y la app ve el intento.
        int sv2[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv2) == 0, "socketpair");
        std::thread s2([&] { w.atender(sv2[0], "GET", "/ws", "registro_atiende=1", cab, lan, false); });
        const std::string f = frameCliente(json{{"type", "auth"}, {"t", std::string(64, 'e')}, {"nombre", "X"}}.dump());
        (void)::write(sv2[1], f.data(), f.size());
        s2.join();
        ::shutdown(sv2[0], SHUT_RDWR);
        std::string todo;
        char buf[4096];
        for (ssize_t n; (n = ::read(sv2[1], buf, sizeof buf)) > 0;) todo.append(buf, static_cast<size_t>(n));
        ::close(sv2[0]);
        ::close(sv2[1]);
        CHECK(todo.find("\"type\":\"error\"") != std::string::npos && cuantos("intento") == 1,
              "ws: token malo -> error y el intento a la app");
    }
#endif

    std::printf("%s (%d fallas)\n", g_fallas == 0 ? "TODO OK" : "HAY FALLAS", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
