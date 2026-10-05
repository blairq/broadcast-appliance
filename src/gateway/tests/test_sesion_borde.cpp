// test_sesion_borde.cpp — el borde de la sesión remota (Sesion.hpp): link + PIN
// con hash y sal, el PIN errado solo cuenta si el link es bueno, bloqueo a las
// 5 fallas, Origin igual a Host (y por el túnel también).

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "Log.hpp"
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

    // La credencial como la arma la app (legislatura/SesionRemota).
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

    CHECK(SesionBorde::origenValido("Host: sesion.ejemplo.org\r\nOrigin: https://sesion.ejemplo.org\r\nCF-Ray: x"),
          "por el túnel, mismo origen: sí");
    CHECK(!SesionBorde::origenValido("Host: sesion.ejemplo.org\r\nOrigin: https://malo.example"), "origen ajeno: no");
    CHECK(SesionBorde::origenValido("Host: 192.168.1.5:8789"), "sin Origin");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
