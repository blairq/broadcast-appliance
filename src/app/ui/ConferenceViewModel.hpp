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
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

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
    /// Nombre del invitado de cada slot (indice slot-1; vacio = libre).
    Q_PROPERTY(QStringList slotNames READ slotNames NOTIFY roomChanged)
    /// Destinos del chat: [{label, value}] ("all" + cada participante).
    Q_PROPERTY(QVariantList chatTargets READ chatTargets NOTIFY roomChanged)
    /// Con que aparece el operador ante el LOBBY (fuentes por nombre).
    Q_PROPERTY(QString operatorVideo READ operatorVideo NOTIFY operatorChanged)
    Q_PROPERTY(QString operatorAudio READ operatorAudio NOTIFY operatorChanged)
    /// Opciones para elegirlas (fuentes existentes del motor).
    Q_PROPERTY(QStringList videoSourceNames READ videoSourceNames NOTIFY operatorChanged)
    Q_PROPERTY(QStringList audioSourceNames READ audioSourceNames NOTIFY operatorChanged)
    /// Estado de red de B: HTTPS, tunel WireGuard, enlaces (GET /api/v1/network).
    Q_PROPERTY(QVariantMap network READ network NOTIFY networkChanged)
    Q_PROPERTY(QObject* waiting READ waiting CONSTANT)
    Q_PROPERTY(QObject* inside READ inside CONSTANT)
    Q_PROPERTY(QObject* shares READ shares CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
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
    QStringList slotNames() const;
    QVariantList chatTargets() const;
    QString operatorVideo() const;
    QString operatorAudio() const;
    QStringList videoSourceNames() const;
    QStringList audioSourceNames() const;
    QVariantMap network() const { return network_; }
    QObject* waiting() const;
    QObject* inside() const;
    QObject* shares() const;
    QObject* chat() const;

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
    Q_INVOKABLE void copyToClipboard(const QString& text);
    /// Nombre de un participante por id (para el chat).
    Q_INVOKABLE QString nameOf(const QString& id) const;
    /// Genera claves y wg0.conf para el servidor con IP publica y deja este
    /// equipo configurado (tunel encendido); B se reinicia con la config nueva.
    Q_INVOKABLE void configureDirectAccess(const QString& endpoint, const QString& publicAddress,
                                           const QString& iface);

   signals:
    void onlineChanged();
    void roomChanged();
    void operatorChanged();
    void networkChanged();
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
    std::unique_ptr<ConferenceClient> client_;
    std::unique_ptr<JsonListModel> waiting_;
    std::unique_ptr<JsonListModel> inside_;
    std::unique_ptr<JsonListModel> shares_;
    std::unique_ptr<JsonListModel> chat_;
    QStringList knownWaiting_;
    QVariantMap network_;
    QTimer networkPoll_;
    QStringList slotNames_;
    qint64 lastSeq_ = 0;
    bool firstSnapshot_ = true;
};

} // namespace ba::app
