// test_facebook.cpp
//
// Cliente de Facebook contra un Graph FALSO local (sin red ni cuenta real):
//   1. App de Meta: ID y clave validos/invalidos; solo-dueño.
//   2. Login: dialogo con app, redireccion a localhost:<puerto>, permisos de
//      Paginas y video, state; codigo -> token corto -> token largo ->
//      usuario -> Paginas (con sus tokens, que NO salen por pages()).
//   3. Vivo: crear (RTMPS separado en servidor + clave), estado, publicar,
//      terminar, borrar; la llamada usa el token de la PAGINA.
//   4. Comentarios (de viejo a nuevo) y comentar.
//   5. Videos: listar, editar; subida por partes (start/transfer/finish) con
//      progreso y el archivo completo reconstruido del lado del servidor.
//   6. Token invalido (190) -> desconectado; logout revoca permisos.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

#include <cstdio>
#include <functional>
#include <memory>

#include "facebook/FacebookClient.hpp"
#include "net/Credentials.hpp"

using ba::app::FacebookClient;

// Permisos POSIX 0600: en Windows (NTFS) Qt no los refleja; ahi la carpeta
// Credenciales/ queda protegida por la ACL del usuario.
#ifdef Q_OS_WIN
constexpr bool kPosixPerms = false;
#else
constexpr bool kPosixPerms = true;
#endif
namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

bool waitFor(const std::function<bool()>& cond, int ms = 8000) {
    for (int i = 0; i < ms / 10 && !cond(); ++i) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return cond();
}

QByteArray reply(int code, const QJsonObject& o) {
    const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return "HTTP/1.1 " + QByteArray::number(code) +
           " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\n\r\n" + body;
}

quint16 freePort() {
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_facebook ==\n");
    QTemporaryDir dir;

    QTcpServer graph;
    graph.listen(QHostAddress::LocalHost, 0);
    QString lastQuery, lastPath, lastBody;
    QByteArray assembled;
    bool revoked = false;
    bool invalid = false;
    QObject::connect(&graph, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket* s = graph.nextPendingConnection()) {
            auto buf = std::make_shared<QByteArray>();
            QObject::connect(s, &QTcpSocket::readyRead, [&, s, buf]() {
                *buf += s->readAll();
                const int end = buf->indexOf("\r\n\r\n");
                if (end < 0) return;
                int len = 0;
                for (const QByteArray& l : buf->left(end).split('\n'))
                    if (l.toLower().startsWith("content-length:")) len = l.mid(15).trimmed().toInt();
                if (buf->size() < end + 4 + len) return;
                const QByteArray headers = buf->left(end);
                const QByteArray body = buf->mid(end + 4, len);
                const QList<QByteArray> first = headers.left(headers.indexOf("\r\n")).split(' ');
                const QUrl u("http://x" + QString::fromLatin1(first.value(1)));
                const QString path = u.path();
                const QByteArray method = first.value(0);
                const QUrlQuery q(headers.contains("x-www-form-urlencoded") ? QString::fromUtf8(body)
                                                                            : u.query());
                lastPath = path;
                lastQuery = q.toString(QUrl::FullyDecoded);
                lastBody = QString::fromUtf8(body);
                const QString token = q.queryItemValue("access_token", QUrl::FullyDecoded);
                QByteArray out;
                if (invalid) {
                    out = reply(400, {{"error", QJsonObject{{"code", 190},
                                                            {"message", "Error validating access token"}}}});
                } else if (path == "/oauth/access_token") {
                    if (q.queryItemValue("grant_type") == "fb_exchange_token")
                        out = reply(200, {{"access_token", "LONG"}, {"expires_in", 5184000}});
                    else if (q.queryItemValue("code") == "C1")
                        out = reply(200, {{"access_token", "SHORT"}, {"expires_in", 3600}});
                    else
                        out = reply(400, {{"error", QJsonObject{{"code", 100}, {"message", "bad code"}}}});
                } else if (path == "/me/permissions" && method == "DELETE") {
                    revoked = true;
                    out = reply(200, {{"success", true}});
                } else if (path == "/me") {
                    out = reply(200, {{"id", "U9"}, {"name", "Jorge"}});
                } else if (path == "/me/accounts") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "P1"},
                                                                      {"name", "Mi Pagina"},
                                                                      {"access_token", "PAGETOKEN"},
                                                                      {"category", "Media"}}}}});
                } else if (token != "PAGETOKEN" && !headers.contains("multipart/form-data")) {
                    out = reply(400,
                                {{"error", QJsonObject{{"code", 200}, {"message", "wrong token " + token}}}});
                } else if (path == "/P1/live_videos" && method == "POST") {
                    out =
                        reply(200, {{"id", "L1"},
                                    {"secure_stream_url",
                                     "rtmps://live-api-s.facebook.com:443/rtmp/FB-L1-0-AbC?s_bl=1&s_ps=1"}});
                } else if (path == "/P1/live_videos") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "L1"},
                                                                      {"title", "Vivo"},
                                                                      {"status", "LIVE"},
                                                                      {"permalink_url", "/P1/videos/L1"}}}}});
                } else if (path == "/L1/comments" && method == "GET") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "c2"},
                                                                      {"from", QJsonObject{{"name", "Beto"}}},
                                                                      {"message", "segundo"}},
                                                          QJsonObject{{"id", "c1"},
                                                                      {"from", QJsonObject{{"name", "Ana"}}},
                                                                      {"message", "primero"}}}}});
                } else if (path == "/L1/comments") {
                    out = reply(200, {{"id", "c3"}});
                } else if (path == "/L1" && method == "GET") {
                    out = reply(200, {{"id", "L1"},
                                      {"status", "LIVE"},
                                      {"live_views", 7},
                                      {"secure_stream_url", "rtmps://x/rtmp/KEY"}});
                } else if ((path == "/L1" || path == "/V5") && (method == "POST" || method == "DELETE")) {
                    out = reply(200, {{"success", true}});
                } else if (path == "/P1/videos" && method == "GET") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "V5"},
                                                                      {"title", "Video"},
                                                                      {"permalink_url", "/P1/videos/V5"}}}}});
                } else if (path == "/P1/videos" && method == "POST") {
                    // Subida por partes
                    if (headers.contains("multipart/form-data")) {
                        const int at = body.indexOf("filename=\"chunk\"");
                        const int start = body.indexOf("\r\n\r\n", at) + 4;
                        const int stop = body.lastIndexOf("\r\n--");
                        assembled += body.mid(start, stop - start);
                        const qint64 next = assembled.size();
                        out = reply(
                            200, {{"start_offset", QString::number(next)},
                                  {"end_offset", QString::number(std::min<qint64>(next + 300000, 700000))}});
                    } else if (q.queryItemValue("upload_phase") == "start") {
                        out = reply(200, {{"upload_session_id", "S1"},
                                          {"video_id", "V7"},
                                          {"start_offset", "0"},
                                          {"end_offset", "300000"}});
                    } else {
                        out = reply(200, {{"success", true}});
                    }
                } else {
                    out = reply(404, {{"error", QJsonObject{{"code", 803}, {"message", "ruta " + path}}}});
                }
                s->write(out);
                s->disconnectFromHost();
            });
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    FacebookClient fb(dir.path());
    FacebookClient::Endpoints ep;
    const QString base = QString("http://127.0.0.1:%1").arg(graph.serverPort());
    ep.dialog = base + "/dialog/oauth";
    ep.graph = base;
    ep.video = base;
    fb.setEndpoints(ep);

    // --- 1) app ------------------------------------------------------------------------
    QString err;
    CHECK(!fb.saveClient("abc", "secreto-largo-1234567", 8791, err), "ID invalido");
    CHECK(!fb.saveClient("123456789012345", "corto", 8791, err), "clave invalida");
    const quint16 port = freePort();
    CHECK(fb.saveClient("123456789012345", "0123456789abcdef0123", port, err), "app valida");
    CHECK(!kPosixPerms || QFile(dir.path() + "/facebook-client.cred").permissions() ==
                              (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                               QFileDevice::WriteUser),
          "guardado solo-dueño");
    {
        QFile raw(dir.path() + "/facebook-client.cred");
        raw.open(QIODevice::ReadOnly);
        CHECK(!raw.readAll().contains("0123456789abcdef0123"), "la clave secreta queda cifrada");
    }

    // --- 2) login -------------------------------------------------------------------------
    QNetworkAccessManager nam;
    QString dialogUrl;
    fb.setBrowserOpener([&](const QString& url) {
        dialogUrl = url;
        const QUrlQuery q{QUrl(url)};
        QUrl back(q.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        back.setQuery("code=C1&state=" + q.queryItemValue("state"));
        QTimer::singleShot(20, [&nam, back]() { nam.get(QNetworkRequest(back)); });
    });
    bool done = false, ok = false;
    fb.login([&](bool o, const QJsonObject&, const QString& e) {
        done = true;
        ok = o;
        err = e;
    });
    CHECK(waitFor([&] { return done; }) && ok, "login completo");
    if (!ok) std::printf("   %s\n", err.toUtf8().constData());
    const QUrlQuery dq{QUrl(dialogUrl)};
    CHECK(dq.queryItemValue("client_id") == "123456789012345" &&
              dq.queryItemValue("redirect_uri", QUrl::FullyDecoded) ==
                  QString("http://localhost:%1/").arg(port) &&
              dq.queryItemValue("scope", QUrl::FullyDecoded).contains("publish_video"),
          "dialogo: app, redireccion local fija y permisos");
    CHECK(fb.loggedIn() && fb.pageId() == "P1" && fb.page().value("name") == "Mi Pagina", "Pagina elegida");
    CHECK(!QJsonDocument(fb.pages()).toJson().contains("PAGETOKEN"),
          "los tokens de Pagina no salen de la capa");

    // --- 3) vivo ----------------------------------------------------------------------------
    QJsonObject data;
    done = false;
    fb.createLive(QJsonObject{{"title", "Prueba"}, {"status", "UNPUBLISHED"}},
                  [&](bool o, const QJsonObject& d, const QString& e) {
                      done = true;
                      ok = o;
                      data = d;
                      err = e;
                  });
    CHECK(waitFor([&] { return done; }) && ok, "crear vivo");
    CHECK(lastQuery.contains("status=UNPUBLISHED") && lastQuery.contains("access_token=PAGETOKEN"),
          "en prueba, con el token de la Pagina");
    CHECK(data["ingestion"].toObject()["url"] == "rtmps://live-api-s.facebook.com:443/rtmp/" &&
              data["ingestion"].toObject()["key"] == "FB-L1-0-AbC?s_bl=1&s_ps=1",
          "RTMPS separado en servidor + clave");
    done = false;
    fb.liveStatus("L1", [&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && data.value("live_views").toInt() == 7, "estado del vivo");
    done = false;
    fb.publishLive("L1", [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok && lastQuery.contains("status=LIVE_NOW"), "publicar");
    done = false;
    fb.endLive("L1", [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok && lastQuery.contains("end_live_video=true"), "terminar");

    // --- 4) comentarios ------------------------------------------------------------------------
    done = false;
    fb.comments("L1", [&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && data["items"].toArray().at(0).toObject()["author"] == "Ana",
          "comentarios de viejo a nuevo");
    done = false;
    fb.comment("L1", "gracias", [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok && lastQuery.contains("message=gracias"), "comentar");

    // --- 5) videos y subida ------------------------------------------------------------------------
    done = false;
    fb.videos(10, [&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) &&
              data["items"].toArray().at(0).toObject()["url"] == "https://www.facebook.com/P1/videos/V5",
          "videos con enlace completo");
    done = false;
    fb.updateVideo("V5", QJsonObject{{"title", "Nuevo"}}, [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok && lastQuery.contains("name=Nuevo"), "editar video");
    const QString file = dir.path() + "/v.mp4";
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        QByteArray bytes(700000, 0);
        for (int i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<char>(i % 251);
        f.write(bytes);
    }
    qint64 last = 0;
    QObject::connect(&fb, &FacebookClient::uploadProgress,
                     [&](const QString&, qint64 s, qint64) { last = s; });
    done = false;
    fb.uploadVideo(file, QJsonObject{{"title", "Grabacion"}},
                   [&](bool o, const QJsonObject& d, const QString& e) {
                       done = true;
                       ok = o;
                       data = d;
                       err = e;
                   });
    CHECK(waitFor([&] { return done; }, 15000) && ok && data["id"] == "V7", "subida por partes");
    if (!ok) std::printf("   %s\n", err.toUtf8().constData());
    QFile orig(file);
    orig.open(QIODevice::ReadOnly);
    CHECK(assembled == orig.readAll() && last == 700000, "el archivo llega completo, en orden, con progreso");
    CHECK(lastQuery.contains("upload_phase=finish") && lastQuery.contains("title=Grabacion"),
          "cierra con titulo");

    // --- 6) token invalido y logout --------------------------------------------------------------------
    invalid = true;
    done = false;
    fb.videos(5, [&](bool o, const QJsonObject&, const QString& e) {
        done = true;
        ok = o;
        err = e;
    });
    CHECK(waitFor([&] { return done; }) && !ok && !fb.loggedIn() && err.contains("conecta la cuenta"),
          "token invalido: desconectado");
    invalid = false;
    fb.saveClient("123456789012345", "0123456789abcdef0123", port, err);
    ba::app::cred::write(
        dir.path(), "facebook-token",
        R"({"user_token":"LONG","pages":[{"id":"P1","access_token":"PAGETOKEN"}],"page_id":"P1"})");
    FacebookClient fb2(dir.path());
    fb2.setEndpoints(ep);
    CHECK(fb2.loggedIn(), "sesion cargada");
    done = false;
    fb2.logout([&](bool, const QJsonObject&, const QString&) { done = true; });
    CHECK(waitFor([&] { return done; }) && revoked && !QFile::exists(dir.path() + "/facebook-token.cred"),
          "logout revoca y borra");

    if (g_failures == 0) {
        std::printf("test_facebook: OK\n");
        return 0;
    }
    std::printf("test_facebook: %d fallo(s)\n", g_failures);
    return 1;
}
