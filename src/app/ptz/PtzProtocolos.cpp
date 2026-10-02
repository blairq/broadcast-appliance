// src/app/ptz/PtzProtocolos.cpp — ver PtzProtocolos.hpp

#include "ptz/PtzProtocolos.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimeZone>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace ba::app::ptz {

namespace {

char byteDir(int direccion) { return static_cast<char>(0x80 | std::clamp(direccion, 1, 7)); }

/// |v| (0..1) a un entero entre lo y hi.
int escalar(double v, int lo, int hi) {
    const double a = std::clamp(std::fabs(v), 0.0, 1.0);
    return lo + static_cast<int>(std::lround(a * (hi - lo)));
}

QByteArray bytes(std::initializer_list<int> b) {
    QByteArray out;
    for (int x : b) out.append(static_cast<char>(x & 0xFF));
    return out;
}

QString num(double v) { return QString::number(std::clamp(v, -1.0, 1.0), 'f', 3); }

QString esc(const QString& s) { return s.toHtmlEscaped(); }

} // namespace

// ===========================================================================
// VISCA
// ===========================================================================
namespace visca {

QByteArray panTilt(double pan, double tilt, int direccion) {
    // Con un eje quieto se manda igual una velocidad válida (01): algunas
    // cámaras rechazan 00 aunque la dirección diga «quieto».
    const int vv = pan == 0 ? 1 : escalar(pan, 1, 0x18);
    const int ww = tilt == 0 ? 1 : escalar(tilt, 1, 0x14);
    const int xx = pan > 0 ? 0x02 : pan < 0 ? 0x01 : 0x03;
    const int yy = tilt > 0 ? 0x01 : tilt < 0 ? 0x02 : 0x03;
    QByteArray b = bytes({0, 0x01, 0x06, 0x01, vv, ww, xx, yy, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray zoom(double v, int direccion) {
    const int p = escalar(v, 0, 7);
    QByteArray b = bytes({0, 0x01, 0x04, 0x07, v > 0 ? 0x20 | p : v < 0 ? 0x30 | p : 0x00, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray foco(double v, int direccion) {
    const int p = escalar(v, 0, 7);
    QByteArray b = bytes({0, 0x01, 0x04, 0x08, v > 0 ? 0x20 | p : v < 0 ? 0x30 | p : 0x00, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray focoAuto(bool automatico, int direccion) {
    QByteArray b = bytes({0, 0x01, 0x04, 0x38, automatico ? 0x02 : 0x03, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray focoUnToque(int direccion) {
    QByteArray b = bytes({0, 0x01, 0x04, 0x18, 0x01, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray preset(bool guardar, int numero, int direccion) {
    QByteArray b = bytes({0, 0x01, 0x04, 0x3F, guardar ? 0x01 : 0x02, std::clamp(numero - 1, 0, 127), 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray borrarPreset(int numero, int direccion) {
    QByteArray b = bytes({0, 0x01, 0x04, 0x3F, 0x00, std::clamp(numero - 1, 0, 127), 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray home(int direccion) {
    QByteArray b = bytes({0, 0x01, 0x06, 0x04, 0xFF});
    b[0] = byteDir(direccion);
    return b;
}

QByteArray conEncabezado(std::uint16_t tipo, const QByteArray& visca, std::uint32_t secuencia) {
    QByteArray b;
    b.append(static_cast<char>(tipo >> 8));
    b.append(static_cast<char>(tipo & 0xFF));
    b.append(static_cast<char>((visca.size() >> 8) & 0xFF));
    b.append(static_cast<char>(visca.size() & 0xFF));
    for (int s = 24; s >= 0; s -= 8) b.append(static_cast<char>((secuencia >> s) & 0xFF));
    return b + visca;
}

QByteArray reiniciarSecuencia() { return bytes({0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01}); }

Respuesta leer(const QByteArray& datos) {
    QByteArray m = datos;
    // Con encabezado de Sony: 01 11 (respuesta) o 02 01 (respuesta de control).
    if (m.size() >= 8 && ((static_cast<unsigned char>(m[0]) == 0x01 && static_cast<unsigned char>(m[1]) == 0x11) ||
                          (static_cast<unsigned char>(m[0]) == 0x02 && static_cast<unsigned char>(m[1]) == 0x01))) {
        const bool control = static_cast<unsigned char>(m[0]) == 0x02;
        m = m.mid(8);
        if (control) return {Respuesta::Recibida, 0};
    }
    if (m.size() < 3 || (static_cast<unsigned char>(m[0]) & 0xF0) != 0x90) return {};
    switch (static_cast<unsigned char>(m[1]) & 0xF0) {
    case 0x40: return {Respuesta::Recibida, 0};
    case 0x50: return {Respuesta::Hecha, 0};
    case 0x60: return {Respuesta::Error, static_cast<unsigned char>(m[2])};
    default: return {};
    }
}

QString textoError(int codigo) {
    switch (codigo) {
    case 0x01: return QStringLiteral("largo de mensaje inválido");
    case 0x02: return QStringLiteral("orden que la cámara no entiende");
    case 0x03: return QStringLiteral("la cámara está ocupada (buffer lleno)");
    case 0x04: return QStringLiteral("orden cancelada");
    case 0x05: return QStringLiteral("no hay socket");
    case 0x41: return QStringLiteral("la cámara no puede hacer eso ahora");
    default: return QStringLiteral("error VISCA %1").arg(codigo, 2, 16, QLatin1Char('0'));
    }
}

} // namespace visca

// ===========================================================================
// Panasonic AW
// ===========================================================================
namespace panasonic {

namespace {
int velocidad(double v) {
    return std::clamp(50 + static_cast<int>(std::lround(std::clamp(v, -1.0, 1.0) * 49)), 1, 99);
}
QString dos(int n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); }
} // namespace

QString panTilt(double pan, double tilt) { return "#PTS" + dos(velocidad(pan)) + dos(velocidad(tilt)); }
QString zoom(double v) { return "#Z" + dos(velocidad(v)); }
QString foco(double v) { return "#F" + dos(velocidad(v)); }
QString focoAuto(bool automatico) { return automatico ? QStringLiteral("#D11") : QStringLiteral("#D10"); }
QString preset(bool guardar, int numero) {
    return (guardar ? "#M" : "#R") + dos(std::clamp(numero - 1, 0, 99));
}
QString borrarPreset(int numero) { return "#C" + dos(std::clamp(numero - 1, 0, 99)); }
QString home() { return QStringLiteral("#APC80008000"); }

QString url(const QString& host, int puerto, const QString& orden) {
    const QString h = puerto > 0 && puerto != 80 ? host + ":" + QString::number(puerto) : host;
    return "http://" + h + "/cgi-bin/aw_ptz?cmd=" + QString::fromLatin1(QUrl::toPercentEncoding(orden)) + "&res=1";
}

bool respuestaOk(const QString& orden, const QByteArray& respuesta) {
    const QString r = QString::fromLatin1(respuesta).trimmed();
    if (r.isEmpty() || r.startsWith("er", Qt::CaseInsensitive)) return false;
    // La respuesta repite la orden sin «#» y con la primera letra en
    // minúscula (#PTS5050 -> pTS5050); los presets contestan «s01». Se acepta
    // lo que no sea un error: hay modelos que contestan distinto.
    Q_UNUSED(orden);
    return true;
}

} // namespace panasonic

// ===========================================================================
// ONVIF
// ===========================================================================
namespace onvif {

namespace {

const char* kNs =
    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
    "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
    "xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\"";

const char* kWsse = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
const char* kWsu = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";
const char* kDigest =
    "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest";
const char* kBase64 =
    "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary";

QString perfilXml(const QString& perfil) {
    return "<tptz:ProfileToken>" + esc(perfil) + "</tptz:ProfileToken>";
}

/// El texto del primer elemento con ese nombre local (o vacío).
QString primerTexto(const QByteArray& xml, const QString& nombre) {
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement && r.name() == nombre)
            return r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
    }
    return {};
}

} // namespace

QByteArray digest(const QByteArray& nonce, const QString& creado, const QString& clave) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(nonce);
    h.addData(creado.toUtf8());
    h.addData(clave.toUtf8());
    return h.result().toBase64();
}

QByteArray sobre(const QString& cuerpo, const QString& usuario, const QString& clave, qint64 desfaseSeg,
                 const QByteArray& nonceFijo, const QString& creadoFijo) {
    QString cab;
    if (!usuario.isEmpty()) {
        QByteArray nonce = nonceFijo;
        if (nonce.isEmpty()) {
            nonce.resize(16);
            for (int i = 0; i < 16; ++i) nonce[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        const QString creado =
            creadoFijo.isEmpty()
                ? QDateTime::currentDateTimeUtc().addSecs(desfaseSeg).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"))
                : creadoFijo;
        cab = QStringLiteral("<s:Header><wsse:Security s:mustUnderstand=\"1\" xmlns:wsse=\"%1\" xmlns:wsu=\"%2\">"
                             "<wsse:UsernameToken><wsse:Username>%3</wsse:Username>"
                             "<wsse:Password Type=\"%4\">%5</wsse:Password>"
                             "<wsse:Nonce EncodingType=\"%6\">%7</wsse:Nonce>"
                             "<wsu:Created>%8</wsu:Created></wsse:UsernameToken></wsse:Security></s:Header>")
                  .arg(kWsse, kWsu, esc(usuario), kDigest, QString::fromLatin1(digest(nonce, creado, clave)), kBase64,
                       QString::fromLatin1(nonce.toBase64()), creado);
    }
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><s:Envelope %1>%2<s:Body>%3</s:Body></s:Envelope>")
        .arg(kNs, cab, cuerpo)
        .toUtf8();
}

QString getSystemDateAndTime() { return QStringLiteral("<tds:GetSystemDateAndTime/>"); }
QString getCapabilities() {
    return QStringLiteral("<tds:GetCapabilities><tds:Category>All</tds:Category></tds:GetCapabilities>");
}
QString getProfiles() { return QStringLiteral("<trt:GetProfiles/>"); }

QString continuousMove(const QString& perfil, double pan, double tilt, double zoom) {
    return "<tptz:ContinuousMove>" + perfilXml(perfil) + "<tptz:Velocity><tt:PanTilt x=\"" + num(pan) + "\" y=\"" +
           num(tilt) + "\"/><tt:Zoom x=\"" + num(zoom) + "\"/></tptz:Velocity></tptz:ContinuousMove>";
}

QString stop(const QString& perfil) {
    return "<tptz:Stop>" + perfilXml(perfil) + "<tptz:PanTilt>true</tptz:PanTilt><tptz:Zoom>true</tptz:Zoom></tptz:Stop>";
}

QString getPresets(const QString& perfil) { return "<tptz:GetPresets>" + perfilXml(perfil) + "</tptz:GetPresets>"; }

QString gotoPreset(const QString& perfil, const QString& preset) {
    return "<tptz:GotoPreset>" + perfilXml(perfil) + "<tptz:PresetToken>" + esc(preset) +
           "</tptz:PresetToken></tptz:GotoPreset>";
}

QString setPreset(const QString& perfil, const QString& nombre, const QString& preset) {
    QString b = "<tptz:SetPreset>" + perfilXml(perfil) + "<tptz:PresetName>" + esc(nombre) + "</tptz:PresetName>";
    if (!preset.isEmpty()) b += "<tptz:PresetToken>" + esc(preset) + "</tptz:PresetToken>";
    return b + "</tptz:SetPreset>";
}

QString gotoHomePosition(const QString& perfil) {
    return "<tptz:GotoHomePosition>" + perfilXml(perfil) + "</tptz:GotoHomePosition>";
}

QString removePreset(const QString& perfil, const QString& preset) {
    return "<tptz:RemovePreset>" + perfilXml(perfil) + "<tptz:PresetToken>" + esc(preset) +
           "</tptz:PresetToken></tptz:RemovePreset>";
}

QString focoMover(const QString& fuenteVideo, double v) {
    return "<timg:Move><timg:VideoSourceToken>" + esc(fuenteVideo) +
           "</timg:VideoSourceToken><timg:Focus><tt:Continuous><tt:Speed>" + num(v) +
           "</tt:Speed></tt:Continuous></timg:Focus></timg:Move>";
}

QString focoParar(const QString& fuenteVideo) {
    return "<timg:Stop><timg:VideoSourceToken>" + esc(fuenteVideo) + "</timg:VideoSourceToken></timg:Stop>";
}

Servicios leerCapacidades(const QByteArray& xml) {
    Servicios s;
    QXmlStreamReader r(xml);
    QStringList pila;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (r.name() == QLatin1String("XAddr") && !pila.isEmpty()) {
                const QString padre = pila.last();
                const QString dir = r.readElementText().trimmed();
                if (padre == "PTZ" && s.ptz.isEmpty()) s.ptz = dir;
                else if (padre == "Media" && s.media.isEmpty()) s.media = dir;
                else if (padre == "Imaging" && s.imaging.isEmpty()) s.imaging = dir;
                continue; // readElementText ya consumió el fin del elemento
            }
            pila << r.name().toString();
        } else if (t == QXmlStreamReader::EndElement && !pila.isEmpty()) {
            pila.removeLast();
        }
    }
    return s;
}

QDateTime leerHora(const QByteArray& xml) {
    QXmlStreamReader r(xml);
    bool enUtc = false;
    int a = 0, me = 0, d = 0, h = 0, mi = 0, se = 0;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            if (n == QLatin1String("UTCDateTime")) enUtc = true;
            else if (enUtc && n == QLatin1String("Year")) a = r.readElementText().toInt();
            else if (enUtc && n == QLatin1String("Month")) me = r.readElementText().toInt();
            else if (enUtc && n == QLatin1String("Day")) d = r.readElementText().toInt();
            else if (enUtc && n == QLatin1String("Hour")) h = r.readElementText().toInt();
            else if (enUtc && n == QLatin1String("Minute")) mi = r.readElementText().toInt();
            else if (enUtc && n == QLatin1String("Second")) se = r.readElementText().toInt();
        } else if (t == QXmlStreamReader::EndElement && r.name() == QLatin1String("UTCDateTime")) {
            break;
        }
    }
    if (a == 0) return {};
    return QDateTime(QDate(a, me, d), QTime(h, mi, se), QTimeZone::UTC);
}

QList<Perfil> leerPerfiles(const QByteArray& xml) {
    QList<Perfil> out;
    QXmlStreamReader r(xml);
    Perfil p;
    int profundidad = 0; // dentro de un Profiles: 1 = sus hijos directos
    bool en = false, enFuente = false;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            if (!en && n == QLatin1String("Profiles")) {
                en = true;
                profundidad = 0;
                p = Perfil{};
                p.token = r.attributes().value("token").toString();
                continue;
            }
            if (!en) continue;
            ++profundidad;
            if (profundidad == 1 && n == QLatin1String("Name")) {
                p.nombre = r.readElementText().trimmed();
                --profundidad;
            } else if (profundidad == 1 && n == QLatin1String("PTZConfiguration")) {
                p.tienePtz = true;
            } else if (profundidad == 1 && n == QLatin1String("VideoSourceConfiguration")) {
                enFuente = true;
            } else if (enFuente && profundidad == 2 && n == QLatin1String("SourceToken")) {
                p.fuenteVideo = r.readElementText().trimmed();
                --profundidad;
            }
        } else if (t == QXmlStreamReader::EndElement && en) {
            if (profundidad == 0 && r.name() == QLatin1String("Profiles")) {
                out << p;
                en = false;
                continue;
            }
            if (profundidad == 1 && r.name() == QLatin1String("VideoSourceConfiguration")) enFuente = false;
            --profundidad;
        }
    }
    return out;
}

QList<Preset> leerPresets(const QByteArray& xml) {
    QList<Preset> out;
    QXmlStreamReader r(xml);
    bool en = false;
    Preset p;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (r.name() == QLatin1String("Preset")) {
                en = true;
                p = Preset{r.attributes().value("token").toString(), {}};
            } else if (en && r.name() == QLatin1String("Name") && p.nombre.isEmpty()) {
                p.nombre = r.readElementText().trimmed();
            }
        } else if (t == QXmlStreamReader::EndElement && r.name() == QLatin1String("Preset") && en) {
            out << p;
            en = false;
        }
    }
    return out;
}

QString leerTokenPreset(const QByteArray& xml) { return primerTexto(xml, QStringLiteral("PresetToken")); }

QString leerFalla(const QByteArray& xml) {
    if (!xml.contains("Fault")) return {};
    QString t = primerTexto(xml, QStringLiteral("Text"));
    if (t.isEmpty()) t = primerTexto(xml, QStringLiteral("Reason"));
    if (t.isEmpty()) t = primerTexto(xml, QStringLiteral("Value"));
    return t.isEmpty() ? QStringLiteral("la cámara contestó un error SOAP") : t;
}

QByteArray sondeo(const QString& idMensaje) {
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
               "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
               "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
               "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
               "<e:Header><w:MessageID>uuid:%1</w:MessageID>"
               "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
               "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
               "</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body>"
               "</e:Envelope>")
        .arg(idMensaje)
        .toUtf8();
}

Encontrada leerEncontrada(const QByteArray& xml) {
    Encontrada e;
    if (!xml.contains("ProbeMatch")) return e;
    const QStringList dirs = primerTexto(xml, QStringLiteral("XAddrs")).split(' ', Qt::SkipEmptyParts);
    // Si hay varias (IPv4, IPv6, otra placa), la primera http con IPv4.
    for (const QString& d : dirs) {
        if (!d.contains('[')) {
            e.direccion = d;
            break;
        }
    }
    if (e.direccion.isEmpty() && !dirs.isEmpty()) e.direccion = dirs.first();
    for (const QString& s : primerTexto(xml, QStringLiteral("Scopes")).split(' ', Qt::SkipEmptyParts)) {
        static const QString kNombre = QStringLiteral("onvif://www.onvif.org/name/");
        static const QString kModelo = QStringLiteral("onvif://www.onvif.org/hardware/");
        if (s.startsWith(kNombre)) e.nombre = QUrl::fromPercentEncoding(s.mid(kNombre.size()).toUtf8());
        else if (s.startsWith(kModelo)) e.modelo = QUrl::fromPercentEncoding(s.mid(kModelo.size()).toUtf8());
    }
    return e;
}

} // namespace onvif

} // namespace ba::app::ptz
