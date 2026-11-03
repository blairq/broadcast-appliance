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
//     bajo candado (fotoSala, fotoSalaJson): la lee cualquier hilo sin I/O (el vigía de
//     las alarmas, en el plano de control).
//   - LA FOTO DE LA SALA NO SE PROCESA EN LA INTERFAZ (sala/ResumenSala.hpp): el lector la
//     resume y calcula, contra lo que la interfaz ya tiene, qué cambió (partes y, por lista,
//     los cambios por fila y rol). A la interfaz le llega UN aviso (`salaCambio(partes)`)
//     con el paquete chico (`paquete()`); si llegan fotos mientras no lo tomó, la última
//     gana (nunca se encolan fotos en su hilo). Lo que no puede esperar a la interfaz (qué
//     lugares tienen gente: el motor despierta sus entradas antes de que lleguen los
//     medios) se engancha en el hilo del lector (`alLeerFoto`).
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

#include <map>
#include <memory>
#include <mutex>

#include "sala/ResumenSala.hpp"

class QNetworkReply;

namespace ba::app {

/// El que lee el flujo de eventos de B, en su hilo (ver arriba). Solo transporte: no sabe
/// qué quiere decir cada evento.
/// Lo que comparten el lector (su hilo) y el cliente (la interfaz).
struct CanalSala {
    sala::BuzonSala buzon;
    std::mutex oyentesMu;
    std::map<int, std::function<void(const sala::ResumenSala*)>> oyentes; ///< alLeerFoto (con oyentesMu)
    int proximo = 1;
    /// En el hilo del lector: a los enganchados (nulo = sin B).
    void avisar(const sala::ResumenSala* r);
};

class LectorSala : public QObject {
    Q_OBJECT
   public:
    explicit LectorSala(std::shared_ptr<CanalSala> canal) : canal_(std::move(canal)) {}
    /// Se crea en el hilo de la interfaz y se mueve a su hilo; `iniciar` arranca allá.
    Q_INVOKABLE void iniciar();
    /// Corta y vuelve a entrar (B se reinició con otro token: un 401 en un pedido).
    Q_INVOKABLE void reconectar();
    Q_INVOKABLE void parar();
    /// Rearma la última foto (cambió la cantidad de lugares que muestra la interfaz).
    Q_INVOKABLE void rehacer();

   signals:
    /// Antes de cada conexión, el puerto y el token leídos de gateway-admin.json.
    void acceso(int puerto, const QString& token);
    /// Un evento entero, ya armado (en el hilo del lector: se conecta por cola). La foto de
    /// la sala («room») no va por acá: va al buzón (hayFoto).
    void evento(const QByteArray& nombre, const QJsonObject& datos);
    /// Hay un paquete de la sala en el buzón y no había otro esperando (uno solo en camino).
    void hayFoto();
    /// Se cortó el flujo (o no se pudo abrir): va a volver a probar solo.
    void cerrado();

   private:
    void conectar();
    void alLeer();
    /// Una foto de la sala: al buzón (resumida acá) y a los enganchados.
    void alFoto(const QJsonObject& foto);
    void alCerrar();
    void reintentar();

    std::shared_ptr<CanalSala> canal_;
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
    /// La foto entera que tiene la interfaz (la del último paquete tomado). Para leer UN
    /// dato a pedido; recorrerla en cada aviso es lo que no hay que hacer (ver paquete()).
    const QJsonObject& snapshot() const { return snap_; }
    /// El último paquete que tomó la interfaz: lo derivado de la foto (ResumenSala) y los
    /// cambios contra el anterior. Nulo antes de la primera foto.
    const std::shared_ptr<const sala::PaqueteSala>& paquete() const { return paquete_; }
    /// Lo derivado de la foto que tiene la interfaz (nunca nulo: vacío antes de la primera).
    const sala::ResumenSala& resumen() const;
    /// La misma foto, en nlohmann::json (armada en el lector: no se convierte de nuevo).
    std::shared_ptr<const nlohmann::json> fotoJson() const;
    /// La última foto de la sala que llegó por eventos, desde CUALQUIER hilo y sin I/O (la
    /// guarda el lector, bajo candado). Vacía si no hay conexión con B.
    static QJsonObject fotoSala();
    /// La misma, en nlohmann::json (armada una vez por foto en el lector); nula sin B.
    static std::shared_ptr<const nlohmann::json> fotoSalaJson();
    /// Sube con cada foto que guarda el lector (y al vaciarse): para no recorrer dos veces
    /// la misma (el vigía de las alarmas). Nunca 0 después de la primera.
    static std::uint64_t fotoSalaNumero();
    /// Cuántos lugares de invitado muestra la interfaz (las filas de `resumen().lugares`).
    void setLugares(int n);
    /// La URL de la foto de un participante, para las tarjetas de oradores (la llama el lector).
    void setAvatar(std::function<QString(const QString& id)> fn);
    /// Engancha `fn` en el HILO DEL LECTOR: corre con cada foto (ya resumida) y con nulo
    /// cuando se corta B. Para lo que no puede esperar a la interfaz; tiene que ser corto y
    /// seguro entre hilos (no tocar objetos de la interfaz). Devuelve con qué soltarlo
    /// (soltarAlLeerFoto espera a que termine una llamada en curso: no se llama desde `fn`).
    int alLeerFoto(std::function<void(const sala::ResumenSala*)> fn);
    void soltarAlLeerFoto(int id);
    /// Lo del buzón (depositadas, tomadas, descartadas): para el diagnóstico y los tests.
    sala::BuzonSala::Cuenta cuentaFotos() const { return canal_->buzon.cuenta(); }

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
    /// La interfaz tomó una foto nueva de la sala: cambiaron estas partes (sala::Parte). Lo
    /// derivado y los cambios, en paquete(). Cada oyente mira solo las partes que usa.
    void salaCambio(unsigned partes);
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
    /// proxima, foto, evento}); "prueba-sala" ({activa, directa, motivo?…}) y "prueba-sala-hd" ({id, ms}).
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
    /// Toma lo que espera en el buzón (la última foto) y avisa.
    void tomarFoto();
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
    std::shared_ptr<CanalSala> canal_ = std::make_shared<CanalSala>();
    std::shared_ptr<const sala::PaqueteSala> paquete_;
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
