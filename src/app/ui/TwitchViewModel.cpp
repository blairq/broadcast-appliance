// ============================================================================
// src/app/ui/TwitchViewModel.cpp — ver TwitchViewModel.hpp
// ============================================================================

#include "ui/TwitchViewModel.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <QPointer>
#include <QUrlQuery>

#include "arranque/RuntimeConfig.hpp"
#include "ui/AireDeCuentas.hpp"
#include "arranque/AppSettings.hpp"
#include "net/AsyncHttp.hpp"
#include "ui/ListModels.hpp" // JsonListModel

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

TwitchViewModel::TwitchViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : PlataformaViewModel(parent), rt_(rt), ventanilla_(ventanilla), client_(std::make_unique<TwitchClient>(
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
                emit avatarChanged();
            });
        }
        if (!client_->loggedIn()) {
            if (!avatar_.isEmpty()) {
                avatar_.clear();
                emit avatarChanged();
            }
            emit chatChanged(); // desconectar o cambiar de app apagan el chat adentro del cliente
        }
        revisarCuenta();
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

void TwitchViewModel::revisarCuenta() {
    const QVariantMap app{{"configured", configured()}, {"clientId", clientId()}};
    const QVariantMap cuenta{{"loggedIn", loggedIn()},       {"loginPending", loginPending()}, {"userCode", userCode()},
                             {"uri", verificationUri()}, {"user", user()}};
    bool algo = false;
    if (app != appAntes_) {
        appAntes_ = app;
        emit appChanged();
        algo = true;
    }
    if (cuenta != cuentaAntes_) {
        const bool codigoNuevo = !userCode().isEmpty() && userCode() != cuentaAntes_.value("userCode").toString();
        cuentaAntes_ = cuenta;
        emit cuentaChanged();
        if (codigoNuevo) emit codigoListo();
        algo = true;
    }
    if (algo) emit stateChanged();
}

void TwitchViewModel::setStreamingHere(bool si) {
    if (si == streamingHere_) return;
    streamingHere_ = si;
    emit streamingHereChanged();
    emit stateChanged();
}

void TwitchViewModel::setChannel(const QJsonObject& c) {
    if (c == channel_) return;
    channel_ = c;
    emit channelChanged();
}

void TwitchViewModel::setStream(const QJsonObject& s) {
    if (s == stream_) return;
    stream_ = s;
    emit streamChanged();
}

QObject* TwitchViewModel::videos() const { return videos_.get(); }
QObject* TwitchViewModel::chat() const { return chat_.get(); }
QObject* TwitchViewModel::categories() const { return categories_.get(); }

QJsonObject TwitchViewModel::estadoCuenta() const {
    QJsonObject s = PlataformaViewModel::estadoCuenta();
    if (loginPending()) { // el código que la persona confirma
        s.insert("userCode", userCode());
        s.insert("verificationUri", verificationUri());
    }
    return s;
}

QJsonObject TwitchViewModel::estado() const {
    return QJsonObject{{"configured", configured()},
                       {"loggedIn", loggedIn()},
                       {"loginPending", loginPending()},
                       {"userCode", userCode()},
                       {"verificationUri", verificationUri()},
                       {"clientId", clientId()},
                       {"user", client_->user()},
                       {"channel", channel_},
                       {"stream", stream_},
                       {"streamingHere", streamingHere_},
                       {"appStreaming", cuentas::transmitiendo(ventanilla_)}, // de la foto
                       {"chatOn", chatOn()}};
}

bool TwitchViewModel::guardarCredencial(const QVariantMap& v) {
    return saveClient(v.value("clientId").toString().trimmed(), v.value("clientSecret").toString().trimmed());
}

bool TwitchViewModel::saveClient(const QString& id, const QString& secret) {
    QString err;
    if (!client_->saveClient(id, secret, err)) {
        avisarError(err);
        return false;
    }
    setError({});
    emit notify(tr("App de Twitch guardada: ahora conecta tu cuenta."), "ok");
    return true;
}

void TwitchViewModel::clearClient() { client_->clearClient(); }

void TwitchViewModel::login(Done listo) {
    setError({});
    client_->login([this, listo](bool ok, const QJsonObject& u, const QString& e) {
        if (!ok) {
            setError(e);
            if (listo) listo(false, u, e);
            return;
        }
        emit notify(tr("Conectado a Twitch: %1").arg(u.value("display_name").toString()), "ok");
        refresh();
        if (listo) listo(true, u, {});
    });
}

void TwitchViewModel::cancelLogin() { client_->cancelLogin(); }

void TwitchViewModel::logout() {
    client_->logout([this](bool, const QJsonObject&, const QString&) {
        videos_->setItems({});
        setChannel({});
        setStream({});
        emit notify(tr("Cuenta de Twitch desconectada."), "info");
    });
}

void TwitchViewModel::refresh() {
    if (!loggedIn()) return;
    track(true);
    client_->channel([this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        setChannel(d);
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
        if (ok) setStream(d);
    });
    if (streamingHere_ && !cuentas::transmitiendo(ventanilla_)) setStreamingHere(false); // se corto por otro lado
}

void TwitchViewModel::updateChannel(const QString& title, const QString& category) {
    QJsonObject changes{{"title", title}};
    if (!category.trimmed().isEmpty() && category != channel_.value("game_name").toString())
        changes.insert("game", category.trimmed());
    cambiarCanal(changes, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("Canal de Twitch actualizado."), "ok");
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
    if (cuentas::transmitiendo(ventanilla_))
        return done(false, {},
                    streamingHere_ ? tr("Ya se esta transmitiendo a Twitch.")
                                   : tr("La app ya está transmitiendo: cortá esa transmisión primero."));
    track(true);
    QPointer<TwitchViewModel> self(this);
    client_->ingest([self, done](bool ok, const QJsonObject& ing, const QString& e) {
        if (!self) return;
        self->track(false);
        if (!ok) return done(false, {}, e);
        // Servidor y clave SOLO en memoria; sigue cuando el motor contesta.
        cuentas::transmitir(self->ventanilla_, self->rt_, self, ing.value("url").toString().toStdString(),
                            ing.value("key").toString().toStdString(), [self, ing, done](const std::string& error) {
                                if (self) self->alSalir(ing, error, done);
                            });
    });
}

void TwitchViewModel::alSalir(const QJsonObject& ing, const std::string& error, const TwitchClient::Done& done) {
    if (!error.empty())
        return done(false, {}, tr("La app no pudo empezar a transmitir: %1").arg(QString::fromStdString(error)));
    {
        QPointer<TwitchViewModel> self(this);
        self->setStreamingHere(true);
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
    }
}

void TwitchViewModel::stopLive() {
    stopLive([this](bool, const QJsonObject&, const QString&) {
        emit notify(tr("Transmisión a Twitch cortada."), "info");
    });
}

void TwitchViewModel::stopLive(TwitchClient::Done done) {
    if (streamingHere_) cuentas::cortar(ventanilla_, rt_);
    setStreamingHere(false);
    QTimer::singleShot(5000, this, &TwitchViewModel::pollStream);
    done(true, {}, {});
}

void TwitchViewModel::deleteVideo(const QString& id) {
    borrarVideos({id}, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("VOD borrado de Twitch."), "ok");
    });
}

void TwitchViewModel::borrarVideos(const QStringList& ids, Done done) {
    client_->deleteVideos(ids, alVolver(std::move(done)));
}

void TwitchViewModel::leerCanal(Done done) {
    client_->channel([this, done = std::move(done)](bool ok, const QJsonObject& d, const QString& e) {
        if (ok) setChannel(d);
        done(ok, d, e);
    });
}

void TwitchViewModel::cambiarCanal(const QJsonObject& cambios, Done done) {
    client_->updateChannel(cambios, alVolver(std::move(done)));
}

void TwitchViewModel::buscarCategorias(const QString& texto, Done done) {
    client_->searchCategories(texto, std::move(done));
}

void TwitchViewModel::estadoDelVivo(Done done) {
    client_->streamStatus([this, done = std::move(done)](bool ok, const QJsonObject& d, const QString& e) {
        if (ok) setStream(d);
        done(ok, d, e);
    });
}

void TwitchViewModel::probarClave(Done done) {
    QUrlQuery q;
    q.addQueryItem("broadcaster_id", client_->user().value("id").toString());
    client_->api("GET", "/streams/key", q, {}, [done = std::move(done)](bool ok, const QJsonObject&, const QString& e) {
        done(ok, {}, e); // la clave no sale de acá
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

} // namespace ba::app
