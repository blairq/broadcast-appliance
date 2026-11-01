// ============================================================================
// src/app/ui/ConferenceClient.hpp — Cliente de la API de control de la sala
//
// Capa de SERVICIO (modelo): habla con la API del operador que expone B en
// 127.0.0.1 (ver src/gateway/AdminApi.cpp) y no sabe nada de la vista.
//   - Descubre puerto y token en Runtime/gateway-admin.json (dentro del bundle) (lo escribe B
//     con permisos solo-usuario). Si B se reinicia el token cambia: ante un
//     401 o un corte se vuelve a leer el archivo.
//   - Estado: flujo SSE /api/v1/events. "room" trae el snapshot completo;
//     "chat", un mensaje. Reconexion automatica con espera creciente y un
//     vigia que corta si el servidor deja de latir (B manda ": ping" cada 15 s).
//     Cada evento trae su id; al reconectar se manda Last-Event-ID y B reenvía
//     los de un solo disparo que se perdieron (o dice que no pudo: `reanudado`).
//   - El flujo lo lee LectorSala, en SU PROPIO HILO: una interfaz trabada no deja de
//     leer (B cortaba el flujo a una app que no leía, y los eventos de estado se
//     perdían). Arma cada evento ahí y se lo pasa a este objeto por cola (las señales
//     siguen saliendo en el hilo de la interfaz); la última foto de la sala queda además
//     bajo candado (fotoSala): la lee cualquier hilo sin I/O (el vigía de las alarmas).
//   - Acciones: POST/DELETE con Bearer.
//
// A ES LA FUENTE DE VERDAD; B, EL BORDE (gateway/Sesion.hpp). Lo que A le dice a B
// de estado (las salidas, el tally, el teleprompter, las claves del evento…) B lo
// olvida si se reinicia. Cada modelo que le dice algo así registra cómo volver a
// decírselo (registrarRepublicador) y este cliente los llama TODOS cuando vuelve
// una B que no lo sabe: otro arranque (`epoch` del primer "room") o una
// reconexión que no pudo reenviar lo perdido. Un modelo nuevo que le manda estado
// a B y no se registra queda sin dueño tras una caída: lo vigila
// tests/test_republicadores.py.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>
#include <vector>

#include <QThread>

class QNetworkReply;

namespace ba::app {

/// El que lee el flujo de eventos de B, en su hilo (ver arriba). Solo transporte: no sabe
/// qué quiere decir cada evento.
class LectorSala : public QObject {
    Q_OBJECT
   public:
    /// Se crea en el hilo de la interfaz y se mueve a su hilo; `iniciar` arranca allá.
    Q_INVOKABLE void iniciar();
    /// Corta y vuelve a entrar (B se reinició con otro token: un 401 en un pedido).
    Q_INVOKABLE void reconectar();
    Q_INVOKABLE void parar();

   signals:
    /// Antes de cada conexión, el puerto y el token leídos de gateway-admin.json.
    void acceso(int puerto, const QString& token);
    /// Un evento entero, ya armado (en el hilo del lector: se conecta por cola).
    void evento(const QByteArray& nombre, const QJsonObject& datos);
    /// Se cortó el flujo (o no se pudo abrir): va a volver a probar solo.
    void cerrado();

   private:
    void conectar();
    void alLeer();
    void alCerrar();
    void reintentar();

    QNetworkAccessManager* nam_ = nullptr; ///< del hilo del lector (se crea en iniciar)
    QNetworkReply* sse_ = nullptr;
    QTimer* retry_ = nullptr;
    QTimer* watchdog_ = nullptr;
    QByteArray buf_;
    QByteArray ultimoId_; ///< el id del último evento (Last-Event-ID al reconectar)
    int backoffMs_ = 1000;
    bool parado_ = false;
};

class ConferenceClient : public QObject {
    Q_OBJECT
   public:
    using Done = std::function<void(bool ok, const QJsonObject& reply)>;

    explicit ConferenceClient(QObject* parent = nullptr);
    ~ConferenceClient() override;

    bool online() const { return online_; }
    const QJsonObject& snapshot() const { return snap_; }
    /// La última foto de la sala que llegó por eventos, desde CUALQUIER hilo y sin I/O (la
    /// guarda el lector, bajo candado). Vacía si no hay conexión con B.
    static QJsonObject fotoSala();

    /// Cómo volver a decirle a B algo de estado que A le dijo (ver arriba). `fn` se
    /// llama con B en línea: enseguida si ya lo está (en la vuelta siguiente del
    /// bucle, no adentro de esta llamada) y cada vez que vuelve una B que no lo sabe.
    /// Un `nombre` ya registrado se reemplaza; se borra solo cuando `dueno` muere.
    void registrarRepublicador(const QString& nombre, QObject* dueno, std::function<void()> fn);
    /// Los registrados (para el diagnóstico y los tests).
    QStringList republicadores() const;
    /// El arranque de la B con la que se habla ("" = todavía ninguna).
    const QString& epoch() const { return epoch_; }

    /// POST a la API (`path` absoluto, p. ej. "/api/v1/room").
    void post(const QString& path, const QJsonObject& body = {}, Done done = {}, int timeoutMs = 5000);
    /// PUT (la configuración en caliente: /api/v1/config/<sección>).
    void put(const QString& path, const QJsonObject& body, Done done = {});
    void del(const QString& path, Done done = {});
    void get(const QString& path, Done done);
    /// GET de bytes crudos (fotos de las tarjetas). No avisa `failed`: una
    /// foto que falta no es un error para el operador.
    void getRaw(const QString& path, std::function<void(bool ok, const QByteArray& body)> done);
    /// Puerto y token de la API de B (Runtime/gateway-admin.json). Sin estado: lo usa
    /// también quien habla con B desde otro hilo (AvisoFueraDeUi).
    static bool leerAdmin(int& puerto, QString& token);
    /// GET a la API de B SIN la interfaz (F3-03): bloqueante, desde cualquier hilo (sin
    /// bucle de eventos: un socket propio que espera hasta `topeMs`). Para lo que tiene que
    /// saber de la sala con la interfaz trabada (el MCP en el pool, el vigía de las alarmas
    /// en el plano de control): la misma foto que llega por eventos, preguntada a su dueño.
    /// El cuerpo si B contestó 2xx; nada si no está o no contesta a tiempo.
    static std::optional<QJsonObject> pedirSinInterfaz(const QString& ruta, int topeMs = 2000);

   signals:
    void onlineChanged(bool online);
    /// Volvió una B que no sabe lo que A le había dicho (ver registrarRepublicador):
    /// los republicadores ya corrieron.
    void salaNueva();
    void snapshotChanged();
    void chatReceived(const QJsonObject& msg);
    /// La botonera (superficie de control): solicitud, entrada o conexión de una tablet.
    void botoneraEvento(const QJsonObject& ev);
    /// Lo que mandó el corrector de subtítulos desde /correccion/ (T44): {id, texto?, descartar}.
    void correccionEvento(const QJsonObject& ev);
    void sesionEvento(const QJsonObject& ev);
    /// El control web (/control/, /ws?control): solicitud, conexión o entrada de un aparato.
    void controlEvento(const QJsonObject& ev);
    /// El catálogo de direcciones de los servicios cambió (sala/Direcciones).
    void direccionesEvento(const QJsonObject& catalogo);
    /// Cambió quién habla en la sala o la asamblea (lo mide el gateway): {ids: [clave, …]}.
    void hablanEvento(const QJsonObject& ev);
    /// Los tótems de registro (/registro/): conexión, canal, audio, foto, toque, intento.
    void registroEvento(const QJsonObject& ev);
    /// Cambió un teleprompter o quién lo mira: {prompter, estado (con `texto` solo si se
    /// cargó un discurso), clientes} o {prompter, quitado}.
    void prompterEvento(const QJsonObject& ev);
    /// Cambió algo que llega a la sala y guarda el evento (sala/GuardadoEnEvento):
    /// `nombre` "votacion" ({votacion, foto, evento}) o "preguntas" ({preguntas, recuento,
    /// proxima, foto, evento}).
    void estadoEvento(const QString& nombre, const QJsonObject& ev);
    /// Encargaron un rótulo desde un celular: hay que ir a buscarlo ({pendientes}).
    void rotulosEvento(const QJsonObject& ev);
    /// Cómo anda ba-web, el servidor de la página pública ({vivo, reinicios, cortado, seguidas}).
    void webEvento(const QJsonObject& ev);
    /// Alguien escaneó su invitación: hay acreditaciones para ir a buscar ({pendientes}).
    void acreditacionEvento();
    /// Cambió el estado de red de la sala (lo mismo que GET /api/v1/network).
    void redEvento(const QJsonObject& red);
    /// Un participante cambió su foto (la de su tarjeta): {id}.
    void avatarEvento(const QString& id);
    /// Quién mira el programa y cada salida: {mirando, salidas: {id: cuántos}} (sala/SalidasSala).
    void programaEvento(const QJsonObject& ev);
    /// Hay una foto nueva de la cámara de un integrante de la sesión remota: {id}.
    void fotoSesionEvento(const QString& id);
    /// Una accion fallo; `error` es el codigo de la API ("full", "not_found"...).
    void failed(const QString& error);

   private:
    void dispatch(const QByteArray& event, const QJsonObject& obj);
    void setOnline(bool on);
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, Done done, int timeoutMs = 5000);
    void republicarTodo();

    struct Republicador {
        QString nombre;
        QPointer<QObject> dueno;
        const QObject* crudo = nullptr; ///< el mismo, para reconocerlo cuando muere
        std::function<void()> fn;
    };

    QNetworkAccessManager nam_;
    QThread hilo_;                 ///< el del lector
    LectorSala* lector_ = nullptr; ///< vive en hilo_
    QString token_;
    int port_ = 0;
    bool online_ = false;
    QJsonObject snap_;
    std::vector<Republicador> republicadores_;
    QString epoch_;
};

} // namespace ba::app
