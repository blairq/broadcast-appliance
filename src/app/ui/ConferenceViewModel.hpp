// ============================================================================
// src/app/ui/ConferenceViewModel.hpp — La sala de videoconferencia (ViewModel)
//
// Adapta el estado que publica la API de control de B (via ConferenceClient)
// a lo que necesita la vista QML: propiedades simples, listas (lobby, adentro,
// archivos, chat) y comandos. No tiene reglas de sala propias: admitir,
// expulsar, el PIN, el chat, los oradores, los enlaces… son operaciones del
// modelo (src/app/sala/: ControlSala, ParticipantesSala, OradoresSala,
// EnlacesSala, PaginaPublico, Teleprompter), las mismas que usa el MCP. Las
// crea y las tiene este view model porque usan su cliente; el resultado vuelve
// por el flujo de eventos como un snapshot nuevo (una sola fuente de verdad).
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

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

#include "ui/ListModels.hpp" // JsonListModel
#include "ui/CapturaError.hpp"

namespace ba::app::sala {
class ControlSala;
class EnlacesSala;
class SalidasSala;
class OradoresSala;
class PaginaPublico;
class ParticipantesSala;
class Teleprompter;
} // namespace ba::app::sala

namespace ba::app {

class AppRuntime;
class UsoPorVistas;
class ConferenceClient;
class VentanillaMotor;

class ConferenceViewModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    /// El proxy por nombre (T61): lo último que dijo la sala (GET /api/v1/proxy) más la configuración.
    Q_PROPERTY(QVariantMap proxy READ proxy NOTIFY proxyChanged)
    Q_PROPERTY(QString room READ room NOTIFY salaChanged)
    Q_PROPERTY(QString pin READ pin NOTIFY salaChanged)
    /// Los bloqueos por PIN mal ({global, ips, intentosPorIp, bloqueoS}): se piden con pinBloqueos().
    Q_PROPERTY(QVariantMap bloqueosPin READ bloqueosPin NOTIFY bloqueosPinChanged)
    /// Cómo anda ba-web, el servidor de la página pública (lo avisa la sala: evento «web»):
    /// {vivo, cortado, reinicios, seguidas, problema ("" | "version" | "falta"), version,
    /// esperada}. Vacío = la sala todavía no dijo nada (o no usa ba-web).
    Q_PROPERTY(QVariantMap baWeb READ baWeb NOTIFY baWebChanged)
    Q_PROPERTY(QString guestUrl READ guestUrl NOTIFY salaChanged)
    Q_PROPERTY(bool lobby READ lobby NOTIFY salaChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY salaChanged)
    Q_PROPERTY(int maxGuests READ maxGuests NOTIFY salaChanged)
    Q_PROPERTY(int slotsFree READ slotsFree NOTIFY salaChanged)
    /// La entrada escalonada (room.entradaPorSegundo): cuántos aprobados esperan su turno
    /// para pasar a la sala (siguen en `waiting` con el rol `entrando`) y a qué ritmo pasan.
    Q_PROPERTY(int entrando READ entrando NOTIFY salaChanged)
    Q_PROPERTY(int entradaPorSegundo READ entradaPorSegundo NOTIFY salaChanged)
    /// Todos los conectados a la sala: lobby, programa y fila de oradores. Es lo
    /// que se pierde si la app se cierra (guardias de cierre y de actualizacion).
    Q_PROPERTY(int peopleConnected READ peopleConnected NOTIFY participantesChanged)
    /// Nombre del invitado de cada slot (indice slot-1; vacio = libre).
    Q_PROPERTY(QStringList slotNames READ slotNames NOTIFY participantesChanged)
    /// Los lugares de invitado (1..invitados, uno por fila): slot, nombre, pid (el participante;
    /// vacío = libre), info, sinRetorno (adentro y su mix-minus no está abierto en el gateway),
    /// semaforo (verde | ambar | rojo | "") y red {kbps_in, perdida_pct, jitter_ms, rtt_ms, alto}.
    /// Cada fila avisa solo el rol que le cambió: la red cambia en cada foto y no despierta lo demás.
    Q_PROPERTY(QObject* lugares READ lugares CONSTANT)
    /// Destinos del chat: [{label, value}] ("all" + cada participante).
    Q_PROPERTY(QVariantList chatTargets READ chatTargets NOTIFY participantesChanged)
    /// {id: nombre} de la gente de la sala (y de quien se atiende sin estar): para la vista.
    Q_PROPERTY(QVariantMap nombres READ nombres NOTIFY participantesChanged)
    /// Los hilos privados abiertos: [{uid, nombre}]. El chat de la sala no
    /// esta aca: ese es el de siempre y va primero. (Los mensajes de un hilo: el
    /// modelo `chat` filtrado con ui/ChatHilo.)
    Q_PROPERTY(QVariantList chatHilos READ chatHilos NOTIFY chatCambio)
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
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY usoChanged)
    Q_PROPERTY(QObject* waiting READ waiting CONSTANT)
    Q_PROPERTY(QObject* inside READ inside CONSTANT)
    Q_PROPERTY(QObject* shares READ shares CONSTANT)
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    // --- Modo oradores ---------------------------------------------------------
    /// Configuracion: {enabled, defaultSec, defaultTexto ("m:ss"), carousel, graceSec, multi, defaultSlot}.
    Q_PROPERTY(QVariantMap speakerCfg READ speakerCfg NOTIFY oradoresCfgChanged)
    Q_PROPERTY(bool autoIntake READ autoIntake NOTIFY autoIntakeChanged)
    Q_PROPERTY(bool speakerMode READ speakerMode NOTIFY oradoresCfgChanged)
    /// Fila de oradores en orden (tarjetas): uid, name, info, contact, speaker
    /// ("queued"|"onair"|"done"), position, allottedSec, onAirSinceMs, usedMs,
    /// connected, slot, avatar (su foto: image://personas/…, o vacío), backstageSlot (preview de
    /// la fila en BA.Backstage.<k>, 0 = sin preview); y, del que está al aire, su reloj
    /// (sala::OradoresSala::reloj): restanteMs, estadoTiempo (ok | aviso | excedido),
    /// textoTiempo ("m:ss" o "+m:ss"); textoAsignado y textoUsado ("m:ss").
    Q_PROPERTY(QObject* speakers READ speakers CONSTANT)
    /// Los que estan al aire ahora: [{uid, name, allottedSec, onAirSinceMs, restanteMs,
    /// estadoTiempo, textoTiempo}]. El reloj lo da el modelo (cuatro veces por segundo,
    /// solo con alguien al aire).
    Q_PROPERTY(QVariantList onAir READ onAir NOTIFY onAirCambio)
    /// El peor reloj de los que hablan: "" (nadie al aire), ok, aviso o excedido.
    Q_PROPERTY(QString peorEstado READ peorEstado NOTIFY onAirCambio)
    /// Reloj de la sala - reloj local (ms): los relojes se calculan con
    /// Date.now() + clockOffset.
    Q_PROPERTY(double clockOffset READ clockOffset NOTIFY usoChanged)
   public:
    /// Lo del motor (las fuentes, con qué aparece el operador, lo guardado de la sala) se lee
    /// de la foto de `ventanilla` y se pide por ella (F3-03).
    ConferenceViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~ConferenceViewModel() override;

    bool online() const;
    QString room() const;
    QString pin() const;
    QString guestUrl() const;
    bool lobby() const;
    bool locked() const;
    int maxGuests() const;
    int slotsFree() const;
    int entrando() const;
    int entradaPorSegundo() const;
    int peopleConnected() const;
    QStringList slotNames() const;
    QObject* lugares() const;
    /// Cuántos lugares de invitado muestra la interfaz (los ajustes: el cupo).
    void setLugares(int n);
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
    /// Las operaciones de la sala (modelo). La vista y el MCP piden por acá.
    sala::ControlSala& control() { return *control_; }
    sala::ParticipantesSala& participantes() { return *participantes_; }
    sala::OradoresSala& oradores() { return *oradores_; }
    sala::EnlacesSala& enlaces() { return *enlaces_; }
    sala::SalidasSala& salidas() { return *salidas_; }
    sala::PaginaPublico& paginaPublico() { return *paginaPublico_; }
    sala::Teleprompter& teleprompter() { return *teleprompter_; }
    QVariantMap speakerCfg() const;
    bool speakerMode() const;
    bool autoIntake() const;
    QObject* speakers() const;
    QVariantList onAir() const { return onAir_; }
    QString peorEstado() const { return peorEstado_; }
    double clockOffset() const { return clockOffset_; }

    /// Cambia los campos presentes (ver speakerCfg).
    Q_INVOKABLE void setSpeakerConfig(const QVariantMap& cfg);
    Q_INVOKABLE void speakerOnAir(const QString& id, bool on);
    Q_INVOKABLE void speakerNext();
    /// Lugar en la fila (0 = primero).
    Q_INVOKABLE void speakerMove(const QString& id, int index);
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
    /// Lo que DIJO alguien que se atiende hablando, ya pasado a texto (ui/
    /// AtencionViewModel): entra a su hilo del chat como un mensaje suyo, con
    /// `voz: true`. Así el que atiende lo lee con `chat_wait` /
    /// `invitados_chat_leer` igual que si lo hubiera escrito, y el operador lo
    /// ve en el hilo privado. No va a la sala: es lo que se le oyó, no algo que
    /// mandó (la sala no lo tiene ni lo reparte).
    /// `nombre`: el de alguien que no está en la sala (el integrante que espera en la
    /// sesión remota de la asamblea); vacío = el de la sala.
    void agregarDichoPorVoz(const QString& id, const QString& texto, const QString& nombre = {});
    /// Lo que se le DIJO con la voz del atendedor a alguien que no está en la sala (el
    /// integrante que espera en la sesión remota): al mismo hilo, como un mensaje
    /// tuyo hacia él. A los de la sala no hace falta: el chat de la sala ya lo trae.
    void agregarDichoAlAtendido(const QString& id, const QString& nombre, const QString& texto);
    Q_INVOKABLE void setProfile(const QString& id, const QString& name, const QString& info,
                                const QString& contact);
    /// La ficha de un orador desde el diálogo: los datos y, si `tiempo` no va vacío, su
    /// tiempo para hablar (sala::OradoresSala::leerTiempo: «2:30», «3» —tres minutos—, «90s», «+1:00»…).
    Q_INVOKABLE void guardarFicha(const QString& id, const QString& name, const QString& info,
                                  const QString& contact, const QString& tiempo);
    /// El tiempo general por orador, escrito (ver guardarFicha). El error va por la señal del modelo.
    Q_INVOKABLE void setTiempoGeneral(const QString& texto);
    /// Una vista empieza o deja de mirar lo que la sala no avisa y hay que pedirle: "proxy"
    /// (cada 5 s) o "bloqueos" (los de PIN, cada 3 s). La cuenta es por vista
    /// (comun/UsoPorVistas); sin nadie mirando no se pregunta.
    Q_INVOKABLE void usar(QObject* vista, bool si, const QString& que = {});
    /// Enlace + QR del tablero de oradores (tablet). Responde con qrReady,
    /// target "board-local" / "board-remote".
    Q_INVOKABLE void requestBoardLink(const QString& target);

    Q_INVOKABLE void admit(const QString& id);
    Q_INVOKABLE void admitAll();
    Q_INVOKABLE void reject(const QString& id);
    Q_INVOKABLE void kick(const QString& id);
    /// Lobby: hablarle en privado (le llega tu voz y vos escuchas la suya).
    Q_INVOKABLE void talk(const QString& id, bool on);
    /// Prueba técnica (T33): eco por su micrófono y red. El resultado llega por pruebaLista.
    Q_INVOKABLE void pruebaTecnica(const QString& id);
    QVariantMap proxy() const { return proxy_; }
    void refrescarProxy();
    Q_INVOKABLE void renovarProxy(const QString& host);
    /// Cambia la configuración del proxy (activo, dominio, acme…) y reinicia la sala. "" o el error.
    Q_INVOKABLE QString configurarProxy(const QVariantMap& cambios);
    /// Reinicia solo el gateway (la sala): relee su configuración (IP, puertos,
    /// túnel…) sin tocar el resto del programa. Los invitados se cortan y vuelven
    /// a entrar. "" o el error.
    Q_INVOKABLE QString reiniciarSala();
    /// Elegir con que fuentes te ve/oye el lobby ("" = ninguna).
    Q_INVOKABLE void setOperatorFeed(const QString& video, const QString& audio);
    /// Refresca las listas de fuentes (se agregaron o quitaron fuentes).
    Q_INVOKABLE void refreshSources() { emit operatorChanged(); }
    /// "mute-mic" | "mute-cam" | "stop-screen" | "lower-hand"
    Q_INVOKABLE void request(const QString& id, const QString& what);
    /// El participante en el slot (1..invitados): {id, name, red{kbps_in, perdida_pct, jitter_ms, rtt_ms, semaforo}…}; {} si no hay.
    QVariantMap participanteEnSlot(int slot) const;
    /// `to`: "all" o el id de un participante.
    Q_INVOKABLE void sendChat(const QString& to, const QString& text);
    Q_INVOKABLE void setLobby(bool on);
    Q_INVOKABLE void setLocked(bool on);
    /// PIN nuevo (vacio = generar uno).
    Q_INVOKABLE void newPin(const QString& pin = {});
    /// Pide a la sala quién está bloqueado por PIN mal (actualiza bloqueosPin).
    void pinBloqueos();
    /// Levanta los bloqueos: los de esa IP, o (vacío) todos, el global incluido.
    Q_INVOKABLE void desbloquearPin(const QString& ip = {});
    QVariantMap bloqueosPin() const { return bloqueosPin_; }
    Q_INVOKABLE void removeShare(const QString& id);
    /// Copia el archivo a <datos>/Media y lo agrega a la escena actual.
    Q_INVOKABLE void takeShare(const QString& id);
    /// Nombre de un participante por id (para el chat).
    QString nameOf(const QString& id) const;
    QVariantMap nombres() const;
    /// Los mensajes de UN hilo. `uid` vacio = el de la sala (lo que ve todo el
    /// mundo); con `uid`, la conversacion privada con esa persona.
    ///
    /// POR QUE SEPARADOS. La charla en la que se le piden los datos a alguien
    /// que llega es privada, y mezclada en el chat de la sala se lee como si
    /// fuera para todos —y peor: contestarle ahi manda a toda la sala lo que
    /// era para uno—.
    QVariantList chatHilos() const;
    /// QR de ingreso al lobby: "local" (red local) o "remote" (Cloudflare).
    /// Responde con qrReady.
    Q_INVOKABLE void requestQr(const QString& target, bool withPin = false);
    /// Genera claves y wg0.conf para el servidor con IP publica y deja este
    /// equipo configurado (tunel encendido); B se reinicia con la config nueva.
    Q_INVOKABLE void configureDirectAccess(const QString& endpoint, const QString& publicAddress,
                                           const QString& iface);

   signals:
    void proxyChanged();
    void pruebaLista(const QString& id, const QVariantMap& resultado);
    void onlineChanged();
    /// Cambió la GENTE de la sala: quién está, en qué estado, su lugar, su micrófono… (no la
    /// red ni el uso, que cambian en cada foto). Para el C++ que mira a la gente (lo que se
    /// corta al cerrar, los nombres de las fuentes, la atención, la calidad por calor); la
    /// QML se enlaza a los avisos por grupo, que salen solo si cambió lo suyo.
    void roomChanged();
    void salaChanged();          ///< room, pin, guestUrl, lobby, locked, maxGuests, slotsFree
    void participantesChanged(); ///< peopleConnected, slotNames, chatTargets
    void usoChanged();           ///< usage, clockOffset
    void oradoresCfgChanged();   ///< speakerCfg, speakerMode
    void onAirCambio();
    void autoIntakeChanged();
    /// Llegó o se fue un mensaje del chat (los hilos y quien espera, chat_esperar).
    void chatCambio();
    void operatorChanged();
    void networkChanged();
    /// `modules`: filas de '0'/'1'. Sin url -> `message` explica que falta.
    void qrReady(const QString& target, const QString& url, const QStringList& modules,
                 const QString& message);
    /// Configuracion del servidor lista para copiar (o error).
    void serverConfigReady(const QString& config, const QString& error);
    void notify(const QString& message, const QString& level);
    void bloqueosPinChanged();
    void baWebChanged();
    /// Se agrego una fuente a la escena (la vista de fuentes debe recargar).
    void sourceAdded();

   public:
    /// Un mensaje del chat de la sala (como viene de B) como queda en el modelo `chat`:
    /// con `id`/`uid` (su número de secuencia) y `mine`. Sin estado: lo usa también el
    /// vigía de las alarmas cuando le pregunta a B con la interfaz trabada.
    static QJsonObject mensajeDelChat(const QJsonObject& msg);
    QVariantMap baWeb() const { return baWeb_; }

   private:
    /// Llegó un paquete de la sala (ConferenceClient::salaCambio): aplica SOLO lo que cambió
    /// (los pasos por fila y rol, armados en el hilo del lector) y avisa por grupo.
    void aplicarPaquete(unsigned partes);
    /// Los que esperan en el lobby y la atención automática (al cambiar la espera o al
    /// prenderla).
    void atenderLobby();
    void onChatMsg(const QJsonObject& msg, bool live);

    AppRuntime& rt_; ///< solo para las órdenes que corren en el dueño (y reiniciar la sala)
    VentanillaMotor& ventanilla_;
    std::vector<bool> monitorLobby_; ///< lo último pedido al motor (se pide solo si cambia)
    QVariantMap proxy_;
    QVariantMap bloqueosPin_;


    std::function<QString(const QString&, const QString&, bool)> intakeSink_;

    QSet<QString> intakeDone_; // a quienes ya se les encargo
    std::unique_ptr<ConferenceClient> client_;
    // Usan el cliente: van después (se destruyen antes).
    std::unique_ptr<sala::ControlSala> control_;
    std::unique_ptr<sala::ParticipantesSala> participantes_;
    std::unique_ptr<sala::OradoresSala> oradores_;
    std::unique_ptr<sala::EnlacesSala> enlaces_;
    std::unique_ptr<sala::SalidasSala> salidas_;
    std::unique_ptr<sala::PaginaPublico> paginaPublico_;
    std::unique_ptr<sala::Teleprompter> teleprompter_;
    std::unique_ptr<JsonListModel> waiting_;
    std::unique_ptr<JsonListModel> inside_;
    std::unique_ptr<JsonListModel> shares_;
    std::unique_ptr<JsonListModel> chat_;
    std::unique_ptr<JsonListModel> speakers_;
    QVariantList onAir_;
    double clockOffset_ = 0;
    QHash<QString, QString> nombresDeAfuera_; // id -> nombre de quien se atiende sin estar en la sala
    std::unique_ptr<UsoPorVistas> usos_;
    QTimer proxyPoll_, bloqueosPoll_;
    /// El reloj de los que hablan: corre solo con alguien al aire (actualizarRelojes).
    QTimer relojOradores_;
    QString peorEstado_;
    /// Pone el reloj (restante, estado, texto) a los que están al aire, en la fila y en onAir.
    void actualizarRelojes();
    /// Todas las fotos (al volver la sala) y una (la sala avisa que cambió: «avatar»).
    void pollAvatars();
    void traerAvatar(const QString& id);

   public:
    /// Lo que la sala avisa: la red de cada uno y la foto de un invitado.
    void aplicarRed(const nlohmann::json& r);
    void aplicarAvatar(const QString& id, const QByteArray& jpeg);

   private:
    QVariantMap network_;
    std::unique_ptr<JsonListModel> lugares_;
    int nLugares_ = 0;
    /// Los lugares desde lo que tiene la interfaz (sin la sala o con otra cantidad): O(lugares),
    /// solo en esos casos.
    void rehacerLugares();
    bool lugaresSucios_ = false; ///< la lista no es la del último resumen (sin la sala, otra cantidad)
    qint64 lastSeq_ = 0;
    QVariantMap baWeb_; ///< ver la propiedad baWeb
    qint64 avisoSinSalaMs_ = 0; ///< cuándo se avisó que la sala no responde (uno cada 30 s, no uno por pedido)
    int authSeguidos_ = 0;      ///< 401 seguidos: el primero lo arregla el cliente releyendo el token
};

} // namespace ba::app
