// ============================================================================
// src/app/ui/FacebookViewModel.cpp — ver FacebookViewModel.hpp
// ============================================================================

#include "ui/FacebookViewModel.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>

#include "arranque/AppRuntime.hpp"
#include "ui/AireDeCuentas.hpp"
#include "arranque/AppSettings.hpp"
#include "ui/GrabacionesModel.hpp"
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

FacebookViewModel::FacebookViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, GrabacionesModel& grabaciones, QObject* parent)
    : PlataformaViewModel(parent), rt_(rt), ventanilla_(ventanilla), client_(std::make_unique<FacebookClient>(
                                    QString::fromStdString((ba::platform::credentialsDir()).string()))),
      lives_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "status", "url", "views", "created"})),
      videos_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "url", "created", "length"})),
      grabaciones_(grabaciones),
      chat_(std::make_unique<JsonListModel>(QStringList{"uid", "author", "text"})) {
    connect(client_.get(), &FacebookClient::stateChanged, this, &FacebookViewModel::revisarCuenta);
    connect(client_.get(), &FacebookClient::uploadProgress, this,
            [this](const QString& path, qint64 sent, qint64 total) {
                if (upload_.value("path").toString() != path) return;
                upload_["sent"] = sent;
                upload_["total"] = total;
                emit uploadChanged();
            });
    chatTimer_.setSingleShot(true);
    connect(&chatTimer_, &QTimer::timeout, this, &FacebookViewModel::pollChat);
    if (loggedIn()) QTimer::singleShot(2000, this, &FacebookViewModel::refresh);
}

FacebookViewModel::~FacebookViewModel() = default;

void FacebookViewModel::revisarCuenta() {
    const QVariantMap app{{"configured", configured()}, {"appId", appId()}, {"port", port()}, {"uri", redirectUri()}};
    const QVariantMap cuenta{{"loggedIn", loggedIn()}, {"loginPending", loginPending()}, {"user", user()}};
    const QVariantMap pagina{{"pages", pages()}, {"pageId", pageId()}, {"page", page()}};
    bool algo = false;
    const auto revisar = [&algo](const QVariantMap& ahora, QVariantMap& antes, auto&& avisar) {
        if (ahora == antes) return;
        antes = ahora;
        avisar();
        algo = true;
    };
    revisar(app, appAntes_, [this] { emit appChanged(); });
    revisar(cuenta, cuentaAntes_, [this] { emit cuentaChanged(); });
    revisar(pagina, paginaAntes_, [this] { emit paginaChanged(); });
    if (algo) emit stateChanged();
}

void FacebookViewModel::setLiveId(const QString& id) {
    if (id == liveId_) return;
    liveId_ = id;
    emit liveIdChanged();
    emit stateChanged();
}

QObject* FacebookViewModel::lives() const { return lives_.get(); }
QObject* FacebookViewModel::videos() const { return videos_.get(); }
QObject* FacebookViewModel::recordings() const { return &grabaciones_; }
QObject* FacebookViewModel::chat() const { return chat_.get(); }

PlataformaViewModel::Falta FacebookViewModel::falta() const {
    const Falta f = PlataformaViewModel::falta();
    return f == Falta::Nada && pageId().isEmpty() ? Falta::Pagina : f;
}

QJsonObject FacebookViewModel::estadoCuenta() const {
    QJsonObject s = PlataformaViewModel::estadoCuenta();
    s.insert("page", client_->page().value("name").toString());
    s.insert("redirectUri", redirectUri());
    return s;
}

QJsonObject FacebookViewModel::estado() const {
    return QJsonObject{{"configured", configured()},
                       {"loggedIn", loggedIn()},
                       {"loginPending", loginPending()},
                       {"appId", appId()},
                       {"redirectUri", redirectUri()},
                       {"user", client_->user()},
                       {"page", client_->page()},
                       {"liveId", liveId_},
                       {"appStreaming", cuentas::transmitiendo(ventanilla_)}, // de la foto
                       {"chatLiveId", chatLiveId_},
                       {"upload", QJsonObject::fromVariantMap(upload_)}};
}

bool FacebookViewModel::guardarCredencial(const QVariantMap& v) {
    return saveClient(v.value("appId").toString().trimmed(), v.value("appSecret").toString().trimmed(),
                      v.value("port", port()).toInt());
}

bool FacebookViewModel::saveClient(const QString& appId, const QString& secret, int port) {
    QString err;
    if (!client_->saveClient(appId, secret, port, err)) {
        avisarError(err);
        return false;
    }
    setError({});
    emit notify(tr("App de Meta guardada: ahora conecta tu cuenta."), "ok");
    return true;
}

void FacebookViewModel::clearClient() { client_->clearClient(); }

void FacebookViewModel::login(Done listo) {
    setError({});
    client_->login([this, listo](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) {
            if (e != "Login cancelado.") setError(e);
            if (listo) listo(false, d, e);
            return;
        }
        emit notify(tr("Conectado a Facebook: %1").arg(d.value("user").toObject().value("name").toString()), "ok");
        refresh();
        if (listo) listo(true, d, {});
    });
}

void FacebookViewModel::cancelLogin() { client_->cancelLogin(); }

void FacebookViewModel::logout() {
    setChatLive({});
    client_->logout([this](bool, const QJsonObject&, const QString&) {
        lives_->setItems({});
        videos_->setItems({});
        emit notify(tr("Cuenta de Facebook desconectada."), "info");
    });
}

void FacebookViewModel::selectPage(const QString& id) {
    if (client_->selectPage(id)) refresh();
}

void FacebookViewModel::refresh() {
    if (!loggedIn()) return;
    track(true);
    client_->liveVideos([this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        lives_->setItems(withUid(d.value("items").toArray()));
    });
    track(true);
    client_->videos(50, [this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        videos_->setItems(withUid(d.value("items").toArray()));
    });
    refreshRecordings();
}

QString FacebookViewModel::recordingsDir() const { return grabaciones_.carpeta(); }

void FacebookViewModel::refreshRecordings() { grabaciones_.refrescar(); }

void FacebookViewModel::goLive(const QString& title, const QString& description, bool test) {
    goLive(
        QJsonObject{
            {"title", title}, {"description", description}, {"status", test ? "UNPUBLISHED" : "LIVE_NOW"}},
        [this, test](bool ok, const QJsonObject&, const QString& e) {
            if (!ok) return setError(e);
            emit notify(test ? tr("Transmitiendo a Facebook en PRUEBA (sin publicar): publicalo cuando quieras.")
                             : tr("Transmitiendo en vivo a Facebook."),
                        "ok");
        });
}

void FacebookViewModel::goLive(const QJsonObject& p, FacebookClient::Done done) {
    if (cuentas::transmitiendo(ventanilla_))
        return done(false, {}, tr("La app ya esta transmitiendo: corta esa transmision primero."));
    track(true);
    QPointer<FacebookViewModel> self(this);
    client_->createLive(p, [self, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!self) return;
        self->track(false);
        if (!ok) return done(false, {}, e);
        const QJsonObject ing = d.value("ingestion").toObject();
        // Servidor RTMPS y clave SOLO en memoria; sigue cuando el motor contesta.
        cuentas::transmitir(self->ventanilla_, self->rt_, self, ing.value("url").toString().toStdString(),
                            ing.value("key").toString().toStdString(), [self, d, ing, done](const std::string& error) {
                                if (self) self->alSalir(d, ing, error, done);
                            });
    });
}

void FacebookViewModel::alSalir(const QJsonObject& d, const QJsonObject& ing, const std::string& error,
                                const FacebookClient::Done& done) {
    if (!error.empty())
        return done(false, {}, tr("La app no pudo empezar a transmitir: %1").arg(QString::fromStdString(error)));
    {
        QPointer<FacebookViewModel> self(this);
        self->setLiveId(d.value("id").toString());
        self->setChatLive(self->liveId_);
        QVariantMap v{{"plataforma", "facebook"}, {"id", self->liveId_}};
        if (d.contains("permalink_url")) v["url"] = "https://www.facebook.com" + d.value("permalink_url").toString();
        emit self->vivo(v);
        QTimer::singleShot(4000, self, [self]() {
            if (self) self->refresh();
        });
        done(true, QJsonObject{{"id", d.value("id")}, {"server", ing.value("url")}}, {});
    }
}

void FacebookViewModel::publishLive(const QString& id) {
    publicarVivo(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("Vivo publicado en la Pagina."), "ok");
    });
}

void FacebookViewModel::publicarVivo(const QString& id, Done done) { client_->publishLive(id, alVolver(std::move(done))); }

void FacebookViewModel::vivos(Done done) { client_->liveVideos(std::move(done)); }

void FacebookViewModel::estadoDeVivo(const QString& id, Done done) { client_->liveStatus(id, std::move(done)); }

void FacebookViewModel::permisosOtorgados(Done done) { client_->graph("GET", "/me/permissions", {}, std::move(done)); }

void FacebookViewModel::endLive(const QString& id) {
    endLive(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("Vivo de Facebook terminado."), "ok");
    });
}

void FacebookViewModel::endLive(const QString& id, FacebookClient::Done done) {
    if (id == liveId_) {
        cuentas::cortar(ventanilla_, rt_);
        setLiveId({});
    }
    client_->endLive(id, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        refresh();
        done(ok, d, e);
    });
}

void FacebookViewModel::deleteItem(const QString& id) {
    borrar(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify(tr("Borrado de Facebook."), "ok");
    });
}

void FacebookViewModel::borrar(const QString& id, Done done) { client_->deleteObject(id, alVolver(std::move(done))); }

void FacebookViewModel::updateVideo(const QString& id, const QString& title, const QString& description) {
    cambiarVideo(id, QJsonObject{{"title", title}, {"description", description}},
                 [this](bool ok, const QJsonObject&, const QString& e) {
                     if (!ok) return setError(e);
                     emit notify(tr("Video actualizado."), "ok");
                 });
}

void FacebookViewModel::cambiarVideo(const QString& id, const QJsonObject& cambios, Done done) {
    client_->updateVideo(id, cambios, alVolver(std::move(done)));
}

void FacebookViewModel::uploadRecording(const QString& path, const QString& title,
                                        const QString& description) {
    uploadRecording(path, QJsonObject{{"title", title}, {"description", description}},
                    [this](bool ok, const QJsonObject& d, const QString& e) {
                        if (!ok) return setError(e);
                        emit notify(tr("Subido a Facebook: %1").arg(d.value("url").toString()), "ok");
                    });
}

void FacebookViewModel::uploadRecording(const QString& path, const QJsonObject& meta,
                                        FacebookClient::Done done) {
    if (!upload_.isEmpty()) return done(false, {}, tr("Ya hay una subida en curso."));
    upload_ = QVariantMap{{"path", path},
                          {"title", meta.value("title").toString(QFileInfo(path).completeBaseName())},
                          {"sent", 0},
                          {"total", QFileInfo(path).size()}};
    emit uploadChanged();
    client_->uploadVideo(path, meta, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        upload_.clear();
        emit uploadChanged();
        if (ok) refresh();
        done(ok, d, e);
    });
}

void FacebookViewModel::setChatLive(const QString& id) {
    if (id == chatLiveId_) return;
    chatLiveId_ = id;
    chatItems_ = {};
    chat_->setItems({});
    chatTimer_.stop();
    emit chatChanged();
    if (!id.isEmpty()) QTimer::singleShot(0, this, &FacebookViewModel::pollChat);
}

void FacebookViewModel::pollChat() {
    if (chatLiveId_.isEmpty() || !loggedIn()) return;
    const QString forLive = chatLiveId_;
    client_->comments(forLive, [this, forLive](bool ok, const QJsonObject& d, const QString&) {
        if (forLive != chatLiveId_) return;
        if (ok) {
            QSet<QString> seen;
            for (const QJsonValue& v : chatItems_)
                seen.insert(v.toObject().value("id").toString());
            bool added = false;
            for (const QJsonValue& v : d.value("items").toArray()) {
                if (seen.contains(v.toObject().value("id").toString())) continue;
                chatItems_.append(v);
                added = true;
            }
            while (chatItems_.size() > ba::ajustes::entero("chat.historial", 300))
                chatItems_.removeFirst();
            if (added) {
                chat_->setItems(withUid(chatItems_));
                emit chatChanged();
            }
        }
        chatTimer_.start(ba::ajustes::sAMs("chat.sondeoS", 5));
    });
}

void FacebookViewModel::sendChat(const QString& text) {
    if (chatLiveId_.isEmpty() || text.trimmed().isEmpty()) return;
    client_->comment(chatLiveId_, text.trimmed(), [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        chatTimer_.start(500);
    });
}

} // namespace ba::app
