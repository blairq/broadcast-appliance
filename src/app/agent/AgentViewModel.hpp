// ============================================================================
// src/app/agent/AgentViewModel.hpp — Pestaña Asistente: chat con el LLM que
// maneja la app por MCP
//
// La pestaña corre, en una terminal propia (TerminalSession + TerminalView),
// la interfaz OFICIAL del agente elegido, ya conectada al servidor MCP de la
// app (puente stdio ba-mcp: sin token en ningun archivo) y con el prompt del
// sistema de Broadcast Appliance:
//
//   claude-cuenta   Claude Code con la suscripcion Pro/Max (login en la TUI)
//   claude-api      Claude Code con clave de API de Anthropic
//   gemini-cuenta   Gemini CLI con la cuenta de Google (login en la TUI)
//   gemini-api      Gemini CLI con clave de API de Google AI Studio
//   opencode-go     opencode con la suscripcion OpenCode Go (/connect)
//   openrouter      opencode con clave de OpenRouter
//
// Los agentes se instalan desde la pestaña (version FIJADA y SHA-256, ver
// agentes.json) en <sistema>/Agentes/, y todo lo que guardan (sesiones,
// logins, historial) va a Credenciales/agentes/: portable, fuera del perfil
// del usuario, y a salvo de las actualizaciones. Las claves de API, cifradas
// en Credenciales/ y solo en el entorno del proceso del agente.
//
// Alertas: cada disparo de un disparador (AlertEngine) con "llm" se escribe
// en el prompt del agente (agrupadas si llegan juntas) y lo despierta... salvo
// que un ROL activo lo reclame (ver Roles/Despachador): entonces lo atiende un
// agente de un solo tiro y esta conversacion no se entera.
//
// Esta clase tambien sabe LANZAR agentes: el interactivo (una terminal) y los
// de un tiro (`launchOptionsHeadless`: -p, salida en JSON, sin consola), con
// el mismo motor instalado, las mismas credenciales y el puente ba-mcp con
// `--rol`. Es el unico lugar que conoce a cada motor.
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "comun/JsonSignals.hpp"

#include <atomic>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <thread>

#include "agent/Lanzamiento.hpp"
#include "term/TerminalSession.hpp"

namespace ba::app {
class AppRuntime;
}

namespace ba::app::agent {

class AlertEngine;

class AgentViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* session READ session CONSTANT)
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(QString provider READ provider NOTIFY configChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY configChanged)
    Q_PROPERTY(QString model READ model NOTIFY configChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY configChanged)
    Q_PROPERTY(bool alertsToChat READ alertsToChat NOTIFY configChanged)
    Q_PROPERTY(bool autoStart READ autoStart NOTIFY configChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY configChanged)
    Q_PROPERTY(bool installed READ installed NOTIFY installChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY installChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY installChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
    Q_PROPERTY(double installProgress READ installProgress NOTIFY installChanged)
    Q_PROPERTY(QString installText READ installText NOTIFY installChanged)
    // Actualizar desde la fuente oficial (sin esperar una version nueva del
    // sistema). Ver checkUpdate().
    Q_PROPERTY(bool checkingUpdate READ checkingUpdate NOTIFY latestChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestChanged)
    Q_PROPERTY(bool newerAvailable READ newerAvailable NOTIFY latestChanged)
    Q_PROPERTY(bool usingChosenVersion READ usingChosenVersion NOTIFY configChanged)
    Q_PROPERTY(QString pinnedVersion READ pinnedVersion NOTIFY configChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    /// OJO CON LA SEÑAL. Esto avisaba por `runningChanged`, que es de otra
    /// cosa: el servidor MCP arranca DESPUES de que la interfaz se dibuja, asi
    /// que el enlace de QML se evaluaba en false y se quedaba ahi hasta que el
    /// agente arrancara o parara. Si nunca arrancaba —porque falta la clave,
    /// por ejemplo— el cartel de «el servidor MCP esta apagado» quedaba puesto
    /// con el MCP andando. Un estado pegado se ve exactamente igual que uno
    /// real, y manda a buscar donde no es.
    Q_PROPERTY(bool mcpReady READ mcpReady NOTIFY mcpReadyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantList alerts READ alerts NOTIFY alertsChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY alertsChanged)
    /// LIMPIEZA: por cada motor de agente (claude, gemini, opencode) lo que ocupa
    /// en el disco: [{runtime, versiones: [{version, bytes, enUso}], bytesPrograma,
    /// bytesChats, bytesPerfil, enUso}]. Los chats se acumulan en su perfil
    /// (Credenciales/agentes/<motor>), al lado del login. Se recalcula con
    /// `revisarLimpieza`.
    Q_PROPERTY(QVariantList limpieza READ limpieza NOTIFY limpiezaChanged)
   public:
    AgentViewModel(AppRuntime& rt, AlertEngine& alerts, QObject* parent = nullptr);
    ~AgentViewModel() override;

    QObject* session() const { return session_.get(); }
    QVariantList providers() const;
    QString provider() const { return provider_; }
    QVariantMap current() const;
    QString model() const { return model_; }
    bool hasKey() const;
    bool alertsToChat() const { return alertsToChat_; }
    bool autoStart() const { return autoStart_; }
    int fontSize() const { return fontSize_; }
    bool installed() const;
    /// Hay un agente usable pero de una version distinta a la que fija esta
    /// version del sistema: se puede seguir usando y se ofrece actualizarlo.
    bool updateAvailable() const;
    /// Version instalada que se va a usar ("" si no hay ninguna).
    QString installedVersion() const;
    bool installing() const { return installing_; }
    double installProgress() const { return installProgress_; }
    QString installText() const { return installText_; }
    bool checkingUpdate() const { return checking_; }
    /// La ultima version publicada del agente actual, si ya se consulto ("" si no).
    QString latestVersion() const;
    /// La publicada es mas nueva que la que se usa.
    bool newerAvailable() const;
    /// Se usa una version elegida desde la pestaña (Agentes/versiones.json) y
    /// no la que fija esta version del sistema.
    bool usingChosenVersion() const;
    /// La que fija esta version del sistema (agentes.json, compilado).
    QString pinnedVersion() const;
    bool running() const { return localActivo_ || session_->running(); }
    /// Un asistente GRANDE andando (no el modelo local): a quien derivar.
    bool asistenteGrande() const { return !localActivo_ && session_->running(); }
    /// El asistente es el modelo local y está «andando» (lo que le llega va a Voz).
    bool asistenteLocal() const { return localActivo_; }
    /// El MODELO LOCAL como motor (proveedor «local»): lo engancha StudioViewModel
    /// con agent/Voz. `rol`: quién pide ("asistente" o un rol); `disparador`:
    /// viene de un disparador (se dice en voz alta y nunca se hace solo).
    using Local = std::function<void(const QString& rol, const QString& texto, bool disparador)>;
    void setLocal(Local l) { local_ = std::move(l); }
    bool mcpReady() const { return mcpReady_ ? mcpReady_() : false; }
    QString lastError() const { return lastError_; }
    QVariantList alerts() const;
    QVariantList events() const;

    Q_INVOKABLE void setProvider(const QString& id);
    Q_INVOKABLE void setModel(const QString& model);
    Q_INVOKABLE QString saveKey(const QString& key);

    /// La configuración del modelo propio, para editarla desde la pestaña:
    /// {url, modelo, canal, nombre, extra (texto JSON), instrucciones, piensa}.
    /// Vacía si no hay ninguno configurado todavía.
    Q_INVOKABLE QVariantMap modeloPropio() const;
    /// La guarda. Devuelve "" si salió bien, o el motivo —por ejemplo que los
    /// parámetros extra no sean un JSON válido—.
    /// Le pregunta al servidor propio si está ahí y si la clave sirve.
    ///
    /// Contesta por `notify` y no devolviendo: sale por la red, y bloquear la
    /// interfaz hasta que un servidor que puede no existir conteste es
    /// exactamente lo que no hay que hacer en un aparato que está al aire.
    Q_INVOKABLE void probarModeloPropio();

    Q_INVOKABLE QString guardarModeloPropio(const QString& url, const QString& modelo, const QString& nombre,
                                            const QString& extraJson, const QString& instrucciones,
                                            bool piensa);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void setAlertsToChat(bool on);
    Q_INVOKABLE void setAutoStart(bool on);
    Q_INVOKABLE void setFontSize(int px);
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancelInstall();
    /// Pregunta a la fuente OFICIAL del agente actual cual es su ultima
    /// version, con la URL y el SHA-256 (o SHA-512 de npm) que publica para
    /// cada plataforma. No instala nada.
    Q_INVOKABLE void checkUpdate();
    /// Instala la version que trajo checkUpdate(): queda elegida en
    /// Agentes/versiones.json (dentro del bundle) y se verifica igual que la
    /// fijada. La instalacion anterior sigue andando hasta que la nueva esta.
    Q_INVOKABLE void installLatest();
    /// Vuelve a la version que fija esta version del sistema.
    Q_INVOKABLE void revertToPinned();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    /// Guardas de aire: con la transmisión o la grabación andando, el asistente
    /// interactivo corre SIN consola (Bash/PowerShell); si estaba con consola,
    /// se relanza sin ella.
    void setHayAire(std::function<bool()> fn);
    /// La consola del asistente está apagada por el aire (para el candado de la barra).
    Q_INVOKABLE bool consolaApagadaPorAire() const { return hayAire_ && hayAire_(); }
    /// Las herramientas de aire (para la sección Seguridad): {hay_aire, herramientas}.
    void setAireInfo(std::function<QVariantMap()> fn) { aireInfo_ = std::move(fn); }
    Q_INVOKABLE QVariantMap aireUi() const { return aireInfo_ ? aireInfo_() : QVariantMap{}; }
    /// Escribe un pedido en el prompt del agente y lo manda.
    Q_INVOKABLE void send(const QString& text);
    Q_INVOKABLE void setAlertEnabled(const QString& id, bool on);
    Q_INVOKABLE void deleteAlert(const QString& id);
    /// Sin sonido (sus alarmas siguen apareciendo).
    Q_INVOKABLE void setAlertSilenciada(const QString& id, bool silenciada);
    /// Los tipos de disparador con sus parámetros (para el editor).
    Q_INVOKABLE QVariantList alertTypesUi() const;
    /// Crea un disparador desde el editor ({type, name, params, sonido, reconocer, notificar, actions}). "" o el error.
    Q_INVOKABLE QString createAlertUi(const QVariantMap& spec);
    Q_INVOKABLE void openAgentsFolder() const;
    /// Control de Windows (agent/ControlWindows): {habilitado, preguntar, permisos{}, capacidades[]}.
    Q_INVOKABLE QVariantMap controlWindows() const;
    /// Aplica {habilitado?, preguntar?, permisos?{}} y guarda. "" o el error.
    Q_INVOKABLE QString setControlWindows(const QVariantMap& cambios);

    // --- Limpieza -----------------------------------------------------------------
    QVariantList limpieza() const { return limpieza_; }
    Q_INVOKABLE void revisarLimpieza();
    /// Borra los chats guardados de un motor ("" = de todos, y los espacios de
    /// trabajo de Runtime/agente*). El login y los ajustes quedan.
    /// {ok, bytes, error}. No con un agente corriendo.
    Q_INVOKABLE QVariantMap borrarChats(const QString& runtime);
    /// Borra las versiones del programa que no se usan (quedan de actualizaciones).
    Q_INVOKABLE QVariantMap borrarVersionesViejas(const QString& runtime);
    /// Si algo falla: borra el programa del motor y lo vuelve a bajar. Con
    /// `deCero`, además el perfil entero (chats, ajustes y el LOGIN: hay que volver
    /// a entrar; las claves de API pegadas quedan). "" si arrancó, o el motivo.
    Q_INVOKABLE QString reinstalar(const QString& runtime, bool deCero);
    /// Cuántos encargos de roles corren (para no borrar debajo de ellos). Lo pone la app.
    void setCorridasEnCurso(std::function<int()> fn) { corridasEnCurso_ = std::move(fn); }

    /// Nota del sistema al agente (respuesta del operador, etc.): va a su chat.
    void deliverNote(const QString& text);

    /// Lo conecta GuiMode: prompt del sistema y si el servidor MCP esta arriba.
    void setSystemPrompt(std::function<QString()> fn) { systemPrompt_ = std::move(fn); }
    void setMcpReady(std::function<bool()> fn) {
        mcpReady_ = std::move(fn);
        emit mcpReadyChanged();
    }
    /// Lo llama quien prende o apaga el servidor MCP.
    void avisarMcpCambio() { emit mcpReadyChanged(); }
    /// Ruta del puente ba-mcp (junto a ba-app).
    QString bridgePath() const;
    /// ba-control: el servidor MCP del escritorio de Windows (src/app/escritorio).
    QString controlPath() const;
    /// Carpeta de trabajo del agente con su configuracion (para los tests).
    QString prepareWorkspace(QString* error);
    /// Programa, argumentos y entorno con los que se lanza (para los tests).
    term::Pty::Options launchOptions(QString* error);

    /// Un agente de UN SOLO TIRO para un rol: recibe el encargo como
    /// argumento, escribe eventos JSON por stdout (uno por linea) y termina.
    /// Sin consola ni edicion de archivos: solo el MCP, con `--rol`.
    term::Pty::Options launchOptionsHeadless(const Lanzamiento& l, QString* error);
    /// Lo que el Despachador necesita de esta clase.
    Motores motores();
    /// "claude" | "gemini" | "opencode" | "" (proveedor desconocido).
    static QString runtimeDe(const QString& proveedor);
    /// Los modelos que ofrece un proveedor (vacio: se escribe a mano).
    static QStringList modelosDe(const QString& proveedor);
    /// El Despachador decide si un evento lo atiende un rol: si dice que si,
    /// esta conversacion no lo recibe.
    void setReclamado(std::function<bool(const nlohmann::json&)> fn) { reclamado_ = std::move(fn); }

   signals:
    void configChanged();
    void installChanged();
    void latestChanged();
    void runningChanged();
    void mcpReadyChanged();
    void errorChanged();
    void alertsChanged();
    void limpiezaChanged();
    void notify(const QString& message, const QString& level);

   private:
    QVariantList limpieza_;
    std::function<int()> corridasEnCurso_;
    /// "" si se puede tocar el disco de los agentes; si no, por qué no.
    QString ocupadoParaLimpiar() const;
    void loadConfig();
    void saveConfig() const;
    void setError(const QString& e);
    void onAlert(const nlohmann::json& ev);
    void flushAlerts();
    QString runtimeOf(const QString& provider) const;
    /// Espacio de trabajo en `ws` para ese proveedor: instrucciones y la
    /// configuracion del MCP (con los argumentos del puente).
    QString prepareWorkspaceEn(const QString& ws, const QString& proveedor, const QString& modelo,
                               const QStringList& bridgeArgs, const QString& prompt, QString* error,
                               bool consola, bool conControl = false);
    /// Programa, entorno y credenciales de un proveedor, sin los argumentos
    /// del modo (interactivo o de un tiro): eso lo agrega quien la llama.
    term::Pty::Options baseLaunch(const QString& proveedor, const QString& modelo, const QString& ws,
                                  QString* error);
    /// Carpeta DESTINO de una instalacion nueva: la version que fija esta
    /// version del sistema.
    QString installDir(const QString& runtime) const;
    /// Carpeta de la instalacion que se va a USAR: la fijada si esta, si no la
    /// mas nueva que haya quedado de antes. Vacia si no hay ninguna con `.ok`.
    /// En `exeOut` deja el ejecutable que registro esa instalacion.
    QString usableDir(const QString& runtime, QString* exeOut = nullptr) const;
    QString platformKey() const;
    /// La entrada de `plataformas` para esta maquina: la variante "-baseline"
    /// si el procesador no tiene AVX2 y el agente la publica.
    nlohmann::json platformPin(const nlohmann::json& pin) const;
    /// Las versiones de agentes.json, con las elegidas desde la pestaña
    /// (Agentes/versiones.json) encima.
    nlohmann::json pins() const;
    /// Solo las de agentes.json, compiladas.
    nlohmann::json pinnedPins() const;
    QString chosenFile() const;
    /// La entrada nueva de `runtime` armada con lo que publica su fuente
    /// oficial. Corre en otro hilo (bloquea en la red). Vacia + `err` si falla.
    static nlohmann::json fetchLatest(const QString& runtime, const nlohmann::json& base, QString* err);

    AppRuntime& rt_;
    AlertEngine& alerts_;
    std::unique_ptr<term::TerminalSession> session_;
    Local local_;
    bool localActivo_ = false; ///< el asistente es el modelo local y está «andando»
    QString provider_ = "claude-cuenta";
    QString model_;
    bool alertsToChat_ = true;
    bool autoStart_ = false;
    int fontSize_ = 15;
    bool installing_ = false;
    double installProgress_ = 0;
    QString installText_;
    std::atomic<bool> cancelInstall_{false};
    std::thread installThread_;
    bool checking_ = false;
    std::thread checkThread_;
    std::map<QString, nlohmann::json> latest_; ///< runtime -> entrada nueva
    QString lastError_;
    std::function<QString()> systemPrompt_;
    std::function<bool()> mcpReady_;
    QStringList pendingAlerts_;
    bool flushScheduled_ = false;
    bool expectedStop_ = false; ///< lo detuvimos nosotros: no es un error
    std::function<bool(const nlohmann::json&)> reclamado_;
    std::function<bool()> hayAire_;
    bool conConsola_ = false; ///< el interactivo arrancó con Bash/PowerShell
    QTimer* vigiaAire_ = nullptr;
    std::function<QVariantMap()> aireInfo_;
};

} // namespace ba::app::agent
