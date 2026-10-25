// test_sesion_borde.cpp — el borde de la sesión remota (Sesion.hpp): link + PIN
// con hash y sal, el PIN errado solo cuenta si el link es bueno, bloqueo a las
// 5 fallas, Origin igual a Host (y por el túnel también). La atención hablada
// del que espera: solo con conexión, la oferta sube solo audio, y la voz del que
// atiende le llega solo a él (MediaRoute::oyeAlAtendedor).

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "Log.hpp"
#include "MediaRoute.hpp"
#include "Sesion.hpp"

using ba::gateway::BotoneraBorde;
using ba::gateway::SesionBorde;

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
} // namespace

int main() {
    ba::gateway::Log log;
    int eventos = 0;
    std::string ultimo;
    SesionBorde s("", log, [&](const nlohmann::json& d) {
        ++eventos;
        ultimo = d.value("tipo", std::string());
    });

    // La credencial como la arma la app (asamblea/SesionRemota).
    const std::string sal = "00112233445566778899aabbccddeeff";
    const std::string token(64, 'a');
    const std::string pin = "123456";
    s.ponerCredenciales(nlohmann::json::array(
        {{{"id", "leg-1"},
          {"nombre", "Ana Pérez"},
          {"banca", "12"},
          {"sal", sal},
          {"hash_token", BotoneraBorde::sha256Hex(sal + token)},
          {"hash_pin", BotoneraBorde::sha256Hex(sal + ":" + pin + ":" + token)}}}));

    bool pinMal = false;
    CHECK(s.autenticar("leg-1", token, pin, pinMal) == "leg-1" && !pinMal, "link y PIN correctos");
    CHECK(s.autenticar("leg-1", token, "654321", pinMal).empty() && pinMal, "PIN errado con el link bueno");
    CHECK(s.autenticar("leg-1", std::string(64, 'b'), pin, pinMal).empty() && !pinMal,
          "link malo: no cuenta como PIN errado");
    CHECK(s.autenticar("otro", token, pin, pinMal).empty(), "otro integrante: no");
    CHECK(s.autenticar("leg-1", token.substr(1), pin, pinMal).empty(), "token corto: no");

    for (int i = 0; i < 4; ++i) s.anotarPinMal("leg-1");
    CHECK(!s.bloqueado("leg-1") && eventos == 0, "4 fallas: todavía no");
    s.anotarPinMal("leg-1");
    CHECK(s.bloqueado("leg-1") && ultimo == "bloqueo", "a la 5ª se bloquea y avisa a la app");
    // Reenviar la lista (la app lo hace seguido) no levanta el bloqueo.
    s.ponerCredenciales(nlohmann::json::array(
        {{{"id", "leg-1"},
          {"sal", sal},
          {"hash_token", BotoneraBorde::sha256Hex(sal + token)},
          {"hash_pin", BotoneraBorde::sha256Hex(sal + ":" + pin + ":" + token)}}}));
    CHECK(s.bloqueado("leg-1"), "el bloqueo sobrevive a que se reenvíen las credenciales");
    // Un link nuevo (regenerado) sí empieza de cero.
    const std::string token2(64, 'c');
    s.ponerCredenciales(nlohmann::json::array(
        {{{"id", "leg-1"},
          {"sal", sal},
          {"hash_token", BotoneraBorde::sha256Hex(sal + token2)},
          {"hash_pin", BotoneraBorde::sha256Hex(sal + ":" + pin + ":" + token2)}}}));
    CHECK(!s.bloqueado("leg-1"), "con el link regenerado se desbloquea");
    CHECK(s.autenticar("leg-1", token, pin, pinMal).empty(), "el link viejo ya no anda");

    // Usuario y contraseña (entrar tipeando, sin el link).
    s.ponerCredenciales(nlohmann::json::array(
        {{{"id", "leg-2"},
          {"sal", sal},
          {"hash_token", BotoneraBorde::sha256Hex(sal + token2)},
          {"hash_pin", BotoneraBorde::sha256Hex(sal + ":" + pin + ":" + token2)},
          {"usuario", "Banca7"},
          {"hash_clave", BotoneraBorde::sha256Hex(sal + ":clave:" + "abcdefgh2345")}}}));
    std::string idU;
    bool claveMal = false;
    CHECK(s.autenticarUsuario("banca7", "abcdefgh2345", idU, claveMal) == "leg-2", "usuario (sin mayúsculas) y contraseña");
    CHECK(s.autenticarUsuario("banca7", "otra", idU, claveMal).empty() && claveMal && idU == "leg-2",
          "contraseña errada: cuenta para el bloqueo de ese integrante");
    CHECK(s.autenticarUsuario("nadie", "abcdefgh2345", idU, claveMal).empty() && !claveMal && idU.empty(),
          "usuario inexistente: no bloquea a nadie");

    CHECK(SesionBorde::origenValido("Host: sesion.ejemplo.org\r\nOrigin: https://sesion.ejemplo.org\r\nCF-Ray: x"),
          "por el túnel, mismo origen: sí");
    CHECK(!SesionBorde::origenValido("Host: sesion.ejemplo.org\r\nOrigin: https://malo.example"), "origen ajeno: no");
    CHECK(SesionBorde::origenValido("Host: 192.168.1.5:8789"), "sin Origin");

    // --- La atención hablada mientras espera (docs/funciones/ATENCION.md) ---
    CHECK(!s.ponerAtiende("leg-1", true).empty() && s.conAtiende().empty(),
          "atender hablando a alguien que no está conectado: error, y no queda anotado");
    CHECK(!s.ponerAtiende("consola:x", true).empty(), "a una consola nunca");
    CHECK(s.ponerAtiende("leg-1", false).empty(), "apagarla a quien no la tenía: no es un error");
    const auto sdpDe = [](const std::string& secciones) { return "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n" + secciones; };
    CHECK(SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n")),
          "solo el micrófono (sendrecv: también oye la voz del que atiende): pasa");
    CHECK(SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"
                                           "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=recvonly\r\n")),
          "un video que solo recibe no sube nada: pasa");
    CHECK(!SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"
                                            "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=sendonly\r\n")),
          "con la cámara: no");
    CHECK(!SesionBorde::subeSoloAudio(sdpDe("m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=sendrecv\r\n"
                                            "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n")),
          "un video sin dirección sube (sendrecv por omisión): no");
    CHECK(!SesionBorde::subeSoloAudio(sdpDe("m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n")),
          "sin audio no es la atención hablada");
    CHECK(!SesionBorde::subeSoloAudio(""), "vacío: no");

    using ba::gateway::MediaRoute;
    CHECK(MediaRoute::atencion().oyeAlAtendedor(), "el que espera en la sesión remota oye al que atiende");
    CHECK(MediaRoute::lobby(1, false, true).oyeAlAtendedor(), "el del lobby atendido hablando también");
    CHECK(!MediaRoute::lobby(1, true, true).oyeAlAtendedor(), "si el operador le habla, gana el operador");
    CHECK(!MediaRoute::lobby(1, false, false).oyeAlAtendedor(), "el resto del lobby no");
    CHECK(!MediaRoute::program(1).oyeAlAtendedor() && !MediaRoute::remoto(1).oyeAlAtendedor() &&
              !MediaRoute::backstage(0).oyeAlAtendedor(),
          "ni los de adentro ni el remoto con la palabra (la voz nunca va al aire)");
    CHECK(MediaRoute::atencion() != MediaRoute::escena(0), "la ruta de la atención es otra");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
