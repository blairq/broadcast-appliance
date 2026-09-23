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
// Alertas: cada disparo de una vigilancia (AlertEngine) con "llm" se escribe
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

#include "JsonSignals.hpp"

#include <atomic>
#include <functional>
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
    bool running() const { return session_->running(); }
    bool mcpReady() const { return mcpReady_ ? mcpReady_() : false; }
    QString lastError() const { return lastError_; }
    QVariantList alerts() const;
    QVariantList events() const;

    Q_INVOKABLE void setProvider(const QString& id);
    Q_INVOKABLE void setModel(const QString& model);
    Q_INVOKABLE QString saveKey(const QString& key);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void setAlertsToChat(bool on);
    Q_INVOKABLE void setAutoStart(bool on);
    Q_INVOKABLE void setFontSize(int px);
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    /// Escribe un pedido en el prompt del agente y lo manda.
    Q_INVOKABLE void send(const QString& text);
    Q_INVOKABLE void setAlertEnabled(const QString& id, bool on);
    Q_INVOKABLE void deleteAlert(const QString& id);
    Q_INVOKABLE void openAgentsFolder() const;

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
    void runningChanged();
    void mcpReadyChanged();
    void errorChanged();
    void alertsChanged();
    void notify(const QString& message, const QString& level);

   private:
    void loadConfig();
    void saveConfig() const;
    void setError(const QString& e);
    void onAlert(const nlohmann::json& ev);
    void flushAlerts();
    QString runtimeOf(const QString& provider) const;
    /// Espacio de trabajo en `ws` para ese proveedor: instrucciones y la
    /// configuracion del MCP (con los argumentos del puente).
    QString prepareWorkspaceEn(const QString& ws, const QString& proveedor, const QString& modelo,
                               const QStringList& bridgeArgs, const QString& prompt, QString* error);
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
    nlohmann::json pins() const;

    AppRuntime& rt_;
    AlertEngine& alerts_;
    std::unique_ptr<term::TerminalSession> session_;
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
    QString lastError_;
    std::function<QString()> systemPrompt_;
    std::function<bool()> mcpReady_;
    QStringList pendingAlerts_;
    bool flushScheduled_ = false;
    bool expectedStop_ = false; ///< lo detuvimos nosotros: no es un error
    std::function<bool(const nlohmann::json&)> reclamado_;
};

} // namespace ba::app::agent
