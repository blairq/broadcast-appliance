// ============================================================================
// test_canal.cpp — el canal web /ws del lado de la sala (Canal.hpp,
// docs/arquitectura/06-canal-web.md), sin red: un socketpair hace de navegador.
//
//   1. El primer mensaje: {"v":1,"type":"auth"} entra; sin `v` también (las páginas de
//      antes); con otra versión, error_codigo «version»; si no es un auth, «mensaje»; un
//      frame binario (una página de antes del contrato) recibe «recargar».
//   2. El lazo: {"type":"ping"} y el ping del protocolo se contestan solos y no llegan a
//      quien lee; un binario se saltea; los demás llegan en orden; más de N por segundo
//      cortan (Fin::Exceso); callado más que el latido, Fin::Mudo.
//   3. aTodos llega a toda conexión anotada y no a una soltada.
// ============================================================================
#include <cstdio>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "Canal.hpp"
#include "HttpIo.hpp"

#if !defined(_WIN32)
#include <sys/socket.h>
#include <unistd.h>
#endif

using nlohmann::json;
namespace canal = ba::gateway::canal;

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

#if !defined(_WIN32)
/// Un frame de cliente (enmascarado, como manda el RFC 6455).
std::string frame(const std::string& texto, int op = 0x1) {
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
void mandar(int fd, const std::string& f) { (void)::write(fd, f.data(), f.size()); }

/// Lo que la sala le escribió al «navegador» (frames sin máscara), hasta que no llega más.
std::string leerTodo(int fd, int ms = 150) {
    std::string out;
    char buf[4096];
    while (ba::gateway::http::waitReadable(fd, ms) > 0) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

/// El primer mensaje `f` contra primerMensaje; devuelve lo que entregó y lo que contestó.
json primero(const std::string& f, std::string& respuesta) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return nullptr;
    mandar(sv[1], f);
    const json m = canal::primerMensaje(sv[0], 500);
    respuesta = leerTodo(sv[1]);
    ::close(sv[0]);
    ::close(sv[1]);
    return m;
}
#endif
} // namespace

int main() {
#if defined(_WIN32)
    std::printf("SKIP: socketpair\n");
    return 77;
#else
    ba::gateway::http::socketInit();
    // --- 1) El primer mensaje -------------------------------------------------------
    {
        std::string r;
        json m = primero(frame(R"({"v":1,"type":"auth","token":"x"})"), r);
        CHECK(m.is_object() && m.value("token", "") == "x" && r.empty(), "v1: entra, sin contestar nada (contesta el rol)");
        m = primero(frame(R"({"type":"auth","token":"y"})"), r);
        CHECK(m.is_object() && m.value("token", "") == "y", "sin v vale como 1");
        m = primero(frame(R"({"v":2,"type":"auth"})"), r);
        CHECK(m.is_null() && r.find("\"error_codigo\":\"version\"") != std::string::npos && r.find("\"ok\":false") != std::string::npos,
              "otra versión: auth ok:false con «version»");
        m = primero(frame(R"({"v":1,"type":"hola"})"), r);
        CHECK(m.is_null() && r.find("\"error_codigo\":\"mensaje\"") != std::string::npos, "no es un auth: «mensaje»");
        m = primero(frame(std::string("\x05\x00\x00\x00{\"a\"}", 9), 0x2), r);
        CHECK(m.is_null() && r.find("\"type\":\"recargar\"") != std::string::npos, "binario (una página vieja): «recargar»");
        m = primero(frame("x", 0x9) + frame(R"({"v":1,"type":"auth"})"), r);
        CHECK(m.is_object() && r.size() >= 2 && (static_cast<unsigned char>(r[0]) & 0x0F) == 0xA,
              "un ping del protocolo antes del auth: pong y sigue esperando");
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        CHECK(canal::primerMensaje(sv[0], 100).is_null(), "si no llega a tiempo, nada");
        ::close(sv[0]);
        ::close(sv[1]);
    }
    // --- 2) El lazo -------------------------------------------------------------------
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        canal::Salida s;
        s.fd = sv[0];
        std::vector<std::string> llegaron;
        canal::Fin fin = canal::Fin::Cerrado;
        std::thread t([&] {
            fin = canal::leer(s, {2000, 1024, 0}, [&](const json& m) {
                llegaron.push_back(m.value("type", ""));
                return m.value("type", "") != "chau";
            });
        });
        mandar(sv[1], frame(R"({"type":"ping"})") + frame("p", 0x9) + frame("bin", 0x2) + frame(R"({"type":"uno"})") +
                          frame(std::string(2000, 'x')) + frame(R"({"type":"dos"})") + frame(R"({"type":"chau"})"));
        t.join();
        const std::string r = leerTodo(sv[1]);
        CHECK(llegaron.size() == 3 && llegaron[0] == "uno" && llegaron[1] == "dos" && llegaron[2] == "chau",
              "llegan los mensajes en orden; ping, binario y uno muy grande no");
        CHECK(r.find("{\"type\":\"pong\"}") != std::string::npos, "{type:ping} -> {type:pong}");
        CHECK(fin == canal::Fin::Cerrado, "quien lee devuelve false: se cierra");
        ::close(sv[0]);
        ::close(sv[1]);
    }
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        canal::Salida s;
        s.fd = sv[0];
        std::string todo;
        for (int i = 0; i < 10; ++i) todo += frame(R"({"type":"x"})");
        mandar(sv[1], todo);
        CHECK(canal::leer(s, {2000, 1024, 5}, [](const json&) { return true; }) == canal::Fin::Exceso,
              "más de 5 por segundo: Fin::Exceso");
        CHECK(canal::leer(s, {100, 1024, 0}, [](const json&) { return true; }) == canal::Fin::Mudo,
              "callado más que el latido: Fin::Mudo");
        ::close(sv[0]);
        ::close(sv[1]);
    }
    // --- 3) A todas --------------------------------------------------------------------
    {
        int a[2], b[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, a);
        socketpair(AF_UNIX, SOCK_STREAM, 0, b);
        auto sa = std::make_shared<canal::Salida>();
        auto sb = std::make_shared<canal::Salida>();
        sa->fd = a[0];
        sb->fd = b[0];
        canal::anotar(sa);
        canal::anotar(sb);
        sb->soltar();
        canal::aTodos(json{{"type", "tema"}});
        CHECK(leerTodo(a[1]).find("{\"type\":\"tema\"}") != std::string::npos, "aTodos llega a una anotada");
        CHECK(leerTodo(b[1]).empty(), "y no a una soltada");
        sa.reset(); // liberada: se borra sola
        canal::aTodos(json{{"type", "tema"}});
        CHECK(leerTodo(a[1]).empty(), "ni a una liberada");
        for (int fd : {a[0], a[1], b[0], b[1]}) ::close(fd);
    }
    std::printf(g_fallas ? "\n%d FALLAS\n" : "\nTODO BIEN\n", g_fallas);
    return g_fallas ? 1 : 0;
#endif
}
