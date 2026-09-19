// ============================================================================
// src/app/facebook/FacebookClient.cpp — ver FacebookClient.hpp
// ============================================================================

#include "facebook/FacebookClient.hpp"

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
#include <QUrl>

#include "net/AsyncHttp.hpp"
#include "net/Credentials.hpp"

namespace ba::app {

namespace {

constexpr const char* kScopes =
    "pages_show_list,pages_read_engagement,pages_read_user_content,pages_manage_posts,"
    "pages_manage_engagement,publish_video";

QJsonObject parse(const std::string& b) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(b)).object();
}

QString randomState() {
    QString s;
    for (int i = 0; i < 32; ++i)
        s += QChar("abcdefghijklmnopqrstuvwxyz0123456789"[QRandomGenerator::system()->bounded(36)]);
    return s;
}

} // namespace

FacebookClient::FacebookClient(QString configDir, QObject* parent)
    : QObject(parent), configDir_(std::move(configDir)) {
    openBrowser_ = [](const QString& u) { QDesktopServices::openUrl(QUrl(u)); };
    loadFiles();
}

FacebookClient::~FacebookClient() { cancelLogin(); }

void FacebookClient::loadFiles() {
    if (QByteArray cRaw; cred::read(configDir_, "facebook-client", cRaw)) {
        const QJsonObject o = QJsonDocument::fromJson(cRaw).object();
        appId_ = o.value("app_id").toString();
        appSecret_ = o.value("app_secret").toString();
        port_ = o.value("port").toInt(8791);
    }
    if (QByteArray tRaw; cred::read(configDir_, "facebook-token", tRaw)) {
        const QJsonObject o = QJsonDocument::fromJson(tRaw).object();
        userToken_ = o.value("user_token").toString();
        expiresAtMs_ = static_cast<qint64>(o.value("expires_at").toDouble());
        user_ = o.value("user").toObject();
        pages_ = o.value("pages").toArray();
        pageId_ = o.value("page_id").toString();
    }
}

void FacebookClient::persist() {
    if (userToken_.isEmpty()) {
        cred::remove(configDir_, "facebook-token");
        return;
    }
    const QJsonObject o{{"user_token", userToken_},
                        {"expires_at", static_cast<double>(expiresAtMs_)},
                        {"user", user_},
                        {"pages", pages_},
                        {"page_id", pageId_}};
    cred::write(configDir_, "facebook-token", QJsonDocument(o).toJson());
}

bool FacebookClient::saveClient(const QString& appId, const QString& appSecret, int port, QString& error) {
    const QString id = appId.trimmed();
    const QString sec = appSecret.trimmed();
    bool digits = !id.isEmpty();
    for (const QChar c : id)
        digits = digits && c.isDigit();
    if (!digits || id.size() < 10) {
        error = "El ID de la app de Meta es un numero largo (Panel de la app -> Configuracion -> Basica).";
        return false;
    }
    if (sec.size() < 16) {
        error = "Falta la clave secreta de la app (Configuracion -> Basica -> Clave secreta, \"Mostrar\").";
        return false;
    }
    const QJsonObject o{{"app_id", id}, {"app_secret", sec}, {"port", std::clamp(port, 1024, 65535)}};
    if (!cred::write(configDir_, "facebook-client", QJsonDocument(o).toJson())) {
        error = "No se pudo guardar en " + configDir_;
        return false;
    }
    if (id != appId_) {
        userToken_.clear();
        pages_ = {};
        user_ = {};
        pageId_.clear();
        persist();
    }
    appId_ = id;
    appSecret_ = sec;
    port_ = std::clamp(port, 1024, 65535);
    emit stateChanged();
    return true;
}

void FacebookClient::clearClient() {
    appId_.clear();
    appSecret_.clear();
    userToken_.clear();
    pages_ = {};
    user_ = {};
    pageId_.clear();
    cred::remove(configDir_, "facebook-client");
    persist();
    emit stateChanged();
}

QJsonArray FacebookClient::pages() const {
    QJsonArray out;
    for (const QJsonValue& v : pages_) {
        QJsonObject p = v.toObject();
        p.remove("access_token");
        out.append(p);
    }
    return out;
}

QJsonObject FacebookClient::page() const {
    for (const QJsonValue& v : pages())
        if (v.toObject().value("id").toString() == pageId_) return v.toObject();
    return {};
}

QString FacebookClient::pageToken() const {
    for (const QJsonValue& v : pages_)
        if (v.toObject().value("id").toString() == pageId_)
            return v.toObject().value("access_token").toString();
    return {};
}

bool FacebookClient::selectPage(const QString& id) {
    for (const QJsonValue& v : pages_) {
        if (v.toObject().value("id").toString() == id) {
            pageId_ = id;
            persist();
            emit stateChanged();
            return true;
        }
    }
    return false;
}

QString FacebookClient::explain(int status, const QByteArray& body) {
    const QJsonObject e = QJsonDocument::fromJson(body).object().value("error").toObject();
    const int code = e.value("code").toInt();
    const QString msg = e.value("message").toString();
    if (code == 190) return "La sesion de Facebook vencio o fue revocada: conecta la cuenta de nuevo.";
    if (code == 200 || code == 10) return "Falta un permiso (o la app no lo tiene aprobado): " + msg;
    if (code == 4 || code == 17 || code == 32) return "Facebook pide esperar (limite de pedidos).";
    if (msg.contains("redirect_uri", Qt::CaseInsensitive))
        return "Facebook rechazo la redireccion: agrega la URL de redireccion en la app de Meta (Inicio de "
               "sesion con Facebook -> Configuracion).";
    if (!msg.isEmpty()) return "Facebook: " + msg;
    return QString("Facebook respondio %1").arg(status);
}

// --- Login ------------------------------------------------------------------------------

void FacebookClient::login(Done done) {
    if (!hasClient()) return done(false, {}, "Falta configurar la app de Meta (ID y clave secreta).");
    cancelLogin();
    loginServer_ = new QTcpServer(this);
    if (!loginServer_->listen(QHostAddress::LocalHost, static_cast<quint16>(port_))) {
        delete loginServer_;
        loginServer_ = nullptr;
        return done(
            false, {},
            QString(
                "El puerto %1 esta ocupado: cambialo en la configuracion de Facebook (y en la app de Meta).")
                .arg(port_));
    }
    loginState_ = randomState();
    loginDone_ = std::move(done);
    QUrlQuery q;
    q.addQueryItem("client_id", appId_);
    q.addQueryItem("redirect_uri", redirectUri());
    q.addQueryItem("state", loginState_);
    q.addQueryItem("scope", kScopes);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("auth_type", "rerequest");
    QUrl url(ep_.dialog);
    url.setQuery(q);
    connect(loginServer_, &QTcpServer::newConnection, this, [this]() {
        while (QTcpSocket* s = loginServer_ ? loginServer_->nextPendingConnection() : nullptr) {
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::readyRead, this, [this, s]() {
                const QByteArray head = s->readAll();
                const QList<QByteArray> parts = head.left(head.indexOf("\r\n")).split(' ');
                const QUrlQuery q(QUrl("http://localhost" + QString::fromLatin1(parts.value(1))));
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
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: "
                             "close\r\nContent-Length: " +
                             QByteArray::number(html.size()) + "\r\n\r\n" + html);
                    s->disconnectFromHost();
                };
                if (!q.hasQueryItem("code") && !q.hasQueryItem("error"))
                    return page("…", "Esperando a Facebook.", false);
                if (q.queryItemValue("state") != loginState_)
                    return page("No coincide", "Este enlace no corresponde al login en curso.", false);
                if (q.hasQueryItem("error")) {
                    page("Cancelado", "No se dio permiso. Podes cerrar esta pestaña.", false);
                    return finishLogin(false, "Cancelaste el permiso en Facebook.");
                }
                page("¡Listo!",
                     "Broadcast Appliance ya puede usar tus Paginas de Facebook. Podes cerrar esta pestaña.",
                     true);
                exchange(q.queryItemValue("code", QUrl::FullyDecoded),
                         [this](bool ok, const QJsonObject&, const QString& e) { finishLogin(ok, e); });
            });
        }
    });
    QPointer<QTcpServer> server(loginServer_);
    QTimer::singleShot(5 * 60 * 1000, this, [this, server]() {
        if (server && server == loginServer_) finishLogin(false, "El login no se completo en 5 minutos.");
    });
    emit stateChanged();
    openBrowser_(url.toString(QUrl::FullyEncoded));
}

void FacebookClient::exchange(const QString& code, Done done) {
    // Codigo -> token corto -> token largo -> usuario -> Paginas.
    QUrlQuery q;
    q.addQueryItem("client_id", appId_);
    q.addQueryItem("client_secret", appSecret_);
    q.addQueryItem("redirect_uri", redirectUri());
    q.addQueryItem("code", code);
    net::HttpRequest req;
    req.url = (ep_.graph + "/oauth/access_token?" + q.toString(QUrl::FullyEncoded)).toStdString();
    QPointer<FacebookClient> self(this);
    net::asyncRequest(this, req, [self, done](const net::HttpResponse& r) {
        if (!self) return;
        const QJsonObject o = parse(r.body);
        if (r.status != 200 || !o.contains("access_token"))
            return done(false, {},
                        r.status == 0 ? "Sin conexion con Facebook: " + QString::fromStdString(r.error)
                                      : explain(r.status, QByteArray::fromStdString(r.body)));
        QUrlQuery lq;
        lq.addQueryItem("grant_type", "fb_exchange_token");
        lq.addQueryItem("client_id", self->appId_);
        lq.addQueryItem("client_secret", self->appSecret_);
        lq.addQueryItem("fb_exchange_token", o.value("access_token").toString());
        net::HttpRequest lr;
        lr.url = (self->ep_.graph + "/oauth/access_token?" + lq.toString(QUrl::FullyEncoded)).toStdString();
        net::asyncRequest(self, lr, [self, done](const net::HttpResponse& r2) {
            if (!self) return;
            const QJsonObject lo = parse(r2.body);
            if (r2.status != 200 || !lo.contains("access_token"))
                return done(false, {}, explain(r2.status, QByteArray::fromStdString(r2.body)));
            self->userToken_ = lo.value("access_token").toString();
            self->expiresAtMs_ =
                QDateTime::currentMSecsSinceEpoch() + lo.value("expires_in").toInt(60 * 86400) * 1000LL;
            self->loadPages(done);
        });
    });
}

void FacebookClient::loadPages(Done done) {
    QUrlQuery q;
    q.addQueryItem("fields", "id,name");
    q.addQueryItem("access_token", userToken_);
    net::HttpRequest req;
    req.url = (ep_.graph + "/me?" + q.toString(QUrl::FullyEncoded)).toStdString();
    QPointer<FacebookClient> self(this);
    net::asyncRequest(this, req, [self, done](const net::HttpResponse& r) {
        if (!self) return;
        if (r.status != 200) return done(false, {}, explain(r.status, QByteArray::fromStdString(r.body)));
        self->user_ = parse(r.body);
        QUrlQuery pq;
        pq.addQueryItem("fields", "id,name,category,access_token,picture{url}");
        pq.addQueryItem("limit", "100");
        pq.addQueryItem("access_token", self->userToken_);
        net::HttpRequest pr;
        pr.url = (self->ep_.graph + "/me/accounts?" + pq.toString(QUrl::FullyEncoded)).toStdString();
        net::asyncRequest(self, pr, [self, done](const net::HttpResponse& r2) {
            if (!self) return;
            if (r2.status != 200)
                return done(false, {}, explain(r2.status, QByteArray::fromStdString(r2.body)));
            QJsonArray pages;
            for (const QJsonValue& v : parse(r2.body).value("data").toArray()) {
                const QJsonObject p = v.toObject();
                pages.append(QJsonObject{
                    {"id", p.value("id")},
                    {"name", p.value("name")},
                    {"category", p.value("category")},
                    {"picture", p.value("picture").toObject().value("data").toObject().value("url")},
                    {"access_token", p.value("access_token")}});
            }
            self->pages_ = pages;
            bool still = false;
            for (const QJsonValue& v : pages)
                still = still || v.toObject().value("id").toString() == self->pageId_;
            if (!still)
                self->pageId_ = pages.isEmpty() ? QString() : pages.at(0).toObject().value("id").toString();
            self->persist();
            emit self->stateChanged();
            if (pages.isEmpty())
                return done(false, {},
                            "La cuenta no administra ninguna Pagina de Facebook (la API solo permite "
                            "transmitir a Paginas).");
            done(true, QJsonObject{{"user", self->user_}, {"pages", self->pages()}}, {});
        });
    });
}

void FacebookClient::finishLogin(bool ok, const QString& error) {
    if (loginServer_) {
        loginServer_->close();
        loginServer_->deleteLater();
        loginServer_ = nullptr;
    }
    Done d = std::move(loginDone_);
    loginDone_ = nullptr;
    emit stateChanged();
    if (d) d(ok, QJsonObject{{"user", user_}, {"pages", pages()}}, error);
}

void FacebookClient::cancelLogin() {
    if (loginServer_) finishLogin(false, "Login cancelado.");
}

void FacebookClient::logout(Done done) {
    const QString token = userToken_;
    userToken_.clear();
    pages_ = {};
    user_ = {};
    pageId_.clear();
    persist();
    emit stateChanged();
    if (token.isEmpty()) return done(true, {}, {});
    // Revoca los permisos de la app para esa cuenta.
    net::HttpRequest req;
    req.method = "DELETE";
    req.url = (ep_.graph +
               "/me/permissions?access_token=" + QString::fromStdString(net::urlEncode(token.toStdString())))
                  .toStdString();
    net::asyncRequest(this, req, [done](const net::HttpResponse&) { done(true, {}, {}); });
}

// --- Graph ----------------------------------------------------------------------------------

void FacebookClient::graph(const QString& method, const QString& path, const QUrlQuery& params, Done done) {
    if (!loggedIn()) return done(false, {}, "No hay una cuenta de Facebook conectada.");
    const QString token = pageToken();
    if (token.isEmpty()) return done(false, {}, "No hay una Pagina de Facebook elegida.");
    QUrlQuery q = params;
    q.addQueryItem("access_token", token);
    net::HttpRequest req;
    req.method = method.toStdString();
    if (method == "POST") {
        req.url = (ep_.graph + path).toStdString();
        req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
        req.body = q.toString(QUrl::FullyEncoded).toStdString();
    } else {
        req.url = (ep_.graph + path + "?" + q.toString(QUrl::FullyEncoded)).toStdString();
    }
    QPointer<FacebookClient> self(this);
    net::asyncRequest(this, req, [self, done](const net::HttpResponse& r) {
        if (!self) return;
        if (r.status >= 200 && r.status < 300) return done(true, parse(r.body), {});
        const QString e = r.status == 0 ? "Sin conexion con Facebook: " + QString::fromStdString(r.error)
                                        : explain(r.status, QByteArray::fromStdString(r.body));
        if (parse(r.body).value("error").toObject().value("code").toInt() == 190) { // token invalido
            self->userToken_.clear();
            self->persist();
            emit self->stateChanged();
        }
        done(false, parse(r.body), e);
    });
}

bool FacebookClient::splitIngest(const QString& url, QString& server, QString& key) {
    // rtmps://live-api-s.facebook.com:443/rtmp/FB-123-0-Abc?s_bl=...  ->  servidor .../rtmp/ + clave FB-...
    const int at = url.indexOf("/rtmp/");
    if (at < 0) return false;
    server = url.left(at + 6);
    key = url.mid(at + 6);
    return !key.isEmpty();
}

void FacebookClient::liveVideos(Done done) {
    QUrlQuery q;
    q.addQueryItem("fields", "id,title,description,status,permalink_url,live_views,creation_time");
    q.addQueryItem("limit", "50");
    graph("GET", "/" + pageId_ + "/live_videos", q, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            QString link = o.value("permalink_url").toString();
            if (link.startsWith('/')) link = "https://www.facebook.com" + link;
            out.append(QJsonObject{{"id", o.value("id")},
                                   {"title", o.value("title")},
                                   {"description", o.value("description")},
                                   {"status", o.value("status")},
                                   {"url", link},
                                   {"views", o.value("live_views")},
                                   {"created", o.value("creation_time")}});
        }
        done(true, QJsonObject{{"items", out}}, {});
    });
}

void FacebookClient::createLive(const QJsonObject& p, Done done) {
    QUrlQuery q;
    q.addQueryItem("status", p.value("status").toString("LIVE_NOW"));
    q.addQueryItem("title", p.value("title").toString("Transmision en vivo"));
    if (p.contains("description")) q.addQueryItem("description", p.value("description").toString());
    graph("POST", "/" + pageId_ + "/live_videos", q, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QString server, key;
        const QString secure = d.value("secure_stream_url").toString(d.value("stream_url").toString());
        if (!splitIngest(secure, server, key))
            return done(false, {}, "Facebook no entrego la direccion de ingesta.");
        done(true,
             QJsonObject{{"id", d.value("id")}, {"ingestion", QJsonObject{{"url", server}, {"key", key}}}},
             {});
    });
}

void FacebookClient::liveStatus(const QString& id, Done done) {
    QUrlQuery q;
    q.addQueryItem("fields", "id,title,status,live_views,permalink_url,secure_stream_url");
    graph("GET", "/" + id, q, [done](bool ok, QJsonObject d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QString server, key;
        if (splitIngest(d.value("secure_stream_url").toString(), server, key))
            d.insert("ingestion", QJsonObject{{"url", server}, {"key", key}});
        d.remove("secure_stream_url");
        done(true, d, {});
    });
}

void FacebookClient::publishLive(const QString& id, Done done) {
    QUrlQuery q;
    q.addQueryItem("status", "LIVE_NOW");
    graph("POST", "/" + id, q, done);
}

void FacebookClient::endLive(const QString& id, Done done) {
    QUrlQuery q;
    q.addQueryItem("end_live_video", "true");
    graph("POST", "/" + id, q, done);
}

void FacebookClient::deleteObject(const QString& id, Done done) { graph("DELETE", "/" + id, {}, done); }

void FacebookClient::videos(int max, Done done) {
    QUrlQuery q;
    q.addQueryItem("fields", "id,title,description,permalink_url,picture,created_time,length");
    q.addQueryItem("limit", QString::number(std::clamp(max, 1, 100)));
    graph("GET", "/" + pageId_ + "/videos", q, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            QString link = o.value("permalink_url").toString();
            if (link.startsWith('/')) link = "https://www.facebook.com" + link;
            out.append(QJsonObject{{"id", o.value("id")},
                                   {"title", o.value("title")},
                                   {"description", o.value("description")},
                                   {"url", link},
                                   {"picture", o.value("picture")},
                                   {"created", o.value("created_time")},
                                   {"length", o.value("length")}});
        }
        done(true, QJsonObject{{"items", out}}, {});
    });
}

void FacebookClient::updateVideo(const QString& id, const QJsonObject& changes, Done done) {
    QUrlQuery q;
    if (changes.contains("title")) q.addQueryItem("name", changes.value("title").toString());
    if (changes.contains("description"))
        q.addQueryItem("description", changes.value("description").toString());
    graph("POST", "/" + id, q, done);
}

void FacebookClient::comments(const QString& liveId, Done done) {
    QUrlQuery q;
    q.addQueryItem("fields", "id,from{name},message,created_time");
    q.addQueryItem("order", "reverse_chronological");
    q.addQueryItem("filter", "stream");
    q.addQueryItem("limit", "50");
    graph("GET", "/" + liveId + "/comments", q, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        const QJsonArray data = d.value("data").toArray();
        for (int i = data.size() - 1; i >= 0; --i) { // de viejo a nuevo
            const QJsonObject o = data.at(i).toObject();
            out.append(QJsonObject{
                {"id", o.value("id")},
                {"author", o.value("from").toObject().value("name").toString("Alguien")},
                {"text", o.value("message")},
                {"atMs",
                 static_cast<double>(QDateTime::fromString(o.value("created_time").toString(), Qt::ISODate)
                                         .toMSecsSinceEpoch())}});
        }
        done(true, QJsonObject{{"items", out}}, {});
    });
}

void FacebookClient::comment(const QString& liveId, const QString& text, Done done) {
    QUrlQuery q;
    q.addQueryItem("message", text);
    graph("POST", "/" + liveId + "/comments", q, done);
}

// --- Subida por partes ---------------------------------------------------------------------

void FacebookClient::uploadVideo(const QString& path, const QJsonObject& meta, Done done) {
    const QFileInfo fi(path);
    if (!fi.isFile()) return done(false, {}, "No existe el archivo " + path);
    if (pageToken().isEmpty()) return done(false, {}, "No hay una Pagina de Facebook elegida.");
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.video + "/" + pageId_ + "/videos").toStdString();
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    QUrlQuery q;
    q.addQueryItem("upload_phase", "start");
    q.addQueryItem("file_size", QString::number(fi.size()));
    q.addQueryItem("access_token", pageToken());
    req.body = q.toString(QUrl::FullyEncoded).toStdString();
    QPointer<FacebookClient> self(this);
    net::asyncRequest(this, req, [self, path, fi, meta, done](const net::HttpResponse& r) {
        if (!self) return;
        const QJsonObject o = parse(r.body);
        if (r.status != 200 || !o.contains("upload_session_id"))
            return done(false, {},
                        r.status == 0 ? "Sin conexion con Facebook: " + QString::fromStdString(r.error)
                                      : explain(r.status, QByteArray::fromStdString(r.body)));
        self->uploadChunk(path, o.value("upload_session_id").toString(),
                          o.value("start_offset").toString().toLongLong(),
                          o.value("end_offset").toString().toLongLong(), fi.size(), meta,
                          o.value("video_id").toString(), done);
    });
}

void FacebookClient::uploadChunk(const QString& path, const QString& session, qint64 start, qint64 end,
                                 qint64 total, const QJsonObject& meta, const QString& videoId, Done done) {
    QPointer<FacebookClient> self(this);
    if (start >= end) { // todo subido: cerrar
        net::HttpRequest req;
        req.method = "POST";
        req.url = (ep_.video + "/" + pageId_ + "/videos").toStdString();
        req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
        QUrlQuery q;
        q.addQueryItem("upload_phase", "finish");
        q.addQueryItem("upload_session_id", session);
        q.addQueryItem("title", meta.value("title").toString(QFileInfo(path).completeBaseName()));
        q.addQueryItem("description", meta.value("description").toString());
        q.addQueryItem("access_token", pageToken());
        req.body = q.toString(QUrl::FullyEncoded).toStdString();
        net::asyncRequest(this, req, [videoId, done](const net::HttpResponse& r) {
            if (r.status == 200)
                return done(
                    true,
                    QJsonObject{{"id", videoId}, {"url", "https://www.facebook.com/watch/?v=" + videoId}},
                    {});
            done(false, {}, explain(r.status, QByteArray::fromStdString(r.body)));
        });
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(start)) return done(false, {}, "No se pudo leer " + path);
    const QByteArray chunk = f.read(end - start);
    const std::string boundary = "----ba" + std::to_string(QRandomGenerator::system()->generate());
    const auto field = [&boundary](const std::string& k, const std::string& v) {
        return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + k + "\"\r\n\r\n" + v +
               "\r\n";
    };
    std::string body = field("upload_phase", "transfer") + field("upload_session_id", session.toStdString()) +
                       field("start_offset", std::to_string(start)) +
                       field("access_token", pageToken().toStdString()) + "--" + boundary +
                       "\r\nContent-Disposition: form-data; name=\"video_file_chunk\"; filename=\"chunk\"\r\n"
                       "Content-Type: application/octet-stream\r\n\r\n" +
                       chunk.toStdString() + "\r\n--" + boundary + "--\r\n";
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.video + "/" + pageId_ + "/videos").toStdString();
    req.headers = {{"Content-Type", "multipart/form-data; boundary=" + boundary}};
    req.body = std::move(body);
    req.timeoutMs = 120000;
    net::asyncRequest(
        this, req, [self, path, session, total, meta, videoId, done](const net::HttpResponse& r) {
            if (!self) return;
            const QJsonObject o = parse(r.body);
            if (r.status != 200 || !o.contains("start_offset"))
                return done(false, {},
                            r.status == 0 ? "La subida se corto: " + QString::fromStdString(r.error)
                                          : explain(r.status, QByteArray::fromStdString(r.body)));
            const qint64 ns = o.value("start_offset").toString().toLongLong();
            const qint64 ne = o.value("end_offset").toString().toLongLong();
            emit self->uploadProgress(path, ns, total);
            self->uploadChunk(path, session, ns, ne, total, meta, videoId, done);
        });
}

} // namespace ba::app
