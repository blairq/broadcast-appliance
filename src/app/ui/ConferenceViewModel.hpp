// ============================================================================
// src/app/ui/ConferenceViewModel.hpp — La sala de videoconferencia (ViewModel)
//
// Adapta el estado que publica la API de control de B (via ConferenceClient)
// a lo que necesita la vista QML: propiedades simples, listas (lobby, adentro,
// archivos, chat) y comandos. No tiene reglas de sala propias: admitir,
// expulsar, etc. son pedidos a la API, y el resultado vuelve por el flujo de
// eventos como un snapshot nuevo (una sola fuente de verdad).
//
// Lo unico que hace del lado de A es "tomar" un archivo compartido: lo copia
// a <datos>/Media (B limpia su carpeta al reiniciar) y lo agrega como fuente
// a la escena actual.
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace ba::app {

class AppRuntime;
class ConferenceClient;

/// Lista generica sobre un arreglo JSON de objetos con campo "id". Cada clave
/// de `keys` es un rol de QML con el mismo nombre ("id" es palabra reservada
/// en QML: las filas lo repiten como "uid"). Si los ids no cambiaron se
/// actualiza en el lugar (sin rearmar los delegados: la vista no parpadea).
class JsonListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
   public:
    JsonListModel(QStringList keys, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override { return items_.size(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return items_.size(); }
    /// Una fila por posicion, con sus claves. Vacio si no existe. Es lo que
    /// deja manejar estas listas CON EL TECLADO sin que la vista tenga que
    /// pedir datos por numero de rol.
    Q_INVOKABLE QVariantMap fila(int i) const;
    void setItems(const QJsonArray& items);
    void append(const QJsonObject& item);
    const QJsonArray& items() const { return items_; }

   signals:
    void countChanged();

   private:
    QStringList keys_;
    QJsonArray items_;
};

class ConferenceViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(QString room READ room NOTIFY roomChanged)
    Q_PROPERTY(QString pin READ pin NOTIFY roomChanged)
    Q_PROPERTY(QString guestUrl READ guestUrl NOTIFY roomChanged)
    Q_PROPERTY(bool lobby READ lobby NOTIFY roomChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY roomChanged)
    Q_PROPERTY(int maxGuests READ maxGuests NOTIFY roomChanged)
    Q_PROPERTY(int slotsFree READ slotsFree NOTIFY roomChanged)
    /// Todos los conectados a la sala: lobby, programa y fila de oradores. Es lo
    /// que se pierde si la app se cierra (guardias de cierre y de actualizacion).
    Q_PROPERTY(int peopleConnected READ peopleConnected NOTIFY roomChanged)
    /// Nombre del invitado de cada slot (indice slot-1; vacio = libre).
    Q_PROPERTY(QStringList slotNames READ slotNames NOTIFY roomChanged)
    /// Destinos del chat: [{label, value}] ("all" + cada participante).
    Q_PROPERTY(QVariantList chatTargets READ chatTargets NOTIFY roomChanged)
    /// Sube con cada mensaje. Es para que la vista sepa que tiene que volver a
    /// pedir el hilo: `chatCon()` es una funcion, y QML no re-evalua una
    /// funcion porque cambie un modelo que no esta mirando.
    Q_PROPERTY(int chatRev READ chatRev NOTIFY chatRevChanged)
    /// Los hilos privados abiertos: [{uid, nombre}]. El chat de la sala no
    /// esta aca: ese es el de siempre y va primero.
    Q_PROPERTY(QVariantList chatHilos READ chatHilos NOTIFY chatRevChanged)
    /// Con que aparece el operador ante el LOBBY (fuentes por nombre).
    Q_PROPERTY(QString operatorVideo READ operatorVideo NOTIFY operatorChanged)
    Q_PROPERTY(QString operatorAudio READ operatorAudio NOTIFY operatorChanged)
    /// Opciones para elegirlas (fuentes existentes del motor).
    Q_PROPERTY(QStringList videoSourceNames READ videoSourceNames NOTIFY operatorChanged)
    Q_PROPERTY(QStringList audioSourceNames READ audioSourceNames NOTIFY operatorChanged)
    /// Estado de red de B: HTTPS, tunel WireGuard, enlaces (GET /api/v1/network).
    Q_PROPERTY(QVariantMap network READ network NOTIFY networkChanged)
    /// Uso de la sala: cuanta gente hay, cuantos mandan video y cuanto le
    /// cuesta al gateway (cpuPct, memMb, nucleos). Para ver si nos pasamos.
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY roomChanged)
    Q_PROPERTY(QObject* waiting READ waiting CONSTANT)
    Q_PROPERTY(QObject* inside READ inside CONSTANT)
    Q_PROPERTY(QObject* shares READ shares CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    // --- Modo oradores ---------------------------------------------------------
    /// Configuracion: {enabled, defaultSec, carousel, graceSec, multi, defaultSlot}.
    Q_PROPERTY(QVariantMap speakerCfg READ speakerCfg NOTIFY roomChanged)
    Q_PROPERTY(bool autoIntake READ autoIntake NOTIFY autoIntakeChanged)
    Q_PROPERTY(bool speakerMode READ speakerMode NOTIFY roomChanged)
    /// Fila de oradores en orden (tarjetas): uid, name, info, contact, speaker
    /// ("queued"|"onair"|"done"), position, allottedSec, onAirSinceMs, usedMs,
    /// connected, slot, avatar (data: URL o vacio), backstageSlot (preview de
    /// la fila en BA.Backstage.<k>, 0 = sin preview).
    Q_PROPERTY(QObject* speakers READ speakers CONSTANT)
    /// Los que estan al aire ahora: [{uid, name, allottedSec, onAirSinceMs}].
    Q_PROPERTY(QVariantList onAir READ onAir NOTIFY roomChanged)
    /// Reloj de la sala - reloj local (ms): los relojes se calculan con
    /// Date.now() + clockOffset.
    Q_PROPERTY(double clockOffset READ clockOffset NOTIFY roomChanged)
   public:
    ConferenceViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~ConferenceViewModel() override;

    bool online() const;
    QString room() const;
    QString pin() const;
    QString guestUrl() const;
    bool lobby() const;
    bool locked() const;
    int maxGuests() const;
    int slotsFree() const;
    int peopleConnected() const;
    QStringList slotNames() const;
    QVariantList chatTargets() const;
    QString operatorVideo() const;
    QString operatorAudio() const;
    QStringList videoSourceNames() const;
    QStringList audioSourceNames() const;
    QVariantMap network() const { return network_; }
    QVariantMap usage() const;
    QObject* waiting() const;
    QObject* inside() const;
    QObject* shares() const;
    QObject* chat() const;
    /// Cliente de la API de control de B (lo usa tambien el servidor MCP).
    ConferenceClient& client() { return *client_; }
    QVariantMap speakerCfg() const;
    bool speakerMode() const;
    bool autoIntake() const { return autoIntake_; }
    QObject* speakers() const;
    QVariantList onAir() const { return onAir_; }
    double clockOffset() const { return clockOffset_; }

    /// Cambia los campos presentes (ver speakerCfg).
    Q_INVOKABLE void setSpeakerConfig(const QVariantMap& cfg);
    Q_INVOKABLE void speakerOnAir(const QString& id, bool on);
    Q_INVOKABLE void speakerNext();
    /// Lugar en la fila (0 = primero).
    Q_INVOKABLE void speakerMove(const QString& id, int index);
    Q_INVOKABLE void speakerTime(const QString& id, int sec);
    /// Mete a alguien de la sala a la fila de oradores (o lo devuelve a ella).
    Q_INVOKABLE void speakerRequeue(const QString& id);
    /// Presentador fijo: se queda al aire mientras rotan los oradores.
    Q_INVOKABLE void setPresenter(const QString& id, bool on);
    /// Ultima foto que mando la camara de esa persona (id o nombre), como
    /// data: URL. Vacio si todavia no mando ninguna.
    Q_INVOKABLE QString photoOf(const QString& idOrName) const;
    /// Atencion automatica: a cada persona que entra al lobby la atiende el
    /// asistente (le pide los datos y despues pregunta si la hacemos pasar).
    Q_INVOKABLE void setAutoIntake(bool on);
    /// Lo engancha StudioViewModel (el encargo vive alla, con el MCP).
    /// :param yaAdentro: true si esa persona ya entró a la sala sola (estaba
    ///        en la lista del evento). El encargo es otro: confirmarle los
    ///        datos que ya tenemos, no pedírselos de cero.
    void setIntakeSink(std::function<QString(const QString&, const QString&, bool)> fn) {
        intakeSink_ = std::move(fn);
    }
    /// Da por atendida a esa persona: el asistente no la va a abordar. Lo usa
    /// el auto-aceptado, que la atiende de otra manera.
    void marcarAtendido(const QString& id) { intakeDone_.insert(id); }
    Q_INVOKABLE void setProfile(const QString& id, const QString& name, const QString& info,
                                const QString& contact);
    /// "m:ss", "m" (minutos) o "Ns" -> segundos (0 = invalido).
    Q_INVOKABLE int parseDuration(const QString& text) const;
    /// Enlace + QR del tablero de oradores (tablet). Responde con qrReady,
    /// target "board-local" / "board-remote".
    Q_INVOKABLE void requestBoardLink(const QString& target);

    Q_INVOKABLE void admit(const QString& id);
    Q_INVOKABLE void admitAll();
    Q_INVOKABLE void reject(const QString& id);
    Q_INVOKABLE void kick(const QString& id);
    /// Lobby: hablarle en privado (le llega tu voz y vos escuchas la suya).
    Q_INVOKABLE void talk(const QString& id, bool on);
    /// Elegir con que fuentes te ve/oye el lobby ("" = ninguna).
    Q_INVOKABLE void setOperatorFeed(const QString& video, const QString& audio);
    /// Refresca las listas de fuentes (se agregaron o quitaron fuentes).
    Q_INVOKABLE void refreshSources() { emit operatorChanged(); }
    /// "mute-mic" | "mute-cam" | "stop-screen" | "lower-hand"
    Q_INVOKABLE void request(const QString& id, const QString& what);
    /// El participante en el slot (1..invitados): {id, name, red{kbps_in, perdida_pct, jitter_ms, rtt_ms, semaforo}…}; {} si no hay.
    Q_INVOKABLE QVariantMap participanteEnSlot(int slot) const;
    /// `to`: "all" o el id de un participante.
    Q_INVOKABLE void sendChat(const QString& to, const QString& text);
    Q_INVOKABLE void setLobby(bool on);
    Q_INVOKABLE void setLocked(bool on);
    /// PIN nuevo (vacio = generar uno).
    Q_INVOKABLE void newPin(const QString& pin = {});
    Q_INVOKABLE void removeShare(const QString& id);
    /// Copia el archivo a <datos>/Media y lo agrega a la escena actual.
    Q_INVOKABLE void takeShare(const QString& id);
    /// Abre la consola web (codigo de un solo uso -> navegador).
    Q_INVOKABLE void openWebConsole();
    /// Nombre de un participante por id (para el chat).
    Q_INVOKABLE QString nameOf(const QString& id) const;
    /// Los mensajes de UN hilo. `uid` vacio = el de la sala (lo que ve todo el
    /// mundo); con `uid`, la conversacion privada con esa persona.
    ///
    /// POR QUE SEPARADOS. La charla en la que se le piden los datos a alguien
    /// que llega es privada, y mezclada en el chat de la sala se lee como si
    /// fuera para todos —y peor: contestarle ahi manda a toda la sala lo que
    /// era para uno—.
    Q_INVOKABLE QVariantList chatCon(const QString& uid) const;
    int chatRev() const { return chatRev_; }
    QVariantList chatHilos() const;
    /// QR de ingreso al lobby: "local" (red local) o "remote" (Cloudflare).
    /// Responde con qrReady.
    Q_INVOKABLE void requestQr(const QString& target, bool withPin = false);
    /// Genera claves y wg0.conf para el servidor con IP publica y deja este
    /// equipo configurado (tunel encendido); B se reinicia con la config nueva.
    Q_INVOKABLE void configureDirectAccess(const QString& endpoint, const QString& publicAddress,
                                           const QString& iface);

   signals:
    void onlineChanged();
    void roomChanged();
    void autoIntakeChanged();
    void chatRevChanged();
    void operatorChanged();
    void networkChanged();
    /// `modules`: filas de '0'/'1'. Sin url -> `message` explica que falta.
    void qrReady(const QString& target, const QString& url, const QStringList& modules,
                 const QString& message);
    /// Configuracion del servidor lista para copiar (o error).
    void serverConfigReady(const QString& config, const QString& error);
    void notify(const QString& message, const QString& level);
    /// Se agrego una fuente a la escena (la vista de fuentes debe recargar).
    void sourceAdded();

   private:
    void onSnapshot();
    void onChatMsg(const QJsonObject& msg, bool live);
    void participantAction(const QString& id, const QString& action, const QJsonObject& body = {});

    AppRuntime& rt_;

    bool autoIntake_ = false;
    int chatRev_ = 0;

    std::function<QString(const QString&, const QString&, bool)> intakeSink_;

    QSet<QString> intakeDone_; // a quienes ya se les encargo
    std::unique_ptr<ConferenceClient> client_;
    std::unique_ptr<JsonListModel> waiting_;
    std::unique_ptr<JsonListModel> inside_;
    std::unique_ptr<JsonListModel> shares_;
    std::unique_ptr<JsonListModel> chat_;
    std::unique_ptr<JsonListModel> speakers_;
    QVariantList onAir_;
    double clockOffset_ = 0;
    QHash<QString, QString> avatars_; // id -> data:image/jpeg;base64,...
    QTimer avatarPoll_;
    void pollAvatars();
    void rebuildSpeakers();
    QStringList knownWaiting_;
    QVariantMap network_;
    QTimer networkPoll_;
    QStringList slotNames_;
    qint64 lastSeq_ = 0;
    bool firstSnapshot_ = true;
};

} // namespace ba::app
