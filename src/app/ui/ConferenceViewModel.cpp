// ============================================================================
// src/app/ui/ConferenceViewModel.cpp — ver el header
// ============================================================================

#include "ui/ConferenceViewModel.hpp"
#include <QMetaObject>
#include <QPointer>
#include "ajustes/Ajustes.hpp"

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

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/OradoresSala.hpp"
#include "sala/PaginaPublico.hpp"
#include "sala/ParticipantesSala.hpp"
#include "sala/ReglasSala.hpp"
#include "sala/Teleprompter.hpp"
#include "ui/ConferenceClient.hpp"

namespace ba::app {

/// Ver el uso: cuánto se espera antes de que el asistente aborde a alguien
/// que acaba de llegar al lobby, para que el reconocimiento llegue a contestar.
inline int kEsperarAlReconocimientoMs() { return ba::ajustes::entero("asistente.esperaLobbyMs", 2500); }

namespace fs = std::filesystem;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }
QVariantMap aMapa(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}
/// La primera letra en mayúscula: los textos del modelo vienen en minúscula
/// para que el MCP les ponga algo adelante.
QString frase(const std::string& s) {
    QString q = qs(s);
    if (!q.isEmpty()) q[0] = q[0].toUpper();
    return q;
}
QStringList filasDe(const sala::Enlace& e) {
    QStringList rows;
    for (const std::string& f : e.filas) rows << qs(f);
    return rows;
}
} // namespace

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
                                                          "presenter", "speaker", "info", "contact", "talk"})),
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
    control_ = std::make_unique<sala::ControlSala>(rt_, *client_);
    participantes_ = std::make_unique<sala::ParticipantesSala>(*client_);
    oradores_ = std::make_unique<sala::OradoresSala>(*client_);
    enlaces_ = std::make_unique<sala::EnlacesSala>(*client_);
    paginaPublico_ = std::make_unique<sala::PaginaPublico>(*client_);
    teleprompter_ = std::make_unique<sala::Teleprompter>(*client_);
    // Un archivo compartido que entra a la escena (por la GUI o el MCP): las
    // listas de fuentes se recargan.
    control_->setAlTomarArchivo([this]() { emit sourceAdded(); });
    // Todo pedido que falla pasa por acá: el texto es el del modelo (el mismo
    // que lee el MCP).
    connect(client_.get(), &ConferenceClient::failed, this, [this](const QString& e) {
        // Sin sala (se está reiniciando o se cayó) fallan todos los pedidos, también los
        // periódicos: un aviso por caída, no una ráfaga (trababa la interfaz con avisos).
        if (e == "offline") {
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            if (ahora - avisoSinSalaMs_ < 30000) return;
            avisoSinSalaMs_ = ahora;
        } else if (e == "auth" || e == "401") {
            // La sala volvió con otro token: el cliente lo relee solo. Se avisa si sigue.
            if (++authSeguidos_ < 2) return;
        } else {
            authSeguidos_ = 0;
        }
        const std::string t = sala::explicarError(ss(e));
        emit notify(t == ss(e) ? tr("La conferencia respondio: %1").arg(e) : frase(t), "error");
    });
    connect(client_.get(), &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (!on) return;
        avisoSinSalaMs_ = 0; // volvió: la próxima caída se avisa enseguida
        authSeguidos_ = 0;
    });
    // Estado de red: cada 5 s (cambia poco; no amerita un evento propio).
    const auto pollNetwork = [this]() {
        if (!client_->online()) return;
        QPointer<ConferenceViewModel> yo(this);
        control_->verRed([yo](const std::string& err, const nlohmann::json& r) {
            if (!yo || !err.empty()) return;
            const QVariantMap m = aMapa(r);
            if (m != yo->network_) {
                yo->network_ = m;
                emit yo->networkChanged();
            }
        });
    };
    // Fotos de las tarjetas de oradores (las renueva el invitado cada 5 s).
    connect(&avatarPoll_, &QTimer::timeout, this, &ConferenceViewModel::pollAvatars);
    avatarPoll_.start(std::max(1000, ba::ajustes::entero("room.sondeoMs", 5000)));
    connect(&networkPoll_, &QTimer::timeout, this, pollNetwork);
    connect(client_.get(), &ConferenceClient::onlineChanged, this, pollNetwork);
    networkPoll_.start(std::max(1000, ba::ajustes::entero("room.sondeoMs", 5000)));
    // La atención automática la guarda el modelo (sigue prendida al arrancar
    // y la sala se entera cuando conecta). Si no hay asistente, se apaga sola
    // en cuanto lo intenta (ver onSnapshot).
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
        participantes_->foto(id.toStdString(), [this, id](bool ok, const QByteArray& jpeg) {
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
    const nlohmann::json c =
        nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(cfg)).toJson().toStdString(), nullptr, false);
    if (const std::string e = oradores_->configurar(c); !e.empty()) emit notify(frase(e), "error");
}
void ConferenceViewModel::speakerOnAir(const QString& id, bool on) { oradores_->alAire(ss(id), on); }
void ConferenceViewModel::speakerNext() {
    QPointer<ConferenceViewModel> yo(this);
    oradores_->siguiente([yo](const std::string& err, const nlohmann::json&) {
        if (yo && !err.empty()) emit yo->notify(tr("No hay nadie mas en la fila"), "info");
    });
}
void ConferenceViewModel::speakerMove(const QString& id, int index) { oradores_->mover(ss(id), index); }
void ConferenceViewModel::speakerTime(const QString& id, int sec) {
    if (const std::string e = oradores_->tiempo(ss(id), sec); !e.empty()) emit notify(frase(e), "error");
}
void ConferenceViewModel::speakerRequeue(const QString& id) { oradores_->aLaFila(ss(id)); }
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

bool ConferenceViewModel::autoIntake() const { return control_->atencionAutomatica(); }

void ConferenceViewModel::setAutoIntake(bool on) {
    if (control_->atencionAutomatica() == on) return;
    // Al prenderlo, los que ya estaban esperando tambien se atienden.
    if (!on) intakeDone_.clear();
    control_->setAtencionAutomatica(on);
    emit autoIntakeChanged();
    if (on) onSnapshot();
}

void ConferenceViewModel::setPresenter(const QString& id, bool on) { oradores_->presentador(ss(id), on); }
void ConferenceViewModel::setProfile(const QString& id, const QString& name, const QString& info,
                                     const QString& contact) {
    participantes_->perfil(ss(id), ss(name), ss(info), ss(contact));
}

void ConferenceViewModel::guardarFicha(const QString& id, const QString& name, const QString& info,
                                       const QString& contact, const QString& tiempo) {
    setProfile(id, name, info, contact);
    // Sin tiempo (desde el lobby o la sala) o uno que no se entiende: solo la ficha.
    if (const int s = parseDuration(tiempo); s > 0) speakerTime(id, s);
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
    const bool remoto = target == "remote";
    QPointer<ConferenceViewModel> yo(this);
    enlaces_->tablero(remoto, [yo, remoto](const sala::Enlace& e) {
        if (yo)
            emit yo->qrReady(remoto ? "board-remote" : "board-local", qs(e.url), filasDe(e), qs(e.mensaje));
    });
}

void ConferenceViewModel::onSnapshot() {
    const QJsonObject s = client_->snapshot();
    QJsonArray waiting;
    QJsonArray inside;
    QStringList nowWaiting;
    QStringList slotNames;
    QVariantList sinRetorno;
    for (int i = 0; i < s.value("maxGuests").toInt(); ++i) {
        slotNames << QString();
        sinRetorno << false;
    }
    for (const QJsonValue& v : s.value("participants").toArray()) {
        QJsonObject p = v.toObject();
        p.insert("uid", p.value("id")); // "id" no puede ser un rol en QML
        const int slot = p.value("slot").toInt();
        if (p.value("state").toString() == "admitted" && slot >= 1 && slot <= slotNames.size()) {
            slotNames[slot - 1] = p.value("name").toString();
            sinRetorno[slot - 1] = p.value("retorno").toString() == "apagado";
        }
        if (p.value("state").toString() == "waiting") {
            waiting.append(p);
            nowWaiting << p.value("id").toString();
            if (!knownWaiting_.contains(p.value("id").toString()) && !firstSnapshot_) {
                emit notify(tr("%1 espera en el lobby").arg(p.value("name").toString()), "info");
            }
            // Atencion automatica: se le encarga al asistente una sola vez por
            // persona (mientras siga esperando).
            const QString pid = p.value("id").toString();
            if (autoIntake() && intakeSink_ && !intakeDone_.contains(pid)) {
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
                QTimer::singleShot(kEsperarAlReconocimientoMs(), this, [this, pid, nombre]() {
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
    slotsSinRetorno_ = sinRetorno;
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

void ConferenceViewModel::agregarDichoPorVoz(const QString& id, const QString& texto, const QString& nombre) {
    const QString t = texto.trimmed();
    if (t.isEmpty()) return;
    if (!nombre.isEmpty()) nombresDeAfuera_.insert(id, nombre);
    // Sin `seq`: no viene de la sala, así que no corre la cuenta de lo ya
    // recibido (onChatMsg sigue comparando solo contra lo de la sala). La
    // `uid` propia es la que usan chat_wait y la vista para no repetirlo.
    static int n = 0;
    QJsonObject m;
    m.insert("uid", "voz-" + QString::number(++n));
    m.insert("id", m.value("uid"));
    m.insert("fromId", id);
    m.insert("fromName", nameOf(id));
    m.insert("to", "host");
    m.insert("text", t);
    m.insert("voz", true);
    m.insert("mine", false);
    m.insert("atMs", static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    chat_->append(m);
    ++chatRev_;
    emit chatRevChanged();
}

void ConferenceViewModel::agregarDichoAlAtendido(const QString& id, const QString& nombre, const QString& texto) {
    const QString t = texto.trimmed();
    if (t.isEmpty()) return;
    if (!nombre.isEmpty()) nombresDeAfuera_.insert(id, nombre);
    static int n = 0;
    QJsonObject m;
    m.insert("uid", "voz-dicho-" + QString::number(++n));
    m.insert("id", m.value("uid"));
    m.insert("fromId", "host");
    m.insert("fromName", "Operador");
    m.insert("to", id);
    m.insert("text", t);
    m.insert("voz", true);
    m.insert("mine", true);
    m.insert("atMs", static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    chat_->append(m);
    ++chatRev_;
    emit chatRevChanged();
}

QString ConferenceViewModel::nameOf(const QString& id) const {
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        if (v.toObject().value("id").toString() == id) return v.toObject().value("name").toString();
    }
    return nombresDeAfuera_.value(id, id);
}

void ConferenceViewModel::pruebaTecnica(const QString& id) {
    QPointer<ConferenceViewModel> yo(this);
    participantes_->prueba(ss(id), [yo, id](const std::string& err, const nlohmann::json& r) {
        if (!yo) return;
        QVariantMap m = aMapa(r);
        m["ok"] = err.empty();
        emit yo->pruebaLista(id, m);
    });
}

void ConferenceViewModel::refrescarProxy() {
    QPointer<ConferenceViewModel> yo(this);
    control_->verProxy([yo](const std::string& err, const nlohmann::json& r) {
        if (!yo) return;
        QVariantMap m = aMapa(r);
        if (!err.empty()) m["activo"] = false; // sin la sala, solo la configuración guardada
        yo->proxy_ = m;
        emit yo->proxyChanged();
    });
}

void ConferenceViewModel::renovarProxy(const QString& host) { control_->renovarProxy(ss(host)); }

QString ConferenceViewModel::configurarProxy(const QVariantMap& cambios) {
    const nlohmann::json c = nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(), nullptr, false);
    QPointer<ConferenceViewModel> yo(this);
    return qs(control_->configurarProxy(c, [yo](const std::string& e) {
        if (yo && !e.empty())
            emit yo->notify(tr("No se pudo reiniciar la conferencia: %1. Si sigue, cerrá y volvé a abrir el programa.").arg(qs(e)),
                            "error");
    }));
}

QString ConferenceViewModel::reiniciarSala() {
    // Aparte: esperar que el gateway se cierre trababa la interfaz. Si falla, avisa.
    QPointer<ConferenceViewModel> yo(this);
    rt_.reiniciarSalaEnElFondo([yo](const std::string& e) {
        if (e.empty() || !yo) return;
        QMetaObject::invokeMethod(yo.data(), [yo, e]() {
            if (yo)
                emit yo->notify(tr("No se pudo reiniciar la conferencia: %1. Si sigue, cerrá y volvé a abrir el programa.")
                                    .arg(QString::fromStdString(e)),
                                "error");
        }, Qt::QueuedConnection);
    });
    return {};
}

void ConferenceViewModel::admit(const QString& id) { participantes_->admitir(ss(id)); }
void ConferenceViewModel::reject(const QString& id) { participantes_->rechazar(ss(id)); }
void ConferenceViewModel::kick(const QString& id) { participantes_->sacar(ss(id)); }
void ConferenceViewModel::request(const QString& id, const QString& what) {
    if (const std::string e = participantes_->pedirle(ss(id), ss(what)); !e.empty()) emit notify(frase(e), "error");
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

QVariantList ConferenceViewModel::slotsSinRetorno() const {
    if (!online()) return {};
    return slotsSinRetorno_;
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

void ConferenceViewModel::talk(const QString& id, bool on) { participantes_->hablar(ss(id), on); }

void ConferenceViewModel::setOperatorFeed(const QString& video, const QString& audio) {
    if (!rt_.setOperatorFeed(video.toStdString(), audio.toStdString())) {
        emit notify(QString::fromStdString(rt_.lastError()), "error");
    }
    rt_.save();
    emit operatorChanged();
}

void ConferenceViewModel::configureDirectAccess(const QString& endpoint, const QString& publicAddress,
                                                const QString& iface) {
    QPointer<ConferenceViewModel> yo(this);
    const std::string e = control_->accesoDirecto(
        ss(endpoint), ss(publicAddress), ss(iface),
        [yo](const std::string& err, const nlohmann::json& r) {
            if (!yo) return;
            if (!err.empty()) return emit yo->serverConfigReady({}, qs(err));
            emit yo->serverConfigReady(qs(r.value("config", std::string())), {});
            emit yo->notify(tr("Tunel configurado: reiniciando la conferencia"), "ok");
        },
        [yo](const std::string& err) {
            if (yo && !err.empty())
                emit yo->notify(tr("No se pudo reiniciar la conferencia: %1. Si sigue, cerrá y volvé a abrir el programa.")
                                    .arg(qs(err)),
                                "error");
        });
    if (!e.empty()) emit serverConfigReady({}, qs(e));
}

void ConferenceViewModel::requestQr(const QString& target, bool withPin) {
    const bool remoto = target == "remote";
    QPointer<ConferenceViewModel> yo(this);
    enlaces_->ingreso(remoto, withPin, [yo, remoto](const sala::Enlace& e) {
        if (yo) emit yo->qrReady(remoto ? "remote" : "local", qs(e.url), filasDe(e), qs(e.mensaje));
    });
}

void ConferenceViewModel::admitAll() {
    QPointer<ConferenceViewModel> yo(this);
    participantes_->admitirATodos([yo](int pedidos, int admitidos, const std::string& err) {
        // Los que no pudieron pasar ya los avisó `failed` (sala llena, etc.).
        if (yo && pedidos > 0 && err.empty())
            emit yo->notify(tr("Pasaron los %1 que esperaban").arg(admitidos), "ok");
    });
}

void ConferenceViewModel::sendChat(const QString& to, const QString& text) {
    control_->chatear(ss(to), ss(text)); // vacío no se manda (no hay nada que avisar)
}

void ConferenceViewModel::setLobby(bool on) { control_->entrada(on, std::nullopt); }
void ConferenceViewModel::setLocked(bool on) { control_->entrada(std::nullopt, on); }

void ConferenceViewModel::newPin(const QString& pin) {
    QPointer<ConferenceViewModel> yo(this);
    control_->cambiarPin(ss(pin), [yo](const std::string& err, const nlohmann::json& r) {
        if (yo && err.empty()) emit yo->notify(tr("PIN nuevo: %1").arg(qs(r.value("pin", std::string()))), "ok");
    });
}

void ConferenceViewModel::pinBloqueos() {
    QPointer<ConferenceViewModel> yo(this);
    control_->verBloqueosPin([yo](const std::string& err, const nlohmann::json& r) {
        if (!yo || !err.empty()) return;
        yo->bloqueosPin_ = aMapa(r.value("bloqueos", nlohmann::json::object()));
        emit yo->bloqueosPinChanged();
    });
}

void ConferenceViewModel::desbloquearPin(const QString& ip) {
    QPointer<ConferenceViewModel> yo(this);
    const QString cual = ip.trimmed();
    control_->desbloquearPin(ss(cual), [yo, cual](const std::string& err, const nlohmann::json& r) {
        if (!yo) return;
        if (!err.empty()) return emit yo->notify(tr("No se pudo desbloquear (¿la conferencia está conectada?)"), "error");
        yo->bloqueosPin_ = aMapa(r.value("bloqueos", nlohmann::json::object()));
        emit yo->bloqueosPinChanged();
        emit yo->notify(cual.isEmpty() ? tr("Desbloqueados todos (y el bloqueo general)") : tr("Desbloqueada %1").arg(cual),
                        "ok");
    });
}

void ConferenceViewModel::removeShare(const QString& id) { control_->borrarArchivo(ss(id)); }

void ConferenceViewModel::takeShare(const QString& id) {
    // Las reglas (copia propia en Media/, el nombre, el tipo de fuente) son del
    // modelo, el mismo que usa el MCP (share_take).
    const sala::ControlSala::Tomado t = control_->tomarArchivo(ss(id));
    if (!t.error.empty()) return emit notify(qs(t.error), "error");
    emit notify(tr("«%1» agregado a la escena %2").arg(qs(t.nombre), qs(t.escena)), "ok");
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
