// src/app/red/Rtsp.cpp — ver Rtsp.hpp

#include "red/Rtsp.hpp"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>

namespace ba::app::red::rtsp {

Url partir(const QString& url) {
    Url u;
    const QUrl q(url);
    if (!q.isValid() || (q.scheme() != "rtsp" && q.scheme() != "rtsps") || q.host().isEmpty()) return u;
    u.host = q.host();
    u.puerto = q.port(554);
    u.usuario = q.userName(QUrl::FullyDecoded);
    u.clave = q.password(QUrl::FullyDecoded);
    u.ruta = q.path(QUrl::FullyEncoded);
    if (u.ruta.isEmpty()) u.ruta = "/";
    if (q.hasQuery()) u.ruta += "?" + q.query(QUrl::FullyEncoded);
    u.ok = true;
    return u;
}

QString sinClave(const QString& url) {
    QUrl q(url);
    q.setUserInfo({});
    return q.toString();
}

QString conClave(const QString& url, const QString& usuario, const QString& clave) {
    if (usuario.isEmpty()) return sinClave(url);
    QUrl q(url);
    q.setUserName(usuario);
    q.setPassword(clave);
    return q.toString(QUrl::FullyEncoded);
}

Respuesta leer(const QByteArray& datos) {
    Respuesta r;
    const int fin = datos.indexOf("\r\n\r\n");
    if (fin < 0) return r;
    const QString cab = QString::fromLatin1(datos.left(fin));
    const QStringList lineas = cab.split("\r\n");
    if (lineas.isEmpty()) return r;
    const QStringList primera = lineas[0].split(' ');
    if (primera.size() < 2 || !primera[0].startsWith("RTSP/")) return r;
    r.codigo = primera[1].toInt();
    int largo = 0;
    for (const QString& l : lineas.mid(1)) {
        const int dos = l.indexOf(':');
        if (dos < 0) continue;
        const QString k = l.left(dos).trimmed().toLower(), v = l.mid(dos + 1).trimmed();
        if (k == "content-length") largo = v.toInt();
        if (k == "www-authenticate") {
            const QString esquema = v.section(' ', 0, 0);
            // Si ofrece los dos, mejor Digest (la clave no viaja).
            if (r.esquema.isEmpty() || esquema.compare("Digest", Qt::CaseInsensitive) == 0) {
                r.esquema = esquema;
                const auto campo = [&](const char* n) {
                    const QRegularExpression re(QString(n) + R"re(\s*=\s*"([^"]*)")re",
                                                QRegularExpression::CaseInsensitiveOption);
                    const auto m = re.match(v);
                    return m.hasMatch() ? m.captured(1) : QString();
                };
                r.realm = campo("realm");
                r.nonce = campo("nonce");
            }
        }
    }
    const QByteArray cuerpo = datos.mid(fin + 4);
    r.completa = cuerpo.size() >= largo;
    r.sdp = cuerpo.left(largo);
    const QString sdp = QString::fromLatin1(r.sdp);
    const auto rtpmap = QRegularExpression(R"(a=rtpmap:\d+\s+([A-Za-z0-9\-]+)/)").match(sdp);
    if (rtpmap.hasMatch()) r.codec = rtpmap.captured(1).toUpper();
    if (r.codec.isEmpty() && sdp.contains("m=video 0 RTP/AVP 26")) r.codec = "JPEG";
    const auto dim = QRegularExpression(R"((?:x-dimensions:|framesize:\d+\s+)(\d+)[,\-x](\d+))").match(sdp);
    if (dim.hasMatch()) {
        r.ancho = dim.captured(1).toInt();
        r.alto = dim.captured(2).toInt();
    }
    return r;
}

QByteArray describe(const QString& url, int cseq, const QString& autorizacion) {
    QByteArray p = "DESCRIBE " + sinClave(url).toUtf8() + " RTSP/1.0\r\nCSeq: " + QByteArray::number(cseq) +
                   "\r\nAccept: application/sdp\r\nUser-Agent: BroadcastAppliance\r\n";
    if (!autorizacion.isEmpty()) p += "Authorization: " + autorizacion.toUtf8() + "\r\n";
    return p + "\r\n";
}

QString autorizacion(const Respuesta& desafio, const QString& url, const QString& usuario,
                     const QString& clave) {
    if (desafio.esquema.compare("Basic", Qt::CaseInsensitive) == 0)
        return "Basic " + QString::fromLatin1((usuario + ":" + clave).toUtf8().toBase64());
    const auto md5 = [](const QString& s) {
        return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex());
    };
    const QString uri = sinClave(url);
    const QString ha1 = md5(usuario + ":" + desafio.realm + ":" + clave);
    const QString ha2 = md5("DESCRIBE:" + uri);
    const QString resp = md5(ha1 + ":" + desafio.nonce + ":" + ha2);
    return QString(R"(Digest username="%1", realm="%2", nonce="%3", uri="%4", response="%5")")
        .arg(usuario, desafio.realm, desafio.nonce, uri, resp);
}

QString estadoDe(int codigo) {
    if (codigo == 200) return "se ve";
    if (codigo == 401) return "pide usuario y clave";
    if (codigo == 403) return "clave incorrecta o sin permiso";
    if (codigo == 404 || codigo == 400 || codigo == 454) return "no existe";
    if (codigo == 0) return "no contesta";
    return "contestó " + QString::number(codigo);
}

} // namespace ba::app::red::rtsp
