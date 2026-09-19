// ============================================================================
// src/app/ui/FacebookViewModel.cpp — ver FacebookViewModel.hpp
// ============================================================================

#include "ui/FacebookViewModel.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>
#include <QUrl>

#include "AppRuntime.hpp"
#include "AppSettings.hpp"
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

FacebookViewModel::FacebookViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), rt_(rt), client_(std::make_unique<FacebookClient>(
                                    QString::fromStdString((rt.dataRoot() / "Credenciales").string()))),
      lives_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "status", "url", "views", "created"})),
      videos_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "url", "created", "length"})),
      recordings_(std::make_unique<JsonListModel>(QStringList{"uid", "name", "size", "modified", "path"})),
      chat_(std::make_unique<JsonListModel>(QStringList{"uid", "author", "text"})) {
    connect(client_.get(), &FacebookClient::stateChanged, this, &FacebookViewModel::stateChanged);
    connect(client_.get(), &FacebookClient::uploadProgress, this,
            [this](const QString& path, qint64 sent, qint64 total) {
                if (upload_.value("path").toString() != path) return;
                upload_["sent"] = sent;
                upload_["total"] = total;
                emit uploadChanged();
            });
    chatTimer_.setSingleShot(true);
    connect(&chatTimer_, &QTimer::timeout, this, &FacebookViewModel::pollChat);
    refreshRecordings();
    if (loggedIn()) QTimer::singleShot(2000, this, &FacebookViewModel::refresh);
}

FacebookViewModel::~FacebookViewModel() = default;

QObject* FacebookViewModel::lives() const { return lives_.get(); }
QObject* FacebookViewModel::videos() const { return videos_.get(); }
QObject* FacebookViewModel::recordings() const { return recordings_.get(); }
QObject* FacebookViewModel::chat() const { return chat_.get(); }

void FacebookViewModel::setError(const QString& e) {
    lastError_ = e;
    emit errorChanged();
    if (!e.isEmpty()) emit notify(e.startsWith("Facebook") ? e : "Facebook: " + e, "error");
}

void FacebookViewModel::track(bool start) {
    busy_ = std::max(0, busy_ + (start ? 1 : -1));
    emit busyChanged();
}

QString FacebookViewModel::saveClient(const QString& appId, const QString& secret, int port) {
    QString err;
    if (!client_->saveClient(appId, secret, port, err)) return err;
    emit notify("App de Meta guardada: ahora conecta tu cuenta.", "ok");
    return {};
}

void FacebookViewModel::clearClient() { client_->clearClient(); }

void FacebookViewModel::login() {
    setError({});
    client_->login([this](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) {
            if (e != "Login cancelado.") setError(e);
            return;
        }
        emit notify("Conectado a Facebook: " + d.value("user").toObject().value("name").toString(), "ok");
        refresh();
    });
}

void FacebookViewModel::cancelLogin() { client_->cancelLogin(); }

void FacebookViewModel::logout() {
    setChatLive({});
    client_->logout([this](bool, const QJsonObject&, const QString&) {
        lives_->setItems({});
        videos_->setItems({});
        emit notify("Cuenta de Facebook desconectada.", "info");
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

QString FacebookViewModel::recordingsDir() const {
    return QString::fromStdString(rt_.config().rec.directory);
}

void FacebookViewModel::refreshRecordings() {
    QJsonArray out;
    for (const QFileInfo& f :
         QDir(recordingsDir())
             .entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.webm"}, QDir::Files, QDir::Time)) {
        out.append(QJsonObject{{"id", f.absoluteFilePath()},
                               {"uid", f.absoluteFilePath()},
                               {"name", f.fileName()},
                               {"size", static_cast<double>(f.size())},
                               {"modified", f.lastModified().toString("dd/MM/yyyy HH:mm")},
                               {"path", f.absoluteFilePath()}});
    }
    recordings_->setItems(out);
}

void FacebookViewModel::goLive(const QString& title, const QString& description, bool test) {
    goLive(
        QJsonObject{
            {"title", title}, {"description", description}, {"status", test ? "UNPUBLISHED" : "LIVE_NOW"}},
        [this, test](bool ok, const QJsonObject&, const QString& e) {
            if (!ok) return setError(e);
            emit notify(test ? "Transmitiendo a Facebook en PRUEBA (sin publicar): publicalo cuando quieras."
                             : "Transmitiendo en vivo a Facebook.",
                        "ok");
        });
}

void FacebookViewModel::goLive(const QJsonObject& p, FacebookClient::Done done) {
    if (rt_.engine().status().streaming)
        return done(false, {}, "La app ya esta transmitiendo: corta esa transmision primero.");
    track(true);
    QPointer<FacebookViewModel> self(this);
    client_->createLive(p, [self, done](bool ok, const QJsonObject& d, const QString& e) {
        if (!self) return;
        self->track(false);
        if (!ok) return done(false, {}, e);
        const QJsonObject ing = d.value("ingestion").toObject();
        // Servidor RTMPS y clave SOLO en memoria.
        if (!self->rt_.startStreaming(ing.value("url").toString().toStdString(),
                                      ing.value("key").toString().toStdString()))
            return done(false, {},
                        "La app no pudo empezar a transmitir: " +
                            QString::fromStdString(self->rt_.lastError()));
        self->liveId_ = d.value("id").toString();
        self->setChatLive(self->liveId_);
        emit self->stateChanged();
        QTimer::singleShot(4000, self, [self]() {
            if (self) self->refresh();
        });
        done(true, QJsonObject{{"id", d.value("id")}, {"server", ing.value("url")}}, {});
    });
}

void FacebookViewModel::publishLive(const QString& id) {
    client_->publishLive(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Vivo publicado en la Pagina.", "ok");
        refresh();
    });
}

void FacebookViewModel::endLive(const QString& id) {
    endLive(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Vivo de Facebook terminado.", "ok");
    });
}

void FacebookViewModel::endLive(const QString& id, FacebookClient::Done done) {
    if (id == liveId_) {
        rt_.stopStreaming();
        liveId_.clear();
        emit stateChanged();
    }
    client_->endLive(id, [this, done](bool ok, const QJsonObject& d, const QString& e) {
        refresh();
        done(ok, d, e);
    });
}

void FacebookViewModel::deleteItem(const QString& id) {
    client_->deleteObject(id, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Borrado de Facebook.", "ok");
        refresh();
    });
}

void FacebookViewModel::updateVideo(const QString& id, const QString& title, const QString& description) {
    client_->updateVideo(id, QJsonObject{{"title", title}, {"description", description}},
                         [this](bool ok, const QJsonObject&, const QString& e) {
                             if (!ok) return setError(e);
                             emit notify("Video actualizado.", "ok");
                             refresh();
                         });
}

void FacebookViewModel::uploadRecording(const QString& path, const QString& title,
                                        const QString& description) {
    uploadRecording(path, QJsonObject{{"title", title}, {"description", description}},
                    [this](bool ok, const QJsonObject& d, const QString& e) {
                        if (!ok) return setError(e);
                        emit notify("Subido a Facebook: " + d.value("url").toString(), "ok");
                    });
}

void FacebookViewModel::uploadRecording(const QString& path, const QJsonObject& meta,
                                        FacebookClient::Done done) {
    if (!upload_.isEmpty()) return done(false, {}, "Ya hay una subida en curso.");
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
            while (chatItems_.size() > 300)
                chatItems_.removeFirst();
            if (added) {
                chat_->setItems(withUid(chatItems_));
                emit chatChanged();
            }
        }
        chatTimer_.start(5000);
    });
}

void FacebookViewModel::sendChat(const QString& text) {
    if (chatLiveId_.isEmpty() || text.trimmed().isEmpty()) return;
    client_->comment(chatLiveId_, text.trimmed(), [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        chatTimer_.start(500);
    });
}

void FacebookViewModel::openUrl(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }
void FacebookViewModel::copy(const QString& text) { QGuiApplication::clipboard()->setText(text); }

} // namespace ba::app
