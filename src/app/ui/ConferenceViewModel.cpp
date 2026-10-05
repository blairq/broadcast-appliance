// ============================================================================
// src/app/ui/ConferenceViewModel.cpp — ver el header
// ============================================================================

#include "ui/ConferenceViewModel.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "AppRuntime.hpp"
#include "AppSettings.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app {

/// Ver el uso: cuánto se espera antes de que el asistente aborde a alguien
/// que acaba de llegar al lobby, para que el reconocimiento llegue a contestar.
constexpr int kEsperarAlReconocimientoMs = 2500;

namespace fs = std::filesystem;

// --- JsonListModel ------------------------------------------------------------

JsonListModel::JsonListModel(QStringList keys, QObject* parent)
    : QAbstractListModel(parent), keys_(std::move(keys)) {}

QVariant JsonListModel::data(const QModelIndex& index, int role) const {
    const int k = role - Qt::UserRole - 1;
    if (!index.isValid() || index.row() >= items_.size() || k < 0 || k >= keys_.size()) return {};
    return items_.at(index.row()).toObject().value(keys_.at(k)).toVariant();
}

QHash<int, QByteArray> JsonListModel::roleNames() const {
    QHash<int, QByteArray> r;
    for (int i = 0; i < keys_.size(); ++i)
        r.insert(Qt::UserRole + 1 + i, keys_.at(i).toUtf8());
    return r;
}

QVariantMap JsonListModel::fila(int i) const {
    if (i < 0 || i >= items_.size()) return {};
    const QJsonObject o = items_.at(i).toObject();
    QVariantMap out;
    for (const QString& k : keys_)
        out.insert(k == "id" ? QStringLiteral("uid") : k, o.value(k).toVariant());
    return out;
}

void JsonListModel::setItems(const QJsonArray& items) {
    bool sameIds = items.size() == items_.size();
    for (int i = 0; sameIds && i < items.size(); ++i) {
        sameIds = items.at(i).toObject().value("id") == items_.at(i).toObject().value("id");
    }
    if (sameIds) {
        if (items == items_) return;
        items_ = items;
        if (!items_.isEmpty()) emit dataChanged(index(0), index(items_.size() - 1));
        return;
    }
    beginResetModel();
    items_ = items;
    endResetModel();
    emit countChanged();
}

void JsonListModel::append(const QJsonObject& item) {
    beginInsertRows({}, items_.size(), items_.size());
    items_.append(item);
    endInsertRows();
    emit countChanged();
}

// --- ConferenceViewModel ------------------------------------------------------

ConferenceViewModel::ConferenceViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), rt_(rt), client_(std::make_unique<ConferenceClient>()),
      waiting_(std::make_unique<JsonListModel>(
          QStringList{"uid", "name", "connected", "joinedMs", "lobbySlot", "talk", "info", "contact"})),
      inside_(std::make_unique<JsonListModel>(QStringList{"uid", "name", "slot", "connected", "mediaUp",
                                                          "mic", "cam", "screen", "hand", "admittedMs",
                                                          "presenter", "speaker", "info", "contact"})),
      shares_(std::make_unique<JsonListModel>(
          QStringList{"uid", "name", "kind", "mime", "bytes", "participantName", "path", "atMs", "url"})),
      chat_(std::make_unique<JsonListModel>(
          QStringList{"uid", "fromId", "fromName", "to", "text", "atMs", "mine"})),
      speakers_(std::make_unique<JsonListModel>(
          QStringList{"uid", "name", "info", "contact", "speaker", "position", "allottedSec", "onAirSinceMs",
                      "usedMs", "connected", "slot", "avatar", "backstageSlot", "presenter"})) {
    connect(client_.get(), &ConferenceClient::onlineChanged, this, [this]() { emit onlineChanged(); });
    connect(client_.get(), &ConferenceClient::snapshotChanged, this, &ConferenceViewModel::onSnapshot);
    connect(client_.get(), &ConferenceClient::chatReceived, this,
            [this](const QJsonObject& m) { onChatMsg(m, true); });
    connect(client_.get(), &ConferenceClient::failed, this, [this](const QString& e) {
        static const QHash<QString, QString> kText{
            {"full", "La sala esta llena: sacá a alguien primero"},
            {"not_found", "Ya no esta en la sala"},
            {"state", "No aplica en su estado actual"},
            {"offline", "La sala no responde (¿el gateway esta arriba?)"}};
        emit notify(kText.value(e, "La sala respondio: " + e), "error");
    });
    // Estado de red: cada 5 s (cambia poco; no amerita un evento propio).
    const auto pollNetwork = [this]() {
        if (!client_->online()) return;
        client_->get("/api/v1/network", [this](bool ok, const QJsonObject& r) {
            if (!ok) return;
            const QVariantMap m = r.toVariantMap();
            if (m != network_) {
                network_ = m;
                emit networkChanged();
            }
        });
    };
    // Fotos de las tarjetas de oradores (las renueva el invitado cada 5 s).
    connect(&avatarPoll_, &QTimer::timeout, this, &ConferenceViewModel::pollAvatars);
    avatarPoll_.start(5000);
    connect(&networkPoll_, &QTimer::timeout, this, pollNetwork);
    connect(client_.get(), &ConferenceClient::onlineChanged, this, pollNetwork);
    networkPoll_.start(5000);
    // Si el operador lo dejo prendido, sigue prendido al arrancar: es una
    // decision del evento, no un boton que haya que volver a apretar. Si no
    // hay asistente, se apaga solo en cuanto lo intenta (ver onSnapshot).
    autoIntake_ = rt_.gateway().autoIntake;
    // La sala arranca despues que esto, asi que se le avisa cuando conecta.
    connect(client_.get(), &ConferenceClient::onlineChanged, this, [this]() {
        if (client_->online()) client_->post("/api/v1/room", QJsonObject{{"intake", autoIntake_}});
    });
}

ConferenceViewModel::~ConferenceViewModel() = default;

bool ConferenceViewModel::online() const { return client_->online(); }
QString ConferenceViewModel::room() const { return client_->snapshot().value("room").toString(); }
QString ConferenceViewModel::pin() const { return client_->snapshot().value("pin").toString(); }
QString ConferenceViewModel::guestUrl() const { return client_->snapshot().value("guestUrl").toString(); }
bool ConferenceViewModel::lobby() const { return client_->snapshot().value("lobby").toBool(true); }
bool ConferenceViewModel::locked() const { return client_->snapshot().value("locked").toBool(); }
int ConferenceViewModel::maxGuests() const { return client_->snapshot().value("maxGuests").toInt(); }
int ConferenceViewModel::slotsFree() const { return client_->snapshot().value("slotsFree").toInt(); }
int ConferenceViewModel::peopleConnected() const {
    return static_cast<int>(client_->snapshot().value("participants").toArray().size());
}
QObject* ConferenceViewModel::waiting() const { return waiting_.get(); }
QObject* ConferenceViewModel::inside() const { return inside_.get(); }
QObject* ConferenceViewModel::shares() const { return shares_.get(); }
QObject* ConferenceViewModel::chat() const { return chat_.get(); }
QObject* ConferenceViewModel::speakers() const { return speakers_.get(); }
QVariantMap ConferenceViewModel::speakerCfg() const {
    return client_->snapshot().value("speakers").toObject().toVariantMap();
}
bool ConferenceViewModel::speakerMode() const {
    return client_->snapshot().value("speakers").toObject().value("enabled").toBool();
}

void ConferenceViewModel::rebuildSpeakers() {
    const QJsonObject s = client_->snapshot();
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue& v : s.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        byId.insert(p.value("id").toString(), p);
    }
    QJsonArray rows;
    QVariantList onAir;
    int pos = 0;
    for (const QJsonValue& v : s.value("order").toArray()) {
        const auto it = byId.constFind(v.toString());
        if (it == byId.constEnd()) continue;
        QJsonObject p = *it;
        p.insert("uid", p.value("id"));
        const QString st = p.value("speaker").toString();
        p.insert("position", st == "queued" ? ++pos : 0);
        p.insert("avatar", avatars_.value(p.value("id").toString()));
        if (st == "onair") {
            onAir << QVariantMap{{"uid", p.value("id").toString()},
                                 {"name", p.value("name").toString()},
                                 {"allottedSec", p.value("allottedSec").toInt()},
                                 {"onAirSinceMs", p.value("onAirSinceMs").toDouble()}};
        }
        rows.append(p);
    }
    speakers_->setItems(rows);
    onAir_ = onAir;
}

void ConferenceViewModel::pollAvatars() {
    if (!client_->online()) return;
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (!p.value("hasAvatar").toBool()) continue;
        const QString id = p.value("id").toString();
        client_->getRaw("/api/v1/participants/" + QString::fromUtf8(QUrl::toPercentEncoding(id)) + "/avatar",
                        [this, id](bool ok, const QByteArray& jpeg) {
                            if (!ok || jpeg.isEmpty()) return;
                            const QString url =
                                "data:image/jpeg;base64," + QString::fromLatin1(jpeg.toBase64());
                            if (avatars_.value(id) == url) return;
                            avatars_.insert(id, url);
                            rebuildSpeakers();
                        });
    }
    // Las de quien ya no esta.
    QSet<QString> alive;
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray())
        alive.insert(v.toObject().value("id").toString());
    for (auto it = avatars_.begin(); it != avatars_.end();)
        it = alive.contains(it.key()) ? std::next(it) : avatars_.erase(it);
}

void ConferenceViewModel::setSpeakerConfig(const QVariantMap& cfg) {
    client_->post("/api/v1/speakers", QJsonObject::fromVariantMap(cfg));
}
void ConferenceViewModel::speakerOnAir(const QString& id, bool on) {
    participantAction(id, "onair", QJsonObject{{"on", on}});
}
void ConferenceViewModel::speakerNext() {
    client_->post("/api/v1/speakers/next", {}, [this](bool ok, const QJsonObject& r) {
        Q_UNUSED(r);
        if (!ok) emit notify("No hay nadie mas en la fila", "info");
    });
}
void ConferenceViewModel::speakerMove(const QString& id, int index) {
    participantAction(id, "move", QJsonObject{{"index", std::max(0, index)}});
}
void ConferenceViewModel::speakerTime(const QString& id, int sec) {
    if (sec > 0) participantAction(id, "time", QJsonObject{{"sec", sec}});
}
void ConferenceViewModel::speakerRequeue(const QString& id) { participantAction(id, "requeue"); }
QVariantMap ConferenceViewModel::usage() const {
    const QJsonObject s = client_->snapshot();
    QVariantMap u = s.value("uso").toObject().toVariantMap();
    int conectados = 0;
    int conVideo = 0;
    int enSala = 0;
    for (const QJsonValue& v : s.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (!p.value("connected").toBool()) continue;
        ++conectados;
        if (p.value("mediaUp").toBool() && p.value("cam").toBool()) ++conVideo;
        if (p.value("state").toString() == "admitted") ++enSala;
    }
    u["conectados"] = conectados;
    u["conVideo"] = conVideo;
    u["enSala"] = enSala;
    u["lugares"] = s.value("maxGuests").toInt();
    return u;
}

QString ConferenceViewModel::photoOf(const QString& idOrName) const {
    if (avatars_.contains(idOrName)) return avatars_.value(idOrName);
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("name").toString().compare(idOrName, Qt::CaseInsensitive) == 0)
            return avatars_.value(p.value("id").toString());
    }
    return {};
}

void ConferenceViewModel::setAutoIntake(bool on) {
    if (autoIntake_ == on) return;
    autoIntake_ = on;
    // Al prenderlo, los que ya estaban esperando tambien se atienden.
    if (!on) intakeDone_.clear();
    emit autoIntakeChanged();
    rt_.gateway().autoIntake = on;
    rt_.save();
    // B tiene que enterarse: con el asistente atendiendo, la pantalla de
    // ingreso del invitado no le pide los datos (se los pregunta el asistente).
    client_->post("/api/v1/room", QJsonObject{{"intake", on}});
    if (on) onSnapshot();
}

void ConferenceViewModel::setPresenter(const QString& id, bool on) {
    participantAction(id, "presenter", QJsonObject{{"on", on}});
}
void ConferenceViewModel::setProfile(const QString& id, const QString& name, const QString& info,
                                     const QString& contact) {
    participantAction(id, "profile", QJsonObject{{"name", name}, {"info", info}, {"contact", contact}});
}

int ConferenceViewModel::parseDuration(const QString& text) const {
    const QString t = text.trimmed();
    static const QRegularExpression mss("^(\\d+):(\\d{1,2})$");
    static const QRegularExpression secs("^(\\d+)\\s*s$");
    static const QRegularExpression mins("^(\\d+)([.,](\\d+))?$");
    if (const auto m = mss.match(t); m.hasMatch()) return m.captured(1).toInt() * 60 + m.captured(2).toInt();
    if (const auto m = secs.match(t); m.hasMatch()) return m.captured(1).toInt();
    if (const auto m = mins.match(t); m.hasMatch()) {
        return static_cast<int>(QString(t).replace(',', '.').toDouble() * 60.0 + 0.5);
    }
    return 0;
}

void ConferenceViewModel::requestBoardLink(const QString& target) {
    const QString t = target == "remote" ? "remote" : "local";
    client_->get("/api/v1/board-link?target=" + t, [this, t](bool ok, const QJsonObject& r) {
        QStringList rows;
        for (const QJsonValue& v : r.value("modules").toArray())
            rows << v.toString();
        QString msg = r.value("message").toString();
        const bool good = ok && r.value("ok").toBool();
        if (!good && msg.isEmpty()) msg = "La sala no responde";
        emit qrReady("board-" + t, good ? r.value("url").toString() : QString(), rows, msg);
    });
}

void ConferenceViewModel::onSnapshot() {
    const QJsonObject s = client_->snapshot();
    QJsonArray waiting;
    QJsonArray inside;
    QStringList nowWaiting;
    QStringList slotNames;
    for (int i = 0; i < s.value("maxGuests").toInt(); ++i)
        slotNames << QString();
    for (const QJsonValue& v : s.value("participants").toArray()) {
        QJsonObject p = v.toObject();
        p.insert("uid", p.value("id")); // "id" no puede ser un rol en QML
        const int slot = p.value("slot").toInt();
        if (p.value("state").toString() == "admitted" && slot >= 1 && slot <= slotNames.size()) {
            slotNames[slot - 1] = p.value("name").toString();
        }
        if (p.value("state").toString() == "waiting") {
            waiting.append(p);
            nowWaiting << p.value("id").toString();
            if (!knownWaiting_.contains(p.value("id").toString()) && !firstSnapshot_) {
                emit notify(p.value("name").toString() + " espera en el lobby", "info");
            }
            // Atencion automatica: se le encarga al asistente una sola vez por
            // persona (mientras siga esperando).
            const QString pid = p.value("id").toString();
            if (autoIntake_ && intakeSink_ && !intakeDone_.contains(pid)) {
                intakeDone_.insert(pid);
                // UN MOMENTO ANTES DE ABORDARLO, a propósito. El
                // reconocimiento de rostros necesita una vuelta para tener algo
                // que decir de alguien que se acaba de conectar. Atendiéndolo
                // en el instante en que aparece, el asistente mira la libreta,
                // no encuentra nada todavía y le pide los datos de cero a
                // alguien que el sistema conoce perfectamente —y que un
                // segundo después va a estar identificado—. Dos segundos y
                // medio no los nota nadie del otro lado.
                const QString nombre = p.value("name").toString();
                QTimer::singleShot(kEsperarAlReconocimientoMs, this, [this, pid, nombre]() {
                    if (intakeSink_ == nullptr) return;
                    // Si mientras tanto entró sola (lista del evento), la
                    // atiende el otro camino: `marcarAtendido` lo saca de acá.
                    if (!intakeDone_.contains(pid)) return;
                    const QString err = intakeSink_(pid, nombre, false);
                    if (!err.isEmpty()) {
                        // Sin asistente no tiene sentido seguir: se apaga como si
                        // lo hubiera apagado el operador (se guarda y B se entera).
                        setAutoIntake(false);
                        emit notify(err, "error");
                    }
                });
            }
        } else if (slot > 0) {
            inside.append(p); // en el programa (la fila de oradores va aparte)
        } else if (p.value("speaker").toString() == "none") {
            // Admitido, sin lugar en el programa y fuera de la fila de oradores:
            // igual esta en la sala y conectado. Se muestra (sin video) para que
            // nunca quede alguien invisible para el operador.
            inside.append(p);
        }
    }
    // Los que ya no esperan se olvidan: si vuelven, se los atiende de nuevo.
    for (auto it = intakeDone_.begin(); it != intakeDone_.end();)
        it = nowWaiting.contains(*it) ? std::next(it) : intakeDone_.erase(it);
    knownWaiting_ = nowWaiting;
    slotNames_ = slotNames;
    // Adentro, ordenados por slot (coincide con "Invitado N" de las fuentes).
    QList<QJsonObject> sorted;
    for (const QJsonValue& v : inside)
        sorted << v.toObject();
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value("slot").toInt() < b.value("slot").toInt();
    });
    inside = QJsonArray();
    for (const QJsonObject& o : sorted)
        inside.append(o);
    waiting_->setItems(waiting);
    inside_->setItems(inside);

    // Monitoreo: solo se escucha (por auriculares) a quien se le esta hablando.
    std::vector<bool> monitor(9, false);
    for (const QJsonValue& v : waiting) {
        const QJsonObject p = v.toObject();
        const int k = p.value("lobbySlot").toInt();
        if (k >= 1 && k <= 8 && p.value("talk").toBool()) monitor[static_cast<size_t>(k)] = true;
    }
    for (int k = 1; k <= 8; ++k)
        rt_.setLobbyMonitor(k, monitor[static_cast<size_t>(k)]);

    QJsonArray shares;
    const QJsonArray all = s.value("shares").toArray();
    for (int i = all.size() - 1; i >= 0; --i) { // lo mas nuevo arriba
        QJsonObject f = all.at(i).toObject();
        f.insert("url", QUrl::fromLocalFile(f.value("path").toString()).toString());
        f.insert("uid", f.value("id"));
        shares.append(f);
    }
    shares_->setItems(shares);

    // Chat: el snapshot trae el historial reciente. Se agregan solo los que
    // faltan (por numero de secuencia); si la secuencia volvio atras, B se
    // reinicio y el historial se reemplaza.
    const QJsonArray history = s.value("chat").toArray();
    const qint64 lastInSnap = history.isEmpty() ? 0 : history.last().toObject().value("seq").toInteger();
    if (lastInSnap < lastSeq_) {
        chat_->setItems({});
        ++chatRev_;
        emit chatRevChanged();
        lastSeq_ = 0;
    }
    for (const QJsonValue& v : history)
        onChatMsg(v.toObject(), false);
    if (s.contains("serverNowMs")) {
        clockOffset_ =
            s.value("serverNowMs").toDouble() - static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    }
    rebuildSpeakers();
    firstSnapshot_ = false;
    emit roomChanged();
}

void ConferenceViewModel::onChatMsg(const QJsonObject& msg, bool live) {
    const qint64 seq = msg.value("seq").toInteger();
    if (seq <= lastSeq_) return; // ya estaba (llega por snapshot y por evento)
    lastSeq_ = seq;
    QJsonObject m = msg;
    m.insert("id", QString::number(seq));
    m.insert("uid", QString::number(seq));
    const bool mine = m.value("fromId").toString() == "host";
    m.insert("mine", mine);
    chat_->append(m);
    ++chatRev_;
    emit chatRevChanged();
    if (live && !mine)
        emit notify(m.value("fromName").toString() + ": " + m.value("text").toString(), "info");
}

QString ConferenceViewModel::nameOf(const QString& id) const {
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        if (v.toObject().value("id").toString() == id) return v.toObject().value("name").toString();
    }
    return id;
}

void ConferenceViewModel::participantAction(const QString& id, const QString& action,
                                            const QJsonObject& body) {
    client_->post("/api/v1/participants/" + QString::fromUtf8(QUrl::toPercentEncoding(id)) + "/" + action,
                  body);
}

void ConferenceViewModel::admit(const QString& id) { participantAction(id, "admit"); }
void ConferenceViewModel::reject(const QString& id) { participantAction(id, "reject"); }
void ConferenceViewModel::kick(const QString& id) { participantAction(id, "kick"); }
void ConferenceViewModel::request(const QString& id, const QString& what) {
    participantAction(id, "request", QJsonObject{{"what", what}});
}

QVariantList ConferenceViewModel::chatTargets() const {
    QVariantList out{QVariantMap{{"label", "A todos"}, {"value", "all"}}};
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const QString where = p.value("state").toString() == "waiting" ? " (lobby)" : "";
        out << QVariantMap{{"label", "A " + p.value("name").toString() + where},
                           {"value", p.value("id").toString()}};
    }
    return out;
}

QVariantMap ConferenceViewModel::participanteEnSlot(int slot) const {
    const QJsonObject snap = const_cast<ConferenceViewModel*>(this)->client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("slot").toInt() == slot) return p.toVariantMap();
    }
    return {};
}

QStringList ConferenceViewModel::slotNames() const {
    if (!online()) return {};
    return slotNames_;
}

QString ConferenceViewModel::operatorVideo() const {
    return QString::fromStdString(rt_.config().operatorVideo);
}
QString ConferenceViewModel::operatorAudio() const {
    return QString::fromStdString(rt_.config().operatorAudio);
}

QStringList ConferenceViewModel::videoSourceNames() const {
    QStringList out;
    for (const std::string& scene : rt_.engine().sceneNames()) {
        for (const std::string& s : rt_.engine().sceneSources(scene)) {
            const QString q = QString::fromStdString(s);
            if (!out.contains(q)) out << q;
        }
    }
    return out;
}

QStringList ConferenceViewModel::audioSourceNames() const {
    QStringList out;
    for (const std::string& s : rt_.engine().audioSources()) {
        // Las voces de los invitados no son "el operador".
        if (s.rfind("Invitado", 0) == 0) continue;
        out << QString::fromStdString(s);
    }
    return out;
}

void ConferenceViewModel::talk(const QString& id, bool on) {
    participantAction(id, "talk", QJsonObject{{"on", on}});
}

void ConferenceViewModel::setOperatorFeed(const QString& video, const QString& audio) {
    if (!rt_.setOperatorFeed(video.toStdString(), audio.toStdString())) {
        emit notify(QString::fromStdString(rt_.lastError()), "error");
    }
    rt_.save();
    emit operatorChanged();
}

void ConferenceViewModel::configureDirectAccess(const QString& endpoint, const QString& publicAddress,
                                                const QString& iface) {
    const QString ep = endpoint.trimmed();
    if (ep.isEmpty()) {
        emit serverConfigReady({}, "Falta la direccion del servidor (host:puerto)");
        return;
    }
    QJsonObject req{{"endpoint", ep},
                    {"interface", iface.trimmed().isEmpty() ? QString("eth0") : iface.trimmed()}};
    client_->post("/api/v1/network/server-config", req,
                  [this, ep, publicAddress](bool ok, const QJsonObject& r) {
                      if (!ok) {
                          emit serverConfigReady({}, "El gateway no pudo generar la configuracion: " +
                                                         r.value("error").toString());
                          return;
                      }
                      // Este equipo queda apuntando a ese servidor, con el tunel encendido.
                      GatewaySettings& g = rt_.gateway();
                      g.wgEnabled = true;
                      g.wgEndpoint = r.value("endpoint").toString().toStdString();
                      g.wgServerPublicKey = r.value("serverPublicKey").toString().toStdString();
                      QString pub = publicAddress.trimmed();
                      if (pub.isEmpty()) pub = ep.section(':', 0, 0); // por defecto, el mismo host
                      g.wgPublicAddress = pub.toStdString();
                      rt_.save();
                      emit serverConfigReady(r.value("config").toString(), {});
                      emit notify(rt_.restartGateway() ? "Tunel configurado: reiniciando el gateway"
                                                       : "Tunel configurado (reinicia el gateway)",
                                  "ok");
                  });
}

void ConferenceViewModel::requestQr(const QString& target, bool withPin) {
    const QString t = target == "remote" ? "remote" : "local";
    client_->get("/api/v1/qr?target=" + t + (withPin ? "&pin=1" : ""),
                 [this, t](bool ok, const QJsonObject& r) {
                     QStringList rows;
                     for (const QJsonValue& v : r.value("modules").toArray())
                         rows << v.toString();
                     QString msg = r.value("message").toString();
                     if (msg.isEmpty()) msg = r.value("warning").toString();
                     const bool good = ok && r.value("ok").toBool();
                     if (!good && msg.isEmpty()) msg = "La sala no responde";
                     emit qrReady(t, good ? r.value("url").toString() : QString(), rows, msg);
                 });
}

void ConferenceViewModel::admitAll() {
    for (const QJsonValue& v : waiting_->items())
        admit(v.toObject().value("id").toString());
}

void ConferenceViewModel::sendChat(const QString& to, const QString& text) {
    if (text.trimmed().isEmpty()) return;
    client_->post("/api/v1/chat", QJsonObject{{"to", to.isEmpty() ? QString("all") : to}, {"text", text}});
}

void ConferenceViewModel::setLobby(bool on) { client_->post("/api/v1/room", QJsonObject{{"lobby", on}}); }
void ConferenceViewModel::setLocked(bool on) { client_->post("/api/v1/room", QJsonObject{{"locked", on}}); }

void ConferenceViewModel::newPin(const QString& pin) {
    QJsonObject body;
    if (!pin.trimmed().isEmpty()) body.insert("pin", pin.trimmed());
    client_->post("/api/v1/pin", body, [this](bool ok, const QJsonObject& r) {
        if (ok) emit notify("PIN nuevo: " + r.value("pin").toString(), "ok");
    });
}

void ConferenceViewModel::removeShare(const QString& id) {
    client_->del("/api/v1/shares/" + QString::fromUtf8(QUrl::toPercentEncoding(id)));
}

void ConferenceViewModel::openWebConsole() {
    client_->post("/api/v1/login-code", {}, [this](bool ok, const QJsonObject& r) {
        if (ok && QDesktopServices::openUrl(QUrl(r.value("url").toString()))) return;
        emit notify("No se pudo abrir la consola web", "error");
    });
}

void ConferenceViewModel::takeShare(const QString& id) {
    QJsonObject share;
    for (const QJsonValue& v : shares_->items()) {
        if (v.toObject().value("id").toString() == id) share = v.toObject();
    }
    if (share.isEmpty()) {
        emit notify("Ese archivo ya no esta", "error");
        return;
    }
    // Copia propia: la carpeta de B se limpia al reiniciarlo, y la escena de A
    // no debe depender de eso.
    const QString srcPath = share.value("path").toString();
    const fs::path src(srcPath.toStdU16String());
    const fs::path mediaDir = rt_.dataRoot() / "Media";
    std::error_code ec;
    fs::create_directories(mediaDir, ec);
    const QString shown = share.value("name").toString();
    QString stem = QString::fromStdU16String(fs::path(shown.toStdU16String()).stem().u16string());
    stem.replace(QRegularExpression("[^\\w\\- ]"), "_");
    if (stem.isEmpty()) stem = "archivo";
    fs::path dst = mediaDir / (stem.toStdU16String() + src.extension().u16string());
    for (int n = 2; fs::exists(dst, ec); ++n) {
        dst = mediaDir / ((stem + QString(" (%1)").arg(n)).toStdU16String() + src.extension().u16string());
    }
    fs::copy_file(src, dst, ec);
    if (ec) {
        emit notify("No se pudo copiar el archivo: " + QString::fromStdString(ec.message()), "error");
        return;
    }

    const QString kind = share.value("kind").toString();
    const QString file = QString::fromStdU16String(dst.u16string());
    SourceSpec spec;
    QJsonObject settings;
    if (kind == "image") {
        spec.id = "image_source";
        settings.insert("file", file);
    } else {
        spec.id = "ffmpeg_source";
        settings.insert("local_file", file);
        settings.insert("is_local_file", true);
        settings.insert("looping", kind == "video");
        settings.insert("restart_on_activate", true);
    }
    spec.settingsJson = QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
    const std::string scene = rt_.engine().currentScene();
    const QString base = share.value("participantName").toString() + " — " + shown;
    QString name = base;
    for (int n = 2; rt_.engine().sourceHandle(name.toStdString()) != nullptr; ++n)
        name = base + QString(" (%1)").arg(n);
    spec.name = name.toStdString();
    if (!rt_.addSource(scene, spec)) {
        emit notify("No se pudo agregar la fuente: " + QString::fromStdString(rt_.lastError()), "error");
        return;
    }
    rt_.save();
    emit sourceAdded();
    emit notify("«" + shown + "» agregado a la escena " + QString::fromStdString(scene), "ok");
}

QVariantList ConferenceViewModel::chatCon(const QString& uid) const {
    QVariantList out;
    for (const QJsonValue& v : chat_->items()) {
        const QJsonObject o = v.toObject();
        const QString para = o.value("to").toString();
        const QString de = o.value("fromId").toString();
        const bool esDeLaSala = para == "all";
        // El hilo privado con alguien son las dos direcciones: lo que le
        // mandamos y lo que contesto. Si solo se mirara `to`, sus respuestas
        // (que vienen dirigidas al operador) caerian fuera del hilo.
        const bool esSuyo = !esDeLaSala && (para == uid || de == uid);
        if (uid.isEmpty() ? esDeLaSala : esSuyo) out.append(v.toObject().toVariantMap());
    }
    return out;
}

QVariantList ConferenceViewModel::chatHilos() const {
    QVariantList out;
    QStringList vistos;
    for (const QJsonValue& v : chat_->items()) {
        const QJsonObject o = v.toObject();
        const QString para = o.value("to").toString();
        const QString de = o.value("fromId").toString();
        // El otro extremo del hilo: si lo mandamos nosotros, el destinatario;
        // si nos lo mandaron, quien lo mando.
        const QString quien = para == "all" ? QString() : (de == "host" ? para : de);
        if (quien.isEmpty() || quien == "host" || vistos.contains(quien)) continue;
        vistos << quien;
        out.append(QVariantMap{{"uid", quien}, {"nombre", nameOf(quien)}});
    }
    return out;
}

} // namespace ba::app
