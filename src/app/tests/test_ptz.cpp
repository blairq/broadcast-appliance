// src/app/tests/test_ptz.cpp — el PTZ: protocolos byte a byte y backends contra cámaras falsas
//
// 1. Funciones puras (ptz/PtzProtocolos): los bytes VISCA, las órdenes
//    Panasonic, el XML ONVIF y el digest de WS-Security (con un vector
//    calculado aparte: base64(sha1(nonce + created + clave))).
// 2. Cada backend contra una cámara FALSA en 127.0.0.1 que contesta como la
//    de verdad: VISCA de Sony por UDP (con encabezado y secuencia), VISCA
//    crudo por TCP, Panasonic por HTTP, ONVIF por HTTP (verifica que la clave
//    que firma cada pedido sea la correcta) y la búsqueda ONVIF por UDP.
//
// Sin red de verdad ni cámaras: corre en cualquier máquina (y en CI).

#include "ptz/OnvifBusqueda.hpp"
#include "ptz/Posiciones.hpp"
#include "ptz/PtzOnvif.hpp"
#include "ptz/PtzPanasonic.hpp"
#include "ptz/PtzProtocolos.hpp"
#include "ptz/PtzVisca.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUdpSocket>

#include <cstdio>
#include <functional>

using namespace ba::app::ptz;

namespace {

int fallos = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

QByteArray hex(const char* s) { return QByteArray::fromHex(QByteArray(s).replace(' ', "")); }

bool esperar(const std::function<bool()>& cond, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

void dormir(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// --- 1. Funciones puras ------------------------------------------------------------

void pruebaVisca() {
    CHECK(visca::panTilt(1, 0) == hex("81 01 06 01 18 01 02 03 FF"), "VISCA: derecha a fondo");
    CHECK(visca::panTilt(-0.5, 0.5) == hex("81 01 06 01 0D 0B 01 01 FF"),
          "VISCA: arriba a la izquierda a media");
    CHECK(visca::panTilt(0, -1) == hex("81 01 06 01 01 14 03 02 FF"), "VISCA: abajo a fondo");
    CHECK(visca::panTilt(0, 0) == hex("81 01 06 01 01 01 03 03 FF"), "VISCA: quieto");
    CHECK(visca::panTilt(1, 0, 2) == hex("82 01 06 01 18 01 02 03 FF"), "VISCA: cámara 2");
    CHECK(visca::zoom(1) == hex("81 01 04 07 27 FF"), "VISCA: zoom tele a fondo");
    CHECK(visca::zoom(-1) == hex("81 01 04 07 37 FF"), "VISCA: zoom wide a fondo");
    CHECK(visca::zoom(0) == hex("81 01 04 07 00 FF"), "VISCA: zoom quieto");
    CHECK(visca::foco(1) == hex("81 01 04 08 27 FF"), "VISCA: foco lejos");
    CHECK(visca::focoAuto(true) == hex("81 01 04 38 02 FF"), "VISCA: autofoco");
    CHECK(visca::focoAuto(false) == hex("81 01 04 38 03 FF"), "VISCA: foco manual");
    CHECK(visca::preset(true, 1) == hex("81 01 04 3F 01 00 FF"), "VISCA: guardar preset 1");
    CHECK(visca::preset(false, 3) == hex("81 01 04 3F 02 02 FF"), "VISCA: ir al preset 3");
    CHECK(visca::home() == hex("81 01 06 04 FF"), "VISCA: home");
    CHECK(visca::borrarPreset(128) == hex("81 01 04 3F 00 7F FF"), "VISCA: borrar la memoria 128");
    CHECK(visca::conEncabezado(0x0100, visca::home(), 5) == hex("01 00 00 05 00 00 00 05 81 01 06 04 FF"),
          "VISCA over IP: encabezado de Sony");
    CHECK(visca::reiniciarSecuencia() == hex("02 00 00 01 00 00 00 00 01"),
          "VISCA over IP: reiniciar secuencia");
    CHECK(visca::leer(hex("90 41 FF")).tipo == visca::Respuesta::Recibida, "VISCA: ACK");
    CHECK(visca::leer(hex("90 51 FF")).tipo == visca::Respuesta::Hecha, "VISCA: hecha");
    const auto err = visca::leer(hex("90 60 02 FF"));
    CHECK(err.tipo == visca::Respuesta::Error && err.codigo == 2, "VISCA: error de sintaxis");
    CHECK(visca::leer(hex("01 11 00 03 00 00 00 05 90 51 FF")).tipo == visca::Respuesta::Hecha,
          "VISCA over IP: respuesta con encabezado");
}

void pruebaPanasonic() {
    CHECK(panasonic::panTilt(0, 0) == "#PTS5050", "Panasonic: quieto");
    CHECK(panasonic::panTilt(1, -1) == "#PTS9901", "Panasonic: derecha y abajo a fondo");
    CHECK(panasonic::zoom(1) == "#Z99" && panasonic::zoom(-1) == "#Z01", "Panasonic: zoom");
    CHECK(panasonic::preset(false, 1) == "#R00" && panasonic::preset(true, 12) == "#M11",
          "Panasonic: presets");
    CHECK(panasonic::borrarPreset(100) == "#C99", "Panasonic: borrar la memoria 100");
    CHECK(panasonic::url("10.0.0.5", 80, "#PTS5050") == "http://10.0.0.5/cgi-bin/aw_ptz?cmd=%23PTS5050&res=1",
          "Panasonic: URL");
    CHECK(panasonic::url("10.0.0.5", 8080, "#Z50") == "http://10.0.0.5:8080/cgi-bin/aw_ptz?cmd=%23Z50&res=1",
          "Panasonic: URL con puerto");
    CHECK(panasonic::respuestaOk("#PTS5050", "pTS5050") && !panasonic::respuestaOk("#Z50", "er1:Z50"),
          "Panasonic: respuestas");
}

const char* kPerfiles = R"(<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"
xmlns:trt="http://www.onvif.org/ver10/media/wsdl" xmlns:tt="http://www.onvif.org/ver10/schema"><s:Body>
<trt:GetProfilesResponse>
 <trt:Profiles token="perfil_sub" fixed="true"><tt:Name>Sub</tt:Name>
  <tt:VideoSourceConfiguration token="vsc"><tt:Name>VSC</tt:Name><tt:SourceToken>fuente0</tt:SourceToken></tt:VideoSourceConfiguration>
 </trt:Profiles>
 <trt:Profiles token="perfil_ptz" fixed="true"><tt:Name>Principal</tt:Name>
  <tt:VideoSourceConfiguration token="vsc"><tt:Name>VSC</tt:Name><tt:SourceToken>fuente0</tt:SourceToken></tt:VideoSourceConfiguration>
  <tt:PTZConfiguration token="ptzc"><tt:Name>PTZ</tt:Name><tt:NodeToken>nodo</tt:NodeToken></tt:PTZConfiguration>
 </trt:Profiles>
</trt:GetProfilesResponse></s:Body></s:Envelope>)";

const char* kPresets =
    R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl"
xmlns:tt="http://www.onvif.org/ver10/schema"><s:Body><tptz:GetPresetsResponse>
<tptz:Preset token="p1"><tt:Name>Atril</tt:Name><tt:PTZPosition><tt:PanTilt x="0" y="0"/></tt:PTZPosition></tptz:Preset>
<tptz:Preset token="p2"><tt:Name>Mesa</tt:Name></tptz:Preset>
</tptz:GetPresetsResponse></s:Body></s:Envelope>)";

QByteArray capacidades(const QString& base) {
    return QStringLiteral(
               R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl"
xmlns:tt="http://www.onvif.org/ver10/schema"><s:Body><tds:GetCapabilitiesResponse><tds:Capabilities>
<tt:Device><tt:XAddr>%1/onvif/device_service</tt:XAddr></tt:Device>
<tt:Imaging><tt:XAddr>%1/onvif/imaging</tt:XAddr></tt:Imaging>
<tt:Media><tt:XAddr>%1/onvif/media</tt:XAddr><tt:StreamingCapabilities><tt:RTPMulticast>false</tt:RTPMulticast></tt:StreamingCapabilities></tt:Media>
<tt:PTZ><tt:XAddr>%1/onvif/ptz</tt:XAddr></tt:PTZ>
</tds:Capabilities></tds:GetCapabilitiesResponse></s:Body></s:Envelope>)")
        .arg(base)
        .toUtf8();
}

const char* kHora =
    R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl"
xmlns:tt="http://www.onvif.org/ver10/schema"><s:Body><tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime>
<tt:DateTimeType>NTP</tt:DateTimeType><tt:UTCDateTime><tt:Time><tt:Hour>5</tt:Hour><tt:Minute>0</tt:Minute><tt:Second>0</tt:Second></tt:Time>
<tt:Date><tt:Year>2026</tt:Year><tt:Month>10</tt:Month><tt:Day>2</tt:Day></tt:Date></tt:UTCDateTime>
</tds:SystemDateAndTime></tds:GetSystemDateAndTimeResponse></s:Body></s:Envelope>)";

const char* kFalla = R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body><s:Fault>
<s:Code><s:Value>s:Sender</s:Value></s:Code><s:Reason><s:Text xml:lang="en">Sender not Authorized</s:Text></s:Reason>
</s:Fault></s:Body></s:Envelope>)";

const char* kEncontrada =
    R"(<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope" xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery">
<e:Body><d:ProbeMatches><d:ProbeMatch><d:Types>dn:NetworkVideoTransmitter</d:Types>
<d:Scopes>onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/name/PTZ%20Atril onvif://www.onvif.org/hardware/HD-20X</d:Scopes>
<d:XAddrs>http://[fe80::1]/onvif/device_service http://192.168.1.64/onvif/device_service</d:XAddrs>
</d:ProbeMatch></d:ProbeMatches></e:Body></e:Envelope>)";

void pruebaOnvif() {
    CHECK(onvif::digest("0123456789abcdef", "2026-10-02T05:00:00Z", "secreto") ==
              "sAkR8MH1Ps/6OVenrcWVQPMZ0vs=",
          "ONVIF: digest de WS-Security");
    const QByteArray s =
        onvif::sobre(onvif::getProfiles(), "admin", "secreto", 0, "0123456789abcdef", "2026-10-02T05:00:00Z");
    CHECK(s.contains("<wsse:Username>admin</wsse:Username>"), "ONVIF: usuario en el sobre");
    CHECK(s.contains(">sAkR8MH1Ps/6OVenrcWVQPMZ0vs=</wsse:Password>"), "ONVIF: digest en el sobre");
    CHECK(s.contains(">MDEyMzQ1Njc4OWFiY2RlZg==</wsse:Nonce>"), "ONVIF: nonce en base64");
    CHECK(s.contains("soap-message-security-1.0#Base64Binary"),
          "ONVIF: EncodingType del nonce (Base64Binary)");
    CHECK(s.contains("<s:Body><trt:GetProfiles/></s:Body>"), "ONVIF: cuerpo");
    CHECK(!onvif::sobre(onvif::getProfiles()).contains("Security"), "ONVIF: sin usuario, sin WS-Security");
    CHECK(onvif::continuousMove("T", 1, -0.5, 0)
              .contains(R"(<tt:PanTilt x="1.000" y="-0.500"/></tptz:Velocity>)"),
          "onvif continuousMove: solo pan/tilt (sin zoom en 0)");
    CHECK(onvif::continuousMove("T", 0, 0, 0.5)
              .contains(R"(<tptz:Velocity><tt:Zoom x="0.500"/></tptz:Velocity>)"),
          "onvif continuousMove: solo zoom (sin PanTilt en 0,0: la Axis lo toma como parar)");
    CHECK(onvif::setPreset("T", "Preset 2").indexOf("PresetToken") < 0 &&
              onvif::setPreset("T", "P", "p2").contains("p2"),
          "ONVIF: SetPreset nuevo o pisando");

    CHECK(onvif::removePreset("T", "p9") ==
              "<tptz:RemovePreset><tptz:ProfileToken>T</tptz:ProfileToken><tptz:PresetToken>p9</"
              "tptz:PresetToken></tptz:RemovePreset>",
          "ONVIF: RemovePreset");
    const auto perfiles = onvif::leerPerfiles(kPerfiles);
    CHECK(perfiles.size() == 2, "ONVIF: dos perfiles");
    if (perfiles.size() == 2) {
        CHECK(!perfiles[0].tienePtz && perfiles[1].tienePtz, "ONVIF: cuál tiene PTZ");
        CHECK(perfiles[1].token == "perfil_ptz" && perfiles[1].nombre == "Principal" &&
                  perfiles[1].fuenteVideo == "fuente0",
              "ONVIF: token, nombre y fuente de video del perfil");
    }
    const auto presets = onvif::leerPresets(kPresets);
    CHECK(presets.size() == 2 && presets[0].token == "p1" && presets[0].nombre == "Atril", "ONVIF: presets");
    const auto sv = onvif::leerCapacidades(capacidades("http://cam"));
    CHECK(sv.ptz == "http://cam/onvif/ptz" && sv.media == "http://cam/onvif/media" &&
              sv.imaging == "http://cam/onvif/imaging",
          "ONVIF: servicios de GetCapabilities");
    CHECK(onvif::leerHora(kHora) == QDateTime(QDate(2026, 10, 2), QTime(5, 0), QTimeZone::UTC),
          "ONVIF: hora de la cámara");
    CHECK(onvif::leerFalla(kFalla) == "Sender not Authorized", "ONVIF: falla SOAP");
    CHECK(onvif::leerFalla(kPresets).isEmpty(), "ONVIF: sin falla");
    const auto e = onvif::leerEncontrada(kEncontrada);
    CHECK(e.direccion == "http://192.168.1.64/onvif/device_service" && e.nombre == "PTZ Atril" &&
              e.modelo == "HD-20X",
          "WS-Discovery: ProbeMatch (IPv4 primero, nombre y modelo)");
    CHECK(onvif::sondeo("abc").contains("dn:NetworkVideoTransmitter") &&
              onvif::sondeo("abc").contains("uuid:abc"),
          "WS-Discovery: Probe");
}

void pruebaPosiciones() {
    Posiciones ps;
    Posicion a;
    a.nombre = "  Atril ";
    a.comentario = "Plano medio";
    a.digital = true;
    a.x = 0.3;
    a.zoom = 2;
    a.ancho = 1920;
    a.alto = 1080;
    Posicion& g = ps.agregar(a);
    CHECK(g.id == "1" && g.nombre == "Atril" && !g.creada.isEmpty(),
          "Posiciones: id, nombre recortado y fecha");
    CHECK(ps.validarNombre("atríl") != "",
          "Posiciones: el nombre es único sin importar mayúsculas ni tildes");
    CHECK(ps.validarNombre("") != "" && ps.validarNombre("7") != "",
          "Posiciones: sin nombre o solo un número, no");
    CHECK(ps.validarNombre("Atril", "1").isEmpty(), "Posiciones: la misma puede quedarse con su nombre");
    Posicion b;
    b.nombre = "Mesa del jurado";
    b.backend = "visca";
    b.memoria = ps.memoriaLibre(128);
    ps.agregar(b);
    CHECK(b.memoria == 128 && ps.memoriaLibre(128) == 127, "Posiciones: memorias de arriba para abajo");
    CHECK(ps.memoriaLibre(16) == 0, "Posiciones: no pisa los presets rápidos 1-16");
    CHECK(ps.buscar("2") && ps.buscar("2")->nombre == "Mesa del jurado", "Posiciones: buscar por id");
    CHECK(ps.buscar("MESA DEL JURADO") && ps.buscar("mesa  del jurado"), "Posiciones: buscar por nombre");
    CHECK(ps.buscar("atril")->definicion() == "1920x1080", "Posiciones: la definición");
    // Ida y vuelta por json, y los ids siguen sin repetirse.
    Posiciones otra;
    otra.cargar(ps.json());
    CHECK(otra.lista().size() == 2, "Posiciones: json");
    const Posicion* r = otra.buscar("Atril");
    CHECK(r && r->digital && r->x == 0.3 && r->zoom == 2 && r->comentario == "Plano medio" &&
              r->ancho == 1920,
          "Posiciones: el json conserva todo");
    CHECK(otra.buscar("2")->memoria == 128 && otra.buscar("2")->backend == "visca",
          "Posiciones: la memoria de la cámara");
    otra.borrar("1");
    Posicion c;
    c.nombre = "Tribuna";
    CHECK(otra.agregar(c).id == "3", "Posiciones: un id borrado no se reusa");
}

// --- 2. Cámaras falsas ---------------------------------------------------------------

/// Un HTTP mínimo: junta cada pedido (método, ruta, cuerpo) y contesta lo que diga `responder`.
class HttpFalso : public QObject {
   public:
    struct Pedido {
        QString metodo, ruta;
        QByteArray cuerpo;
        qint64 ms;
    };
    std::function<QByteArray(const Pedido&)> responder;
    QList<Pedido> pedidos;
    QElapsedTimer reloj;

    HttpFalso() {
        reloj.start();
        servidor.listen(QHostAddress::LocalHost, 0);
        connect(&servidor, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = servidor.nextPendingConnection()) {
                auto* buf = new QByteArray;
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() {
                    *buf += s->readAll();
                    const int fin = buf->indexOf("\r\n\r\n");
                    if (fin < 0) return;
                    const QString cab = QString::fromLatin1(buf->left(fin));
                    const auto m = QRegularExpression("Content-Length: *(\\d+)",
                                                      QRegularExpression::CaseInsensitiveOption)
                                       .match(cab);
                    const int largo = m.hasMatch() ? m.captured(1).toInt() : 0;
                    if (buf->size() < fin + 4 + largo) return;
                    const QStringList linea = cab.section("\r\n", 0, 0).split(' ');
                    Pedido p{linea.value(0), linea.value(1), buf->mid(fin + 4, largo), reloj.elapsed()};
                    pedidos << p;
                    const QByteArray r = responder ? responder(p) : QByteArray("ok");
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: application/soap+xml\r\nConnection: "
                             "close\r\nContent-Length: " +
                             QByteArray::number(r.size()) + "\r\n\r\n" + r);
                    s->disconnectFromHost();
                    buf->clear();
                });
                connect(s, &QTcpSocket::disconnected, s, [s, buf]() {
                    delete buf;
                    s->deleteLater();
                });
            }
        });
    }
    int puerto() const { return servidor.serverPort(); }

   private:
    QTcpServer servidor;
};

void pruebaViscaSony() {
    QUdpSocket camara;
    camara.bind(QHostAddress::LocalHost, 0);
    QList<QByteArray> recibido;
    QObject::connect(&camara, &QUdpSocket::readyRead, &camara, [&]() {
        while (camara.hasPendingDatagrams()) {
            QByteArray d;
            QHostAddress de;
            quint16 puerto = 0;
            d.resize(static_cast<int>(camara.pendingDatagramSize()));
            camara.readDatagram(d.data(), d.size(), &de, &puerto);
            recibido << d;
            // Contesta ACK y «hecha» con el mismo número de secuencia.
            const QByteArray seq = d.mid(4, 4);
            camara.writeDatagram(hex("01 11 00 03") + seq + hex("90 41 FF"), de, puerto);
            camara.writeDatagram(hex("01 11 00 03") + seq + hex("90 51 FF"), de, puerto);
        }
    });
    PtzVisca v;
    v.configurar(QJsonObject{{"host", "127.0.0.1"}, {"puerto", camara.localPort()}, {"transporte", "sony"}});
    CHECK(esperar([&]() { return v.listo(); }), "VISCA Sony: la cámara falsa contesta");
    CHECK(!recibido.isEmpty() && recibido[0] == visca::reiniciarSecuencia(),
          "VISCA Sony: primero reinicia la secuencia");
    CHECK(recibido.size() >= 2 && recibido[1] == visca::conEncabezado(0x0100, visca::zoom(0), 1),
          "VISCA Sony: el sondeo va con encabezado y secuencia 1");
    recibido.clear();
    v.mover(1, 0, 0);
    v.irAPreset(2);
    esperar([&]() { return recibido.size() >= 3; });
    bool panTilt = false, preset = false;
    for (const QByteArray& d : recibido) {
        panTilt = panTilt || d.mid(8) == visca::panTilt(1, 0);
        preset = preset || d.mid(8) == visca::preset(false, 2);
    }
    CHECK(panTilt && preset, "VISCA Sony: mover y preset llegan con encabezado");
    recibido.clear();
    v.parar();
    esperar([&]() { return recibido.size() >= 2; });
    CHECK(recibido.size() >= 2 && recibido[0].mid(8) == visca::panTilt(0, 0) &&
              recibido[1].mid(8) == visca::zoom(0),
          "VISCA Sony: parar frena pan/tilt y zoom");
    // Muchas órdenes seguidas del joystick: se juntan (no salen 100).
    recibido.clear();
    for (int i = 0; i < 100; ++i)
        v.mover(0.01 * i, 0, 0);
    dormir(200);
    std::printf("VISCA Sony: 100 movimientos del joystick en el acto -> %lld mensajes\n",
                static_cast<long long>(recibido.size()));
    CHECK(recibido.size() <= 6, "VISCA Sony: el joystick se junta");
}

void pruebaViscaTcp() {
    QTcpServer camara;
    camara.listen(QHostAddress::LocalHost, 0);
    QByteArray recibido;
    QTcpSocket* conexion = nullptr;
    QObject::connect(&camara, &QTcpServer::newConnection, &camara, [&]() {
        conexion = camara.nextPendingConnection();
        QObject::connect(conexion, &QTcpSocket::readyRead, conexion, [&]() {
            recibido += conexion->readAll();
            conexion->write(hex("90 41 FF 90 51 FF")); // dos respuestas pegadas
        });
    });
    PtzVisca v;
    v.configurar(QJsonObject{{"host", "127.0.0.1"}, {"puerto", camara.serverPort()}, {"transporte", "tcp"}});
    CHECK(esperar([&]() { return v.listo(); }), "VISCA TCP: conecta");
    v.guardarPreset(5);
    CHECK(esperar([&]() { return recibido.contains(visca::preset(true, 5)); }),
          "VISCA TCP: guardar preset llega crudo");
    CHECK(!recibido.startsWith(char(0x01)), "VISCA TCP: sin encabezado de Sony");
}

void pruebaPanasonicHttp() {
    HttpFalso camara;
    camara.responder = [](const HttpFalso::Pedido& p) {
        const QString orden =
            QUrl::fromPercentEncoding(p.ruta.section("cmd=", 1).section('&', 0, 0).toUtf8());
        return orden.mid(1, 1).toLower().toUtf8() + orden.mid(2).toUtf8(); // #PTS5050 -> pTS5050
    };
    PtzPanasonic v;
    v.configurar(QJsonObject{{"host", "127.0.0.1"}, {"puerto", camara.puerto()}});
    CHECK(esperar([&]() { return v.listo(); }), "Panasonic: la cámara falsa contesta");
    v.mover(1, 0, 0);
    v.irAPreset(3);
    CHECK(esperar([&]() { return camara.pedidos.size() >= 4; }), "Panasonic: llegan las órdenes");
    QStringList rutas;
    for (const auto& p : camara.pedidos)
        rutas << p.ruta;
    CHECK(rutas.contains("/cgi-bin/aw_ptz?cmd=%23PTS9950&res=1"), "Panasonic: pan a la derecha");
    CHECK(rutas.contains("/cgi-bin/aw_ptz?cmd=%23R02&res=1"), "Panasonic: preset 3");
    bool espaciadas = true;
    for (int i = 1; i < camara.pedidos.size(); ++i)
        espaciadas = espaciadas && camara.pedidos[i].ms - camara.pedidos[i - 1].ms >= 120;
    CHECK(espaciadas, "Panasonic: 130 ms entre órdenes");
}

void pruebaOnvifHttp() {
    HttpFalso camara;
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(camara.puerto());
    QList<QByteArray> conClave;
    bool presetNuevo = false;
    camara.responder = [&](const HttpFalso::Pedido& p) -> QByteArray {
        const QByteArray b = p.cuerpo;
        if (b.contains("Security")) conClave << b;
        const QString env =
            R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:tptz="http://www.onvif.org/ver20/ptz/wsdl"><s:Body>%1</s:Body></s:Envelope>)";
        if (b.contains("GetSystemDateAndTime")) return kHora;
        // Todo lo demás exige usuario y clave (como una cámara de verdad).
        if (!b.contains("<wsse:Username>admin</wsse:Username>")) return kFalla;
        if (b.contains("GetCapabilities")) return capacidades(base);
        if (b.contains("GetProfiles")) return kPerfiles;
        if (b.contains("GetPresets")) {
            if (!presetNuevo) return kPresets;
            QByteArray r = kPresets;
            return r.replace("</tptz:GetPresetsResponse>",
                             "<tptz:Preset token=\"p3\"><tt:Name>Preset "
                             "3</tt:Name></tptz:Preset></tptz:GetPresetsResponse>");
        }
        if (b.contains("SetPreset")) {
            presetNuevo = true;
            return env
                .arg("<tptz:SetPresetResponse><tptz:PresetToken>p3</tptz:PresetToken></"
                     "tptz:SetPresetResponse>")
                .toUtf8();
        }
        return env.arg("<tptz:Ok/>").toUtf8();
    };
    PtzOnvif v;
    v.configurar(QJsonObject{{"host", base}, {"usuario", "admin"}, {"clave", "secreto"}});
    CHECK(esperar([&]() { return v.listo() && v.capacidades().panTilt; }),
          "ONVIF: conecta y encuentra el PTZ");
    std::printf("ONVIF: %s\n", qPrintable(v.estado()));
    CHECK(v.capacidades().foco, "ONVIF: foco por Imaging");
    v.mover(0.5, 0, 1);
    CHECK(esperar([&]() {
              for (const auto& p : camara.pedidos)
                  if (p.cuerpo.contains("ContinuousMove")) return true;
              return false;
          }),
          "ONVIF: ContinuousMove");
    for (const auto& p : camara.pedidos) {
        if (!p.cuerpo.contains("ContinuousMove")) continue;
        CHECK(p.ruta == "/onvif/ptz", "ONVIF: va al servicio PTZ");
        CHECK(p.cuerpo.contains("<tptz:ProfileToken>perfil_ptz</tptz:ProfileToken>"),
              "ONVIF: con el perfil que tiene PTZ");
        CHECK(p.cuerpo.contains(R"(<tt:PanTilt x="0.500" y="0.000"/><tt:Zoom x="1.000"/>)"),
              "ONVIF: velocidades");
    }
    // La clave que firma cada pedido es la correcta (la cámara recalcula el digest).
    bool firmasOk = !conClave.isEmpty();
    for (const QByteArray& b : conClave) {
        const QString x = QString::fromUtf8(b);
        const auto campo = [&](const char* re) {
            return QRegularExpression(re).match(x).captured(1).toUtf8();
        };
        const QByteArray nonce = QByteArray::fromBase64(campo("<wsse:Nonce[^>]*>([^<]*)</wsse:Nonce>"));
        const QByteArray creado = campo("<wsu:Created>([^<]*)</wsu:Created>");
        const QByteArray digest = campo("<wsse:Password[^>]*>([^<]*)</wsse:Password>");
        firmasOk = firmasOk && nonce.size() == 16 &&
                   digest == onvif::digest(nonce, QString::fromUtf8(creado), "secreto");
    }
    CHECK(firmasOk, "ONVIF: cada pedido firmado con la clave correcta");
    v.irAPreset(2);
    CHECK(esperar([&]() {
              for (const auto& p : camara.pedidos)
                  if (p.cuerpo.contains("GotoPreset") &&
                      p.cuerpo.contains("<tptz:PresetToken>p2</tptz:PresetToken>"))
                      return true;
              return false;
          }),
          "ONVIF: el preset 2 es el segundo de la cámara");
    v.guardarPreset(3);
    CHECK(esperar([&]() { return v.estado().contains("3 presets"); }),
          "ONVIF: guardar un preset nuevo y releerlos");
    // Posición con nombre: la cámara elige el token.
    QString token;
    bool listoPos = false;
    v.grabarPosicion(0, "Atril", [&](bool ok, QString t) {
        listoPos = ok;
        token = t;
    });
    CHECK(esperar([&]() { return listoPos; }) && token == "p3",
          "ONVIF: grabar una posición devuelve el token de la cámara");
    v.borrarPosicion(0, token);
    CHECK(esperar([&]() {
              for (const auto& p : camara.pedidos)
                  if (p.cuerpo.contains("<tptz:RemovePreset>") && p.cuerpo.contains(">p3<")) return true;
              return false;
          }),
          "ONVIF: borrar la posición libera la memoria");
    v.parar();
    CHECK(esperar([&]() {
              for (const auto& p : camara.pedidos)
                  if (p.cuerpo.contains("<tptz:Stop>")) return true;
              return false;
          }),
          "ONVIF: Stop");

    // Con la clave equivocada: lo dice.
    PtzOnvif mala;
    mala.configurar(QJsonObject{{"host", base}, {"usuario", "otro"}, {"clave", "x"}});
    CHECK(esperar([&]() { return mala.estado().contains("usuario o la clave"); }),
          "ONVIF: avisa la clave equivocada");
}

void pruebaBusqueda() {
    QUdpSocket camara;
    camara.bind(QHostAddress::LocalHost, 0);
    QByteArray sondeo;
    QObject::connect(&camara, &QUdpSocket::readyRead, &camara, [&]() {
        QByteArray d;
        QHostAddress de;
        quint16 puerto = 0;
        d.resize(static_cast<int>(camara.pendingDatagramSize()));
        camara.readDatagram(d.data(), d.size(), &de, &puerto);
        sondeo = d;
        camara.writeDatagram(kEncontrada, de, puerto);
    });
    OnvifBusqueda b;
    b.setDestino("127.0.0.1", camara.localPort());
    b.buscar(800);
    CHECK(esperar([&]() { return !b.buscando(); }), "WS-Discovery: termina");
    CHECK(sondeo.contains("Probe"), "WS-Discovery: manda el Probe");
    const QVariantList e = b.encontradas();
    CHECK(e.size() == 1 && e[0].toMap()["host"] == "192.168.1.64" && e[0].toMap()["nombre"] == "PTZ Atril",
          "WS-Discovery: encuentra la cámara");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    pruebaVisca();
    pruebaPanasonic();
    pruebaOnvif();
    pruebaPosiciones();
    pruebaViscaSony();
    pruebaViscaTcp();
    pruebaPanasonicHttp();
    pruebaOnvifHttp();
    pruebaBusqueda();
    std::printf(fallos == 0 ? "OK: PTZ\n" : "%d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
