// ============================================================================
// src/app/ui/TwitchViewModel.cpp — ver TwitchViewModel.hpp
// ============================================================================

#include "ui/TwitchViewModel.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <QDesktopServices>
#include <QPointer>
#include <QUrl>

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "net/AsyncHttp.hpp"
#include "ui/ConferenceViewModel.hpp" // JsonListModel

namespace ba::app {

namespace {
QJsonArray withUid(const QJsonArray& in) {
    QJsonArray out;
    for (const QJsonValue& v : in) {
        QJsonObject o = v.toObject();
        o.insert("uid", o.value("id"));
        out.append(o);
    }
    return out;
}
} // namespace

TwitchViewModel::TwitchViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), rt_(rt), client_(std::make_unique<TwitchClient>(
                                    QString::fromStdString((ba::platform::credentialsDir()).string()))),
      videos_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "url", "thumb", "views", "created_at", "duration", "type"})),
      chat_(std::make_unique<JsonListModel>(
          QStringList{"uid", "author", "color", "text", "broadcaster", "moderator", "subscriber"})),
      categories_(std::make_unique<JsonListModel>(QStringList{"uid", "name"})) {
    connect(client_.get(), &TwitchClient::stateChanged, this, [this]() {
        const QString url = client_->user().value("profile_image_url").toString();
        if (!url.isEmpty() && avatar_.isEmpty()) {
            net::HttpRequest req;
            req.url = url.toStdString();
            net::asyncRequest(this, req, [this](const net::HttpResponse& r) {
                if (r.status != 200) return;
                const QByteArray b = QByteArray::fromStdString(r.body);
                avatar_ = QString("data:image/%1;base64,").arg(b.startsWith("\x89PNG") ? "png" : "jpeg") +
                          QString::fromLatin1(b.toBase64());
                emit stateChanged();
            });
        }
        if (!client_->loggedIn()) {
            avatar_.clear();
            emit chatChanged(); // desconectar o cambiar de app apagan el chat adentro del cliente
        }
        emit stateChanged();
    });
    connect(client_.get(), &TwitchClient::chatChanged, this, [this]() {
        chat_->setItems(withUid(client_->chatMessages()));
        emit chatChanged();
    });
    connect(client_.get(), &TwitchClient::chatStatus, this, [this](const QString& s) {
        emit chatChanged();
        if (s.contains("valida") || s.contains("no se pudo")) emit notify(s, "warn");
    });
    connect(&streamTimer_, &QTimer::timeout, this, &TwitchViewModel::pollStream);
    streamTimer_.start(ba::ajustes::sAMs("twitch.estadoS", 30)); // estado del vivo (barato)
    QTimer::singleShot(0, client_.get(), &TwitchClient::stateChanged);
    if (loggedIn()) QTimer::singleShot(1500, this, &TwitchViewModel::refresh);
}

TwitchViewModel::~TwitchViewModel() = default;

QObject* TwitchViewModel::videos() const { return videos_.get(); }
QObject* TwitchViewModel::chat() const { return chat_.get(); }
QObject* TwitchViewModel::categories() const { return categories_.get(); }

void TwitchViewModel::setError(const QString& e) {
    lastError_ = e;
    emit errorChanged();
    if (!e.isEmpty()) emit notify(e.startsWith("Twitch") ? e : "Twitch: " + e, "error");
}

void TwitchViewModel::track(bool start) {
    busy_ = std::max(0, busy_ + (start ? 1 : -1));
    emit busyChanged();
}

QString TwitchViewModel::saveClient(const QString& id, const QString& secret) {
    QString err;
    if (!client_->saveClient(id, secret, err)) return err;
    emit notify(tr("App de Twitch guardada: ahora conecta tu cuenta."), "ok");
    return {};
}

void TwitchViewModel::clearClient() { client_->clearClient(); }

void TwitchViewModel::login() {
    setError({});
    client_->login([this](bool ok, const QJsonObject& u, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Conectado a Twitch: " + u.value("display_name").toString(), "ok");
        refresh();
    });
}

void TwitchViewModel::cancelLogin() { client_->cancelLogin(); }

void TwitchViewModel::logout() {
    client_->logout([this](bool, const QJsonObject&, const QString&) {
        videos_->setItems({});
        channel_ = {};
        stream_ = {};
        emit channelChanged();
        emit notify(tr("Cuenta de Twitch desconectada."), "info");
    });
}

void TwitchViewModel::refresh() {
    if (!loggedIn()) return;
    track(true);
    client_->channel([this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        channel_ = d;
        emit channelChanged();
    });
    track(true);
    client_->videos(40, [this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        QJsonArray items = withUid(d.value("items").toArray());
        videos_->setItems(items);
        // Miniaturas por el HTTPS propio (QML no tiene TLS del sistema).
        for (int i = 0; i < items.size(); ++i) {
            const QString url = items[i].toObject().value("thumbnail").toString();
            const QString id = items[i].toObject().value("id").toString();
            if (url.isEmpty() || url.contains("%{")) continue;
            net::HttpRequest req;
            req.url = url.toStdString();
            net::asyncRequest(this, req, [this, id](const net::HttpResponse& r) {
                if (r.status != 200) return;
                QJsonArray cur = videos_->items();
                for (int k = 0; k < cur.size(); ++k) {
                    QJsonObject o = cur[k].toObject();
                    if (o.value("id").toString() != id) continue;
                    o.insert("thumb", "data:image/jpeg;base64," +
                                          QString::fromLatin1(QByteArray::fromStdString(r.body).toBase64()));
                    cur[k] = o;
                }
                videos_->setItems(cur);
            });
        }
    });
    pollStream();
}

void TwitchViewModel::pollStream() {
    if (!loggedIn()) return;
    client_->streamStatus([this](bool ok, const QJsonObject& d, const QString&) {
        if (!ok) return;
        stream_ = d;
        emit channelChanged();
    });
    if (streamingHere_ && !rt_.engine().status().streaming) { // la transmision se corto por otro lado
        streamingHere_ = false;
        emit stateChanged();
    }
}

void TwitchViewModel::updateChannel(const QString& title, const QString& category) {
    QJsonObject changes{{"title", title}};
    if (!category.trimmed().isEmpty() && category != channel_.value("game_name").toString())
        changes.insert("game", category.trimmed());
    track(true);
    client_->updateChannel(changes, [this](bool ok, const QJsonObject&, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        emit notify(tr("Canal de Twitch actualizado."), "ok");
        refresh();
    });
}

void TwitchViewModel::searchCategories(const QString& text) {
    if (text.trimmed().size() < 2) return categories_->setItems({});
    client_->searchCategories(text, [this](bool ok, const QJsonObject& d, const QString&) {
        if (ok) categories_->setItems(withUid(d.value("items").toArray()));
    });
}

void TwitchViewModel::goLive() {
    goLive([this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("Transmitiendo a Twitch."), "ok");
    });
}

void TwitchViewModel::goLive(TwitchClient::Done done) {
    if (rt_.engine().status().streaming)
        return done(false, {},
                    streamingHere_ ? "Ya se esta transmitiendo a Twitch."
                                   : "La app ya esta transmitiendo: corta esa transmision primero.");
    track(true);
    QPointer<TwitchViewModel> self(this);
    client_->ingest([self, done](bool ok, const QJsonObject& ing, const QString& e) {
        if (!self) return;
        self->track(false);
        if (!ok) return done(false, {}, e);
        // Servidor y clave SOLO en memoria.
        if (!self->rt_.startStreaming(ing.value("url").toString().toStdString(),
                                      ing.value("key").toString().toStdString()))
            return done(false, {},
                        "La app no pudo empezar a transmitir: " +
                            QString::fromStdString(self->rt_.lastError()));
        self->streamingHere_ = true;
        emit self->stateChanged();
        emit self->vivo(QVariantMap{{"plataforma", "twitch"},
                                    {"url", "https://twitch.tv/" + self->client_->user().value("login").toString()}});
        if (!self->client_->chatRunning()) self->setChatOn(true);
        QTimer::singleShot(20000, self, [self]() {
            if (self) self->pollStream();
        });
        done(true,
             QJsonObject{{"server", ing.value("url")},
                         {"url", "https://twitch.tv/" + self->client_->user().value("login").toString()}},
             {});
    });
}

void TwitchViewModel::stopLive() {
    stopLive([this](bool, const QJsonObject&, const QString&) {
        emit notify(tr("Transmision a Twitch cortada."), "info");
    });
}

void TwitchViewModel::stopLive(TwitchClient::Done done) {
    if (streamingHere_) rt_.stopStreaming();
    streamingHere_ = false;
    emit stateChanged();
    QTimer::singleShot(5000, this, &TwitchViewModel::pollStream);
    done(true, {}, {});
}

void TwitchViewModel::deleteVideo(const QString& id) {
    client_->deleteVideos({id}, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("VOD borrado de Twitch."), "ok");
        refresh();
    });
}

void TwitchViewModel::setChatOn(bool on) {
    if (on)
        client_->startChat();
    else
        client_->stopChat();
    QTimer::singleShot(300, this, [this]() { emit chatChanged(); });
}

void TwitchViewModel::sendChat(const QString& text) { client_->sendChat(text); }
void TwitchViewModel::openUrl(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }

} // namespace ba::app
