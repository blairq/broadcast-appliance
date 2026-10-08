// ============================================================================
// src/app/ui/YouTubeViewModel.cpp — ver YouTubeViewModel.hpp
// ============================================================================

#include "ui/YouTubeViewModel.hpp"
#include "platform/Paths.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QUrl>

#include <algorithm>

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "net/HttpsClient.hpp"
#include "ui/ConferenceViewModel.hpp" // JsonListModel

namespace ba::app {

namespace {

QJsonArray withUid(const QJsonArray& in, const QString& key = "id") {
    QJsonArray out;
    for (const QJsonValue& v : in) {
        QJsonObject o = v.toObject();
        o.insert("uid", o.value(key));
        out.append(o);
    }
    return out;
}

} // namespace

YouTubeViewModel::YouTubeViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), rt_(rt), client_(std::make_unique<YouTubeClient>(
                                    QString::fromStdString((ba::platform::credentialsDir()).string()))),
      videos_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "privacy", "publishedAt", "thumb", "views", "likes",
                      "comments", "duration", "url", "live", "uploadStatus"})),
      broadcasts_(std::make_unique<JsonListModel>(
          QStringList{"uid", "title", "description", "status", "privacy", "scheduledStart", "actualStart",
                      "url", "studioUrl", "liveChatId", "thumb", "autoStart"})),
      recordings_(std::make_unique<JsonListModel>(QStringList{"uid", "name", "size", "modified", "path"})),
      chat_(std::make_unique<JsonListModel>(
          QStringList{"uid", "author", "text", "publishedAt", "owner", "moderator", "member"})) {
    connect(client_.get(), &YouTubeClient::stateChanged, this, [this]() {
        const QString url = client_->account().value("thumbnail").toString();
        if (!url.isEmpty() && !thumbs_.contains(url)) {
            thumbs_.insert(url, {});
            QPointer<YouTubeViewModel> self(this);
            client_->download(url, [self, url](bool ok, const QByteArray& img) {
                if (!self || !ok) return;
                self->thumbs_.insert(url, "data:image/jpeg;base64," + QString::fromLatin1(img.toBase64()));
                emit self->stateChanged();
            });
        }
        emit stateChanged();
    });
    connect(client_.get(), &YouTubeClient::uploadProgress, this,
            [this](const QString& path, qint64 sent, qint64 total) {
                if (upload_.value("path").toString() != path) return;
                upload_["sent"] = sent;
                upload_["total"] = total;
                emit uploadChanged();
            });
    QTimer::singleShot(0, client_.get(), &YouTubeClient::stateChanged); // avatar del canal guardado
    chatTimer_.setSingleShot(true);
    connect(&chatTimer_, &QTimer::timeout, this, &YouTubeViewModel::pollChat);
    refreshRecordings();
    if (loggedIn()) QTimer::singleShot(1500, this, &YouTubeViewModel::refresh);
}

YouTubeViewModel::~YouTubeViewModel() = default;

QObject* YouTubeViewModel::videos() const { return videos_.get(); }
QObject* YouTubeViewModel::broadcasts() const { return broadcasts_.get(); }
QObject* YouTubeViewModel::recordings() const { return recordings_.get(); }
QObject* YouTubeViewModel::chat() const { return chat_.get(); }

void YouTubeViewModel::setError(const QString& e) {
    lastError_ = e;
    emit errorChanged();
    if (!e.isEmpty()) emit notify("YouTube: " + e, "error");
}

void YouTubeViewModel::track(bool start) {
    busy_ = std::max(0, busy_ + (start ? 1 : -1));
    emit busyChanged();
}

// --- Configuracion y cuenta ------------------------------------------------------------

QString YouTubeViewModel::saveClient(const QString& jsonOrId, const QString& secret) {
    QString err;
    if (!client_->saveClient(jsonOrId, secret, err)) return err;
    emit notify("Credenciales de Google guardadas: ahora conecta tu cuenta.", "ok");
    return {};
}

void YouTubeViewModel::clearClient() {
    client_->clearClient();
    videos_->setItems({});
    broadcasts_->setItems({});
}

void YouTubeViewModel::login() {
    setError({});
    client_->login([this](bool ok, const QJsonObject& acc, const QString& e) {
        if (!ok) {
            if (e != "Login cancelado.") setError(e);
            return;
        }
        emit notify("Conectado a YouTube: " + acc.value("title").toString(), "ok");
        refresh();
    });
}

void YouTubeViewModel::cancelLogin() { client_->cancelLogin(); }

void YouTubeViewModel::logout() {
    setChatBroadcast({});
    client_->logout([this](bool, const QJsonObject&, const QString&) {
        videos_->setItems({});
        broadcasts_->setItems({});
        emit notify("Cuenta de YouTube desconectada.", "info");
    });
}

void YouTubeViewModel::refresh() {
    if (!loggedIn()) return;
    track(true);
    client_->listVideos(50, [this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        const QJsonArray items = withUid(d.value("items").toArray());
        videos_->setItems(items);
        fetchThumbs(items, videos_.get());
    });
    track(true);
    client_->listBroadcasts([this](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        const QJsonArray items = withUid(d.value("items").toArray());
        broadcasts_->setItems(items);
        fetchThumbs(items, broadcasts_.get());
    });
    refreshRecordings();
}

void YouTubeViewModel::fetchThumbs(const QJsonArray& items, JsonListModel* model) {
    QPointer<YouTubeViewModel> self(this);
    const auto apply = [model](const QHash<QString, QString>& thumbs) {
        QJsonArray cur = model->items();
        bool changed = false;
        for (int i = 0; i < cur.size(); ++i) {
            QJsonObject o = cur[i].toObject();
            const QString data = thumbs.value(o.value("thumbnail").toString());
            if (!data.isEmpty() && o.value("thumb").toString() != data) {
                o.insert("thumb", data);
                cur[i] = o;
                changed = true;
            }
        }
        if (changed) model->setItems(cur);
    };
    apply(thumbs_);
    for (const QJsonValue& v : items) {
        const QString url = v.toObject().value("thumbnail").toString();
        if (url.isEmpty() || thumbs_.contains(url)) continue;
        thumbs_.insert(url, {}); // en camino
        client_->download(url, [self, url, apply](bool ok, const QByteArray& jpeg) {
            if (!self || !ok) return;
            self->thumbs_.insert(url, "data:image/jpeg;base64," + QString::fromLatin1(jpeg.toBase64()));
            apply(self->thumbs_);
        });
    }
}

QString YouTubeViewModel::recordingsDir() const { return QString::fromStdString(rt_.carpetaGrabacion()); }

void YouTubeViewModel::refreshRecordings() {
    // Las del evento en curso y las de Recordings/, lo más nuevo primero.
    QFileInfoList files;
    for (const std::string& c : rt_.carpetasDeGrabaciones())
        files += QDir(QString::fromStdString(c))
                     .entryInfoList({"*.mkv", "*.mp4", "*.mov", "*.flv", "*.webm"}, QDir::Files, QDir::Time);
    std::sort(files.begin(), files.end(),
              [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    QJsonArray out;
    for (const QFileInfo& f : files) {
        out.append(QJsonObject{{"id", f.absoluteFilePath()},
                               {"uid", f.absoluteFilePath()},
                               {"name", f.fileName()},
                               {"size", static_cast<double>(f.size())},
                               {"modified", f.lastModified().toString("dd/MM/yyyy HH:mm")},
                               {"path", f.absoluteFilePath()}});
    }
    recordings_->setItems(out);
}

void YouTubeViewModel::openUrl(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }
void YouTubeViewModel::copy(const QString& text) { QGuiApplication::clipboard()->setText(text); }

// --- Videos --------------------------------------------------------------------------------

void YouTubeViewModel::updateVideo(const QString& id, const QString& title, const QString& description,
                                   const QString& privacy) {
    track(true);
    client_->updateVideo(id,
                         QJsonObject{{"title", title}, {"description", description}, {"privacy", privacy}},
                         [this](bool ok, const QJsonObject&, const QString& e) {
                             track(false);
                             if (!ok) return setError(e);
                             emit notify("Video actualizado.", "ok");
                             refresh();
                         });
}

void YouTubeViewModel::deleteVideo(const QString& id) {
    track(true);
    client_->deleteVideo(id, [this](bool ok, const QJsonObject&, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        emit notify("Video borrado de YouTube.", "ok");
        refresh();
    });
}

void YouTubeViewModel::uploadRecording(const QString& path, const QString& title, const QString& description,
                                       const QString& privacy) {
    uploadRecording(path, QJsonObject{{"title", title}, {"description", description}, {"privacy", privacy}},
                    [this](bool ok, const QJsonObject& d, const QString& e) {
                        if (!ok) return setError(e);
                        emit notify("Subido a YouTube: " + d.value("url").toString(), "ok");
                    });
}

void YouTubeViewModel::uploadRecording(const QString& path, const QJsonObject& meta,
                                       YouTubeClient::Done done) {
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

// --- En vivo -------------------------------------------------------------------------------

void YouTubeViewModel::createBroadcast(const QString& title, const QString& description,
                                       const QString& privacy, const QString& latency) {
    track(true);
    client_->createBroadcast(
        QJsonObject{
            {"title", title}, {"description", description}, {"privacy", privacy}, {"latency", latency}},
        [this](bool ok, const QJsonObject& d, const QString& e) {
            track(false);
            if (!ok) return setError(e);
            emit notify("Evento creado: " + d.value("url").toString(), "ok");
            refresh();
        });
}

void YouTubeViewModel::goLive(const QString& broadcastId) {
    goLive(broadcastId, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Transmitiendo a YouTube. El evento pasa a en vivo cuando YouTube recibe la señal.",
                    "ok");
    });
}

void YouTubeViewModel::goLive(const QString& broadcastId, YouTubeClient::Done done) {
    if (rt_.engine().status().streaming) {
        return done(false, {},
                    broadcastId == liveBroadcastId_
                        ? "Ya se esta transmitiendo a este evento."
                        : "La app ya esta transmitiendo: corta esa transmision primero.");
    }
    track(true);
    QPointer<YouTubeViewModel> self(this);
    client_->ingestion(broadcastId, [self, broadcastId, done](bool ok, const QJsonObject& ing,
                                                              const QString& e) {
        if (!self) return;
        self->track(false);
        if (!ok) return done(false, {}, e);
        const QString url = ing.value("url").toString();
        const QString key = ing.value("key").toString();
        if (url.isEmpty() || key.isEmpty())
            return done(false, {}, "YouTube no entrego servidor o clave de ingesta.");
        // Servidor y clave SOLO en memoria (la politica del appliance).
        if (!self->rt_.startStreaming(url.toStdString(), key.toStdString()))
            return done(false, {},
                        "La app no pudo empezar a transmitir: " +
                            QString::fromStdString(self->rt_.lastError()));
        self->liveBroadcastId_ = broadcastId;
        self->setChatBroadcast(broadcastId);
        emit self->stateChanged();
        emit self->vivo(QVariantMap{{"plataforma", "youtube"},
                                    {"id", broadcastId},
                                    {"url", "https://youtu.be/" + broadcastId},
                                    {"estudio", "https://studio.youtube.com/video/" + broadcastId + "/livestreaming"}});
        done(true,
             QJsonObject{{"broadcastId", broadcastId},
                         {"server", url},
                         {"url", "https://youtu.be/" + broadcastId},
                         {"studioUrl", "https://studio.youtube.com/video/" + broadcastId + "/livestreaming"}},
             {});
        // Eventos sin inicio automatico: cuando YouTube ya recibe la señal, se
        // los pasa a "en vivo" (hasta ~90 s de espera).
        auto tries = std::make_shared<int>(0);
        auto check = std::make_shared<std::function<void()>>();
        *check = [self, broadcastId, tries, check]() {
            if (!self || self->liveBroadcastId_ != broadcastId || ++*tries > 18) {
                *check = nullptr;
                return;
            }
            QUrlQuery q;
            q.addQueryItem("part", "status,contentDetails");
            q.addQueryItem("id", broadcastId);
            self->client_->api(
                "GET", "/liveBroadcasts", q, {},
                [self, broadcastId, check](bool ok2, const QJsonObject& d, const QString&) {
                    if (!self || !*check) return;
                    const QJsonObject b = d.value("items").toArray().at(0).toObject();
                    const QString life = b.value("status").toObject().value("lifeCycleStatus").toString();
                    const bool autoStart =
                        b.value("contentDetails").toObject().value("enableAutoStart").toBool(true);
                    if (ok2 && (life == "live" || life == "complete" || autoStart)) {
                        self->refresh();
                        *check = nullptr;
                        return;
                    }
                    self->client_->ingestion(
                        broadcastId,
                        [self, broadcastId, check](bool ok3, const QJsonObject& ing2, const QString&) {
                            if (!self || !*check) return;
                            if (ok3 && ing2.value("streamStatus").toString() == "active") {
                                self->client_->transition(broadcastId, "live",
                                                          [self](bool, const QJsonObject&, const QString&) {
                                                              if (self) self->refresh();
                                                          });
                                *check = nullptr;
                                return;
                            }
                            QTimer::singleShot(5000, self, [check]() {
                                if (*check) (*check)();
                            });
                        });
                });
        };
        QTimer::singleShot(5000, self, [check]() {
            if (*check) (*check)();
        });
    });
}

void YouTubeViewModel::endBroadcast(const QString& broadcastId) {
    endBroadcast(broadcastId, [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        emit notify("Evento terminado en YouTube.", "ok");
    });
}

void YouTubeViewModel::endBroadcast(const QString& broadcastId, YouTubeClient::Done done) {
    if (broadcastId == liveBroadcastId_) {
        rt_.stopStreaming();
        liveBroadcastId_.clear();
        emit stateChanged();
    }
    track(true);
    client_->transition(broadcastId, "complete",
                        [this, done](bool ok, const QJsonObject& d, const QString& e) {
                            track(false);
                            refresh();
                            done(ok, d, e);
                        });
}

void YouTubeViewModel::deleteBroadcast(const QString& broadcastId) {
    track(true);
    client_->deleteBroadcast(broadcastId, [this](bool ok, const QJsonObject&, const QString& e) {
        track(false);
        if (!ok) return setError(e);
        emit notify("Evento borrado.", "ok");
        refresh();
    });
}

// --- Chat -------------------------------------------------------------------------------------

void YouTubeViewModel::setChatBroadcast(const QString& broadcastId) {
    if (broadcastId == chatBroadcastId_) return;
    chatBroadcastId_ = broadcastId;
    liveChatId_.clear();
    chatPageToken_.clear();
    chatItems_ = {};
    chat_->setItems({});
    chatTimer_.stop();
    emit chatChanged();
    if (!broadcastId.isEmpty()) QTimer::singleShot(0, this, &YouTubeViewModel::pollChat);
}

void YouTubeViewModel::pollChat() {
    if (chatBroadcastId_.isEmpty() || !loggedIn() || chatInFlight_) return;
    chatInFlight_ = true;
    const QString forBroadcast = chatBroadcastId_;
    QPointer<YouTubeViewModel> self(this);
    const auto fetch = [self, forBroadcast]() {
        self->client_->chatMessages(
            self->liveChatId_, self->chatPageToken_,
            [self, forBroadcast](bool ok, const QJsonObject& d, const QString& e) {
                if (!self) return;
                self->chatInFlight_ = false;
                if (forBroadcast != self->chatBroadcastId_) return; // cambio mientras tanto
                if (!ok) {
                    // Chat terminado o deshabilitado: se deja de leer.
                    if (e.contains("ended", Qt::CaseInsensitive) ||
                        e.contains("disabled", Qt::CaseInsensitive) ||
                        e.contains("NotFound", Qt::CaseInsensitive)) {
                        emit self->notify("YouTube: el chat de ese evento no esta disponible (" + e + ")",
                                          "warn");
                        return;
                    }
                    self->chatTimer_.start(15000);
                    return;
                }
                self->chatPageToken_ = d.value("nextPageToken").toString();
                const QJsonArray fresh = d.value("items").toArray();
                if (!fresh.isEmpty()) {
                    for (const QJsonValue& v : fresh)
                        self->chatItems_.append(v);
                    while (self->chatItems_.size() > 300)
                        self->chatItems_.removeFirst();
                    self->chat_->setItems(withUid(self->chatItems_));
                    emit self->chatChanged();
                }
                // Cada lectura cuesta cuota: nunca mas seguido de 6 s.
                self->chatTimer_.start(std::max(6000, d.value("pollingIntervalMillis").toInt(6000)));
            });
    };
    if (!liveChatId_.isEmpty()) {
        fetch();
        return;
    }
    QUrlQuery q;
    q.addQueryItem("part", "snippet");
    q.addQueryItem("id", forBroadcast);
    client_->api("GET", "/liveBroadcasts", q, {},
                 [self, forBroadcast, fetch](bool ok, const QJsonObject& d, const QString& e) {
                     if (!self) return;
                     if (!ok || forBroadcast != self->chatBroadcastId_) {
                         self->chatInFlight_ = false;
                         if (!ok) self->setError(e);
                         return;
                     }
                     self->liveChatId_ = d.value("items")
                                             .toArray()
                                             .at(0)
                                             .toObject()
                                             .value("snippet")
                                             .toObject()
                                             .value("liveChatId")
                                             .toString();
                     if (self->liveChatId_.isEmpty()) {
                         self->chatInFlight_ = false;
                         emit self->notify(
                             "YouTube: ese evento no tiene chat (todavia no empezo o esta deshabilitado).",
                             "warn");
                         self->chatTimer_.start(20000);
                         return;
                     }
                     fetch();
                 });
}

void YouTubeViewModel::sendChat(const QString& text) {
    if (liveChatId_.isEmpty() || text.trimmed().isEmpty()) return;
    client_->chatSend(liveChatId_, text.trimmed(), [this](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return setError(e);
        chatTimer_.start(500); // que aparezca pronto
    });
}

} // namespace ba::app
