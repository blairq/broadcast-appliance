// src/app/tests/test_red.cpp — reconocer equipos de la red y hablarles RTSP (sin red)

#include "red/Biblioteca.hpp"
#include "red/Plan.hpp"
#include "red/Rtsp.hpp"

#include <cstdio>

using namespace ba::app::red;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)
} // namespace

int main() {
    // --- Fabricantes por MAC (la base de verdad) ---------------------------------
    Oui oui;
    CHECK(oui.leer(BA_TEST_DATOS "/oui.tsv") && oui.cantidad() > 30000, "la base OUI se lee entera");
    CHECK(Oui::normalizar("0:40:8c:f5:89:c4") == "00408CF589C4", "MAC como la escribe arp (sin ceros)");
    CHECK(Oui::normalizar("54-C4-15-E8-3D-64") == "54C415E83D64", "MAC con guiones");
    CHECK(Oui::normalizar("hola").empty(), "lo que no es una MAC");
    CHECK(oui.fabricante("0:40:8c:f5:89:c4").find("Axis") != std::string::npos, "00:40:8C es Axis");
    CHECK(oui.fabricante("54:c4:15:e8:3d:64").find("Hikvision") != std::string::npos,
          "54:C4:15 es Hikvision");
    CHECK(oui.fabricante("54:d6:d:aa:74:9b").find("Ezviz") != std::string::npos, "54:D6:0D es EZVIZ");

    // --- Reconocer la marca ---------------------------------------------------------
    Biblioteca b;
    CHECK(b.leer(BA_TEST_DATOS "/camaras-urls.json"), "la biblioteca de URLs se lee");
    Pistas axis;
    axis.onvifNombre = "AXIS P5512";
    axis.fabricanteMac = "Axis Communications AB";
    const Marca ma = b.reconocer(axis);
    CHECK(ma.id == "axis" && ma.confianza == 3, "la Axis por ONVIF");
    // La EZVIZ con MAC de Hikvision: gana el modelo ONVIF.
    Pistas ezviz;
    ezviz.onvifModelo = "CS-H8-R100-1J5WKFL";
    ezviz.fabricanteMac = "Hangzhou Hikvision Digital Technology Co.,Ltd.";
    CHECK(b.reconocer(ezviz).id == "ezviz", "EZVIZ con MAC de Hikvision: gana el modelo ONVIF");
    Pistas dvr;
    dvr.onvifNombre = "Dvr Container";
    dvr.onvifModelo = "DS-7208HQHI-F1/N";
    CHECK(b.reconocer(dvr).id == "hikvision", "el DVR Hikvision por su modelo");
    Pistas solaMac;
    solaMac.fabricanteMac = "Zhejiang Dahua Technology Co., Ltd.";
    CHECK(b.reconocer(solaMac).id == "dahua" && b.reconocer(solaMac).confianza == 1,
          "solo la MAC: Dahua, con poca confianza");
    Pistas nada;
    nada.puertos = {554};
    CHECK(b.reconocer(nada).id == "generica", "sin pistas: genérica");

    // --- URLs candidatas ---------------------------------------------------------------
    const auto hik = b.candidatas("hikvision", "192.168.1.10", 554, 4);
    bool canal3 = false;
    for (const auto& c : hik)
        canal3 = canal3 || c.url == "rtsp://192.168.1.10/Streaming/Channels/301";
    CHECK(!hik.empty() && hik[0].url == "rtsp://192.168.1.10/Streaming/Channels/101" && canal3,
          "Hikvision: el principal primero y los canales del grabador");
    const auto fos = b.candidatas("foscam", "10.0.0.5");
    CHECK(!fos.empty() && fos[0].url == "rtsp://10.0.0.5:88/videoMain", "Foscam: su puerto propio");
    CHECK(b.candidatas("no-existe", "h").size() > 5, "marca desconocida: las genéricas");
    CHECK(b.ficha("ezviz").value("notas", "").find("VERIFICACIÓN") != std::string::npos,
          "EZVIZ: la nota del código de verificación");

    // --- RTSP ----------------------------------------------------------------------------
    const auto u = rtsp::partir("rtsp://admin:cl%40ve@192.168.1.10:8554/Streaming/Channels/101?x=1");
    CHECK(u.ok && u.host == "192.168.1.10" && u.puerto == 8554 && u.usuario == "admin" &&
              u.clave == "cl@ve" && u.ruta == "/Streaming/Channels/101?x=1",
          "partir una URL con usuario, clave, puerto y query");
    CHECK(rtsp::sinClave("rtsp://a:b@h/x") == "rtsp://h/x", "sin la clave para mostrar");
    CHECK(rtsp::describe("rtsp://a:b@h/x", 3).startsWith("DESCRIBE rtsp://h/x RTSP/1.0\r\nCSeq: 3"),
          "el DESCRIBE no lleva la clave en la línea");
    const QByteArray r401 =
        "RTSP/1.0 401 Unauthorized\r\nCSeq: 1\r\nWWW-Authenticate: Basic realm=\"Hik\"\r\n"
        "WWW-Authenticate: Digest realm=\"Hik\", nonce=\"n0nce\", stale=\"FALSE\"\r\n\r\n";
    const auto d = rtsp::leer(r401);
    CHECK(d.codigo == 401 && d.esquema == "Digest" && d.realm == "Hik" && d.nonce == "n0nce",
          "401: prefiere Digest");
    const QString a = rtsp::autorizacion(d, "rtsp://192.168.1.10/Streaming/Channels/101", "admin", "abc123");
    CHECK(a.contains("response=\"16393fde20c02a636e721f1ebd9a0a10\""),
          "Digest MD5 como RFC 2617 (calculado aparte)");
    const QByteArray r200 =
        "RTSP/1.0 200 OK\r\nCSeq: 2\r\nContent-Type: application/sdp\r\nContent-Length: 98\r\n\r\n"
        "v=0\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 "
        "H264/90000\r\na=x-dimensions:1920,1080\r\na=control:trackID=1\r\n";
    const auto ok = rtsp::leer(r200);
    CHECK(ok.codigo == 200 && ok.codec == "H264" && ok.ancho == 1920 && ok.alto == 1080,
          "200: códec y tamaño del SDP");
    CHECK(rtsp::leer("RTSP/1.0 200 OK\r\nContent-Length: 50\r\n\r\nv=0").completa == false,
          "un pedazo: falta el cuerpo");
    CHECK(rtsp::estadoDe(401) == "pide usuario y clave" && rtsp::estadoDe(404) == "no existe",
          "los estados en castellano");
    // --- El plan de conexión ---------------------------------------------------------------
    {
        using nlohmann::json;
        const json lienzo{{"ancho", 1920}, {"alto", 1080}, {"fps", 30.0}};
        json ficha{{"ip", "192.168.1.64"},
                   {"perfiles", json::array({{{"token", "4k"},
                                              {"nombre", "4K"},
                                              {"codec", "H265"},
                                              {"ancho", 3840},
                                              {"alto", 2160},
                                              {"fps", 25},
                                              {"uri", "rtsp://x/1"}},
                                             {{"token", "hd"},
                                              {"nombre", "HD"},
                                              {"codec", "H264"},
                                              {"ancho", 1920},
                                              {"alto", 1080},
                                              {"fps", 30},
                                              {"uri", "rtsp://x/2"},
                                              {"ptz", true}},
                                             {{"token", "sub"},
                                              {"nombre", "Sub"},
                                              {"codec", "H264"},
                                              {"ancho", 640},
                                              {"alto", 360},
                                              {"fps", 15},
                                              {"uri", "rtsp://x/3"}},
                                             {{"token", "mj"},
                                              {"nombre", "MJPEG"},
                                              {"codec", "JPEG"},
                                              {"ancho", 1920},
                                              {"alto", 1080},
                                              {"fps", 30},
                                              {"uri", "rtsp://x/4"}}})},
                   {"ptz", {{"tiene", true}, {"perfil", "hd"}}}};
        CHECK(elegirPerfil(ficha["perfiles"], 1080) == 1,
              "elige el HD H.264 (cubre el lienzo, no 4K, no MJPEG)");
        const json a{{"video",
                      {{"ancho", 1920},
                       {"alto", 1080},
                       {"fps", 30.0},
                       {"entrelazado", false},
                       {"pixel", "cuadrado"}}}};
        const json plan = planDe(ficha, a, lienzo);
        CHECK(plan["argumentos"].value("perfil", "") == "hd" &&
                  plan["argumentos"].value("normalizar", true) == false &&
                  plan["argumentos"].value("ptz", false),
              "plan: perfil HD, sin normalizar (ya viene bien), con PTZ");
        const json e{{"video", {{"fps", 25.0}, {"entrelazado", true}, {"pixel", "cuadrado"}}}};
        CHECK(planDe(ficha, e, lienzo)["argumentos"].value("normalizar", false),
              "plan: entrelazada a 25 fps -> normalizar");
        // Un dato con otro tipo (el entrelazado como número) no lo hace caer.
        const json raro{{"video", {{"entrelazado", 1}, {"fps", "treinta"}}}};
        const json pr = planDe(ficha, raro, lienzo);
        CHECK(pr.is_object(), "plan: datos raros no lo tiran (devuelve el error o un plan)");
        CHECK(planDe(json{{"ip", "x"}}, json::object(), lienzo).contains("error"),
              "plan sin perfiles ni URL: lo dice");
    }

    std::printf(fallos == 0 ? "test_red: OK\n" : "test_red: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
