// ============================================================================
// src/app/ui/ConferenceViewModel.cpp — ver el header
// ============================================================================

#include "ui/ConferenceViewModel.hpp"

#include "diag/Acoples.hpp"
#include <QMetaObject>
#include <QPointer>
#include "ajustes/Ajustes.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "emision/VentanillaMotor.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/OradoresSala.hpp"
#include "comun/UsoPorVistas.hpp"
#include "ui/FotosPersonas.hpp"
#include "sala/PaginaPublico.hpp"
#include "sala/SalidasSala.hpp"
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

// --- ConferenceViewModel ------------------------------------------------------

ConferenceViewModel::ConferenceViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : QObject(parent), rt_(rt), ventanilla_(ventanilla), client_(std::make_unique<ConferenceClient>()),
      // Las claves de cada lista son las del resumen de la sala (sala/ResumenSala): las filas
      // y los cambios se arman en el hilo del lector con estas mismas.
      waiting_(std::make_unique<JsonListModel>(sala::clavesEspera())),
      inside_(std::make_unique<JsonListModel>(sala::clavesAdentro())),
      shares_(std::make_unique<JsonListModel>(sala::clavesArchivos())),
      chat_(std::make_unique<JsonListModel>(
          QStringList{"uid", "fromId", "fromName", "to", "text", "atMs", "mine", "voz"})),
      speakers_(std::make_unique<JsonListModel>(sala::clavesOradores())) {
    lugares_ = std::make_unique<JsonListModel>(sala::clavesLugares());
    // La foto de cada orador (las tarjetas): la pone el lector al armar la fila (seguro entre hilos).
    client_->setAvatar([](const QString& id) { return FotosPersonas::las().url("sala:" + id); });
    connect(client_.get(), &ConferenceClient::onlineChanged, this, [this]() {
        rehacerLugares(); // sin la sala, nadie en ningún lugar
        emit onlineChanged();
    });
    connect(client_.get(), &ConferenceClient::salaCambio, this, &ConferenceViewModel::aplicarPaquete);
    connect(client_.get(), &ConferenceClient::chatReceived, this,
            [this](const QJsonObject& m) { onChatMsg(m, true); });
    control_ = std::make_unique<sala::ControlSala>(rt_, ventanilla_, *client_);
    // Las fuentes, con qué aparece el operador y la atención automática vienen de la foto.
    ventanilla_.escuchar(this, [this](unsigned partes) {
        if ((partes & (FotoMotor::Sala | FotoMotor::Escenas)) != 0) emit operatorChanged();
        if ((partes & FotoMotor::Sala) != 0) emit autoIntakeChanged();
    });
    participantes_ = std::make_unique<sala::ParticipantesSala>(*client_);
    oradores_ = std::make_unique<sala::OradoresSala>(*client_);
    enlaces_ = std::make_unique<sala::EnlacesSala>(*client_);
    salidas_ = std::make_unique<sala::SalidasSala>(*client_);
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
    // Cómo anda ba-web (la página pública): una propiedad (la lee Enlaces) y un aviso cuando
    // se cae una y otra vez (el gateway dejó de insistir seguido) o es otra versión.
    connect(client_.get(), &ConferenceClient::webEvento, this, [this](const QJsonObject& e) {
        const QVariantMap nuevo = e.toVariantMap();
        if (nuevo == baWeb_) return;
        const bool antesCortado = baWeb_.value("cortado").toBool();
        const QString antesProblema = baWeb_.value("problema").toString();
        baWeb_ = nuevo;
        emit baWebChanged();
        const bool cortado = nuevo.value("cortado").toBool();
        if (cortado != antesCortado)
            emit notify(cortado ? tr("La página pública se cae una y otra vez: se reintenta cada pocos minutos. Mirá el "
                                     "registro de la conferencia (ba-web).")
                                : tr("La página pública volvió a andar."),
                        cortado ? "error" : "ok");
        const QString problema = nuevo.value("problema").toString();
        if (problema == "version" && antesProblema != problema)
            emit notify(tr("El servidor de la página pública es de otra versión (%1, se esperaba %2): el túnel va directo "
                           "a la conferencia. ¿Un paquete mezclado?")
                            .arg(nuevo.value("version").toString(), nuevo.value("esperada").toString()),
                        "error");
    });
    // El estado de red y las fotos de las tarjetas los avisa la sala cuando cambian
    // (eventos «red» y «avatar»); al volver la sala se piden una vez.
    connect(client_.get(), &ConferenceClient::redEvento, this, [this](const QJsonObject& o) {
        aplicarRed(nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false));
    });
    connect(client_.get(), &ConferenceClient::avatarEvento, this, [this](const QString& id) { traerAvatar(id); });
    // Volvió la sala (una sola conexión a la señal: antes eran dos).
    connect(client_.get(), &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (!on) return;
        avisoSinSalaMs_ = 0; // la próxima caída se avisa enseguida
        authSeguidos_ = 0;
        QPointer<ConferenceViewModel> yo(this);
        control_->verRed([yo](const std::string& err, const nlohmann::json& r) {
            if (yo && err.empty()) yo->aplicarRed(r);
        });
        pollAvatars();
    });
    // Lo que la sala no avisa (el proxy, los bloqueos por PIN): se pregunta solo mientras
    // alguna vista lo mira.
    proxyPoll_.setInterval(5000);
    bloqueosPoll_.setInterval(3000);
    connect(&proxyPoll_, &QTimer::timeout, this, &ConferenceViewModel::refrescarProxy);
    connect(&bloqueosPoll_, &QTimer::timeout, this, &ConferenceViewModel::pinBloqueos);
    usos_ = std::make_unique<UsoPorVistas>(this, [this](const QString& que) {
        QTimer& t = que == "proxy" ? proxyPoll_ : bloqueosPoll_;
        if (usos_->activo(que)) {
            que == "proxy" ? refrescarProxy() : pinBloqueos();
            t.start();
        } else {
            t.stop();
        }
    });
    // El reloj de los que hablan (cuatro veces por segundo, solo con alguien al aire).
    relojOradores_.setInterval(250);
    connect(&relojOradores_, &QTimer::timeout, this, &ConferenceViewModel::actualizarRelojes);
    // La atención automática la guarda el modelo (sigue prendida al arrancar
    // y la sala se entera cuando conecta). Si no hay asistente, se apaga sola
    // en cuanto lo intenta (ver atenderLobby).
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
int ConferenceViewModel::entrando() const { return client_->snapshot().value("entrando").toInt(); }
int ConferenceViewModel::entradaPorSegundo() const { return client_->snapshot().value("entradaPorSegundo").toInt(); }
int ConferenceViewModel::peopleConnected() const { return client_->resumen().conectados; }
QObject* ConferenceViewModel::waiting() const { return waiting_.get(); }
QObject* ConferenceViewModel::inside() const { return inside_.get(); }
QObject* ConferenceViewModel::shares() const { return shares_.get(); }
QObject* ConferenceViewModel::chat() const { return chat_.get(); }
QObject* ConferenceViewModel::speakers() const { return speakers_.get(); }
QVariantMap ConferenceViewModel::speakerCfg() const { return client_->resumen().speakerCfg; }
bool ConferenceViewModel::speakerMode() const { return client_->resumen().speakerMode; }

void ConferenceViewModel::actualizarRelojes() {
    const double ahora = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) + clockOffset_;
    const auto poner = [ahora](auto& m, int asignado, double desde) {
        const sala::OradoresSala::Reloj r = sala::OradoresSala::reloj(asignado, desde, ahora);
        m.insert("restanteMs", r.restanteMs);
        m.insert("estadoTiempo", QString::fromStdString(r.estado));
        m.insert("textoTiempo", QString::fromStdString(r.texto));
        return r;
    };
    // El peor manda (el color del reloj flotante): excedido > aviso > ok.
    QString peor;
    const auto rango = [](const QString& e) { return e == "excedido" ? 3 : e == "aviso" ? 2 : e == "ok" ? 1 : 0; };
    QVariantList onAir;
    for (const QVariant& v : onAir_) {
        QVariantMap m = v.toMap();
        const auto r = poner(m, m.value("allottedSec").toInt(), m.value("onAirSinceMs").toDouble());
        if (rango(QString::fromStdString(r.estado)) > rango(peor)) peor = QString::fromStdString(r.estado);
        onAir << m;
    }
    // En el lugar: solo las filas de los que hablan (cada una avisa sus roles del reloj).
    const QJsonArray& rows = speakers_->items();
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject p = rows.at(i).toObject();
        if (p.value("speaker").toString() != "onair") continue;
        poner(p, p.value("allottedSec").toInt(), p.value("onAirSinceMs").toDouble());
        speakers_->ponerFila(i, p);
    }
    const bool cambio = onAir != onAir_ || peor != peorEstado_;
    onAir_ = onAir;
    peorEstado_ = peor;
    if (cambio) emit onAirCambio();
}

void ConferenceViewModel::aplicarRed(const nlohmann::json& r) {
    const QVariantMap m = aMapa(r);
    if (m == network_) return;
    network_ = m;
    emit networkChanged();
}

void ConferenceViewModel::aplicarAvatar(const QString& id, const QByteArray& jpeg) {
    // Una sola foto por persona (ui/FotosPersonas): su URL cambia solo si la foto cambió.
    const QString clave = "sala:" + id;
    const QString antes = FotosPersonas::las().url(clave);
    FotosPersonas::las().poner(clave, jpeg);
    const QString url = FotosPersonas::las().url(clave);
    if (url == antes) return;
    // Su tarjeta en la fila de oradores (las que siguen ya la traen del lector).
    const QJsonArray& rows = speakers_->items();
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject p = rows.at(i).toObject();
        if (p.value("uid").toString() != id) continue;
        p.insert("avatar", url);
        speakers_->ponerFila(i, p);
        break;
    }
}

void ConferenceViewModel::traerAvatar(const QString& id) {
    QPointer<ConferenceViewModel> yo(this);
    participantes_->foto(id.toStdString(), [yo, id](bool ok, const QByteArray& jpeg) {
        if (yo && ok && !jpeg.isEmpty()) yo->aplicarAvatar(id, jpeg);
    });
}

void ConferenceViewModel::pollAvatars() {
    if (!client_->online()) return;
    QSet<QString> vivas;
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const QString id = p.value("id").toString();
        vivas.insert("sala:" + id);
        if (p.value("hasAvatar").toBool()) traerAvatar(id);
    }
    // Las de quien ya no está.
    FotosPersonas::las().dejarSolo("sala:", vivas);
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
void ConferenceViewModel::speakerRequeue(const QString& id) { oradores_->aLaFila(ss(id)); }
QVariantMap ConferenceViewModel::usage() const { return client_->resumen().uso; }

QString ConferenceViewModel::photoOf(const QString& idOrName) const {
    // Como data: URL (va dentro de un mensaje al operador, no a una vista).
    const auto dataUrl = [](const QString& id) {
        const QByteArray j = FotosPersonas::las().jpeg("sala:" + id);
        return j.isEmpty() ? QString() : "data:image/jpeg;base64," + QString::fromLatin1(j.toBase64());
    };
    if (const QString d = dataUrl(idOrName); !d.isEmpty()) return d;
    for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("name").toString().compare(idOrName, Qt::CaseInsensitive) == 0) return dataUrl(p.value("id").toString());
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
    if (on) atenderLobby();
}

void ConferenceViewModel::setPresenter(const QString& id, bool on) { oradores_->presentador(ss(id), on); }
void ConferenceViewModel::setProfile(const QString& id, const QString& name, const QString& info,
                                     const QString& contact) {
    participantes_->perfil(ss(id), ss(name), ss(info), ss(contact));
}

void ConferenceViewModel::guardarFicha(const QString& id, const QString& name, const QString& info,
                                       const QString& contact, const QString& tiempo) {
    setProfile(id, name, info, contact);
    // Sin tiempo (desde el lobby o la sala): solo la ficha. Uno que no se entiende, se avisa.
    if (tiempo.trimmed().isEmpty()) return;
    // Lo escrito, sobre lo que ya tiene (el suyo o el general): la misma lectura que el MCP.
    if (const std::string e = oradores_->tiempo(ss(id), nlohmann::json(ss(tiempo))); !e.empty()) emit notify(frase(e), "error");
}

QVariantMap ConferenceViewModel::reloj(int asignadoS, double desdeMs, double ahoraMs) const {
    BA_ACOPLE("invocable", "ConferenceViewModel::reloj");
    const sala::OradoresSala::Reloj r = sala::OradoresSala::reloj(asignadoS, desdeMs, ahoraMs);
    return {{"restanteMs", r.restanteMs}, {"estado", QString::fromStdString(r.estado)}, {"texto", QString::fromStdString(r.texto)}};
}

void ConferenceViewModel::setTiempoGeneral(const QString& texto) {
    if (const std::string e = oradores_->tiempoGeneral(nlohmann::json(ss(texto))); !e.empty()) return avisarError(frase(e));
}

void ConferenceViewModel::requestBoardLink(const QString& target) {
    const bool remoto = target == "remote";
    QPointer<ConferenceViewModel> yo(this);
    enlaces_->tablero(remoto, [yo, remoto](const sala::Enlace& e) {
        if (yo)
            emit yo->qrReady(remoto ? "board-remote" : "board-local", qs(e.url), filasDe(e), qs(e.mensaje));
    });
}

void ConferenceViewModel::aplicarPaquete(unsigned partes) {
    // Todo lo que recorre la foto se hizo en el hilo del lector (sala/ResumenSala): acá se
    // aplican los pasos (proporcional a lo que cambió) y se avisa por grupo.
    const std::shared_ptr<const sala::PaqueteSala> p = client_->paquete();
    if (!p || !p->r) return;
    const sala::ResumenSala& r = *p->r;
    using namespace sala;
    if ((partes & Espera) != 0) {
        waiting_->aplicar(p->espera);
        // Los que llegaron al lobby (los de la prueba de sala no se anuncian).
        if (!p->primera)
            for (const QJsonObject& e : p->llegaronAlLobby)
                if (!e.value("prueba").toBool()) emit notify(tr("%1 espera en el lobby").arg(e.value("name").toString()), "info");
        atenderLobby();
    }
    if ((partes & Adentro) != 0) inside_->aplicar(p->adentro);
    if ((partes & Archivos) != 0) shares_->aplicar(p->archivos);
    if (lugaresSucios_ || r.nLugares != nLugares_) rehacerLugares();
    else if ((partes & Lugares) != 0) lugares_->aplicar(p->lugares);

    // Monitoreo: solo se escucha (por auriculares) a quien se le esta hablando. Al motor
    // solo cuando cambia (cada foto de la sala no es una orden).
    if ((partes & Monitor) != 0 && r.monitor != monitorLobby_) {
        monitorLobby_ = r.monitor;
        ventanilla_.pedir("lobby_monitor", [&rt = rt_, monitor = r.monitor]() {
            for (int k = 1; k <= 8 && k < static_cast<int>(monitor.size()); ++k) rt.setLobbyMonitor(k, monitor[static_cast<size_t>(k)]);
        });
    }

    // Chat: solo los mensajes que faltan (el lector ya los separó); si la secuencia volvió
    // atrás, B se reinició y el historial se reemplaza.
    if ((partes & Chat) != 0) {
        if (p->chatReinicio) {
            chat_->setItems({});
            emit chatCambio();
            lastSeq_ = 0;
        }
        for (const QJsonValue& v : p->chatNuevos) onChatMsg(v.toObject(), false);
    }
    if ((partes & Uso) != 0) clockOffset_ = r.clockOffset;

    if ((partes & Oradores) != 0) {
        speakers_->aplicar(p->oradores);
        onAir_ = r.onAir;
        // Con alguien al aire corre el reloj; sin nadie, se apaga.
        if (onAir_.isEmpty()) relojOradores_.stop();
        else if (!relojOradores_.isActive()) relojOradores_.start();
        actualizarRelojes(); // las filas que se reemplazaron vuelven a tener su reloj
        emit onAirCambio();
    }

    // Cada grupo avisa solo si cambió lo suyo (lo comparó el lector).
    if ((partes & Sala) != 0) emit salaChanged();
    if ((partes & Participantes) != 0) emit participantesChanged();
    if ((partes & Uso) != 0) emit usoChanged();
    if ((partes & OradoresCfg) != 0) emit oradoresCfgChanged();
    if ((partes & Gente) != 0) emit roomChanged();
}

void ConferenceViewModel::atenderLobby() {
    // O(los que esperan): solo cuando cambió la espera o se prendió la atención automática.
    QSet<QString> esperan;
    for (const QJsonValue& v : waiting_->items()) {
        const QJsonObject p = v.toObject();
        const QString pid = p.value("id").toString();
        esperan.insert(pid);
        // Los de la prueba de sala no los atiende el asistente.
        if (p.value("prueba").toBool() || !autoIntake() || !intakeSink_ || intakeDone_.contains(pid)) continue;
        // Atencion automatica: se le encarga al asistente una sola vez por persona (mientras
        // siga esperando).
        intakeDone_.insert(pid);
        // UN MOMENTO ANTES DE ABORDARLO, a propósito. El reconocimiento de rostros necesita
        // una vuelta para tener algo que decir de alguien que se acaba de conectar.
        // Atendiéndolo en el instante en que aparece, el asistente mira la libreta, no
        // encuentra nada todavía y le pide los datos de cero a alguien que el sistema conoce
        // perfectamente —y que un segundo después va a estar identificado—. Dos segundos y
        // medio no los nota nadie del otro lado.
        const QString nombre = p.value("name").toString();
        QTimer::singleShot(kEsperarAlReconocimientoMs(), this, [this, pid, nombre]() {
            if (intakeSink_ == nullptr) return;
            // Si mientras tanto entró sola (lista del evento), la atiende el otro camino:
            // `marcarAtendido` lo saca de acá.
            if (!intakeDone_.contains(pid)) return;
            const QString err = intakeSink_(pid, nombre, false);
            if (!err.isEmpty()) {
                // Sin asistente no tiene sentido seguir: se apaga como si lo hubiera apagado
                // el operador (se guarda y B se entera).
                setAutoIntake(false);
                emit notify(err, "error");
            }
        });
    }
    // Los que ya no esperan se olvidan: si vuelven, se los atiende de nuevo.
    for (auto it = intakeDone_.begin(); it != intakeDone_.end();) it = esperan.contains(*it) ? std::next(it) : intakeDone_.erase(it);
}

QJsonObject ConferenceViewModel::mensajeDelChat(const QJsonObject& msg) {
    const QString seq = QString::number(msg.value("seq").toInteger());
    QJsonObject m = msg;
    m.insert("id", seq);
    m.insert("uid", seq);
    m.insert("mine", m.value("fromId").toString() == "host");
    return m;
}

void ConferenceViewModel::onChatMsg(const QJsonObject& msg, bool live) {
    const qint64 seq = msg.value("seq").toInteger();
    if (seq <= lastSeq_) return; // ya estaba (llega por snapshot y por evento)
    lastSeq_ = seq;
    const QJsonObject m = mensajeDelChat(msg);
    const bool mine = m.value("mine").toBool();
    chat_->append(m);
    emit chatCambio();
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
    emit chatCambio();
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
    emit chatCambio();
}

QString ConferenceViewModel::nameOf(const QString& id) const {
    BA_ACOPLE("invocable", "ConferenceViewModel::nameOf");
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

void ConferenceViewModel::usar(QObject* vista, bool si, const QString& que) {
    if (que == "proxy" || que == "bloqueos") usos_->usar(vista, si, que);
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
    if (!client_->paquete()) return {QVariantMap{{"label", "A todos"}, {"value", "all"}}};
    return client_->resumen().chatTargets;
}

QVariantMap ConferenceViewModel::participanteEnSlot(int slot) const {
    const QJsonObject snap = const_cast<ConferenceViewModel*>(this)->client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("slot").toInt() == slot) return p.toVariantMap();
    }
    return {};
}

QObject* ConferenceViewModel::lugares() const { return lugares_.get(); }

void ConferenceViewModel::setLugares(int n) {
    if (n == nLugares_) return;
    nLugares_ = n;
    client_->setLugares(n); // el lector rearma la última foto con esta cantidad
    rehacerLugares();
}

void ConferenceViewModel::rehacerLugares() {
    // Con la sala y el resumen ya armado con esta cantidad: esas filas (y desde ahí, los
    // cambios que manda el lector). Si no, a mano desde la foto, hasta que llegue.
    if (online() && client_->paquete() && client_->resumen().nLugares == nLugares_) {
        lugares_->setItems(client_->resumen().lugares);
        lugaresSucios_ = false;
        return;
    }
    lugaresSucios_ = true;
    QList<QJsonObject> filas;
    for (int k = 1; k <= nLugares_; ++k)
        filas << QJsonObject{{"id", k},         {"slot", k},      {"nombre", ""},          {"pid", ""},
                             {"info", ""},      {"sinRetorno", false}, {"semaforo", ""}, {"red", QJsonObject{}}};
    if (online()) {
        for (const QJsonValue& v : client_->snapshot().value("participants").toArray()) {
            const QJsonObject p = v.toObject();
            const int slot = p.value("slot").toInt();
            if (p.value("state").toString() != "admitted" || slot < 1 || slot > nLugares_) continue;
            QJsonObject& f = filas[slot - 1];
            const QJsonObject red = p.value("red").toObject();
            f["nombre"] = p.value("name").toString();
            f["pid"] = p.value("id").toString();
            f["info"] = p.value("info").toString();
            f["sinRetorno"] = p.value("retorno").toString() == "apagado";
            f["semaforo"] = red.value("semaforo").toString();
            f["red"] = red;
        }
    }
    QJsonArray items;
    for (const QJsonObject& f : filas) items.append(f);
    lugares_->setItems(items); // avisa solo las filas y los roles que cambiaron
}

QStringList ConferenceViewModel::slotNames() const {
    if (!online()) return {};
    return client_->resumen().slotNames;
}


QString ConferenceViewModel::operatorVideo() const { return QString::fromStdString(ventanilla_.foto().sala.operadorVideo); }
QString ConferenceViewModel::operatorAudio() const { return QString::fromStdString(ventanilla_.foto().sala.operadorAudio); }

QStringList ConferenceViewModel::videoSourceNames() const {
    QStringList out;
    for (const FotoMotor::Escena& e : ventanilla_.foto().escenas)
        for (const FotoMotor::Fuente& f : e.fuentes) {
            const QString q = QString::fromStdString(f.nombre);
            if (!out.contains(q)) out << q;
        }
    return out;
}

QStringList ConferenceViewModel::audioSourceNames() const {
    QStringList out;
    for (const std::string& s : ventanilla_.foto().sala.audio) {
        // Las voces de los invitados no son "el operador".
        if (s.rfind("Invitado", 0) == 0) continue;
        out << QString::fromStdString(s);
    }
    return out;
}

void ConferenceViewModel::talk(const QString& id, bool on) { participantes_->hablar(ss(id), on); }

void ConferenceViewModel::setOperatorFeed(const QString& video, const QString& audio) {
    // Lo hace el dueño; la foto trae lo elegido (operatorChanged).
    ventanilla_.pedir(
        this, "operador_feed",
        [&rt = rt_, v = video.toStdString(), a = audio.toStdString()]() {
            const std::string e = rt.setOperatorFeed(v, a) ? std::string() : rt.lastError();
            rt.fotoSucia(FotoMotor::Sala);
            rt.save();
            return e;
        },
        [this](const std::string& e) {
            if (!e.empty()) emit notify(QString::fromStdString(e), "error");
        });
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
    QPointer<ConferenceViewModel> yo(this);
    control_->tomarArchivo(ss(id), [yo](const sala::ControlSala::Tomado& t) {
        if (!yo) return;
        if (!t.error.empty()) return emit yo->notify(qs(t.error), "error");
        emit yo->notify(tr("«%1» agregado a la escena %2").arg(qs(t.nombre), qs(t.escena)), "ok");
    });
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
