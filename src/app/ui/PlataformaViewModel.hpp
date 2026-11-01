// ============================================================================
// src/app/ui/PlataformaViewModel.hpp — Lo común de YouTube, Twitch y Facebook
//
// Las tres páginas (y sus herramientas del MCP) hacen lo mismo con otra API: guardar la
// credencial de la app, conectar la cuenta (en el navegador o con un código), decir qué
// falta, listar los videos, terminar el vivo, leer el chat. Antes cada view model tenía su
// copia de lo común (ocupado, último error, refrescar al volver) y el MCP le hablaba al
// cliente HTTPS directo, así que la página quedaba vieja después de una escritura del
// asistente. Ahora:
//   - esta clase es la interfaz de una plataforma, con sus señales, y lo común;
//   - YouTubeViewModel, TwitchViewModel y FacebookViewModel la implementan;
//   - el MCP registra lo común con una sola fábrica (mcp/McpPlataformas.cpp) y todo pasa
//     por acá: ninguna herramienta toca el cliente.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace ba::app {

class PlataformaViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
   public:
    /// La respuesta de cada pedido (la misma de los tres clientes): `error` ya para una persona.
    using Done = std::function<void(bool ok, const QJsonObject& data, const QString& error)>;
    /// Lo que falta para usarla, en orden: la app (credencial), la cuenta, la Página (Facebook).
    enum class Falta { Nada, Credencial, Cuenta, Pagina };

    // --- Quién es -------------------------------------------------------------------------
    /// "youtube" | "twitch" | "facebook": el prefijo de sus herramientas, del bus y del formulario.
    virtual QString plataforma() const = 0;
    /// "YouTube" | "Twitch" | "Facebook": para el operador y los errores.
    virtual QString nombre() const = 0;

    // --- La cuenta ------------------------------------------------------------------------
    virtual bool configured() const = 0;
    virtual bool loggedIn() const = 0;
    virtual bool loginPending() const = 0;
    /// El nombre de la cuenta conectada (canal, usuario), "" si no hay.
    virtual QString cuentaNombre() const = 0;
    /// Lo primero que falta (Nada = lista para usar).
    virtual Falta falta() const;
    /// {configured, loggedIn, loginPending, account} y lo propio de la plataforma: lo que
    /// muestran accounts_status y account_check (sin secretos).
    virtual QJsonObject estadoCuenta() const;
    /// Todo lo que dice *_status (sin secretos).
    virtual QJsonObject estado() const = 0;
    /// Los permisos que pide al conectar (los mismos que se piden en el login).
    virtual QStringList permisos() const = 0;

    // --- Configurar y conectar ------------------------------------------------------------
    /// Guarda la app con los campos del formulario seguro (los nombres del esquema de
    /// *_setup). "" o el error. YouTube acepta además {"clientJson": contenido}.
    virtual QString guardarCredencial(const QVariantMap& campos) = 0;
    Q_INVOKABLE virtual void clearClient() = 0;
    /// Conectar: la página no espera la respuesta.
    Q_INVOKABLE void login() { login(Done{}); }
    /// El mismo login, con la respuesta (el MCP con `wait`): conectada (o el error), la
    /// cuenta ya releída. Lo que ve la página es lo mismo que con login().
    virtual void login(Done listo) = 0;
    /// Se conecta con un código que la persona confirma en otra página (Twitch): el código
    /// llega enseguida y avisa `codigoListo`.
    virtual bool loginConCodigo() const { return false; }
    virtual QString codigo() const { return {}; }
    virtual QString codigoUri() const { return {}; }
    /// Lo que acompaña a la cuenta al conectar con `wait` (Facebook: las Páginas).
    virtual QJsonObject alConectar() const { return {}; }
    Q_INVOKABLE virtual void cancelLogin() = 0;
    Q_INVOKABLE virtual void logout() = 0;
    Q_INVOKABLE virtual void refresh() = 0;

    // --- Videos, vivo y chat (el MCP; la página tiene los suyos) --------------------------
    /// Los videos del canal o de la Página, los más nuevos primero: {items: […]}.
    virtual void listarVideos(int max, Done done) = 0;
    /// Termina el vivo `id` ("" = el de la app) y corta la transmisión si iba ahí.
    virtual void terminarVivo(const QString& id, Done done) = 0;
    /// Mensajes del chat leídos, del más viejo al más nuevo.
    virtual const QJsonArray& chatMessages() const = 0;
    /// Los últimos `limit` de chatMessages().
    QJsonArray ultimosMensajes(int limit) const;

    // --- Lo común ---------------------------------------------------------------------------
    bool busy() const { return busy_ > 0; }
    QString lastError() const { return lastError_; }
    /// El error con el nombre de la plataforma adelante, una sola vez ("Twitch: …").
    QString conNombre(const QString& error) const;
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void copy(const QString& text);

   signals:
    /// Cambió algo de la cuenta o del vivo: para quien junta todo (la web del evento).
    void stateChanged();
    void busyChanged();
    void errorChanged();
    void chatChanged();
    /// Llegó el código del login (loginConCodigo): codigo() y codigoUri() ya lo tienen.
    void codigoListo();
    /// Aviso para el operador (mismo formato que el resto de la app).
    void notify(const QString& message, const QString& level);
    /// Arrancó un vivo desde acá: {plataforma, url, id, estudio?} (sin la clave).
    /// Va al bus como «salida/vivo» y queda en el histórico del evento.
    void vivo(const QVariantMap& datos);

   protected:
    explicit PlataformaViewModel(QObject* parent = nullptr);
    /// El último error (y el aviso al operador, con el nombre: conNombre).
    void setError(const QString& e);
    /// Un pedido más (o menos) en curso: `busy`.
    void track(bool start);
    /// `done` con la vuelta de la página: cuenta el pedido y, si salió, la relee (refresh)
    /// antes de contestar. Una sola regla para todas las escrituras, de la página o del MCP.
    Done alVolver(Done done);

   private:
    int busy_ = 0;
    QString lastError_;
};

} // namespace ba::app
