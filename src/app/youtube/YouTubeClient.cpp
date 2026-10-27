// ============================================================================
// src/app/youtube/YouTubeClient.cpp — ver YouTubeClient.hpp
// ============================================================================

#include "youtube/YouTubeClient.hpp"
#include "ajustes/Ajustes.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>
#include <thread>

#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"

namespace ba::app {

namespace {

constexpr const char* kScope = "https://www.googleapis.com/auth/youtube";
inline int kLoginTimeoutMs() { return ba::ajustes::entero("plataformas.loginMin", 5) * 60 * 1000; }

QString randomToken(int n) {
    static const char* kSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString s;
    for (int i = 0; i < n; ++i)
        s += QChar(kSet[QRandomGenerator::system()->bounded(66)]);
    return s;
}

QByteArray base64Url(const QByteArray& b) {
    return b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString form(const std::vector<std::pair<QString, QString>>& kv) {
    QString out;
    for (const auto& [k, v] : kv) {
        if (!out.isEmpty()) out += '&';
        out += QString::fromStdString(net::urlEncode(k.toStdString())) + '=' +
               QString::fromStdString(net::urlEncode(v.toStdString()));
    }
    return out;
}

QJsonObject parse(const QByteArray& body) { return QJsonDocument::fromJson(body).object(); }

QString bestThumb(const QJsonObject& thumbs) {
    for (const char* k : {"medium", "high", "default", "standard", "maxres"}) {
        const QString u = thumbs.value(k).toObject().value("url").toString();
        if (!u.isEmpty()) return u;
    }
    return {};
}

} // namespace

struct YouTubeClient::Req {
    net::HttpRequest http;
    QString progressPath; // si no vacio: emite uploadProgress
};

YouTubeClient::YouTubeClient(QString configDir, QObject* parent)
    : QObject(parent), configDir_(std::move(configDir)) {
    openBrowser_ = [](const QUrl& u) { QDesktopServices::openUrl(u); };
    loadFiles();
}

YouTubeClient::~YouTubeClient() { cancelLogin(); }

void YouTubeClient::loadFiles() {
    if (QByteArray cRaw; cred::read(configDir_, "youtube-client", cRaw)) {
        const QJsonObject o = parse(cRaw);
        clientId_ = o.value("client_id").toString();
        clientSecret_ = o.value("client_secret").toString();
    }
    if (QByteArray tRaw; cred::read(configDir_, "youtube-token", tRaw)) {
        const QJsonObject o = parse(tRaw);
        refreshToken_ = o.value("refresh_token").toString();
        account_ = o.value("account").toObject();
    }
}

void YouTubeClient::persistToken() {
    if (refreshToken_.isEmpty()) {
        cred::remove(configDir_, "youtube-token");
        return;
    }
    const QJsonObject o{{"refresh_token", refreshToken_},
                        {"account", account_},
                        {"savedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    cred::write(configDir_, "youtube-token", QJsonDocument(o).toJson());
}

bool YouTubeClient::saveClient(const QString& jsonOrId, const QString& secret, QString& error) {
    QString id;
    QString sec;
    if (!secret.trimmed().isEmpty()) {
        id = jsonOrId.trimmed();
        sec = secret.trimmed();
    } else {
        const QJsonObject o = parse(jsonOrId.toUtf8());
        // "installed" = App de escritorio (lo correcto); "web" tambien trae
        // client_id/secret, pero exige redirecciones registradas.
        const QJsonObject inner = o.contains("installed") ? o.value("installed").toObject()
                                  : o.contains("web")     ? o.value("web").toObject()
                                                          : o;
        id = inner.value("client_id").toString();
        sec = inner.value("client_secret").toString();
        if (o.contains("web")) {
            error = tr("Ese cliente es de tipo \"Aplicacion web\": crea uno de tipo \"App de escritorio\".");
            return false;
        }
    }
    if (!id.endsWith(".apps.googleusercontent.com") || sec.isEmpty()) {
        error = tr("No parece un cliente OAuth de Google: el ID termina en .apps.googleusercontent.com y hace "
                   "falta el secreto. Pega el JSON completo que descargaste.");
        return false;
    }
    const QJsonObject o{{"client_id", id}, {"client_secret", sec}};
    if (!cred::write(configDir_, "youtube-client", QJsonDocument(o).toJson())) {
        error = tr("No se pudo guardar en %1").arg(configDir_);
        return false;
    }
    if (id != clientId_) { // otra app de Google: la sesion anterior no sirve
        refreshToken_.clear();
        accessToken_.clear();
        account_ = {};
        persistToken();
    }
    clientId_ = id;
    clientSecret_ = sec;
    emit stateChanged();
    return true;
}

void YouTubeClient::clearClient() {
    clientId_.clear();
    clientSecret_.clear();
    refreshToken_.clear();
    accessToken_.clear();
    account_ = {};
    cred::remove(configDir_, "youtube-client");
    persistToken();
    emit stateChanged();
}

// --- Red -------------------------------------------------------------------------

void YouTubeClient::run(std::shared_ptr<Req> req, std::function<void(int, QByteArray, QString, QString)> cb) {
    QPointer<YouTubeClient> self(this);
    if (!req->progressPath.isEmpty()) {
        const QString path = req->progressPath;
        req->http.progress = [self, path](std::uint64_t sent, std::uint64_t total) {
            QMetaObject::invokeMethod(
                self.data(),
                [self, path, sent, total]() {
                    if (self)
                        emit self->uploadProgress(path, static_cast<qint64>(sent),
                                                  static_cast<qint64>(total));
                },
                Qt::QueuedConnection);
        };
    }
    std::thread([self, req, cb]() {
        const net::HttpResponse r = net::HttpsClient::request(req->http);
        const QByteArray body = QByteArray::fromStdString(r.body);
        const QString location = QString::fromStdString(r.header("Location"));
        const QString err = QString::fromStdString(r.error);
        const int status = r.status;
        if (!self) return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, cb, status, body, location, err]() {
                if (self) cb(status, body, location, err);
            },
            Qt::QueuedConnection);
    }).detach();
}

QString YouTubeClient::explain(int status, const QByteArray& body) {
    const QJsonObject o = parse(body);
    const QJsonValue ev = o.value("error");
    QString reason;
    QString message;
    if (ev.isObject()) {
        const QJsonObject e = ev.toObject();
        message = e.value("message").toString();
        reason = e.value("errors").toArray().at(0).toObject().value("reason").toString();
        if (reason.isEmpty()) reason = e.value("status").toString();
    } else if (ev.isString()) { // endpoint de tokens: {"error": "invalid_grant", ...}
        reason = ev.toString();
        message = o.value("error_description").toString();
    }
    // Se arma una vez: el texto se traduce al devolverlo.
    static const QHash<QString, const char*> kText{
        {"quotaExceeded", QT_TR_NOOP("Se agoto la cuota diaria de la API de YouTube (10.000 unidades). Se renueva a la "
                                     "medianoche, hora del Pacifico.")},
        {"liveStreamingNotEnabled", QT_TR_NOOP("Tu canal no tiene habilitadas las transmisiones en vivo. Habilitalas "
                                               "en youtube.com/features (tarda hasta 24 h la primera vez).")},
        {"insufficientPermissions", QT_TR_NOOP("El permiso otorgado no alcanza: desconecta y volve a conectar la cuenta.")},
        {"forbidden", QT_TR_NOOP("YouTube no permite esa operacion con esta cuenta.")},
        {"invalid_grant", QT_TR_NOOP("La sesion de YouTube vencio o fue revocada: conecta la cuenta de nuevo.")},
        {"invalid_client", QT_TR_NOOP("El cliente OAuth no es valido (ID o secreto): revisa la configuracion.")},
        {"redirectUriMismatch", QT_TR_NOOP("El cliente OAuth debe ser de tipo \"App de escritorio\".")},
        {"invalidTransition", QT_TR_NOOP("El evento no puede pasar a ese estado ahora (¿el stream esta llegando?).")},
        {"redundantTransition", QT_TR_NOOP("El evento ya esta en ese estado.")},
        {"errorStreamInactive",
         QT_TR_NOOP("YouTube todavia no recibe video: empeza a transmitir desde la app y espera unos segundos.")},
        {"videoNotFound", QT_TR_NOOP("No existe ese video (o no es de este canal).")},
        {"liveBroadcastNotFound", QT_TR_NOOP("No existe ese evento en vivo.")},
        {"uploadLimitExceeded", QT_TR_NOOP("Llegaste al limite de subidas de YouTube por hoy.")},
        {"accessNotConfigured", QT_TR_NOOP("La API de YouTube no esta habilitada en tu proyecto de Google Cloud "
                                           "(Biblioteca -> YouTube Data API v3).")},
        {"SERVICE_DISABLED", QT_TR_NOOP("La API de YouTube no esta habilitada en tu proyecto de Google Cloud "
                                        "(Biblioteca -> YouTube Data API v3).")},
    };
    if (kText.contains(reason)) return tr(kText.value(reason));
    if (!message.isEmpty()) return message + (reason.isEmpty() ? "" : " (" + reason + ")");
    return tr("YouTube respondio %1").arg(status);
}

void YouTubeClient::ensureToken(std::function<void(QString, QString)> cb) {
    if (!loggedIn()) {
        cb({}, tr("No hay una cuenta de YouTube conectada."));
        return;
    }
    if (!accessToken_.isEmpty() && QDateTime::currentMSecsSinceEpoch() < accessExpiresMs_ - 60000) {
        cb(accessToken_, {});
        return;
    }
    tokenWaiters_.push_back(std::move(cb));
    if (refreshing_) return;
    refreshing_ = true;
    auto req = std::make_shared<Req>();
    req->http.method = "POST";
    req->http.url = ep_.token.toStdString();
    req->http.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req->http.body = form({{"client_id", clientId_},
                           {"client_secret", clientSecret_},
                           {"refresh_token", refreshToken_},
                           {"grant_type", "refresh_token"}})
                         .toStdString();
    run(req, [this](int status, QByteArray body, QString, QString err) {
        refreshing_ = false;
        QString token;
        QString error = err;
        const QJsonObject o = parse(body);
        if (status == 200 && o.contains("access_token")) {
            accessToken_ = o.value("access_token").toString();
            accessExpiresMs_ =
                QDateTime::currentMSecsSinceEpoch() + o.value("expires_in").toInt(3600) * 1000LL;
            token = accessToken_;
        } else if (error.isEmpty()) {
            error = explain(status, body);
            if (o.value("error").toString() == "invalid_grant") { // revocado o vencido: hay que reconectar
                refreshToken_.clear();
                accessToken_.clear();
                persistToken();
                emit stateChanged();
            }
        }
        auto waiters = std::move(tokenWaiters_);
        tokenWaiters_.clear();
        for (auto& w : waiters)
            w(token, error);
    });
}

void YouTubeClient::api(const QString& method, const QString& path, const QUrlQuery& query,
                        const QJsonObject& body, Done done) {
    const auto attempt = std::make_shared<std::function<void(bool)>>();
    QPointer<YouTubeClient> self(this);
    *attempt = [self, method, path, query, body, done, attempt](bool retried) {
        if (!self) return;
        self->ensureToken([self, method, path, query, body, done, attempt, retried](QString token,
                                                                                    QString err) {
            if (!self) return;
            if (token.isEmpty()) {
                done(false, {}, err);
                *attempt = nullptr;
                return;
            }
            auto req = std::make_shared<Req>();
            req->http.method = method.toStdString();
            QString url = self->ep_.api + path;
            if (!query.isEmpty()) url += '?' + query.toString(QUrl::FullyEncoded);
            req->http.url = url.toStdString();
            req->http.headers = {{"Authorization", "Bearer " + token.toStdString()},
                                 {"Accept", "application/json"}};
            if (method == "POST" || method == "PUT") {
                req->http.headers.emplace_back("Content-Type", "application/json; charset=UTF-8");
                if (!body.isEmpty())
                    req->http.body = QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
            }
            self->run(req, [self, done, attempt, retried](int status, QByteArray rb, QString, QString nerr) {
                if (!self) return;
                if (status == 401 && !retried) { // token vencido antes de tiempo: renovar y repetir una vez
                    self->accessToken_.clear();
                    (*attempt)(true);
                    return;
                }
                *attempt = nullptr;
                if (status >= 200 && status < 300) {
                    done(true, parse(rb), {});
                } else {
                    done(false, parse(rb),
                         nerr.isEmpty() ? explain(status, rb) : tr("Sin conexion con YouTube: %1").arg(nerr));
                }
            });
        });
    };
    (*attempt)(false);
}

void YouTubeClient::download(const QString& url, std::function<void(bool, const QByteArray&)> done) {
    auto req = std::make_shared<Req>();
    req->http.url = url.toStdString();
    req->http.timeoutMs = 15000;
    run(req, [done](int status, QByteArray body, QString, QString) { done(status == 200, body); });
}

// --- Login -------------------------------------------------------------------------

QString YouTubeClient::redirectUri() const {
    return QString("http://127.0.0.1:%1").arg(loginServer_ ? loginServer_->serverPort() : 0);
}

void YouTubeClient::login(Done done) {
    if (!hasClient()) {
        done(false, {}, tr("Falta configurar el cliente OAuth (ID y secreto de Google Cloud)."));
        return;
    }
    cancelLogin();
    loginServer_ = new QTcpServer(this);
    if (!loginServer_->listen(QHostAddress::LocalHost, 0)) {
        delete loginServer_;
        loginServer_ = nullptr;
        done(false, {}, tr("No se pudo abrir el puerto local para el login."));
        return;
    }
    loginDone_ = std::move(done);
    loginState_ = randomToken(32);
    loginVerifier_ = randomToken(64);
    const QByteArray challenge =
        base64Url(QCryptographicHash::hash(loginVerifier_.toLatin1(), QCryptographicHash::Sha256));
    QUrlQuery q;
    q.addQueryItem("client_id", clientId_);
    q.addQueryItem("redirect_uri", redirectUri());
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", kScope);
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "select_account consent"); // elegir cuenta + asegurar refresh token
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", loginState_);
    QUrl url(ep_.auth);
    url.setQuery(q);

    connect(loginServer_, &QTcpServer::newConnection, this, [this]() {
        while (QTcpSocket* s = loginServer_ ? loginServer_->nextPendingConnection() : nullptr) {
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::readyRead, this, [this, s]() {
                const QByteArray head = s->readAll();
                const QByteArray line = head.left(head.indexOf("\r\n"));
                const QList<QByteArray> parts = line.split(' ');
                const QUrl u("http://127.0.0.1" + QString::fromLatin1(parts.value(1)));
                const QUrlQuery q(u);
                const auto page = [s](const QString& title, const QString& text, bool ok) {
                    const QByteArray html =
                        ("<!doctype html><meta charset=utf-8><title>Broadcast Appliance</title>"
                         "<body "
                         "style=\"font-family:system-ui;background:#0c0c0e;color:#f2f2f4;display:grid;place-"
                         "items:center;height:95vh\">"
                         "<div style=\"text-align:center\"><h1 style=\"color:" +
                         QString(ok ? "#34d399" : "#f87171") + "\">" + title + "</h1><p>" + text +
                         "</p></div>")
                            .toUtf8();
                    s->write(
                        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
                        "Content-Length: " +
                        QByteArray::number(html.size()) + "\r\n\r\n" + html);
                    s->disconnectFromHost();
                };
                if (!q.hasQueryItem("code") && !q.hasQueryItem("error")) {
                    page("…", tr("Esperando la respuesta de Google."), false); // favicon u otro pedido
                    return;
                }
                if (q.queryItemValue("state") != loginState_) {
                    page(tr("No coincide"), tr("Este enlace no corresponde al login en curso."), false);
                    return;
                }
                if (q.hasQueryItem("error")) {
                    page(tr("Cancelado"), tr("No se dio permiso. Podes cerrar esta pestaña."), false);
                    finishLogin(false,
                                q.queryItemValue("error") == "access_denied"
                                    ? tr("Cancelaste el permiso (o tu cuenta no esta como usuario de prueba "
                                         "en la pantalla de consentimiento).")
                                    : tr("Google respondio: %1").arg(q.queryItemValue("error")));
                    return;
                }
                page(tr("¡Listo!"),
                     tr("Broadcast Appliance ya puede usar tu cuenta de YouTube. Podes cerrar esta pestaña."),
                     true);
                exchangeCode(q.queryItemValue("code", QUrl::FullyDecoded),
                             [this](bool ok, const QJsonObject&, const QString& e) { finishLogin(ok, e); });
            });
        }
    });
    QPointer<QTcpServer> server(loginServer_);
    QTimer::singleShot(kLoginTimeoutMs(), this, [this, server]() {
        if (server && server == loginServer_) finishLogin(false, tr("El login no se completo en 5 minutos."));
    });
    emit stateChanged();
    openBrowser_(url);
}

void YouTubeClient::exchangeCode(const QString& code, Done done) {
    auto req = std::make_shared<Req>();
    req->http.method = "POST";
    req->http.url = ep_.token.toStdString();
    req->http.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req->http.body = form({{"code", code},
                           {"client_id", clientId_},
                           {"client_secret", clientSecret_},
                           {"redirect_uri", redirectUri()},
                           {"grant_type", "authorization_code"},
                           {"code_verifier", loginVerifier_}})
                         .toStdString();
    run(req, [this, done](int status, QByteArray body, QString, QString err) {
        const QJsonObject o = parse(body);
        if (status != 200 || !o.contains("refresh_token")) {
            done(false, {},
                 err.isEmpty() ? (o.contains("access_token") ? tr("Google no entrego un refresh token: "
                                                                  "quita el acceso de la app en "
                                                                  "myaccount.google.com/permissions y "
                                                                  "volve a conectar.")
                                                             : explain(status, body))
                               : tr("Sin conexion con Google: %1").arg(err));
            return;
        }
        refreshToken_ = o.value("refresh_token").toString();
        accessToken_ = o.value("access_token").toString();
        accessExpiresMs_ = QDateTime::currentMSecsSinceEpoch() + o.value("expires_in").toInt(3600) * 1000LL;
        persistToken();
        loadChannel(done); // el canal elegido es la "cuenta"
    });
}

void YouTubeClient::finishLogin(bool ok, const QString& error) {
    if (loginServer_) {
        loginServer_->close();
        loginServer_->deleteLater();
        loginServer_ = nullptr;
    }
    Done done = std::move(loginDone_);
    loginDone_ = nullptr;
    emit stateChanged();
    if (done) done(ok, account_, error);
}

void YouTubeClient::cancelLogin() {
    if (loginServer_) finishLogin(false, tr("Login cancelado."));
}

void YouTubeClient::logout(Done done) {
    const QString token = refreshToken_;
    refreshToken_.clear();
    accessToken_.clear();
    account_ = {};
    persistToken();
    emit stateChanged();
    if (token.isEmpty()) {
        done(true, {}, {});
        return;
    }
    // Revocar en Google (si no hay red, igual queda desconectado aca).
    auto req = std::make_shared<Req>();
    req->http.method = "POST";
    req->http.url =
        (ep_.revoke + "?token=" + QString::fromStdString(net::urlEncode(token.toStdString()))).toStdString();
    req->http.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    run(req, [done](int, QByteArray, QString, QString) { done(true, {}, {}); });
}

// --- Canal y videos ----------------------------------------------------------------------

void YouTubeClient::loadChannel(Done done) {
    QUrlQuery q;
    q.addQueryItem("part", "snippet,contentDetails,statistics");
    q.addQueryItem("mine", "true");
    api("GET", "/channels", q, {}, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QJsonObject ch = d.value("items").toArray().at(0).toObject();
        if (ch.isEmpty())
            return done(false, {}, tr("Esa cuenta no tiene un canal de YouTube (crealo en youtube.com)."));
        const QJsonObject sn = ch.value("snippet").toObject();
        account_ = QJsonObject{
            {"id", ch.value("id")},
            {"title", sn.value("title")},
            {"customUrl", sn.value("customUrl")},
            {"thumbnail", bestThumb(sn.value("thumbnails").toObject())},
            {"uploadsPlaylist",
             ch.value("contentDetails").toObject().value("relatedPlaylists").toObject().value("uploads")},
            {"subscribers", ch.value("statistics").toObject().value("subscriberCount")},
            {"videoCount", ch.value("statistics").toObject().value("videoCount")}};
        persistToken();
        emit stateChanged();
        done(true, account_, {});
    });
}

void YouTubeClient::listVideos(int max, Done done) {
    const QString uploads = account_.value("uploadsPlaylist").toString();
    const auto withPlaylist = [this, max, done](const QString& playlist) {
        QUrlQuery q;
        q.addQueryItem("part", "contentDetails");
        q.addQueryItem("playlistId", playlist);
        q.addQueryItem("maxResults", QString::number(std::clamp(max, 1, 50)));
        api("GET", "/playlistItems", q, {}, [this, done](bool ok, const QJsonObject& d, const QString& e) {
            if (!ok) return done(false, {}, e);
            QStringList ids;
            for (const QJsonValue& v : d.value("items").toArray())
                ids << v.toObject().value("contentDetails").toObject().value("videoId").toString();
            if (ids.isEmpty()) return done(true, QJsonObject{{"items", QJsonArray()}}, {});
            QUrlQuery vq;
            vq.addQueryItem("part", "snippet,status,statistics,contentDetails");
            vq.addQueryItem("id", ids.join(','));
            api("GET", "/videos", vq, {}, [done, ids](bool ok2, const QJsonObject& vd, const QString& e2) {
                if (!ok2) return done(false, {}, e2);
                QHash<QString, QJsonObject> byId;
                for (const QJsonValue& v : vd.value("items").toArray())
                    byId.insert(v.toObject().value("id").toString(), v.toObject());
                QJsonArray out;
                for (const QString& id : ids) { // orden de la lista de subidas (mas nuevos primero)
                    const QJsonObject v = byId.value(id);
                    if (v.isEmpty()) continue;
                    const QJsonObject sn = v.value("snippet").toObject();
                    const QJsonObject st = v.value("statistics").toObject();
                    out.append(
                        QJsonObject{{"id", id},
                                    {"title", sn.value("title")},
                                    {"description", sn.value("description")},
                                    {"publishedAt", sn.value("publishedAt")},
                                    {"privacy", v.value("status").toObject().value("privacyStatus")},
                                    {"uploadStatus", v.value("status").toObject().value("uploadStatus")},
                                    {"live", sn.value("liveBroadcastContent")},
                                    {"thumbnail", bestThumb(sn.value("thumbnails").toObject())},
                                    {"views", st.value("viewCount").toString().toLongLong()},
                                    {"likes", st.value("likeCount").toString().toLongLong()},
                                    {"comments", st.value("commentCount").toString().toLongLong()},
                                    {"duration", v.value("contentDetails").toObject().value("duration")},
                                    {"tags", sn.value("tags")},
                                    {"url", "https://youtu.be/" + id}});
                }
                done(true, QJsonObject{{"items", out}}, {});
            });
        });
    };
    if (!uploads.isEmpty()) {
        withPlaylist(uploads);
        return;
    }
    loadChannel([withPlaylist, done, this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return done(false, {}, e);
        withPlaylist(account_.value("uploadsPlaylist").toString());
    });
}

void YouTubeClient::updateVideo(const QString& id, const QJsonObject& changes, Done done) {
    // videos.update reemplaza el snippet entero (y exige categoryId): se lee
    // el actual y se modifica solo lo pedido.
    QUrlQuery q;
    q.addQueryItem("part", "snippet,status");
    q.addQueryItem("id", id);
    api("GET", "/videos", q, {}, [this, id, changes, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QJsonObject v = d.value("items").toArray().at(0).toObject();
        if (v.isEmpty()) return done(false, {}, tr("No existe ese video (o no es de este canal)."));
        QJsonObject sn = v.value("snippet").toObject();
        QJsonObject st = v.value("status").toObject();
        QJsonObject newSn{
            {"title", changes.value("title").toString(sn.value("title").toString())},
            {"description", changes.value("description").toString(sn.value("description").toString())},
            {"categoryId", sn.value("categoryId").toString("22")}};
        newSn.insert("tags", changes.contains("tags") ? changes.value("tags") : sn.value("tags"));
        if (sn.contains("defaultLanguage")) newSn.insert("defaultLanguage", sn.value("defaultLanguage"));
        QJsonObject newSt{
            {"privacyStatus", changes.value("privacy").toString(st.value("privacyStatus").toString())}};
        if (st.contains("selfDeclaredMadeForKids"))
            newSt.insert("selfDeclaredMadeForKids", st.value("selfDeclaredMadeForKids"));
        if (st.contains("embeddable")) newSt.insert("embeddable", st.value("embeddable"));
        QUrlQuery uq;
        uq.addQueryItem("part", "snippet,status");
        api("PUT", "/videos", uq, QJsonObject{{"id", id}, {"snippet", newSn}, {"status", newSt}}, done);
    });
}

void YouTubeClient::deleteVideo(const QString& id, Done done) {
    QUrlQuery q;
    q.addQueryItem("id", id);
    api("DELETE", "/videos", q, {}, done);
}

void YouTubeClient::uploadVideo(const QString& path, const QJsonObject& meta, Done done) {
    const QFileInfo fi(path);
    if (!fi.isFile()) return done(false, {}, tr("No existe el archivo %1").arg(path));
    QPointer<YouTubeClient> self(this);
    ensureToken([self, path, fi, meta, done](QString token, QString err) {
        if (!self) return;
        if (token.isEmpty()) return done(false, {}, err);
        QJsonObject sn{{"title", meta.value("title").toString(fi.completeBaseName())},
                       {"description", meta.value("description").toString()},
                       {"categoryId", meta.value("categoryId").toString("22")}};
        if (meta.contains("tags")) sn.insert("tags", meta.value("tags"));
        const QJsonObject body{
            {"snippet", sn},
            {"status", QJsonObject{{"privacyStatus", meta.value("privacy").toString("private")},
                                   {"selfDeclaredMadeForKids", meta.value("madeForKids").toBool(false)}}}};
        // 1) Sesion de subida reanudable: devuelve la URL de destino.
        auto init = std::make_shared<Req>();
        init->http.method = "POST";
        init->http.url =
            (self->ep_.upload + "/videos?uploadType=resumable&part=snippet,status").toStdString();
        init->http.headers = {{"Authorization", "Bearer " + token.toStdString()},
                              {"Content-Type", "application/json; charset=UTF-8"},
                              {"X-Upload-Content-Type", "video/*"},
                              {"X-Upload-Content-Length", std::to_string(fi.size())}};
        init->http.body = QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
        self->run(init, [self, path, token, done](int status, QByteArray rb, QString location, QString nerr) {
            if (!self) return;
            if (status != 200 || location.isEmpty())
                return done(false, {},
                            nerr.isEmpty() ? explain(status, rb) : tr("Sin conexion con YouTube: %1").arg(nerr));
            // 2) El archivo, en streaming, con progreso.
            auto put = std::make_shared<Req>();
            put->http.method = "PUT";
            put->http.url = location.toStdString();
            put->http.headers = {{"Authorization", "Bearer " + token.toStdString()},
                                 {"Content-Type", "video/*"}};
            put->http.bodyFile = path.toStdString();
            put->http.timeoutMs = 120000;
            put->progressPath = path;
            self->run(put, [done](int st2, QByteArray b2, QString, QString e2) {
                if (st2 == 200 || st2 == 201) {
                    const QJsonObject v = parse(b2);
                    done(true,
                         QJsonObject{{"id", v.value("id")},
                                     {"url", "https://youtu.be/" + v.value("id").toString()}},
                         {});
                } else {
                    done(false, {}, e2.isEmpty() ? explain(st2, b2) : tr("La subida se corto: %1").arg(e2));
                }
            });
        });
    });
}

void YouTubeClient::setThumbnail(const QString& videoId, const QString& imagePath, Done done) {
    QPointer<YouTubeClient> self(this);
    ensureToken([self, videoId, imagePath, done](QString token, QString err) {
        if (!self) return;
        if (token.isEmpty()) return done(false, {}, err);
        auto req = std::make_shared<Req>();
        req->http.method = "POST";
        req->http.url = (self->ep_.upload + "/thumbnails/set?videoId=" + videoId).toStdString();
        const bool png = imagePath.endsWith(".png", Qt::CaseInsensitive);
        req->http.headers = {{"Authorization", "Bearer " + token.toStdString()},
                             {"Content-Type", png ? "image/png" : "image/jpeg"}};
        req->http.bodyFile = imagePath.toStdString();
        self->run(req, [done](int st, QByteArray b, QString, QString e) {
            (st == 200) ? done(true, parse(b), {}) : done(false, {}, e.isEmpty() ? explain(st, b) : e);
        });
    });
}

// --- En vivo ------------------------------------------------------------------------------

namespace {
QJsonObject broadcastView(const QJsonObject& b) {
    const QJsonObject sn = b.value("snippet").toObject();
    const QJsonObject st = b.value("status").toObject();
    const QJsonObject cd = b.value("contentDetails").toObject();
    const QString id = b.value("id").toString();
    return QJsonObject{{"id", id},
                       {"title", sn.value("title")},
                       {"description", sn.value("description")},
                       {"status", st.value("lifeCycleStatus")},
                       {"privacy", st.value("privacyStatus")},
                       {"scheduledStart", sn.value("scheduledStartTime")},
                       {"actualStart", sn.value("actualStartTime")},
                       {"actualEnd", sn.value("actualEndTime")},
                       {"liveChatId", sn.value("liveChatId")},
                       {"boundStreamId", cd.value("boundStreamId")},
                       {"autoStart", cd.value("enableAutoStart")},
                       {"autoStop", cd.value("enableAutoStop")},
                       {"latency", cd.value("latencyPreference")},
                       {"thumbnail", bestThumb(sn.value("thumbnails").toObject())},
                       {"url", "https://youtu.be/" + id},
                       {"studioUrl", "https://studio.youtube.com/video/" + id + "/livestreaming"}};
}
} // namespace

void YouTubeClient::listBroadcasts(Done done) {
    QUrlQuery q;
    q.addQueryItem("part", "snippet,status,contentDetails");
    q.addQueryItem("mine", "true");
    q.addQueryItem("maxResults", "50");
    api("GET", "/liveBroadcasts", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("items").toArray())
            out.append(broadcastView(v.toObject()));
        done(true, QJsonObject{{"items", out}}, {});
    });
}

void YouTubeClient::createBroadcast(const QJsonObject& p, Done done) {
    const QString start =
        p.value("scheduledStart")
            .toString(QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs));
    const QJsonObject broadcast{
        {"snippet", QJsonObject{{"title", p.value("title").toString("Transmision en vivo")},
                                {"description", p.value("description").toString()},
                                {"scheduledStartTime", start}}},
        {"status", QJsonObject{{"privacyStatus", p.value("privacy").toString("private")},
                               {"selfDeclaredMadeForKids", p.value("madeForKids").toBool(false)}}},
        {"contentDetails", QJsonObject{{"enableAutoStart", p.value("autoStart").toBool(true)},
                                       {"enableAutoStop", p.value("autoStop").toBool(true)},
                                       {"enableDvr", p.value("dvr").toBool(true)},
                                       {"recordFromStart", true},
                                       {"latencyPreference", p.value("latency").toString("normal")}}}};
    QUrlQuery q;
    q.addQueryItem("part", "snippet,status,contentDetails");
    api("POST", "/liveBroadcasts", q, broadcast,
        [this, done](bool ok, const QJsonObject& b, const QString& e) {
            if (!ok) return done(false, {}, e);
            const QString id = b.value("id").toString();
            ingestion(id, [done, b](bool ok2, const QJsonObject& ing, const QString& e2) {
                QJsonObject out{{"broadcast", broadcastView(b)},
                                {"url", "https://youtu.be/" + b.value("id").toString()},
                                {"studioUrl", "https://studio.youtube.com/video/" + b.value("id").toString() +
                                                  "/livestreaming"}};
                if (!ok2)
                    return done(false, out, tr("El evento se creo pero no se pudo preparar la ingesta: %1").arg(e2));
                out.insert("ingestion", ing);
                done(true, out, {});
            });
        });
}

void YouTubeClient::ingestion(const QString& broadcastId, Done done) {
    QUrlQuery q;
    q.addQueryItem("part", "contentDetails,status");
    q.addQueryItem("id", broadcastId);
    api("GET", "/liveBroadcasts", q, {},
        [this, broadcastId, done](bool ok, const QJsonObject& d, const QString& e) {
            if (!ok) return done(false, {}, e);
            const QJsonObject b = d.value("items").toArray().at(0).toObject();
            if (b.isEmpty()) return done(false, {}, tr("No existe ese evento en vivo."));
            const auto describe = [this, done](const QString& streamId) {
                QUrlQuery sq;
                sq.addQueryItem("part", "cdn,status");
                sq.addQueryItem("id", streamId);
                api("GET", "/liveStreams", sq, {},
                    [done, streamId](bool ok3, const QJsonObject& sd, const QString& e3) {
                        if (!ok3) return done(false, {}, e3);
                        const QJsonObject s = sd.value("items").toArray().at(0).toObject();
                        const QJsonObject info = s.value("cdn").toObject().value("ingestionInfo").toObject();
                        done(true,
                             QJsonObject{{"url", info.value("ingestionAddress")},
                                         {"key", info.value("streamName")},
                                         {"streamId", streamId},
                                         {"streamStatus", s.value("status").toObject().value("streamStatus")},
                                         {"health", s.value("status")
                                                        .toObject()
                                                        .value("healthStatus")
                                                        .toObject()
                                                        .value("status")}},
                             {});
                    });
            };
            const QString bound = b.value("contentDetails").toObject().value("boundStreamId").toString();
            if (!bound.isEmpty()) return describe(bound);
            // Sin stream vinculado: se crea uno (RTMP, resolucion variable) y se vincula.
            QUrlQuery cq;
            cq.addQueryItem("part", "snippet,cdn,contentDetails,status");
            const QJsonObject stream{{"snippet", QJsonObject{{"title", "Broadcast Appliance"}}},
                                     {"cdn", QJsonObject{{"frameRate", "variable"},
                                                         {"ingestionType", "rtmp"},
                                                         {"resolution", "variable"}}},
                                     {"contentDetails", QJsonObject{{"isReusable", false}}}};
            api("POST", "/liveStreams", cq, stream,
                [this, broadcastId, describe, done](bool ok2, const QJsonObject& s, const QString& e2) {
                    if (!ok2) return done(false, {}, e2);
                    const QString sid = s.value("id").toString();
                    QUrlQuery bq;
                    bq.addQueryItem("id", broadcastId);
                    bq.addQueryItem("part", "id,contentDetails");
                    bq.addQueryItem("streamId", sid);
                    api("POST", "/liveBroadcasts/bind", bq, {},
                        [describe, sid, done](bool ok4, const QJsonObject&, const QString& e4) {
                            if (!ok4) return done(false, {}, e4);
                            describe(sid);
                        });
                });
        });
}

void YouTubeClient::transition(const QString& broadcastId, const QString& status, Done done) {
    QUrlQuery q;
    q.addQueryItem("broadcastStatus", status);
    q.addQueryItem("id", broadcastId);
    q.addQueryItem("part", "status");
    api("POST", "/liveBroadcasts/transition", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        done(ok, QJsonObject{{"status", d.value("status").toObject().value("lifeCycleStatus")}}, e);
    });
}

void YouTubeClient::updateBroadcast(const QString& id, const QJsonObject& changes, Done done) {
    QUrlQuery q;
    q.addQueryItem("part", "snippet,status,contentDetails");
    q.addQueryItem("id", id);
    api("GET", "/liveBroadcasts", q, {},
        [this, id, changes, done](bool ok, const QJsonObject& d, const QString& e) {
            if (!ok) return done(false, {}, e);
            const QJsonObject b = d.value("items").toArray().at(0).toObject();
            if (b.isEmpty()) return done(false, {}, tr("No existe ese evento en vivo."));
            const QJsonObject sn = b.value("snippet").toObject();
            const QJsonObject st = b.value("status").toObject();
            const QJsonObject cd = b.value("contentDetails").toObject();
            const QJsonObject body{
                {"id", id},
                {"snippet",
                 QJsonObject{
                     {"title", changes.value("title").toString(sn.value("title").toString())},
                     {"description",
                      changes.value("description").toString(sn.value("description").toString())},
                     {"scheduledStartTime",
                      changes.value("scheduledStart").toString(sn.value("scheduledStartTime").toString())}}},
                {"status", QJsonObject{{"privacyStatus", changes.value("privacy").toString(
                                                             st.value("privacyStatus").toString())}}},
                {"contentDetails", QJsonObject{{"enableDvr", cd.value("enableDvr")},
                                               {"recordFromStart", cd.value("recordFromStart")},
                                               {"enableAutoStart", cd.value("enableAutoStart")},
                                               {"enableAutoStop", cd.value("enableAutoStop")},
                                               {"monitorStream", cd.value("monitorStream")}}}};
            QUrlQuery uq;
            uq.addQueryItem("part", "snippet,status,contentDetails");
            api("PUT", "/liveBroadcasts", uq, body,
                [done](bool ok2, const QJsonObject& r, const QString& e2) {
                    done(ok2, ok2 ? broadcastView(r) : QJsonObject(), e2);
                });
        });
}

void YouTubeClient::deleteBroadcast(const QString& id, Done done) {
    QUrlQuery q;
    q.addQueryItem("id", id);
    api("DELETE", "/liveBroadcasts", q, {}, done);
}

void YouTubeClient::chatMessages(const QString& liveChatId, const QString& pageToken, Done done) {
    QUrlQuery q;
    q.addQueryItem("liveChatId", liveChatId);
    q.addQueryItem("part", "snippet,authorDetails");
    q.addQueryItem("maxResults", "200");
    if (!pageToken.isEmpty()) q.addQueryItem("pageToken", pageToken);
    api("GET", "/liveChat/messages", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("items").toArray()) {
            const QJsonObject m = v.toObject();
            const QJsonObject sn = m.value("snippet").toObject();
            const QJsonObject a = m.value("authorDetails").toObject();
            out.append(QJsonObject{{"id", m.value("id")},
                                   {"author", a.value("displayName")},
                                   {"text", sn.value("displayMessage")},
                                   {"publishedAt", sn.value("publishedAt")},
                                   {"owner", a.value("isChatOwner")},
                                   {"moderator", a.value("isChatModerator")},
                                   {"member", a.value("isChatSponsor")}});
        }
        done(true,
             QJsonObject{{"items", out},
                         {"nextPageToken", d.value("nextPageToken")},
                         {"pollingIntervalMillis", d.value("pollingIntervalMillis")}},
             {});
    });
}

void YouTubeClient::chatSend(const QString& liveChatId, const QString& text, Done done) {
    QUrlQuery q;
    q.addQueryItem("part", "snippet");
    const QJsonObject body{
        {"snippet", QJsonObject{{"liveChatId", liveChatId},
                                {"type", "textMessageEvent"},
                                {"textMessageDetails", QJsonObject{{"messageText", text}}}}}};
    api("POST", "/liveChat/messages", q, body, done);
}

} // namespace ba::app
