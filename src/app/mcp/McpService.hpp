// ============================================================================
// src/app/mcp/McpService.hpp — Servidor MCP del appliance (vive en el Proceso A)
//
// A es el unico proceso que tiene TODO a mano: el motor (escenas, fuentes,
// audio, salidas), la configuracion, la interfaz (ventanas, dashboard) y el
// cliente de la API de la sala (B). Por eso el servidor MCP corre aca, en el
// mismo event loop que la vista: cada herramienta usa las mismas capas que la
// interfaz (y la interfaz se actualiza sola con lo que haga un asistente).
//
// Conexion:
//   - HTTP: http://127.0.0.1:<puerto>/mcp con `Authorization: Bearer <token>`
//     (credencial mcp-token, cifrada en Credenciales/; `ba-mcp --token` la muestra). Puerto: ajuste mcp.port
//     (8790 por defecto).
//   - stdio: el binario `ba-mcp` (puente) lee Runtime/mcp.json (dentro del bundle) y reenvia.
//
// Herramientas: ver McpTools.cpp (registerTools) y la ayuda (McpHelp.cpp).
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "mcp/McpCore.hpp"

class QQuickWindow;

namespace ba::app {
class AppRuntime;
class StudioViewModel;
class PropertiesModel;
class SettingsModel;
} // namespace ba::app

namespace ba::app::mcp {

class McpHttp;

class McpService : public QObject {
    Q_OBJECT
   public:
    McpService(AppRuntime& rt, StudioViewModel& vm, QObject* parent = nullptr);
    ~McpService() override;

    /// Ventana principal (capturas y navegacion). Antes de start().
    void setMainWindow(std::function<QQuickWindow*()> fn) { mainWindow_ = std::move(fn); }
    /// :param port: 0 = efimero. :returns: false si no pudo escuchar.
    bool start(quint16 port);
    quint16 port() const;
    QString url() const;
    QString token() const;

    McpCore& core() { return core_; }

   private:
    void registerHelp();
    void registerTools();
    void registerImageTools();
    void registerRoomTools();
    void registerUiTools();
    void registerYouTubeTools();
    void registerTwitchTools();
    void registerFacebookTools();
    void registerAlertTools();
    void registerAssistantTools();
    void registerSystemTools();
    void registerInputTools();
    void registerMediaTools();
    void registerOperatorTools();
    void registerAccountTools();
    void registerAudioTools();
    void registerGraficoTools();
    void registerRegistroTools();
    void registerRecetaTools();
    void registerAgenteTools();
    void registerPrompterTools();
    void registerPantallaTools();
    void registerQrTools();
    void registerVentanaTools();
    void registerRostroTools();
    /// Marcas de la salvaguarda (contenido no confiable, herramientas mediadas)
    /// y la aprobacion en la barra del operador.
    void applySecurity();
    void applyApprovalPolicy();
    bool roomHasUntrusted();

   public:
    /// Prompt del sistema para el agente de la pestaña Asistente (markdown).
    QString agentPrompt() const;

   private:
    AppRuntime& rt_;
    StudioViewModel& vm_;
    McpCore core_;
    std::unique_ptr<McpHttp> http_;
    std::unique_ptr<PropertiesModel> props_;  ///< propio: no toca el panel de la vista
    std::unique_ptr<SettingsModel> settings_; ///< propio: borrador independiente
    std::function<QQuickWindow*()> mainWindow_;
};

// --- Utilidades compartidas por los archivos de herramientas ---------------------

/// QImage -> base64 (jpeg o png), achicada a `maxWidth` si hace falta.
std::string encodeImage(const QImage& img, const std::string& format, int maxWidth, int quality,
                        std::string& mime);
/// Agrega la imagen al resultado (+ una linea con su tamaño).
void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth);
/// Rectangulo en fracciones del lienzo a partir de un preset o {x,y,w,h}.
/// :returns: false (con `err`) si no se entiende.
bool parseLayout(const json& v, float& x, float& y, float& w, float& h, std::string& err);
/// "m:ss", "90", "90s", "1.5m" -> segundos (0 = invalido).
int parseSeconds(const json& v);

} // namespace ba::app::mcp
