// test_youtube.cpp
//
// Cliente de YouTube contra un Google FALSO local (sin red ni cuenta real):
//   1. Cliente OAuth: acepta el JSON de la consola (tipo escritorio), rechaza
//      "Aplicacion web" y basura; se guarda solo-dueño.
//   2. Login: abre el "navegador" con client_id, redirect a 127.0.0.1,
//      scope, prompt=select_account, PKCE S256 y state; el canje manda el
//      verificador que corresponde al challenge; refresh token guardado
//      0600; el canal queda como cuenta.
//   3. state equivocado -> ignorado; access_denied -> error claro.
//   4. Token vencido (401) -> renueva y repite una vez.
//   5. Videos: lista (subidas + detalles, en orden); editar conserva la
//      categoria y lo no pedido; borrar.
//   6. En vivo: crear = evento + stream + vinculo + datos de ingesta.
//   7. Subida reanudable con progreso (el archivo llega completo).
//   8. invalid_grant al renovar -> queda desconectado; logout revoca y borra.
//   9. HttpsClient: chunked y un HTTPS real con las raices del bundle (si hay
//      red; si no, se informa y no falla).

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
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

#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"
#include "youtube/YouTubeClient.hpp"

using ba::app::YouTubeClient;

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

void spin(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()>& cond, int ms = 5000) {
    for (int i = 0; i < ms / 10 && !cond(); ++i)
        spin(10);
    return cond();
}

/// Google falso: un pedido por conexion (como hace el cliente).
struct FakeGoogle : QObject {
    QTcpServer server;
    QString challenge;
    QString lastBody;
    QString lastPath;
    int expireNextApiCall = 0; // cuantas llamadas a la API responden 401
    bool revoked = false;
    QByteArray uploaded;
    bool bound = false;
    std::function<QByteArray(const QByteArray& method, const QString& path, const QUrlQuery& q,
                             const QByteArray& body, const QByteArray& headers)>
        route;

    FakeGoogle() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() {
                    *buf += s->readAll();
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray headers = buf->left(end);
                    int len = 0;
                    for (const QByteArray& l : headers.split('\n'))
                        if (l.toLower().startsWith("content-length:")) len = l.mid(15).trimmed().toInt();
                    if (buf->size() < end + 4 + len) return;
                    const QByteArray body = buf->mid(end + 4, len);
                    const QList<QByteArray> first = headers.left(headers.indexOf("\r\n")).split(' ');
                    const QUrl u("http://x" + QString::fromLatin1(first.value(1)));
                    const QByteArray resp = route(first.value(0), u.path(), QUrlQuery(u), body, headers);
                    s->write(resp);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    quint16 port() const { return server.serverPort(); }
    QString base() const { return QString("http://127.0.0.1:%1").arg(port()); }
};

QByteArray reply(int code, const QJsonObject& o, const QByteArray& extra = {}) {
    const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return "HTTP/1.1 " + QByteArray::number(code) +
           " X\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\n" + extra + "\r\n" + body;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_youtube ==\n");
    QTemporaryDir dir;
    FakeGoogle g;

    g.route = [&g](const QByteArray& method, const QString& path, const QUrlQuery& q, const QByteArray& body,
                   const QByteArray& headers) -> QByteArray {
        g.lastPath = path;
        g.lastBody = QString::fromUtf8(body);
        const bool auth = headers.contains("Authorization: Bearer AT");
        if (path == "/token") {
            const QUrlQuery f(QString::fromUtf8(body));
            if (f.queryItemValue("grant_type") == "authorization_code") {
                const QByteArray chall =
                    QCryptographicHash::hash(f.queryItemValue("code_verifier").toLatin1(),
                                             QCryptographicHash::Sha256)
                        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                if (f.queryItemValue("code") != "CODE123" || QString::fromLatin1(chall) != g.challenge)
                    return reply(400, {{"error", "invalid_grant"}});
                return reply(200, {{"access_token", "AT1"}, {"refresh_token", "RT1"}, {"expires_in", 3600}});
            }
            if (f.queryItemValue("refresh_token") == "RT1")
                return reply(200, {{"access_token", "AT2"}, {"expires_in", 3600}});
            return reply(400, {{"error", "invalid_grant"},
                               {"error_description", "Token has been expired or revoked."}});
        }
        if (path == "/revoke") {
            g.revoked = true;
            return reply(200, {});
        }
        if (path.startsWith("/upload-session")) {
            g.uploaded = body;
            return reply(201, {{"id", "V9"}});
        }
        if (path == "/upload/videos") {
            return reply(200, {}, "Location: " + g.base().toUtf8() + "/upload-session/1\r\n");
        }
        if (!auth) return reply(401, {{"error", QJsonObject{{"code", 401}, {"message", "no auth"}}}});
        if (g.expireNextApiCall > 0) {
            --g.expireNextApiCall;
            return reply(401, {{"error", QJsonObject{{"code", 401}, {"message", "expired"}}}});
        }
        if (path == "/api/channels")
            return reply(
                200, {{"items", QJsonArray{QJsonObject{
                                    {"id", "UC1"},
                                    {"snippet", QJsonObject{{"title", "Mi Canal"}}},
                                    {"contentDetails",
                                     QJsonObject{{"relatedPlaylists", QJsonObject{{"uploads", "UU1"}}}}}}}}});
        if (path == "/api/playlistItems")
            return reply(
                200,
                {{"items", QJsonArray{QJsonObject{{"contentDetails", QJsonObject{{"videoId", "vB"}}}},
                                      QJsonObject{{"contentDetails", QJsonObject{{"videoId", "vA"}}}}}}});
        if (path == "/api/videos" && method == "GET") {
            const auto vid = [](const char* id, const char* title) {
                return QJsonObject{{"id", id},
                                   {"snippet", QJsonObject{{"title", title},
                                                           {"description", "d"},
                                                           {"categoryId", "10"},
                                                           {"tags", QJsonArray{"x"}}}},
                                   {"status", QJsonObject{{"privacyStatus", "private"}}},
                                   {"statistics", QJsonObject{{"viewCount", "7"}}}};
            };
            return reply(200, {{"items", QJsonArray{vid("vA", "Video A"), vid("vB", "Video B")}}});
        }
        if (path == "/api/videos" && method == "PUT")
            return reply(200, QJsonDocument::fromJson(body).object());
        if (path == "/api/videos" && method == "DELETE")
            return "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
        if (path == "/api/liveBroadcasts" && method == "POST")
            return reply(200, {{"id", "B1"},
                               {"snippet", QJsonObject{{"title", "Evento"}}},
                               {"status", QJsonObject{{"privacyStatus", "private"}}}});
        if (path == "/api/liveBroadcasts" && method == "GET")
            return reply(
                200,
                {{"items", QJsonArray{QJsonObject{
                               {"id", "B1"},
                               {"contentDetails", QJsonObject{{"boundStreamId", g.bound ? "S1" : ""}}}}}}});
        if (path == "/api/liveStreams" && method == "POST") return reply(200, {{"id", "S1"}});
        if (path == "/api/liveBroadcasts/bind") {
            g.bound = q.queryItemValue("streamId") == "S1";
            return reply(200, {});
        }
        if (path == "/api/liveStreams" && method == "GET")
            return reply(
                200,
                {{"items",
                  QJsonArray{QJsonObject{
                      {"id", "S1"},
                      {"cdn", QJsonObject{{"ingestionInfo", QJsonObject{{"ingestionAddress",
                                                                         "rtmp://a.rtmp.youtube.com/live2"},
                                                                        {"streamName", "abcd-1234"}}}}},
                      {"status", QJsonObject{{"streamStatus", "ready"}}}}}}});
        return reply(404, {{"error", QJsonObject{{"code", 404}, {"message", "ruta"}}}});
    };

    YouTubeClient yt(dir.path());
    YouTubeClient::Endpoints ep;
    ep.auth = g.base() + "/auth";
    ep.token = g.base() + "/token";
    ep.revoke = g.base() + "/revoke";
    ep.api = g.base() + "/api";
    ep.upload = g.base() + "/upload";
    yt.setEndpoints(ep);

    // --- 1) cliente OAuth ------------------------------------------------------
    QString err;
    CHECK(!yt.saveClient(R"({"web":{"client_id":"1.apps.googleusercontent.com","client_secret":"s"}})", {},
                         err) &&
              err.contains("escritorio"),
          "un cliente 'Aplicacion web' se rechaza con explicacion");
    CHECK(!yt.saveClient("basura", {}, err), "basura se rechaza");
    CHECK(
        yt.saveClient(
            R"({"installed":{"client_id":"123-abc.apps.googleusercontent.com","client_secret":"GOCSPX-x"}})",
            {}, err),
        "acepta el JSON de App de escritorio");
    CHECK(!kPosixPerms || QFile(dir.path() + "/youtube-client.cred").permissions() ==
                              (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                               QFileDevice::WriteUser),
          "el cliente se guarda solo-dueño");
    {
        QFile raw(dir.path() + "/youtube-client.cred");
        raw.open(QIODevice::ReadOnly);
        CHECK(!raw.readAll().contains("GOCSPX-x"), "el secreto del cliente queda cifrado (no en claro)");
    }

    // --- 2) login -------------------------------------------------------------
    QNetworkAccessManager nam;
    QUrl seenAuth;
    auto browser = [&](const QString& code, bool goodState, const QString& error = {}) {
        yt.setBrowserOpener([&, code, goodState, error](const QUrl& u) {
            seenAuth = u;
            const QUrlQuery q(u);
            g.challenge = q.queryItemValue("code_challenge");
            QUrl back(q.queryItemValue("redirect_uri"));
            QUrlQuery b;
            if (!error.isEmpty())
                b.addQueryItem("error", error);
            else
                b.addQueryItem("code", code);
            b.addQueryItem("state", goodState ? q.queryItemValue("state") : "otro");
            back.setQuery(b);
            QTimer::singleShot(20, [&nam, back]() { nam.get(QNetworkRequest(back)); });
        });
    };
    browser("CODE123", true);
    bool done = false;
    bool ok = false;
    yt.login([&](bool o, const QJsonObject&, const QString& e) {
        done = true;
        ok = o;
        err = e;
    });
    CHECK(yt.loginPending(), "login en curso");
    CHECK(waitFor([&] { return done; }), "el login termina");
    if (!ok) std::printf("   error: %s\n", err.toUtf8().constData());
    const QUrlQuery aq(seenAuth);
    CHECK(aq.queryItemValue("client_id") == "123-abc.apps.googleusercontent.com" &&
              aq.queryItemValue("redirect_uri").startsWith("http://127.0.0.1:") &&
              aq.queryItemValue("scope").contains("auth/youtube") &&
              aq.queryItemValue("prompt").contains("select_account") &&
              aq.queryItemValue("code_challenge_method") == "S256" &&
              aq.queryItemValue("access_type") == "offline",
          "URL de Google: cliente, redirect local, scope, elegir cuenta, PKCE, offline");
    CHECK(ok && yt.loggedIn() && yt.account().value("title").toString() == "Mi Canal",
          "conectado al canal elegido");
    QFile tok(dir.path() + "/youtube-token.cred");
    CHECK(tok.exists() &&
              (!kPosixPerms || tok.permissions() == (QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                     QFileDevice::ReadUser | QFileDevice::WriteUser)),
          "refresh token guardado solo-dueño");
    QByteArray tokPlain;
    CHECK(ba::app::cred::read(dir.path(), "youtube-token", tokPlain) && tokPlain.contains("refresh_token") &&
              !tokPlain.contains("AT1"),
          "el access token NO se guarda en disco");
    CHECK(tok.open(QIODevice::ReadOnly) && !tok.readAll().contains("refresh_token"),
          "el token queda cifrado");
    tok.close(); // en Windows un archivo abierto no se puede borrar (logout, mas abajo)

    // --- 3) state equivocado / cancelado -------------------------------------------
    {
        YouTubeClient y2(dir.path() + "/b");
        y2.setEndpoints(ep);
        y2.saveClient("123-abc.apps.googleusercontent.com", "s", err);
        bool d2 = false;
        QString e2;
        y2.setBrowserOpener([&](const QUrl& u) {
            QUrl back(QUrlQuery(u).queryItemValue("redirect_uri"));
            back.setQuery("error=access_denied&state=" + QUrlQuery(u).queryItemValue("state"));
            QTimer::singleShot(20, [&nam, back]() { nam.get(QNetworkRequest(back)); });
        });
        y2.login([&](bool, const QJsonObject&, const QString& e) {
            d2 = true;
            e2 = e;
        });
        CHECK(waitFor([&] { return d2; }) && e2.contains("usuario de prueba"),
              "cancelado: explica (usuario de prueba)");
    }

    // --- 4) 401 -> renovar y repetir ------------------------------------------------
    g.expireNextApiCall = 1;
    done = false;
    yt.loadChannel([&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok, "un 401 renueva el token y repite");

    // --- 5) videos --------------------------------------------------------------------
    QJsonObject data;
    done = false;
    yt.listVideos(10, [&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && ok, "lista de videos");
    const QJsonArray items = data.value("items").toArray();
    CHECK(items.size() == 2 && items[0].toObject().value("id") == "vB" &&
              items[0].toObject().value("views") == 7,
          "en el orden de subidas, con estadisticas");
    done = false;
    yt.updateVideo("vA", QJsonObject{{"title", "Nuevo"}, {"privacy", "unlisted"}},
                   [&](bool o, const QJsonObject&, const QString&) {
                       done = true;
                       ok = o;
                   });
    CHECK(waitFor([&] { return done; }) && ok, "editar video");
    const QJsonObject put = QJsonDocument::fromJson(g.lastBody.toUtf8()).object();
    CHECK(put["snippet"].toObject()["title"] == "Nuevo" && put["snippet"].toObject()["categoryId"] == "10" &&
              put["snippet"].toObject()["description"] == "d" &&
              put["status"].toObject()["privacyStatus"] == "unlisted",
          "cambia solo lo pedido y conserva categoria y descripcion");
    done = false;
    yt.deleteVideo("vA", [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok, "borrar (204)");

    // --- 6) en vivo -------------------------------------------------------------------
    done = false;
    yt.createBroadcast(QJsonObject{{"title", "Evento"}}, [&](bool o, const QJsonObject& d, const QString& e) {
        done = true;
        ok = o;
        data = d;
        err = e;
    });
    CHECK(waitFor([&] { return done; }) && ok, "crear evento en vivo");
    CHECK(data["ingestion"].toObject()["key"] == "abcd-1234" &&
              data["ingestion"].toObject()["url"] == "rtmp://a.rtmp.youtube.com/live2" && g.bound,
          "evento + stream vinculado + servidor y clave de ingesta");

    // --- 7) subida ----------------------------------------------------------------------
    const QString file = dir.path() + "/video.mkv";
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray(700 * 1024, 'v'));
    }
    qint64 lastSent = 0;
    QObject::connect(&yt, &YouTubeClient::uploadProgress,
                     [&](const QString&, qint64 s, qint64) { lastSent = s; });
    done = false;
    yt.uploadVideo(file, QJsonObject{{"title", "Grabacion"}},
                   [&](bool o, const QJsonObject& d, const QString& e) {
                       done = true;
                       ok = o;
                       data = d;
                       err = e;
                   });
    CHECK(waitFor([&] { return done; }, 10000) && ok && data["id"] == "V9", "subida reanudable");
    if (!ok) std::printf("   error: %s\n", err.toUtf8().constData());
    CHECK(g.uploaded.size() == 700 * 1024 && lastSent == 700 * 1024,
          "el archivo llega completo, con progreso");

    // --- 8) invalid_grant y logout ------------------------------------------------------------
    {
        QByteArray raw;
        ba::app::cred::read(dir.path(), "youtube-token", raw);
        QJsonObject o = QJsonDocument::fromJson(raw).object();
        o["refresh_token"] = "BAD";
        ba::app::cred::write(dir.path() + "/c", "youtube-token", QJsonDocument(o).toJson());
        ba::app::cred::read(dir.path(), "youtube-client", raw);
        ba::app::cred::write(dir.path() + "/c", "youtube-client", raw);
        YouTubeClient y3(dir.path() + "/c");
        y3.setEndpoints(ep);
        CHECK(y3.loggedIn(), "sesion cargada del disco");
        bool d3 = false;
        QString e3;
        y3.loadChannel([&](bool, const QJsonObject&, const QString& e) {
            d3 = true;
            e3 = e;
        });
        CHECK(waitFor([&] { return d3; }) && !y3.loggedIn() && e3.contains("conecta la cuenta"),
              "refresh revocado: queda desconectado con explicacion");
    }
    done = false;
    yt.logout([&](bool, const QJsonObject&, const QString&) { done = true; });
    CHECK(waitFor([&] { return done; }), "logout termina");
    CHECK(g.revoked, "logout revoca el token en Google");
    CHECK(!QFile::exists(dir.path() + "/youtube-token.cred"), "logout borra el token del disco");
    CHECK(!yt.loggedIn(), "logout deja la sesion cerrada");

    // --- 9) HTTPS real ------------------------------------------------------------------------
    ba::app::net::HttpsClient::setDefaultCaSearch({BA_TEST_CACERT});
    ba::app::net::HttpRequest hr;
    hr.url = "https://www.googleapis.com/discovery/v1/apis?name=youtube&preferred=true";
    hr.timeoutMs = 8000;
    const auto res = ba::app::net::HttpsClient::request(hr);
    if (res.status == 0 && res.error.find("conectar") != std::string::npos) {
        std::printf("   (sin red: se saltea la prueba HTTPS real: %s)\n", res.error.c_str());
    } else {
        CHECK(res.status == 200 && res.body.find("youtube") != std::string::npos,
              "HTTPS real a Google con mbedtls y las raices del bundle");
        if (res.status != 200) std::printf("   https: %d %s\n", res.status, res.error.c_str());
    }

    if (g_failures == 0) {
        std::printf("test_youtube: OK\n");
        return 0;
    }
    std::printf("test_youtube: %d fallo(s)\n", g_failures);
    return 1;
}
