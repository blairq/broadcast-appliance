// test_registro_borde.cpp — el borde del registro de personas por tótem (Registro.hpp):
// el token con hash y sal (y un tótem que cambia de token o se da de baja corta sus
// conexiones), los canales de audio (de a kMaxTotems, se reusan al soltarse, la
// conexión que se va los suelta), la oferta de solo audio, el tope de fotos, los
// «toques» que van a la app y las rutas de MediaRoute (la voz de cada canal le llega
// solo a su conexión). Y las rutas de la API de control por un socket de verdad.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "HttpIo.hpp"
#include "Log.hpp"
#include "MediaRoute.hpp"
#include "Registro.hpp"
#include "Sesion.hpp"

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
#include <cstring>

using ba::gateway::BotoneraBorde;
using ba::gateway::MediaRoute;
using ba::gateway::RegistroBorde;
using nlohmann::json;

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

json totem(const std::string& id, const std::string& sal, const std::string& token, const std::string& nombre = "") {
    return {{"id", id}, {"nombre", nombre}, {"sal", sal}, {"hash_token", BotoneraBorde::sha256Hex(sal + token)}};
}

/// Una petición a la API de control por un socket de verdad: `admin` escribe la
/// respuesta en un extremo y acá se lee del otro.
struct Respuesta {
    int codigo = 0;
    std::string cuerpo;
};
#if defined(_WIN32)
using Sock = SOCKET;
#else
using Sock = int;
#endif
Respuesta pedir(RegistroBorde& r, const std::string& method, const std::string& path, const std::string& query,
                const std::string& body, bool* tomada = nullptr) {
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
    const bool t = r.admin(static_cast<long long>(srv), method, path, query, body);
    if (tomada) *tomada = t;
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
/// Un cuadro de texto como lo manda un navegador (enmascarado).
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
} // namespace

int main() {
    ba::gateway::http::socketInit();
    ba::gateway::Log log;
    std::vector<json> eventos;
    RegistroBorde r("", log, [&](const json& d) { eventos.push_back(d); });
    const auto ultimo = [&](const std::string& tipo) {
        for (auto it = eventos.rbegin(); it != eventos.rend(); ++it)
            if (it->value("tipo", std::string()) == tipo) return *it;
        return json();
    };
    int cerradas = 0;
    std::vector<std::string> ofertas;
    RegistroBorde::Medios m;
    m.oferta = [&](const std::string& conn, int canal, const std::string&, std::function<void(const json&)>) {
        ofertas.push_back(conn + "@" + std::to_string(canal));
        return true;
    };
    m.cerrar = [&](const std::string&) { ++cerradas; };
    r.setMedios(m);

    // --- Los ids y la autenticación ---------------------------------------------------
    CHECK(RegistroBorde::idValido("entrada-1") && RegistroBorde::idValido("a"), "ids válidos");
    CHECK(!RegistroBorde::idValido("") && !RegistroBorde::idValido("Entrada") && !RegistroBorde::idValido("a_b") &&
              !RegistroBorde::idValido(std::string(33, 'a')) && !RegistroBorde::idValido("../x"),
          "ids inválidos: vacío, mayúsculas, guion bajo, largo, rutas");
    const std::string sal = "00112233445566778899aabbccddeeff";
    const std::string token(64, 'a');
    const std::string token2(64, 'b');
    r.ponerTotems(json::array({totem("entrada", sal, token, "Entrada principal"), totem("sala-b", sal, token2),
                               {{"id", "Mal"}, {"sal", sal}, {"hash_token", std::string(64, '0')}},
                               {{"id", "corto"}, {"sal", sal}, {"hash_token", "abc"}}}));
    std::string nombre;
    CHECK(r.autenticar("entrada", token, nombre) && nombre == "Entrada principal", "id y token correctos: el nombre");
    CHECK(!r.autenticar("entrada", token2, nombre), "el token de otro tótem: no");
    CHECK(!r.autenticar("sala-b", token.substr(0, 20), nombre), "un token corto: no");
    CHECK(!r.autenticar("nadie", token, nombre), "un tótem que no existe: no");
    CHECK(!r.autenticar("corto", "x", nombre) && !r.autenticar("Mal", token, nombre),
          "los tótems mal armados no se cargan");

    // --- Conexiones y canales ---------------------------------------------------------
    std::vector<std::string> cs;
    for (int i = 0; i < 6; ++i) cs.push_back(r.alta(i < 5 ? "entrada" : "sala-b", "192.168.1.10", "sala"));
    CHECK(cs[0] != cs[1] && !cs[0].empty() && cs[0][0] == 'c', "cada conexión tiene su conn");
    CHECK(ultimo("conexion").value("conectado", false) && ultimo("conexion").value("id", "") == "sala-b",
          "la app se entera de cada conexión");
    for (int i = 0; i < ba::gateway::kMaxTotems; ++i)
        CHECK(r.tomarCanal(cs[static_cast<size_t>(i)]) == i + 1, "canal libre, en orden");
    CHECK(ultimo("canal").value("canal", 0) == ba::gateway::kMaxTotems, "la app se entera del canal");
    CHECK(r.tomarCanal(cs[1]) == 2, "pedirlo de nuevo devuelve el mismo (sin otro aviso)");
    CHECK(r.tomarCanal(cs[4]) == 0, "con los cuatro tomados: lleno");
    CHECK(r.tomarCanal("c999") == 0, "una conexión que no existe: nada");
    r.soltarCanal(cs[1]);
    CHECK(r.canalDe(cs[1]) == 0 && ultimo("canal").value("canal", -1) == 0 && ultimo("canal").value("conn", "") == cs[1],
          "soltarlo avisa canal 0");
    CHECK(cerradas == 1, "soltar el canal cierra su WebRTC");
    CHECK(r.tomarCanal(cs[4]) == 2, "el canal suelto lo toma el que esperaba");

    // El audio: solo con canal, y solo si cambia.
    const std::size_t antesAudio = eventos.size();
    r.audioListo(cs[4], true);
    r.audioListo(cs[4], true);
    CHECK(eventos.size() == antesAudio + 1 && ultimo("audio").value("listo", false), "audio listo: un aviso");
    r.audioListo(cs[1], true);
    CHECK(eventos.size() == antesAudio + 1, "sin canal, el audio no cuenta");
    r.soltarCanal(cs[4]);
    CHECK(!ultimo("audio").value("listo", true) && ultimo("audio").value("conn", "") == cs[4],
          "soltar el canal con audio avisa que se cayó");
    const std::size_t antesTardio = eventos.size();
    r.audioListo(cs[4], false);
    CHECK(eventos.size() == antesTardio, "el aviso tardío de la sesión cerrada no se repite");

    // La conexión que se va suelta su canal y su foto.
    CHECK(r.tomarCanal(cs[5]) == 2, "otro tótem comparte los mismos canales");
    const std::string jpeg = std::string("\xFF\xD8\xFF\xE0", 4) + std::string(100, 'x');
    CHECK(r.ponerFoto(cs[5], jpeg, 1000) && r.foto(cs[5]) == jpeg, "la foto queda");
    CHECK(ultimo("foto").value("conn", "") == cs[5], "y la app se entera");
    r.baja(cs[5]);
    CHECK(r.canalDe(cs[5]) == 0 && r.foto(cs[5]).empty(), "al irse, sin canal y sin foto");
    CHECK(!ultimo("conexion").value("conectado", true) && ultimo("canal").value("canal", -1) == 0,
          "la app se entera: canal 0 y desconectado");
    CHECK(r.tomarCanal(cs[4]) == 2, "su canal queda libre");

    // --- Fotos: JPEG, tamaño y frecuencia ---------------------------------------------
    CHECK(!r.ponerFoto(cs[0], "no es jpeg", 5000), "algo que no es JPEG: no");
    CHECK(!r.ponerFoto(cs[0], std::string("\xFF\xD8", 2) + std::string(97 * 1024, 'x'), 5000), "más de 96 KB: no");
    int guardadas = 0;
    const std::size_t antesFotos = eventos.size();
    for (int i = 0; i < 6; ++i) guardadas += r.ponerFoto(cs[0], jpeg, 10000 + i * 10) ? 1 : 0;
    CHECK(guardadas == 3, "a lo sumo 3 por segundo");
    CHECK(eventos.size() == antesFotos + 1, "a la app, un aviso cada medio segundo");
    CHECK(r.ponerFoto(cs[0], jpeg, 11100), "pasado el segundo, otra vez");

    // --- Lo que toca la persona va a la app ------------------------------------------
    CHECK(RegistroBorde::toque("c1", "entrada", {{"type", "empezar"}}).value("accion", "") == "empezar", "empezar");
    CHECK(RegistroBorde::toque("c1", "entrada", {{"type", "cancelar"}}).value("accion", "") == "cancelar", "cancelar");
    const json esc = RegistroBorde::toque("c1", "entrada", {{"type", "escrito"}, {"clave", "correo"}, {"valor", "ana@x.org"}});
    CHECK(esc.value("tipo", "") == "toque" && esc.value("clave", "") == "correo" && esc.value("valor", "") == "ana@x.org" &&
              esc.value("conn", "") == "c1" && esc.value("id", "") == "entrada",
          "escrito: clave y valor");
    CHECK(RegistroBorde::toque("c1", "e", {{"type", "escrito"}, {"clave", "x y"}, {"valor", "1"}}).is_null(),
          "una clave rara: no");
    CHECK(RegistroBorde::toque("c1", "e", {{"type", "escrito"}, {"clave", "edad"}, {"valor", 42}}).value("valor", "") == "42",
          "un número llega como texto");
    CHECK(RegistroBorde::toque("c1", "e", {{"type", "escrito"}, {"clave", "x"}, {"valor", std::string(900, 'a')}})
                  .value("valor", "")
                  .size() == 500,
          "un valor largo se recorta");
    CHECK(RegistroBorde::toque("c1", "e", {{"type", "habilitar"}}).is_null(), "lo demás no pasa");

    // --- Un tótem que cambia de token o se da de baja corta lo suyo --------------------
    auto cortar = r.ponerTotems(json::array({totem("entrada", sal, token, "Entrada"), totem("sala-b", sal, token2)}));
    CHECK(cortar.empty(), "reenviar la misma lista no corta a nadie");
    cortar = r.ponerTotems(json::array({totem("entrada", sal, std::string(64, 'c'))}));
    CHECK(cortar.size() == 5, "el token regenerado corta las conexiones de ese tótem");
    CHECK(!r.autenticar("entrada", token, nombre), "el token viejo ya no anda");
    CHECK(!r.autenticar("sala-b", token2, nombre), "el que se dio de baja tampoco");
    CHECK(r.conexionesDe("entrada").size() == 5, "hasta que su lazo sale, siguen anotadas");

    // --- La oferta de solo audio (la misma regla que la atención hablada) --------------
    const auto sdpDe = [](const std::string& secciones) { return "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n" + secciones; };
    CHECK(ba::gateway::SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n")),
          "solo el micrófono: pasa");
    CHECK(!ba::gateway::SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"
                                                         "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\n")),
          "con la cámara: no");

    // --- Rutas de medios ---------------------------------------------------------------
    CHECK(MediaRoute::registro(2).oyeVozRegistro(2), "la conexión del canal 2 oye la voz del canal 2");
    CHECK(!MediaRoute::registro(2).oyeVozRegistro(1), "y no la de otro canal");
    CHECK(!MediaRoute::atencion().oyeVozRegistro(1) && !MediaRoute::program(1).oyeVozRegistro(1) &&
              !MediaRoute::lobby(1, false, true).oyeVozRegistro(1),
          "nadie más oye las voces del registro");
    CHECK(!MediaRoute::registro(1).oyeAlAtendedor(), "ni el registro oye al que atiende la sesión remota");
    CHECK(!MediaRoute::registro(ba::gateway::kMaxTotems + 1).oyeVozRegistro(ba::gateway::kMaxTotems + 1),
          "un canal fuera de rango no oye nada");
    CHECK(MediaRoute::registro(1) != MediaRoute::registro(2) && MediaRoute::registro(1) != MediaRoute::atencion(),
          "cada canal es su ruta");

    // --- La API de control (por un socket de verdad) -----------------------------------
    {
        RegistroBorde a("", log, [&](const json& d) { eventos.push_back(d); });
        bool tomada = false;
        pedir(a, "GET", "/api/v1/sesion/estado", "", "", &tomada);
        CHECK(!tomada, "lo que no es /api/v1/registro/ no lo toma");
        Respuesta t = pedir(a, "POST", "/api/v1/registro/totems", "",
                            json{{"totems", json::array({totem("hall", sal, token, "Hall")})}}.dump());
        CHECK(t.codigo == 200 && json::parse(t.cuerpo, nullptr, false).value("totems", 0) == 1, "POST totems");
        const std::string c1 = a.alta("hall", "10.0.0.7", "sala");
        a.tomarCanal(c1);
        a.ponerFoto(c1, jpeg, 1);
        t = pedir(a, "GET", "/api/v1/registro/estado", "", "");
        const json est = json::parse(t.cuerpo, nullptr, false);
        CHECK(t.codigo == 200 && est["conexiones"].size() == 1, "GET estado: la conexión");
        if (est["conexiones"].size() == 1) {
            const json& e = est["conexiones"][0];
            CHECK(e.value("conn", "") == c1 && e.value("id", "") == "hall" && e.value("canal", 0) == 1 &&
                      e.value("ip", "") == "10.0.0.7" && e.value("lugar", "") == "sala" && e.value("desdeMs", 0LL) > 0 &&
                      e.contains("audio"),
                  "con conn, id, canal, ip, lugar, desdeMs y audio");
        }
        t = pedir(a, "GET", "/api/v1/registro/foto", "conn=" + c1, "");
        CHECK(t.codigo == 200 && t.cuerpo == jpeg, "GET foto");
        t = pedir(a, "GET", "/api/v1/registro/foto", "conn=c999", "");
        CHECK(t.codigo == 404, "sin foto: 404");
        t = pedir(a, "POST", "/api/v1/registro/enviar", "", json{{"id", "hall"}, {"msg", {{"type", "reiniciar"}}}}.dump());
        CHECK(t.codigo == 200 && json::parse(t.cuerpo, nullptr, false).value("entregado", -1) == 1,
              "enviar a todas las del tótem");
        t = pedir(a, "POST", "/api/v1/registro/enviar", "", json{{"conn", c1}, {"msg", "no es objeto"}}.dump());
        CHECK(t.codigo == 400, "enviar sin un mensaje objeto: 400");
        t = pedir(a, "POST", "/api/v1/registro/cortar", "", json{{"conn", "c999"}}.dump());
        CHECK(t.codigo == 404, "cortar una que no existe: 404");
        t = pedir(a, "POST", "/api/v1/registro/cortar", "", json{{"conn", c1}}.dump());
        CHECK(t.codigo == 200, "cortar una que existe");
        t = pedir(a, "POST", "/api/v1/registro/nada", "", "{}");
        CHECK(t.codigo == 404, "una ruta que no existe: 404");
        t = pedir(a, "GET", "/api/v1/registro/totems", "", "");
        CHECK(t.codigo == 400, "totems por GET: 400");
    }


#ifndef _WIN32
    // --- El WebSocket de la página, de punta a punta ----------------------------------
    {
        std::vector<json> ev;
        std::mutex evMu;
        RegistroBorde w("", log, [&](const json& d) {
            std::lock_guard<std::mutex> l(evMu);
            ev.push_back(d);
        });
        std::vector<std::string> ofertasW;
        RegistroBorde::Medios mw;
        mw.oferta = [&](const std::string& conn, int canal, const std::string&, std::function<void(const json&)> enviar) {
            ofertasW.push_back(conn + "@" + std::to_string(canal));
            enviar({{"type", "respuesta"}, {"sdp", "v=0"}});
            return true;
        };
        mw.cerrar = [](const std::string&) {};
        w.setMedios(mw);
        w.ponerTotems(json::array({totem("hall", sal, token, "Hall")}));
        const unsigned char lanB[4] = {192, 168, 1, 50};
        std::uint32_t lan;
        std::memcpy(&lan, lanB, 4);
        const auto cuantos = [&](const std::string& tipo) {
            std::lock_guard<std::mutex> l(evMu);
            int n = 0;
            for (const auto& e : ev) n += e.value("tipo", std::string()) == tipo ? 1 : 0;
            return n;
        };
        const auto esperar = [&](const std::function<bool()>& f) {
            for (int i = 0; i < 200 && !f(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return f();
        };
        const std::string cab = "Host: x\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==";
        // Un token malo: error y se cierra; la app se entera del intento, no de una conexión.
        {
            int sv[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
            std::thread servidor([&] { w.atender(sv[0], "GET", "/ws", "registro=1", cab, lan, false); });
            const std::string f = frameCliente(json{{"type", "auth"}, {"r", "hall"}, {"t", std::string(64, 'e')}}.dump());
            (void)::write(sv[1], f.data(), f.size());
            servidor.join();
            ::shutdown(sv[0], SHUT_RDWR);
            std::string todo;
            char buf[4096];
            for (ssize_t n; (n = ::read(sv[1], buf, sizeof buf)) > 0;) todo.append(buf, static_cast<size_t>(n));
            ::close(sv[0]);
            ::close(sv[1]);
            CHECK(todo.find("\"type\":\"error\"") != std::string::npos && cuantos("conexion") == 0 && cuantos("intento") == 1,
                  "ws: token malo -> error, y la app solo ve el intento");
        }
        {
            int sv[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
            std::thread servidor([&] { w.atender(sv[0], "GET", "/ws", "registro=1", cab, lan, false); });
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
            CHECK(leerHasta(" 101 "), "ws: 101 Switching Protocols");
            mandar({{"type", "auth"}, {"r", "hall"}, {"t", token}});
            CHECK(leerHasta("\"type\":\"hola\"") && leido.find("\"nombre\":\"Hall\"") != std::string::npos &&
                      leido.find("\"conn\":\"c") != std::string::npos,
                  "ws: token bueno -> hola con el nombre y el conn");
            CHECK(esperar([&] { return cuantos("conexion") == 1; }), "ws: la app se entera de la conexión");
            json con;
            {
                std::lock_guard<std::mutex> l(evMu);
                for (const auto& e : ev)
                    if (e.value("tipo", "") == "conexion") con = e;
            }
            CHECK(con.value("id", "") == "hall" && con.value("conectado", false) && con.value("lugar", "") == "sala",
                  "ws: con el tótem y el lugar");
            const std::string conn = con.value("conn", "");
            mandar({{"type", "oferta"}, {"sdp", "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"}});
            CHECK(leerHasta("audio-error") && ofertasW.empty(), "ws: ofrecer sin canal: no");
            mandar({{"type", "canal"}});
            CHECK(leerHasta("\"canal\":1"), "ws: pide canal -> el 1");
            mandar({{"type", "oferta"}, {"sdp", "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"
                                                "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\n"}});
            CHECK(leerHasta("algo más que el micrófono") && ofertasW.empty(), "ws: una oferta con cámara: no");
            mandar({{"type", "oferta"}, {"sdp", "v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"}});
            CHECK(leerHasta("\"type\":\"respuesta\"") && ofertasW.size() == 1 && ofertasW[0] == conn + "@1",
                  "ws: solo audio -> la oferta va por su canal y vuelve la respuesta");
            mandar({{"type", "escrito"}, {"clave", "nombre"}, {"valor", "Ana"}});
            mandar({{"type", "ping"}});
            CHECK(leerHasta("pong"), "ws: ping -> pong");
            CHECK(esperar([&] { return cuantos("toque") == 1; }), "ws: lo escrito va a la app como toque");
            mandar({}, 0x8);
            servidor.join();
            ::close(sv[0]);
            ::close(sv[1]);
            json fin, canal;
            {
                std::lock_guard<std::mutex> l(evMu);
                fin = ev.back();
                for (const auto& e : ev)
                    if (e.value("tipo", "") == "canal") canal = e;
            }
            CHECK(fin.value("tipo", "") == "conexion" && !fin.value("conectado", true) && canal.value("canal", -1) == 0,
                  "ws: al cerrar, suelta el canal y la app se entera");
            CHECK(w.estado()["conexiones"].empty(), "ws: ninguna conexión viva");
        }
    }
#endif

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
