// test_twitch.cpp
//
// Cliente de Twitch contra un Twitch FALSO local (HTTP + IRC), sin red:
//   1. App: Client ID valido/invalido; se guarda solo-dueño.
//   2. Login por codigo: pide el codigo con los permisos, abre la pagina de
//      activacion, espera mientras "authorization_pending" y termina con
//      tokens y el usuario.
//   3. 401 -> renueva (refresh rotado) y repite.
//   4. Canal: leer; cambiar titulo + categoria por nombre (busca el id).
//   5. Ingesta: servidor + clave; estado del vivo; VODs; borrar VOD.
//   6. Chat IRC: se autentica (PASS/NICK/JOIN al canal), recibe un mensaje con
//      etiquetas (nombre, color, insignias), responde PING y manda propios.
//   7. Refresh invalido -> desconectado; logout revoca y borra.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

#include <cstdio>
#include <functional>
#include <memory>

#include "net/Credentials.hpp"
#include "twitch/TwitchClient.hpp"

using ba::app::TwitchClient;

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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_twitch ==\n");
    QTemporaryDir dir;

    // --- Twitch falso (HTTP) -------------------------------------------------------
    QTcpServer http;
    http.listen(QHostAddress::LocalHost, 0);
    int pending = 2; // dos "authorization_pending" antes de aceptar
    int expire = 0;
    bool revoked = false;
    QString lastScopes, lastPatch, lastDelete;
    QString refreshSeen;
    QObject::connect(&http, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket* s = http.nextPendingConnection()) {
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
                const QUrlQuery f(QString::fromUtf8(body));
                QByteArray out;
                if (path == "/oauth2/device") {
                    lastScopes = f.queryItemValue("scopes", QUrl::FullyDecoded);
                    out = reply(200,
                                {{"device_code", "DEV1"},
                                 {"user_code", "ABCD-EFGH"},
                                 {"verification_uri", "https://www.twitch.tv/activate?device-code=ABCD-EFGH"},
                                 {"expires_in", 1800},
                                 {"interval", 1}});
                } else if (path == "/oauth2/token") {
                    if (f.queryItemValue("grant_type", QUrl::FullyDecoded).startsWith("urn:")) {
                        if (pending-- > 0)
                            out = reply(400, {{"status", 400}, {"message", "authorization_pending"}});
                        else
                            out = reply(
                                200,
                                {{"access_token", "AT1"}, {"refresh_token", "RT1"}, {"expires_in", 14000}});
                    } else {
                        refreshSeen = f.queryItemValue("refresh_token", QUrl::FullyDecoded);
                        if (refreshSeen == "RT1")
                            out = reply(
                                200,
                                {{"access_token", "AT2"}, {"refresh_token", "RT2"}, {"expires_in", 14000}});
                        else
                            out = reply(400, {{"status", 400}, {"message", "Invalid refresh token"}});
                    }
                } else if (path == "/oauth2/revoke") {
                    revoked = true;
                    out = reply(200, {});
                } else if (!headers.contains("Authorization: Bearer AT") ||
                           !headers.contains("Client-Id: ")) {
                    out = reply(401, {{"message", "invalid token"}});
                } else if (expire > 0) {
                    --expire;
                    out = reply(401, {{"message", "invalid token"}});
                } else if (path == "/helix/users") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "U1"},
                                                                      {"login", "micanal"},
                                                                      {"display_name", "MiCanal"}}}}});
                } else if (path == "/helix/channels" && method == "GET") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"title", "Viejo"},
                                                                      {"game_name", "Just Chatting"},
                                                                      {"game_id", "509658"}}}}});
                } else if (path == "/helix/channels" && method == "PATCH") {
                    lastPatch = QString::fromUtf8(body);
                    out = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
                } else if (path == "/helix/search/categories") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"id", "1"}, {"name", "Music Videos"}},
                                                          QJsonObject{{"id", "26936"}, {"name", "Music"}}}}});
                } else if (path == "/helix/streams/key") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{{"stream_key", "live_123_abc"}}}}});
                } else if (path == "/helix/streams") {
                    out = reply(
                        200, {{"data", QJsonArray{QJsonObject{{"viewer_count", 42}, {"title", "Nuevo"}}}}});
                } else if (path == "/helix/videos" && method == "GET") {
                    out = reply(200, {{"data", QJsonArray{QJsonObject{
                                                   {"id", "V1"},
                                                   {"title", "Vod"},
                                                   {"thumbnail_url", "https://x/%{width}x%{height}.jpg"},
                                                   {"view_count", 5}}}}});
                } else if (path == "/helix/videos" && method == "DELETE") {
                    lastDelete = u.query();
                    out = reply(200, {{"data", QJsonArray{"V1"}}});
                } else {
                    out = reply(404, {{"message", "ruta"}});
                }
                s->write(out);
                s->disconnectFromHost();
            });
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    // --- IRC falso -------------------------------------------------------------------
    QTcpServer irc;
    irc.listen(QHostAddress::LocalHost, 0);
    QStringList ircIn;
    QTcpSocket* ircClient = nullptr;
    QObject::connect(&irc, &QTcpServer::newConnection, [&]() {
        ircClient = irc.nextPendingConnection();
        QObject::connect(ircClient, &QTcpSocket::readyRead, [&]() {
            while (ircClient->canReadLine()) {
                const QString l = QString::fromUtf8(ircClient->readLine()).trimmed();
                ircIn << l;
                if (l.startsWith("JOIN")) {
                    ircClient->write("PING :tmi.twitch.tv\r\n");
                    ircClient->write(
                        "@badges=moderator/"
                        "1;color=#FF4500;display-name=Fan_1;id=m1;tmi-sent-ts=1700000000000 "
                        ":fan_1!fan_1@fan_1.tmi.twitch.tv PRIVMSG #micanal :hola desde el chat\r\n");
                }
            }
        });
    });

    TwitchClient tw(dir.path());
    TwitchClient::Endpoints ep;
    ep.id = QString("http://127.0.0.1:%1/oauth2").arg(http.serverPort());
    ep.api = QString("http://127.0.0.1:%1/helix").arg(http.serverPort());
    ep.ircHost = "127.0.0.1";
    ep.ircPort = QString::number(irc.serverPort());
    ep.ircTls = false;
    tw.setEndpoints(ep);
    QString openedUrl;
    tw.setBrowserOpener([&](const QString& u) { openedUrl = u; });

    // --- 1) app -------------------------------------------------------------------------
    QString err;
    CHECK(!tw.saveClient("corto", {}, err), "Client ID invalido");
    CHECK(tw.saveClient("abcdefghijklmnopqrstuvwxyz0123", {}, err), "Client ID de app publica");
    CHECK(!kPosixPerms || QFile(dir.path() + "/twitch-client.cred").permissions() ==
                              (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                               QFileDevice::WriteUser),
          "guardado solo-dueño");

    // --- 2) login por codigo ----------------------------------------------------------------
    bool done = false, ok = false;
    tw.login([&](bool o, const QJsonObject&, const QString& e) {
        done = true;
        ok = o;
        err = e;
    });
    CHECK(waitFor([&] { return tw.loginPending(); }), "login en curso");
    CHECK(tw.userCode() == "ABCD-EFGH" && openedUrl.contains("activate"), "codigo y pagina de activacion");
    CHECK(lastScopes.contains("channel:read:stream_key") && lastScopes.contains("chat:read"),
          "pide los permisos");
    CHECK(waitFor([&] { return done; }) && ok, "tras autorizar, conectado");
    if (!ok) std::printf("   %s\n", err.toUtf8().constData());
    CHECK(tw.loggedIn() && tw.user().value("login") == "micanal", "usuario del canal");

    // --- 3) 401 -> refresh ------------------------------------------------------------------
    expire = 1;
    QJsonObject data;
    done = false;
    tw.channel([&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && ok && data.value("title") == "Viejo", "un 401 renueva y repite");
    CHECK(refreshSeen == "RT1", "usa el refresh guardado");

    // --- 4) cambiar canal ------------------------------------------------------------------------
    done = false;
    tw.updateChannel(QJsonObject{{"title", "Nuevo titulo"}, {"game", "music"}},
                     [&](bool o, const QJsonObject&, const QString& e) {
                         done = true;
                         ok = o;
                         err = e;
                     });
    CHECK(waitFor([&] { return done; }) && ok, "cambiar titulo y categoria");
    const QJsonObject patch = QJsonDocument::fromJson(lastPatch.toUtf8()).object();
    CHECK(patch.value("title") == "Nuevo titulo" && patch.value("game_id") == "26936",
          "categoria por nombre (coincidencia exacta)");

    // --- 5) ingesta, estado, VODs ------------------------------------------------------------------
    done = false;
    tw.ingest([&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && ok && data.value("key") == "live_123_abc" &&
              data.value("url") == "rtmp://live.twitch.tv/app",
          "servidor y clave de ingesta");
    done = false;
    tw.streamStatus([&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        ok = o;
        data = d;
    });
    CHECK(waitFor([&] { return done; }) && data.value("live").toBool() && data.value("viewers").toInt() == 42,
          "estado del vivo");
    done = false;
    tw.videos(10, [&](bool o, const QJsonObject& d, const QString&) {
        done = true;
        data = d;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) &&
              data.value("items").toArray().at(0).toObject().value("thumbnail") == "https://x/320x180.jpg",
          "VODs con miniatura");
    done = false;
    tw.deleteVideos({"V1"}, [&](bool o, const QJsonObject&, const QString&) {
        done = true;
        ok = o;
    });
    CHECK(waitFor([&] { return done; }) && ok && lastDelete == "id=V1", "borrar VOD");

    // --- 6) chat IRC -----------------------------------------------------------------------------------
    tw.startChat();
    CHECK(waitFor([&] { return tw.chatMessages().size() >= 1; }), "llega un mensaje del chat");
    CHECK(ircIn.contains("PASS oauth:AT2") && ircIn.contains("NICK micanal") &&
              ircIn.contains("JOIN #micanal"),
          "se autentica y entra al canal");
    const QJsonObject m = tw.chatMessages().at(0).toObject();
    CHECK(m.value("author") == "Fan_1" && m.value("color") == "#FF4500" && m.value("moderator").toBool() &&
              m.value("text") == "hola desde el chat",
          "nombre, color, insignias y texto");
    CHECK(waitFor([&] { return ircIn.contains("PONG :tmi.twitch.tv"); }), "responde PING");
    tw.sendChat("gracias!");
    CHECK(waitFor([&] { return ircIn.contains("PRIVMSG #micanal :gracias!"); }), "manda mensajes propios");
    CHECK(tw.chatMessages().size() == 2, "y los muestra (IRC no los devuelve)");
    tw.stopChat();
    CHECK(!tw.chatRunning(), "el chat se detiene");

    // --- 7) refresh invalido y logout ------------------------------------------------------------------
    {
        QTemporaryDir d2;
        QByteArray raw;
        ba::app::cred::read(dir.path(), "twitch-client", raw);
        ba::app::cred::write(d2.path(), "twitch-client", raw);
        ba::app::cred::write(
            d2.path(), "twitch-token",
            R"({"refresh_token":"MALO","access_token":"","expires_at":0,"user":{"id":"U1","login":"micanal"}})");
        TwitchClient t2(d2.path());
        t2.setEndpoints(ep);
        bool d = false;
        QString e2;
        t2.channel([&](bool, const QJsonObject&, const QString& e) {
            d = true;
            e2 = e;
        });
        CHECK(waitFor([&] { return d; }) && !t2.loggedIn() && e2.contains("conecta la cuenta"),
              "refresh invalido: desconectado");
    }
    done = false;
    tw.logout([&](bool, const QJsonObject&, const QString&) { done = true; });
    CHECK(waitFor([&] { return done; }) && revoked && !QFile::exists(dir.path() + "/twitch-token.cred"),
          "logout revoca y borra");

    if (g_failures == 0) {
        std::printf("test_twitch: OK\n");
        return 0;
    }
    std::printf("test_twitch: %d fallo(s)\n", g_failures);
    return 1;
}
