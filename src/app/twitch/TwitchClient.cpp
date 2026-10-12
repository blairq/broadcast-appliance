// ============================================================================
// src/app/twitch/TwitchClient.cpp — ver TwitchClient.hpp
// ============================================================================

#include "twitch/TwitchClient.hpp"
#include "ajustes/Ajustes.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <map>
#include <string>

#include "net/AsyncHttp.hpp"
#include "net/Credentials.hpp"

namespace ba::app {

namespace {

constexpr const char* kScopes =
    "channel:manage:broadcast channel:read:stream_key channel:manage:videos chat:read chat:edit";

QJsonObject parse(const std::string& body) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
}

std::string form(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out;
    for (const auto& [k, v] : kv) {
        if (!out.empty()) out += '&';
        out += net::urlEncode(k) + '=' + net::urlEncode(v);
    }
    return out;
}

} // namespace

TwitchClient::TwitchClient(QString configDir, QObject* parent)
    : QObject(parent), configDir_(std::move(configDir)) {
    openBrowser_ = [](const QString& u) { QDesktopServices::openUrl(QUrl(u)); };
    loadFiles();
}

TwitchClient::~TwitchClient() { stopChat(); }

void TwitchClient::loadFiles() {
    if (QByteArray cRaw; cred::read(configDir_, "twitch-client", cRaw)) {
        const QJsonObject o = QJsonDocument::fromJson(cRaw).object();
        clientId_ = o.value("client_id").toString();
        clientSecret_ = o.value("client_secret").toString();
    }
    if (QByteArray tRaw; cred::read(configDir_, "twitch-token", tRaw)) {
        const QJsonObject o = QJsonDocument::fromJson(tRaw).object();
        refreshToken_ = o.value("refresh_token").toString();
        accessToken_ = o.value("access_token").toString();
        expiresAtMs_ = static_cast<qint64>(o.value("expires_at").toDouble());
        user_ = o.value("user").toObject();
    }
}

void TwitchClient::persistToken() {
    if (refreshToken_.isEmpty()) {
        cred::remove(configDir_, "twitch-token");
        return;
    }
    // El access token de Twitch dura horas: se guarda tambien (evita un
    // refresh en cada arranque; sigue siendo solo-dueño dentro del bundle).
    const QJsonObject o{{"access_token", accessToken_},
                        {"refresh_token", refreshToken_},
                        {"expires_at", static_cast<double>(expiresAtMs_)},
                        {"user", user_}};
    cred::write(configDir_, "twitch-token", QJsonDocument(o).toJson());
}

bool TwitchClient::saveClient(const QString& id, const QString& secret, QString& error) {
    const QString i = id.trimmed();
    if (i.size() < 20 || i.contains(' ')) {
        error = "El Client ID de Twitch son unos 30 caracteres (lo muestra la consola de desarrolladores al "
                "administrar la app).";
        return false;
    }
    QJsonObject o{{"client_id", i}};
    if (!secret.trimmed().isEmpty()) o.insert("client_secret", secret.trimmed());
    if (!cred::write(configDir_, "twitch-client", QJsonDocument(o).toJson())) {
        error = "No se pudo guardar en " + configDir_;
        return false;
    }
    if (i != clientId_) {
        stopChat();
        refreshToken_.clear();
        accessToken_.clear();
        user_ = {};
        persistToken();
    }
    clientId_ = i;
    clientSecret_ = secret.trimmed();
    emit stateChanged();
    return true;
}

void TwitchClient::clearClient() {
    stopChat();
    clientId_.clear();
    clientSecret_.clear();
    refreshToken_.clear();
    accessToken_.clear();
    user_ = {};
    cred::remove(configDir_, "twitch-client");
    persistToken();
    emit stateChanged();
}

QString TwitchClient::explain(int status, const QByteArray& body) {
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString msg = o.value("message").toString();
    if (msg == "invalid refresh token" || msg.contains("Invalid refresh token", Qt::CaseInsensitive))
        return "La sesion de Twitch vencio o fue revocada: conecta la cuenta de nuevo.";
    if (msg.contains("missing required oauth scope", Qt::CaseInsensitive))
        return "Falta un permiso: desconecta y volve a conectar la cuenta.";
    if (msg.contains("invalid client", Qt::CaseInsensitive)) return "El Client ID de Twitch no es valido.";
    if (status == 429) return "Twitch pide esperar (demasiados pedidos seguidos).";
    if (!msg.isEmpty()) return "Twitch: " + msg;
    return QString("Twitch respondio %1").arg(status);
}

// --- Login por codigo ------------------------------------------------------------------

void TwitchClient::login(Done done) {
    if (!hasClient()) return done(false, {}, "Falta el Client ID de la app de Twitch.");
    cancelLogin();
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.id + "/device").toStdString();
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req.body = form({{"client_id", clientId_.toStdString()}, {"scopes", kScopes}});
    QPointer<TwitchClient> self(this);
    net::asyncRequest(this, req, [self, done](const net::HttpResponse& r) {
        const QJsonObject o = parse(r.body);
        if (r.status != 200 || !o.contains("device_code"))
            return done(false, {},
                        r.status == 0 ? "Sin conexion con Twitch: " + QString::fromStdString(r.error)
                                      : explain(r.status, QByteArray::fromStdString(r.body)));
        self->deviceCode_ = o.value("device_code").toString();
        self->userCode_ = o.value("user_code").toString();
        self->verificationUri_ = o.value("verification_uri").toString();
        self->pollIntervalMs_ = std::max(2, o.value("interval").toInt(5)) * 1000;
        self->deviceExpiresMs_ =
            QDateTime::currentMSecsSinceEpoch() + o.value("expires_in").toInt(1800) * 1000LL;
        self->loginDone_ = done;
        emit self->stateChanged();
        self->openBrowser_(self->verificationUri_);
        QTimer::singleShot(self->pollIntervalMs_, self, [self]() {
            if (self && self->loginPending()) self->pollDevice(self->loginDone_);
        });
    });
}

void TwitchClient::pollDevice(Done done) {
    if (!loginPending()) return;
    if (QDateTime::currentMSecsSinceEpoch() > deviceExpiresMs_) {
        cancelLogin();
        return done(false, {}, "El codigo vencio sin que se autorizara. Proba de nuevo.");
    }
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.id + "/token").toStdString();
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    std::vector<std::pair<std::string, std::string>> kv{
        {"client_id", clientId_.toStdString()},
        {"scopes", kScopes},
        {"device_code", deviceCode_.toStdString()},
        {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}};
    if (!clientSecret_.isEmpty()) kv.emplace_back("client_secret", clientSecret_.toStdString());
    req.body = form(kv);
    const QString code = deviceCode_;
    QPointer<TwitchClient> self(this);
    net::asyncRequest(this, req, [self, done, code](const net::HttpResponse& r) {
        if (!self || self->deviceCode_ != code) return; // cancelado
        const QJsonObject o = parse(r.body);
        if (r.status == 200 && o.contains("access_token")) {
            self->accessToken_ = o.value("access_token").toString();
            self->refreshToken_ = o.value("refresh_token").toString();
            self->expiresAtMs_ =
                QDateTime::currentMSecsSinceEpoch() + o.value("expires_in").toInt(14000) * 1000LL;
            self->deviceCode_.clear();
            self->userCode_.clear();
            self->loginDone_ = nullptr;
            self->persistToken();
            self->loadUser(done);
            return;
        }
        const QString msg = o.value("message").toString();
        if (msg == "authorization_pending" || msg == "slow_down" || r.status == 0) {
            QTimer::singleShot(self->pollIntervalMs_ + (msg == "slow_down" ? 5000 : 0), self,
                               [self, done, code]() {
                                   if (self && self->deviceCode_ == code) self->pollDevice(done);
                               });
            return;
        }
        self->cancelLogin();
        done(false, {},
             msg == "access_denied" ? "Cancelaste el permiso en Twitch."
                                    : explain(r.status, QByteArray::fromStdString(r.body)));
    });
}

void TwitchClient::cancelLogin() {
    if (deviceCode_.isEmpty()) return;
    deviceCode_.clear();
    userCode_.clear();
    loginDone_ = nullptr;
    emit stateChanged();
}

void TwitchClient::loadUser(Done done) {
    api("GET", "/users", {}, {}, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QJsonObject u = d.value("data").toArray().at(0).toObject();
        user_ = QJsonObject{{"id", u.value("id")},
                            {"login", u.value("login")},
                            {"display_name", u.value("display_name")},
                            {"profile_image_url", u.value("profile_image_url")}};
        persistToken();
        emit stateChanged();
        done(true, user_, {});
    });
}

void TwitchClient::logout(Done done) {
    stopChat();
    const std::string token = accessToken_.toStdString();
    accessToken_.clear();
    refreshToken_.clear();
    user_ = {};
    persistToken();
    emit stateChanged();
    if (token.empty()) return done(true, {}, {});
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.id + "/revoke").toStdString();
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req.body = form({{"client_id", clientId_.toStdString()}, {"token", token}});
    net::asyncRequest(this, req, [done](const net::HttpResponse&) { done(true, {}, {}); });
}

// --- Tokens y API ----------------------------------------------------------------------

void TwitchClient::ensureToken(std::function<void(QString, QString)> cb) {
    if (!loggedIn()) return cb({}, "No hay una cuenta de Twitch conectada.");
    if (!accessToken_.isEmpty() && QDateTime::currentMSecsSinceEpoch() < expiresAtMs_ - 60000)
        return cb(accessToken_, {});
    tokenWaiters_.push_back(std::move(cb));
    if (refreshing_) return;
    refreshing_ = true;
    net::HttpRequest req;
    req.method = "POST";
    req.url = (ep_.id + "/token").toStdString();
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    std::vector<std::pair<std::string, std::string>> kv{{"grant_type", "refresh_token"},
                                                        {"refresh_token", refreshToken_.toStdString()},
                                                        {"client_id", clientId_.toStdString()}};
    if (!clientSecret_.isEmpty()) kv.emplace_back("client_secret", clientSecret_.toStdString());
    req.body = form(kv);
    net::asyncRequest(this, req, [this](const net::HttpResponse& r) {
        refreshing_ = false;
        const QJsonObject o = parse(r.body);
        QString token;
        QString err;
        if (r.status == 200 && o.contains("access_token")) {
            accessToken_ = o.value("access_token").toString();
            refreshToken_ =
                o.value("refresh_token").toString(refreshToken_); // las apps publicas rotan el refresh
            expiresAtMs_ = QDateTime::currentMSecsSinceEpoch() + o.value("expires_in").toInt(14000) * 1000LL;
            persistToken();
            token = accessToken_;
        } else if (r.status == 0) {
            err = "Sin conexion con Twitch: " + QString::fromStdString(r.error);
        } else {
            err = explain(r.status, QByteArray::fromStdString(r.body));
            if (r.status == 400 || r.status == 401) { // refresh invalido: hay que reconectar
                refreshToken_.clear();
                accessToken_.clear();
                persistToken();
                emit stateChanged();
            }
        }
        auto waiters = std::move(tokenWaiters_);
        tokenWaiters_.clear();
        for (auto& w : waiters)
            w(token, err);
    });
}

void TwitchClient::api(const QString& method, const QString& path, const QUrlQuery& query,
                       const QJsonObject& body, Done done) {
    auto attempt = std::make_shared<std::function<void(bool)>>();
    QPointer<TwitchClient> self(this);
    *attempt = [self, method, path, query, body, done, attempt](bool retried) {
        if (!self) return;
        self->ensureToken(
            [self, method, path, query, body, done, attempt, retried](QString token, QString err) {
                if (!self) return;
                if (token.isEmpty()) {
                    *attempt = nullptr;
                    return done(false, {}, err);
                }
                net::HttpRequest req;
                req.method = method.toStdString();
                QString url = self->ep_.api + path;
                if (!query.isEmpty()) url += '?' + query.toString(QUrl::FullyEncoded);
                req.url = url.toStdString();
                req.headers = {{"Authorization", "Bearer " + token.toStdString()},
                               {"Client-Id", self->clientId_.toStdString()}};
                if (!body.isEmpty()) {
                    req.headers.emplace_back("Content-Type", "application/json");
                    req.body = QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
                }
                net::asyncRequest(self, req, [self, done, attempt, retried](const net::HttpResponse& r) {
                    if (!self) return;
                    if (r.status == 401 && !retried) {
                        self->accessToken_.clear();
                        (*attempt)(true);
                        return;
                    }
                    *attempt = nullptr;
                    if (r.status >= 200 && r.status < 300) return done(true, parse(r.body), {});
                    done(false, parse(r.body),
                         r.status == 0 ? "Sin conexion con Twitch: " + QString::fromStdString(r.error)
                                       : explain(r.status, QByteArray::fromStdString(r.body)));
                });
            });
    };
    (*attempt)(false);
}

void TwitchClient::channel(Done done) {
    QUrlQuery q;
    q.addQueryItem("broadcaster_id", user_.value("id").toString());
    api("GET", "/channels", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QJsonObject c = d.value("data").toArray().at(0).toObject();
        done(true,
             QJsonObject{{"title", c.value("title")},
                         {"game_id", c.value("game_id")},
                         {"game_name", c.value("game_name")},
                         {"tags", c.value("tags")},
                         {"broadcaster_language", c.value("broadcaster_language")}},
             {});
    });
}

void TwitchClient::updateChannel(const QJsonObject& changes, Done done) {
    const auto patch = [this, changes, done](const QString& gameId) {
        QJsonObject body;
        if (changes.contains("title")) body.insert("title", changes.value("title"));
        if (!gameId.isEmpty()) body.insert("game_id", gameId);
        if (changes.contains("tags")) body.insert("tags", changes.value("tags"));
        if (changes.contains("language")) body.insert("broadcaster_language", changes.value("language"));
        if (body.isEmpty()) return done(true, {}, {});
        QUrlQuery q;
        q.addQueryItem("broadcaster_id", user_.value("id").toString());
        api("PATCH", "/channels", q, body, done);
    };
    if (changes.contains("game_id")) return patch(changes.value("game_id").toString());
    if (!changes.contains("game")) return patch({});
    searchCategories(changes.value("game").toString(),
                     [patch, done, changes](bool ok, const QJsonObject& d, const QString& e) {
                         if (!ok) return done(false, {}, e);
                         const QJsonArray items = d.value("items").toArray();
                         if (items.isEmpty())
                             return done(false, {},
                                         "No hay ninguna categoria de Twitch llamada \"" +
                                             changes.value("game").toString() + "\".");
                         // Coincidencia exacta si la hay; si no, la primera.
                         QString id = items.at(0).toObject().value("id").toString();
                         for (const QJsonValue& v : items)
                             if (v.toObject().value("name").toString().compare(
                                     changes.value("game").toString(), Qt::CaseInsensitive) == 0)
                                 id = v.toObject().value("id").toString();
                         patch(id);
                     });
}

void TwitchClient::streamStatus(Done done) {
    QUrlQuery q;
    q.addQueryItem("user_id", user_.value("id").toString());
    api("GET", "/streams", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QJsonObject s = d.value("data").toArray().at(0).toObject();
        if (s.isEmpty()) return done(true, QJsonObject{{"live", false}}, {});
        done(true,
             QJsonObject{
                 {"live", true},
                 {"viewers", s.value("viewer_count")},
                 {"started_at", s.value("started_at")},
                 {"title", s.value("title")},
                 {"game_name", s.value("game_name")},
                 {"thumbnail",
                  s.value("thumbnail_url").toString().replace("{width}", "320").replace("{height}", "180")}},
             {});
    });
}

void TwitchClient::ingest(Done done) {
    QUrlQuery q;
    q.addQueryItem("broadcaster_id", user_.value("id").toString());
    api("GET", "/streams/key", q, {}, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        const QString key = d.value("data").toArray().at(0).toObject().value("stream_key").toString();
        if (key.isEmpty()) return done(false, {}, "Twitch no entrego la clave de transmision.");
        done(true, QJsonObject{{"url", ep_.ingest}, {"key", key}}, {});
    });
}

void TwitchClient::videos(int max, Done done) {
    QUrlQuery q;
    q.addQueryItem("user_id", user_.value("id").toString());
    q.addQueryItem("first", QString::number(std::clamp(max, 1, 100)));
    api("GET", "/videos", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            out.append(QJsonObject{
                {"id", o.value("id")},
                {"title", o.value("title")},
                {"url", o.value("url")},
                {"thumbnail",
                 o.value("thumbnail_url").toString().replace("%{width}", "320").replace("%{height}", "180")},
                {"views", o.value("view_count")},
                {"created_at", o.value("created_at")},
                {"duration", o.value("duration")},
                {"type", o.value("type")}});
        }
        done(true, QJsonObject{{"items", out}}, {});
    });
}

void TwitchClient::deleteVideos(const QStringList& ids, Done done) {
    QUrlQuery q;
    for (const QString& id : ids)
        q.addQueryItem("id", id);
    api("DELETE", "/videos", q, {}, done);
}

void TwitchClient::searchCategories(const QString& query, Done done) {
    QUrlQuery q;
    q.addQueryItem("query", query);
    q.addQueryItem("first", "10");
    api("GET", "/search/categories", q, {}, [done](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return done(false, {}, e);
        QJsonArray out;
        for (const QJsonValue& v : d.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            out.append(QJsonObject{
                {"id", o.value("id")}, {"name", o.value("name")}, {"box_art", o.value("box_art_url")}});
        }
        done(true, QJsonObject{{"items", out}}, {});
    });
}

// --- Chat (IRC sobre TLS) ------------------------------------------------------------------

void TwitchClient::startChat() {
    if (chatRunning() || !loggedIn()) return;
    QPointer<TwitchClient> self(this);
    ensureToken([self](QString token, QString err) {
        if (!self || self->chatRunning()) return;
        if (token.isEmpty()) {
            emit self->chatStatus("Chat de Twitch: " + err);
            return;
        }
        self->chatStop_ = false;
        const QString login = self->user_.value("login").toString();
        self->chatThread_ = std::thread([self, token, login]() {
            if (self) self->chatLoop(token, login);
        });
    });
}

void TwitchClient::stopChat() {
    chatStop_ = true;
    if (chatThread_.joinable()) chatThread_.join(); // la lectura corta cada 1 s
}

void TwitchClient::sendChat(const QString& text) {
    const QString t = text.simplified();
    if (t.isEmpty() || !chatRunning()) return;
    {
        std::lock_guard<std::mutex> lock(outLock_);
        outgoing_.push_back("PRIVMSG #" + user_.value("login").toString().toStdString() + " :" +
                            t.toStdString() + "\r\n");
    }
    // IRC no devuelve los mensajes propios: se agregan aca.
    onChatLine(QJsonObject{{"id", "self-" + QString::number(QDateTime::currentMSecsSinceEpoch())},
                           {"author", user_.value("display_name")},
                           {"color", ""},
                           {"text", t},
                           {"atMs", static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
                           {"broadcaster", true}});
}

void TwitchClient::onChatLine(const QJsonObject& msg) {
    chat_.append(msg);
    while (chat_.size() > 300)
        chat_.removeFirst();
    emit chatChanged();
}

void TwitchClient::chatLoop(QString token, QString login) {
    // Corre en su propio thread: nada de Qt aca salvo invokeMethod hacia el objeto.
    QPointer<TwitchClient> self(this);
    const auto post = [self](QJsonObject m) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, m]() {
                if (self) self->onChatLine(m);
            },
            Qt::QueuedConnection);
    };
    const auto status = [self](QString s) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, s]() {
                if (self) emit self->chatStatus(s);
            },
            Qt::QueuedConnection);
    };
    int backoff = 2000;
    while (!chatStop_) {
        net::LineSocket sock;
        std::string err;
        if (!sock.open(ep_.ircHost.toStdString(), ep_.ircPort.toStdString(), ep_.ircTls, 1000, err)) {
            status("Chat de Twitch: " + QString::fromStdString(err));
        } else {
            const std::string chan = "#" + login.toLower().toStdString();
            sock.write("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
            sock.write("PASS oauth:" + token.toStdString() + "\r\n");
            sock.write("NICK " + login.toLower().toStdString() + "\r\n");
            sock.write("JOIN " + chan + "\r\n");
            status("Chat de Twitch conectado");
            backoff = 2000;
            std::string line;
            for (;;) {
                if (chatStop_) break;
                const int r = sock.readLine(line);
                if (r < 0) break;
                if (r == 0) {
                    std::deque<std::string> out;
                    {
                        std::lock_guard<std::mutex> lock(outLock_);
                        out.swap(outgoing_);
                    }
                    for (const auto& o : out)
                        sock.write(o);
                    continue;
                }
                if (line.rfind("PING", 0) == 0) {
                    sock.write("PONG" + line.substr(4) + "\r\n");
                    continue;
                }
                if (line.find("Login authentication failed") != std::string::npos) {
                    status("Chat de Twitch: la sesion no es valida (reconecta la cuenta).");
                    chatStop_ = true;
                    break;
                }
                // @tags :nick!nick@nick.tmi.twitch.tv PRIVMSG #canal :texto
                const size_t pm = line.find(" PRIVMSG #");
                if (pm == std::string::npos) continue;
                std::map<std::string, std::string> tags;
                size_t start = 0;
                if (line[0] == '@') {
                    const size_t sp = line.find(' ');
                    std::string t = line.substr(1, sp - 1);
                    size_t p = 0;
                    while (p < t.size()) {
                        size_t semi = t.find(';', p);
                        if (semi == std::string::npos) semi = t.size();
                        const std::string kv = t.substr(p, semi - p);
                        const size_t eq = kv.find('=');
                        if (eq != std::string::npos) tags[kv.substr(0, eq)] = kv.substr(eq + 1);
                        p = semi + 1;
                    }
                    start = sp + 1;
                }
                const size_t colon = line.find(" :", pm);
                if (colon == std::string::npos) continue;
                std::string nick = line.substr(start + 1, line.find('!', start) - start - 1);
                const std::string badges = tags["badges"];
                const std::string text = line.substr(colon + 2);
                const std::string ts = tags["tmi-sent-ts"];
                post(QJsonObject{
                    {"id", QString::fromStdString(tags.count("id") ? tags["id"] : nick + ts)},
                    {"author",
                     QString::fromStdString(tags["display-name"].empty() ? nick : tags["display-name"])},
                    {"color", QString::fromStdString(tags["color"])},
                    {"text", QString::fromStdString(text)},
                    {"atMs", ts.empty() ? static_cast<double>(QDateTime::currentMSecsSinceEpoch())
                                        : std::strtod(ts.c_str(), nullptr)},
                    {"broadcaster", badges.find("broadcaster") != std::string::npos},
                    {"moderator", badges.find("moderator") != std::string::npos},
                    {"subscriber", badges.find("subscriber") != std::string::npos}});
            }
        }
        if (chatStop_) break;
        status("Chat de Twitch: reconectando…");
        for (int waited = 0; waited < backoff && !chatStop_; waited += 200)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        backoff = std::min(backoff * 2, ba::ajustes::sAMs("twitch.reconexionMaxS", 60));
    }
}

} // namespace ba::app
