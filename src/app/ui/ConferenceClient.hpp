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

class QNetworkReply;

namespace ba::app {

class ConferenceClient : public QObject {
    Q_OBJECT
   public:
    using Done = std::function<void(bool ok, const QJsonObject& reply)>;

    explicit ConferenceClient(QObject* parent = nullptr);
    ~ConferenceClient() override;

    bool online() const { return online_; }
    const QJsonObject& snapshot() const { return snap_; }
    /// Cuántas sesiones del gateway reciben el programa (evento «programa»); -1 = todavía
    /// no lo dijo (un gateway que no lo informa).
    int miranPrograma() const { return online_ ? miranPrograma_ : -1; }
    /// Lo mismo por cada salida que no es la por omisión y que alguien mira ({id: cuántos},
    /// mismo evento): la app publica BA.Salida.<id> solo mientras figura acá.
    QJsonObject miranSalidas() const { return online_ ? miranSalidas_ : QJsonObject(); }
    /// Las salidas del aparato para el gateway (docs/planes/PLAN-SALIDAS.md):
    /// [{id, nombre, color, porOmision}]. Se guarda y se vuelve a mandar sola cada vez que
    /// el gateway vuelve (un gateway reiniciado arranca sin lista: todo a BA.Program).
    void ponerSalidas(const QJsonArray& lista);

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
    /// Una accion fallo; `error` es el codigo de la API ("full", "not_found"...).
    void failed(const QString& error);

   private:
    bool loadAdminFile();
    void connectEvents();
    void onEventsData();
    void onEventsClosed();
    void dispatch(const QByteArray& event, const QByteArray& data);
    void setOnline(bool on);
    void scheduleReconnect();
    void send(const QByteArray& verb, const QString& path, const QByteArray& body, Done done, int timeoutMs = 5000);
    void republicarTodo();

    struct Republicador {
        QString nombre;
        QPointer<QObject> dueno;
        const QObject* crudo = nullptr; ///< el mismo, para reconocerlo cuando muere
        std::function<void()> fn;
    };

    QNetworkAccessManager nam_;
    QNetworkReply* sse_ = nullptr;
    QByteArray buf_;
    QString token_;
    int port_ = 0;
    bool online_ = false;
    QJsonObject snap_;
    int miranPrograma_ = -1;
    QJsonObject miranSalidas_;
    QJsonArray salidas_; ///< la última lista de ponerSalidas (vacía = nunca se mandó)
    std::vector<Republicador> republicadores_;
    QString epoch_;
    QByteArray ultimoId_; ///< el id del último evento (Last-Event-ID al reconectar)
    QTimer retry_;
    QTimer watchdog_;
    int backoffMs_ = 1000;
};

} // namespace ba::app
