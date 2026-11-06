// ============================================================================
// src/app/mcp/InterfazMcp.hpp — Lo que el MCP usa de la interfaz (StudioViewModel)
//
// Las herramientas MCP usan los modelos de la interfaz (el cuerpo, las bancas, la sala, la
// ventanilla del motor…) que cuelgan de StudioViewModel. Antes incluían
// ui/StudioViewModel.hpp: tocarlo recompilaba los ~50 archivos del MCP (Ola 3 del diseño
// físico, docs/local/PLAN-DISENO-FISICO.md). Esta clase es lo que el MCP ve: un reenvío por
// cada cosa que usa, con el mismo nombre y la misma firma que en StudioViewModel (el código
// de las herramientas no cambia). Se define en ui/InterfazMcp.cpp (el único que ve
// StudioViewModel.hpp) y es hija de StudioViewModel: vive lo mismo que él (QPointer a esto
// se vacía junto con la interfaz). Las señales (notify, mostrarQr…) se emiten en
// StudioViewModel.
//
// No trae los ViewModels de área (ui/AreasStudio.hpp, Ola 4): los declara, y cada herramienta
// incluye el del área que usa (vm_.escenas() → ui/EscenasViewModel.hpp). Así tocar un
// ViewModel de área recompila las herramientas que lo usan, no el MCP entero.
// ============================================================================
#pragma once

#include <thread>
#include <atomic>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

#include <deque>
#include <map>
#include <memory>

namespace ba::app::agent {
class AlertEngine;
class EntradasAlarmas;
class AlarmaSonora;
class SubtitulosCC;
class Vocabulario;
class Notificador;
class AgentViewModel;
class Despachador;
class ModelosLocales;
class Roles;
class Voz;
class VozPrompter;
class FlujosTexto;
class Traductor;
class EstadoIA;
class Subtitulos;
class VocesRack;
class RotuloTraduccionConfig;
} // namespace ba::app::agent

namespace ba::app::audio {
class RackManager;
class Matriz;
class Mezclador;
class AudioViewModel;
class ObjetivoLoudness;
} // namespace ba::app::audio

namespace ba::app::ptz {
class ControlCamaras;
} // namespace ba::app::ptz
namespace ba::app::tiempos {
class TiemposModel;
} // namespace ba::app::tiempos
namespace ba::app::resolucion {
class ResolucionModel;
} // namespace ba::app::resolucion
namespace ba::app::video {
class VideoGrafos;
class SuperRes;
class BancoSuperRes;
} // namespace ba::app::video
namespace ba::app::sala {
class CupoSala;
class LugaresConGente;
class ConfigSala;
class Direcciones;
class VotacionSala;
class PreguntasSala;
class SeguridadSala;
}
namespace ba::app::escenas {
class Conmutador;
class Conmutadores;
class RespaldoEntradas;
class AjustesEscenas;
class ControlGrillas;
}
namespace ba::app::config {
class ModoVivoConfig;
class InterfazConfig;
}
namespace ba::app::emision {
class RetornosInvitados;
class SeguridadAire;
}
namespace ba::app::control {
class ReplicaPublicador;
class CuadrosWeb;
} // namespace ba::app::control
namespace ba::app::red {
class Red;
} // namespace ba::app::red
namespace ba::app::plugins {
class Plugins;
} // namespace ba::app::plugins
namespace ba::app::dispositivos {
class Dispositivos;
} // namespace ba::app::dispositivos

namespace ba::evento {
class Bitacora;
class FotosConfig;
}
namespace ba::app::control {
class BusEventos;
class Botonera;
class BotoneraGateway;
class ControlWeb;
class ControlWebGateway;
class CarasBotonera;
class SatelliteServidor;
class Dsk;
class Acciones;
}
namespace ba::app {
namespace medios {
class Playlist;
}
class PuestoCorreccion;
class AvisoFueraDeUi;
class AlarmasViewModel;
class AsambleaViewModel;
class BitacoraEvento;
class EnergiaAire;
namespace asamblea {
class CuerpoViewModel;
class Bancas;
class Debate;
class ConferenciaDcn;
class SesionRemota;
class Votaciones;
class Presidencia;
class SeguirMicrofono;
class OrdenDelDia;
class InformesResultado;
class Quorum;
}

class AppRuntime;
class VentanillaMotor;
class AccesoRemoto;
class ConferenceViewModel;
class JsonListModel;
class EventoViewModel;
class TemaEventoViewModel;
class PruebaSalaViewModel;
class PruebaAsambleaViewModel;
class EnlaceViewModel;
class CapacidadViewModel;
namespace sala {
class ClavesEvento;
class GuardadoEnEvento;
class QuienMira;
}
class LineaViewModel;
class AtencionViewModel;
class TotemsViewModel;
class HistoricoViewModel;
class GraficoViewModel;
class RotuladorViewModel;
class PrompterViewModel;
class ManualesViewModel;
class PantallasViewModel;
class CatalogoVisible;
class MonitoresModel;
class VentanasModel;
class MultiviewViewModel;
class EnlacesViewModel;
class QrViewModel;
class PersonasViewModel;
class Seguidor;
class PublicoViewModel;
class CarruselViewModel;
class MediosViewModel;
class RegistroViewModel;
class ExtensionesModel;
class RielConfig;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos
class CuentasViewModel;
class SystemViewModel;
class RecursosViewModel;
class Configuraciones;
class DocumentosViewModel;
class PdfEscena;
class OperatorMessages;
class InputsViewModel;
class ChatOverlayRenderer;
class FuenteQrPintor;
class DashboardModel;
class FrameService;
class MixerModel;
class RelojNiveles;
class PropertiesModel;
class ScenesModel;
class SettingsModel;
class SourcesModel;
class SourceTypesModel;
class StudioViewModel;
class VideoViewModel;

// Los ViewModels de área (ui/AreasStudio.hpp): declarados; quien usa uno incluye su header.
class AppViewModel;
class AsambleaViewModel;
class AsistenteViewModel;
class ControlViewModel;
class EmisionViewModel;
class EscenasViewModel;
class EscritorioViewModel;
class EstadoAireViewModel;
class ImagenViewModel;
class MezclaViewModel;
class MultiviewViewModel;
class SalaViewModel;
class SalidasViewModel;

class InterfazMcp : public QObject {
   public:
    /// Hija de `studio` (se destruye con él). Sin Q_OBJECT: no tiene señales ni propiedades;
    /// es QObject para que QPointer<InterfazMcp> se vacíe junto con la interfaz.
    explicit InterfazMcp(StudioViewModel& studio);
    ~InterfazMcp() override;

    // --- Lo de StudioViewModel que usa el MCP (ver StudioViewModel.hpp) ----------------
    void abrirPanel(const QString& pagina, const QString& panel);
    AccesoRemoto* accesoRemoto();
    control::Acciones& acciones();
    QObject* agent() const;
    QObject* agenteAutomatizacion() const;
    EstadoAireViewModel& aire();
    bool alAire() const;
    agent::AlertEngine& alerts();
    AppViewModel& app();
    AsambleaViewModel& asamblea();
    AsistenteViewModel& asistente();
    AtencionViewModel* atencionVm() const;
    QObject* audio() const;
    asamblea::Bancas& bancas();
    video::BancoSuperRes* bancoSr() const;
    control::Botonera& botonera();
    control::BotoneraGateway& botoneraGateway();
    control::BusEventos& bus();
    QObject* camaras() const;
    QString canvasLabel() const;
    CapacidadViewModel& capacidad();
    control::CarasBotonera& carasBotonera();
    QObject* carruseles() const;
    CatalogoVisible* catalogoVisible() const;
    agent::SubtitulosCC& cc();
    QObject* conference() const;
    sala::ConfigSala& configSala();
    Configuraciones* configuraciones();
    escenas::Conmutador& conmutador();
    escenas::Conmutadores& conmutadores();
    ControlViewModel& control();
    control::ControlWeb& controlWeb();
    control::ControlWebGateway& controlWebGateway();
    double cpuPercent() const;
    CuentasViewModel& cuentas();
    asamblea::CuerpoViewModel& cuerpo();
    sala::CupoSala& cupoSala();
    QString currentScene() const;
    QObject* dashboard() const;
    std::filesystem::path datosRaiz() const;
    asamblea::ConferenciaDcn& dcn();
    asamblea::Debate* debate();
    agent::Despachador& despachador();
    sala::Direcciones& direcciones();
    DocumentosViewModel* documentos() const;
    EmisionViewModel& emision();
    graficos::Emisor* emisor();
    EnergiaAire* energia() const;
    EnlaceViewModel& enlace();
    EnlacesViewModel* enlacesVm() const;
    dispositivos::Dispositivos* equipos() const;
    EscenasViewModel& escenas();
    EscritorioViewModel& escritorio();
    agent::EstadoIA* estadoIAModelo() const;
    QObject* evento() const;
    QObject* extensiones() const;
    QObject* facebook() const;
    QObject* flujos() const;
    QObject* frames() const;
    bool gatewayAlive() const;
    escenas::ControlGrillas* grillasPersonas();
    // El estado de la medición de fluidez (ui/Fluidez), o nullptr si todavía no arrancó.
    const nlohmann::json* fluidezEstado();
    HistoricoViewModel* historico() const;
    asamblea::InformesResultado* informes();
    QObject* inputs() const;
    QObject* matriz() const;
    QObject* medios() const;
    QObject* messages() const;
    MezclaViewModel& mezcla();
    QObject* mezcladorQml() const;
    QObject* mixer() const;
    agent::ModelosLocales* modelosLocales() const;
    bool modoVivo() const;
    MonitoresModel* monitoresModelo() const;
    void mostrarQr(const QString& texto, const QString& descripcion, const QString& mensaje, const QString& color);
    void mostrarSeccion(const QString& pagina, const QString& seccion);
    MultiviewViewModel& multivistas();
    agent::Notificador& notificador();
    void notify(const QString& message, const QString& level);
    audio::ObjetivoLoudness& objetivoLoudness();
    asamblea::OrdenDelDia& ordenDelDia();
    QObject* pantallas() const;
    PdfEscena* pdfEscena() const;
    QObject* personas() const;
    medios::Playlist& playlist();
    plugins::Plugins* plugins() const;
    sala::PreguntasSala& preguntas();
    asamblea::Presidencia* presidencia();
    double programPeakL() const;
    double programPeakR() const;
    QObject* prompter() const;
    PruebaAsambleaViewModel& pruebaAsamblea();
    PruebaSalaViewModel& pruebaSala();
    QObject* qr() const;
    void quitRequested();
    asamblea::Quorum& quorum();
    audio::RackManager* racks() const;
    RecursosViewModel* recursos() const;
    red::Red* redEquipos() const;
    resolucion::ResolucionModel* resolucionModel() const;
    escenas::RespaldoEntradas& respaldoEntradas();
    emision::RetornosInvitados& retornos();
    QObject* riel() const;
    AppRuntime& runtime();
    SalaViewModel& sala();
    SalidasViewModel& salidas();
    control::SatelliteServidor& satellite();
    Seguidor* seguidorPtz() const;
    asamblea::SeguirMicrofono& seguirMicrofono();
    sala::SeguridadSala& seguridad();
    emision::SeguridadAire& seguridadAire();
    asamblea::SesionRemota* sesionRemota();
    void setSeccionConfig(const QString& s);
    QObject* settings() const;
    QStringList sinSenal() const;
    bool streamKeyAvailable() const;
    QString streamServer() const;
    video::SuperRes* superresVideo() const;
    QObject* system() const;
    QObject* temaEvento() const;
    tiempos::TiemposModel* tiemposModel() const;
    TotemsViewModel* totemsVm() const;
    agent::Traductor* traductorAgent() const;
    QObject* twitch() const;
    VentanasModel* ventanasModelo() const;
    VentanillaMotor& ventanilla();
    video::VideoGrafos* videoGrafos() const;
    VideoViewModel* videoVista() const;
    graficos::Vinculos* vinculos();
    agent::Vocabulario& vocabulario();
    QObject* voces() const;
    sala::VotacionSala& votacion();
    asamblea::Votaciones* votaciones();
    QObject* voz() const;
    QObject* vozPrompter() const;
    QObject* youtube() const;

   private:
    StudioViewModel& studio_;
};

} // namespace ba::app
