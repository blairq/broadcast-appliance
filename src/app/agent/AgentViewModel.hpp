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
// en el prompt del agente (agrupadas si llegan juntas) y lo despierta.
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
    Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
    Q_PROPERTY(double installProgress READ installProgress NOTIFY installChanged)
    Q_PROPERTY(QString installText READ installText NOTIFY installChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool mcpReady READ mcpReady NOTIFY runningChanged)
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
    void setMcpReady(std::function<bool()> fn) { mcpReady_ = std::move(fn); }
    /// Ruta del puente ba-mcp (junto a ba-app).
    QString bridgePath() const;
    /// Carpeta de trabajo del agente con su configuracion (para los tests).
    QString prepareWorkspace(QString* error);
    /// Programa, argumentos y entorno con los que se lanza (para los tests).
    term::Pty::Options launchOptions(QString* error);

   signals:
    void configChanged();
    void installChanged();
    void runningChanged();
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
    QString installDir(const QString& runtime) const;
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
};

} // namespace ba::app::agent
