// ============================================================================
// src/gateway/tests/test_pantalla.cpp — que una pantalla no pueda mandar nada
//
// Una PANTALLA es un televisor que muestra el programa. Entra con el mismo
// token de ingreso que un invitado, y ese token lo puede tener cualquiera: la
// URL viaja hasta el aparato. Asi que lo que garantiza que no sea un microfono
// abierto en la sala NO puede ser que su pagina se porte bien — tiene que ser
// el servidor.
//
// `soloRecibe` es esa garantia: mira el SDP del offer y solo deja pasar los
// que no suben NINGUNA pista. Este test es el que lo sostiene.
//
//   1. Un offer de solo recepcion pasa.
//   2. Cualquier forma de ofrecer subida NO pasa: sendrecv, sendonly, una sola
//      de las dos pistas, o no decir nada (en SDP, callar es sendrecv).
//   3. Los canales de datos no cuentan como medios, pero tampoco habilitan.
//   4. Basura y bordes: vacio, sin secciones, renglones con \r.
// ============================================================================
#include <cstdio>
#include <string>

#include "SignalingServer.hpp"

using ba::gateway::SignalingServer;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

/// Un SDP de mentira con las direcciones que se le pidan. Alcanza: lo unico
/// que mira `soloRecibe` son las lineas "m=" y las de direccion.
std::string sdp(const char* video, const char* audio) {
    std::string s = "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
    if (video != nullptr) {
        s += "m=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\na=mid:0\r\n";
        if (*video != '\0') s += std::string("a=") + video + "\r\n";
    }
    if (audio != nullptr) {
        s += "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=mid:1\r\n";
        if (*audio != '\0') s += std::string("a=") + audio + "\r\n";
    }
    return s;
}
} // namespace

int main() {
    std::printf("== test_pantalla ==\n");

    // --- 1) lo unico que se acepta -------------------------------------------
    CHECK(SignalingServer::soloRecibe(sdp("recvonly", "recvonly")), "recvonly en las dos: pasa");
    CHECK(SignalingServer::soloRecibe(sdp("recvonly", "inactive")), "inactive tambien es no subir");
    CHECK(SignalingServer::soloRecibe(sdp("inactive", "inactive")), "las dos inactivas: pasa");
    CHECK(SignalingServer::soloRecibe(sdp("recvonly", nullptr)), "solo video, recvonly: pasa");

    // --- 2) todo lo que ofrece subir ------------------------------------------
    CHECK(!SignalingServer::soloRecibe(sdp("sendrecv", "sendrecv")), "sendrecv: NO");
    CHECK(!SignalingServer::soloRecibe(sdp("sendonly", "sendonly")), "sendonly: NO");
    // El caso que importa de verdad: un televisor que manda AUDIO seria un
    // microfono abierto en la sala. Que el video sea recvonly no lo salva.
    CHECK(!SignalingServer::soloRecibe(sdp("recvonly", "sendrecv")), "el audio sube: NO, aunque el video no");
    CHECK(!SignalingServer::soloRecibe(sdp("sendrecv", "recvonly")), "el video sube: NO, aunque el audio no");
    // En SDP, una seccion que no dice su direccion es sendrecv. Callarse no
    // puede ser una forma de colarse.
    CHECK(!SignalingServer::soloRecibe(sdp("", "recvonly")), "sin direccion en el video: NO");
    CHECK(!SignalingServer::soloRecibe(sdp("recvonly", "")), "sin direccion en el audio: NO");
    CHECK(!SignalingServer::soloRecibe(sdp("", "")), "sin ninguna direccion: NO");

    // --- 3) canales de datos ----------------------------------------------------
    {
        // Un m=application no lleva audio ni video: no se le exige direccion.
        const std::string s = sdp("recvonly", "recvonly") +
                              "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:2\r\n";
        CHECK(SignalingServer::soloRecibe(s), "un canal de datos no estorba");
    }
    {
        // Pero tampoco sirve para habilitar: si una pista de verdad sube, no.
        const std::string s = "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:0\r\n" +
                              sdp("sendrecv", nullptr);
        CHECK(!SignalingServer::soloRecibe(s), "datos + video que sube: NO");
    }

    // --- 4) bordes ---------------------------------------------------------------
    CHECK(SignalingServer::soloRecibe(""), "vacio: no hay nada que suba");
    CHECK(SignalingServer::soloRecibe("v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\n"), "sin secciones: pasa");
    {
        // Sin \r (algunos clientes mandan solo \n).
        std::string s = sdp("recvonly", "recvonly");
        std::string sinCr;
        for (const char c : s)
            if (c != '\r') sinCr.push_back(c);
        CHECK(SignalingServer::soloRecibe(sinCr), "sin retorno de carro: igual se entiende");
        std::string malo = sdp("sendrecv", "recvonly");
        sinCr.clear();
        for (const char c : malo)
            if (c != '\r') sinCr.push_back(c);
        CHECK(!SignalingServer::soloRecibe(sinCr), "sin retorno de carro: y sigue rechazando");
    }
    {
        // Una direccion que no es ninguna de las cuatro no cuenta como
        // declarada: no se cuela por no saber leerla.
        const std::string s = sdp("sendrecv-raro", "recvonly");
        CHECK(!SignalingServer::soloRecibe(s), "una direccion rara no habilita");
    }

    if (g_fallos == 0) {
        std::printf("test_pantalla: OK\n");
        return 0;
    }
    std::printf("test_pantalla: %d fallo(s)\n", g_fallos);
    return 1;
}
