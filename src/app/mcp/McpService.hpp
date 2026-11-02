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

#include <atomic>

#include <filesystem>

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "mcp/McpCore.hpp"
#include "mcp/McpFoto.hpp"
#include "mcp/McpMando.hpp"

class QQuickWindow;
class QThreadPool;

namespace ba::app {
class AppRuntime;
class PlanoControl;
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
    /// El plano de control (F3-03): el transporte vive en su hilo y cada herramienta
    /// corre donde declara (McpCore::Ejecutores). Antes de start(). Sin él, todo en el
    /// hilo de la interfaz, como antes.
    void setPlano(PlanoControl& plano);
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
    void registerPruebaSalaTools(); ///< prueba_sala_* (McpPruebaSalaTools.cpp)
    void registerEnlaceTools();     ///< enlace_* (McpEnlaceTools.cpp)
    void registerCapacidadTools();  ///< capacidad_* (McpCapacidadTools.cpp)
    void registerUiTools();
    /// Lo común de YouTube, Twitch y Facebook (la fábrica de McpPlataformas) y lo propio de cada una.
    void registerPlataformaTools();
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
    void registerProcedimientoTools(); // evento_preparar, evento_verificar
    void registerAgenteTools();
    void registerModeloTools();
    void registerTraduccionTools();
    void registerMatrizTools();
    void registerCalibracionTools();
    void registerPtzTools();
    void registerVideoTools();
    void registerTiemposTools();
    void registerRedTools();
    void registerPluginTools();
    void registerDcnTools();
    void registerSesionTools();
    void registerAsambleaTools();
    void registerConfiguracionesTools();
    void registerDocumentosTools();
    void registerEquiposTools();
    void registerDriversTools(); ///< drivers_listar, driver_probar, drivers_revisar (McpEquiposTools.cpp)
    void registerPrompterTools();
    void registerPantallaTools();
    void registerLaminaTools();
    void registerQrTools();
    void registerGrillaTools();
    void registerVentanaTools();
    void registerRostroTools();
    void registerAtencionTools();
    void registerTotemTools(); // totem_*: el registro de personas por tótem
    void registerControlTools();
    void registerSalidasTools(); ///< salidas_listar, salida_crear/editar/borrar (McpSalidasTools.cpp)
    /// Marcas de la salvaguarda (contenido no confiable, herramientas mediadas)
    /// y la aprobacion en la barra del operador.
    void applySecurity();
    /// Lo que leen las herramientas de la foto (McpFoto.hpp): cada una, segura entre hilos.
    FuentesDeFoto fuentesDeFoto();
    /// Lo que hacen las herramientas de aire del plano de control (McpMando.hpp): los
    /// conmutadores, la seguridad del aire y el runtime, con el turno del motor.
    MandoDeAire mandoDeAire();
    void applyApprovalPolicy();

   public:
    /// Prompt del sistema para el agente de la pestaña Asistente (markdown).
    QString agentPrompt() const;

   private:
    AppRuntime& rt_;
    StudioViewModel& vm_;
    McpCore core_;
    std::unique_ptr<McpHttp> http_;
    PlanoControl* plano_ = nullptr;
    std::unique_ptr<QThreadPool> pool_; ///< las herramientas de la foto (Hilo::Foto)
    std::unique_ptr<QThreadPool> poolSinInterfaz_; ///< las que escriben o tardan sin la interfaz (Hilo::SinInterfaz)
    std::unique_ptr<SettingsModel> settings_; ///< propio: borrador independiente
    std::function<QQuickWindow*()> mainWindow_;
    /// ¿Hay alguien en la sala? Lo pone la interfaz cuando cambia la sala (la copia de
    /// ConferenceClient avisa con snapshotChanged) y lo lee el núcleo desde cualquier
    /// hilo (McpCore::setHayGente, Tool::untrustedConGente).
    std::atomic<bool> hayGente_{false};
    /// Hay un guardado armado que junta los cambios del MCP (ver setSincronizador en el .cpp).
    std::shared_ptr<std::atomic<bool>> guardadoPendiente_ = std::make_shared<std::atomic<bool>>(false);
};

// --- Utilidades compartidas por los archivos de herramientas ---------------------

/// QImage -> base64 (jpeg o png), achicada a `maxWidth` si hace falta.
std::string encodeImage(const QImage& img, const std::string& format, int maxWidth, int quality,
                        std::string& mime);
/// Guarda una captura en Runtime/capturas/ y devuelve su ruta (vacía si no se
/// pudo). Es a donde van a parar las imágenes que NO se mandan por la
/// conversación; ver el comentario de `puedeVerImagenes` en el .cpp.
QString guardarCaptura(const QImage& img, const std::filesystem::path& raiz, const json& args,
                       int defaultWidth);

/// Igual, pero SOLO le manda la imagen a quien puede recibirla (el rol que
/// mira). A los demás les deja el archivo y les dice que usen `imagen_preguntar`.
/// Ver el comentario de `puedeVerImagenes` en el .cpp: no es una preferencia,
/// es que una imagen de más cuelga la sesión del que está operando.
void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth, McpCore& core,
              const std::filesystem::path& raiz);
/// A quién va la imagen, leído AL ENTRAR a la herramienta: una que contesta después
/// (un pedido a B, al renderizador) ya no tiene el contexto de la llamada
/// (McpCore::contextoEnCurso) cuando llega la imagen.
struct DestinoImagen {
    bool ver = false; ///< el rol que mira (puedeVerImagenes): la imagen en la conversación
    std::filesystem::path raiz;
};
DestinoImagen destinoImagen(McpCore& core, const std::filesystem::path& raiz);
void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth, const DestinoImagen& destino);
/// Rectangulo en fracciones del lienzo a partir de un preset o {x,y,w,h}.
/// :returns: false (con `err`) si no se entiende.
/// `{x,y,w,h}` en fracciones del lienzo (o un preset). Si vino en píxeles (algún
/// valor > 4) lo convierte con el lienzo y lo cuenta en `nota`.
bool parseLayout(const json& v, float& x, float& y, float& w, float& h, std::string& err,
                 std::string* nota = nullptr);

} // namespace ba::app::mcp
