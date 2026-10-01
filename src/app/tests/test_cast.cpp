// test_cast.cpp — el protocolo de Google Cast, la parte que se puede probar
// sin un televisor: armar y leer mensajes.
//
//   1. Armar un CastMessage: largo de 4 bytes big endian y los campos del
//      protobuf donde van.
//   2. Ida y vuelta: lo que se arma se lee igual, con acentos y con JSON.
//   3. Trocear: dos mensajes pegados salen de a uno; uno cortado por la mitad
//      espera a que llegue el resto.
//   4. Basura: un largo disparatado no hace pedir memoria a lo loco, y un
//      cuerpo que no es protobuf no revienta.
#include <cstdio>
#include <string>

#include "pantallas/Cast.hpp"

using ba::app::pantallas::armar;
using ba::app::pantallas::Mensaje;
using ba::app::pantallas::sacar;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

std::uint32_t largoDe(const std::string& m) {
    const auto* b = reinterpret_cast<const unsigned char*>(m.data());
    return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
}
} // namespace

int main() {
    std::printf("== test_cast ==\n");

    // --- 1) armado -----------------------------------------------------------
    const Mensaje m{"sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.connection",
                    R"({"type":"CONNECT"})"};
    const std::string bytes = armar(m);
    CHECK(bytes.size() > 4, "tiene cuerpo");
    CHECK(largoDe(bytes) == bytes.size() - 4, "el largo de adelante es el del cuerpo");
    // Los campos, tal como los espera el aparato: 2 origen, 3 destino, 4 tema,
    // 6 texto (y los enums 1 y 5 en cero).
    CHECK(bytes.find("sender-0") != std::string::npos, "va el origen");
    CHECK(bytes.find("receiver-0") != std::string::npos, "va el destino");
    CHECK(bytes.find("tp.connection") != std::string::npos, "va el tema");
    CHECK(bytes.find(R"({"type":"CONNECT"})") != std::string::npos, "va el texto");
    // 0x08 0x00 = campo 1 varint 0 (protocol_version CASTV2_1_0).
    CHECK(bytes[4] == '\x08' && bytes[5] == '\x00', "arranca con protocol_version 0");

    // --- 2) ida y vuelta ------------------------------------------------------
    {
        std::string buf = bytes;
        Mensaje leido;
        CHECK(sacar(buf, leido), "se lee");
        CHECK(buf.empty(), "y no sobra nada");
        CHECK(leido.origen == m.origen && leido.destino == m.destino && leido.tema == m.tema &&
                  leido.texto == m.texto,
              "vuelve igual");
    }
    {
        // Con acentos y con un texto largo: el largo va en bytes, no en letras.
        const Mensaje raro{"sender-0", "transport-1", "urn:x-cast:com.madmod.dashcast",
                           R"({"url":"http://192.168.0.5:8787/pantalla?texto=Sesión de mañana"})"};
        std::string buf = armar(raro);
        Mensaje leido;
        CHECK(sacar(buf, leido), "se lee con acentos");
        CHECK(leido.texto == raro.texto, "el texto vuelve igual");
    }

    // --- 3) trocear -----------------------------------------------------------
    {
        const Mensaje a{"s", "d", "t", "uno"};
        const Mensaje b{"s", "d", "t", "dos"};
        std::string buf = armar(a) + armar(b);
        Mensaje x;
        CHECK(sacar(buf, x) && x.texto == "uno", "sale el primero");
        CHECK(sacar(buf, x) && x.texto == "dos", "sale el segundo");
        CHECK(!sacar(buf, x), "y no hay mas");
    }
    {
        // Cortado por la mitad: no se lee hasta que llega el resto. Es lo que
        // pasa siempre en un socket.
        const std::string entero = armar(Mensaje{"s", "d", "t", "un texto largo para cortar"});
        std::string buf = entero.substr(0, entero.size() / 2);
        Mensaje x;
        CHECK(!sacar(buf, x), "incompleto: todavia no");
        buf += entero.substr(entero.size() / 2);
        CHECK(sacar(buf, x) && x.texto == "un texto largo para cortar", "completo: ahora si");
    }
    {
        std::string buf("\x00\x00", 2); // ni el largo entero (ojo: el nulo corta un literal)
        Mensaje x;
        CHECK(!sacar(buf, x), "menos de cuatro bytes: no");
        CHECK(buf.size() == 2, "y no se pierde lo que habia");
    }

    // --- 4) basura -------------------------------------------------------------
    {
        // Un largo disparatado (lo que manda algo que no es un Chromecast):
        // no se puede quedar esperando gigabytes.
        std::string buf("\x7f\xff\xff\xff", 4);
        buf += "cualquier cosa";
        Mensaje x;
        CHECK(!sacar(buf, x), "largo disparatado: no");
        CHECK(buf.empty(), "y se descarta lo que habia");
    }
    {
        // Un cuerpo que no es protobuf valido: falla, no revienta.
        std::string buf("\x00\x00\x00\x03", 4);
        buf += "\xff\xff\xff";
        Mensaje x;
        CHECK(!sacar(buf, x), "cuerpo invalido: falla sin reventar");
    }

    if (g_fallos == 0) {
        std::printf("test_cast: OK\n");
        return 0;
    }
    std::printf("test_cast: %d fallo(s)\n", g_fallos);
    return 1;
}
