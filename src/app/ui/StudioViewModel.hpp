// ============================================================================
// src/app/ui/StudioViewModel.hpp — Fachada de la interfaz (capa ViewModel)
//
// Unico punto de entrada de la vista (QML): estado observable, submodelos y
// comandos del operador. No sabe nada de QML ni de como se dibuja; la vista no
// sabe nada del motor ni de la configuracion en disco.
//
//   Modelo (C++ puro)          ViewModel (Qt, sin QML)         Vista (QML)
//   AppRuntime / Engine  <-->  StudioViewModel + submodelos <-> tarjetas
//   AppSettings (JSON)         FrameService (servicio)          VideoItem
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
#include "control/BusEventos.hpp"
#include "comun/UsoCpu.hpp"
// Los ViewModels de área (ui/AreasStudio.hpp) van declarados abajo, no incluidos.

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
// Las áreas (ui/AreasStudio.hpp): declaradas; quien las usa incluye su header.
class AppViewModel;
class AsistenteViewModel;
class ControlViewModel;
class EmisionViewModel;
class EstadoAireViewModel;
class EscritorioViewModel;
class EscenasViewModel;
class ImagenViewModel;
class MezclaViewModel;
class SalaViewModel;
class SalidasViewModel;
class VideoViewModel;

class StudioViewModel : public QObject {
    Q_OBJECT
    Q_MOC_INCLUDE("ui/AreasStudio.hpp")
    // --- Estado ---------------------------------------------------------------
    /// Lo que sale de la por omisión, las pastillas (ui/EstadoAireViewModel): recording,
    /// streaming, alAire, transmitiendo, reconnecting, outputProblem, los tiempos y lo que
    /// mide el motor (fps, encoder, droppedFrames, totalFrames, streamKbps).
    Q_PROPERTY(QObject* aire READ aireQml CONSTANT)
    /// La app misma (ui/AppViewModel): el modo en vivo, la caída de la que se vuelve, lo que
    /// se corta al cerrar y la cuenta regresiva del reinicio.
    Q_PROPERTY(QObject* app READ appQml CONSTANT)
    /// Las entradas de video que se quedaron sin señal (el multiview las marca).
    Q_PROPERTY(QStringList sinSenal READ sinSenal NOTIFY senalChanged)
    Q_PROPERTY(QVariantMap latido READ latido NOTIFY latidoChanged)
    /// Qué hacer si cada entrada se cae al aire: {entrada: {escena, al_perder_aire, ms}}.
    Q_PROPERTY(QVariantMap respaldos READ respaldos NOTIFY respaldosChanged)
    /// El área de la asamblea (ui/AsambleaViewModel): presidenciaHabla y los mandos del operador.
    Q_PROPERTY(QObject* asamblea READ asambleaQml CONSTANT)
    /// El área del asistente y la voz (ui/AsistenteViewModel): el vocabulario y el rótulo de traducción.
    Q_PROPERTY(QObject* asistente READ asistenteQml CONSTANT)
    /// El cuerpo deliberativo (integrantes, bancas, presets, micrófonos): asamblea/CuerpoViewModel.
    Q_PROPERTY(QObject* cuerpo READ cuerpoQml CONSTANT)
    /// La fila de oradores presenciales (bancas): asamblea/Bancas.
    Q_PROPERTY(QObject* bancas READ bancasQml CONSTANT)
    /// El debate entre dos (de la sala o remotos): asamblea/Debate.
    Q_PROPERTY(QObject* debate READ debateQml CONSTANT)
    /// Las configuraciones con nombre (cada una con lo suyo): arranque/Configuraciones.
    Q_PROPERTY(QObject* configuraciones READ configuracionesQml CONSTANT)
    /// La central de conferencia Bosch DCN (micrófonos de banca): asamblea/ConferenciaDcn.
    Q_PROPERTY(QObject* dcn READ dcnQml CONSTANT)
    /// La banca virtual (sesión remota, en la sala o afuera): asamblea/SesionRemota.
    Q_PROPERTY(QObject* sesionRemota READ sesionRemotaQml CONSTANT)
    /// Las votaciones de la asamblea (central y celulares; modelo).
    Q_PROPERTY(QObject* votaciones READ votacionesQml CONSTANT)
    /// Subtítulos ocultos (CC) en la transmisión: agent/SubtitulosCC.
    Q_PROPERTY(QObject* cc READ ccQml CONSTANT)
    /// El bus de eventos de la app (control/BusEventos): superficies, plugins, MCP.
    Q_PROPERTY(QObject* bus READ busQml CONSTANT)
    /// Las alarmas de los disparadores (ui/AlarmasViewModel): activas, critica, sonido.
    Q_PROPERTY(QObject* alarmas READ alarmasQml CONSTANT)
    /// La botonera (superficie de control de tablets y hardware): control/Botonera.
    Q_PROPERTY(QObject* botonera READ botoneraQml CONSTANT)
    /// Lo que usa la máquina (CPU, GPU, RAM, red) con su historia y su estado: ui/RecursosViewModel.
    Q_PROPERTY(QObject* recursos READ recursosQml CONSTANT)
    /// La prueba de sala: el simulacro con participantes de prueba antes del evento
    /// (ui/PruebaSalaViewModel).
    Q_PROPERTY(QObject* pruebaSala READ pruebaSalaQml CONSTANT)
    /// La prueba de asamblea: integrantes simulados por la sesión remota (ui/PruebaAsambleaViewModel).
    Q_PROPERTY(QObject* pruebaAsamblea READ pruebaAsambleaQml CONSTANT)
    /// La velocidad del enlace por el túnel y cuánto necesita el evento (ui/EnlaceViewModel).
    Q_PROPERTY(QObject* enlace READ enlaceQml CONSTANT)
    /// ¿Cuántos invitados aguanta? (Configuración → Sistema → Capacidad, ui/CapacidadViewModel).
    Q_PROPERTY(QObject* capacidad READ capacidadQml CONSTANT)
    /// El control web (dashboard /control/, /ws?control): aparatos, perfiles y solicitudes. control/ControlWeb.
    Q_PROPERTY(QObject* controlWeb READ controlWebQml CONSTANT)
    /// El área de las superficies de control (ui/ControlViewModel): el tema web,
    /// emparejar tablets y el control web, la API remota.
    Q_PROPERTY(QObject* control READ controlQml CONSTANT)
    /// Companion Satellite manejando la botonera (control/SatelliteServidor).
    Q_PROPERTY(QObject* satellite READ satelliteQml CONSTANT)
    /// Las caras de los botones (foto o video corto): control/CarasBotonera.
    Q_PROPERTY(QObject* carasBotonera READ carasBotoneraQml CONSTANT)
    /// Las capas DSK encima del programa (control/Dsk): logo, intérprete LSA, zócalo fijo.
    Q_PROPERTY(QObject* dsk READ dskQml CONSTANT)
    /// La grilla de personas (escenas/ControlGrillas): quién está en cada una y lo hecho a mano.
    Q_PROPERTY(QObject* grillas READ grillasQml CONSTANT)
    /// Seguir al micrófono abierto: propone (o arma) la banca que pidió la palabra.
    Q_PROPERTY(QObject* seguirMicrofono READ seguirMicrofonoQml CONSTANT)
    /// El orden del día de la sesión y el uso de la palabra (asamblea/OrdenDelDia).
    Q_PROPERTY(QObject* ordenDelDia READ ordenDelDiaQml CONSTANT)
    /// El resultado de la sesión en PDF, con plantillas (asamblea/InformesResultado).
    Q_PROPERTY(QObject* informes READ informesQml CONSTANT)
    /// Quórum, mayorías y resultados oficiales de las votaciones (asamblea/Quorum).
    Q_PROPERTY(QObject* quorum READ quorumQml CONSTANT)
    /// El catálogo de acciones y los atajos de teclado (control/Acciones).
    Q_PROPERTY(QObject* acciones READ accionesQml CONSTANT)
    /// Avisos fuera del programa (webhook, Telegram, mail) para los disparadores.
    Q_PROPERTY(QObject* notificador READ notificadorQml CONSTANT)
    /// El túnel de pinggy: estado, token (solo guardado/falta) y DNS (net/AccesoRemoto).
    Q_PROPERTY(QObject* accesoRemoto READ accesoRemotoQml CONSTANT)
    /// Grabar, transmitir y lo que los rodea (ui/EmisionViewModel): destinos, señal limpia,
    /// ISO, disco, loudness, retornos y la salud de cada destino.
    Q_PROPERTY(QObject* emision READ emisionQml CONSTANT)
    /// Los mandos de las escenas (ui/EscenasViewModel): armarlas y ordenarlas, el preview y
    /// el TAKE de la activa, el AFV, la repetición.
    Q_PROPERTY(QObject* escenas READ escenasQml CONSTANT)
    /// El CPU que usa la app (medido cada segundo) y si el gateway contesta.
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY cpuChanged)
    Q_PROPERTY(bool gatewayAlive READ gatewayAlive NOTIFY gatewayChanged)
    /// El vúmetro del programa de la salida activa (ver salidaActiva).
    /// Los vúmetros a pedido (ui/RelojNiveles): `Suscripcion { objetivo: Studio.niveles;
    /// activa: visible }` en cada vista que muestra niveles (programPeakL/R, los picos del
    /// mezclador). Sin nadie que mire, no se miden.
    Q_PROPERTY(QObject* niveles READ nivelesQml CONSTANT)
    Q_PROPERTY(double programPeakL READ picoActivaL NOTIFY nivelesChanged)
    Q_PROPERTY(double programPeakR READ picoActivaR NOTIFY nivelesChanged)
    Q_PROPERTY(QString currentScene READ currentScene NOTIFY sceneChanged)
    Q_PROPERTY(QString transitionType READ transitionType NOTIFY sceneChanged)
    /// La escena ARMADA (la que sale con el proximo TAKE). Vacia = ninguna.
    Q_PROPERTY(QString escenaArmada READ escenaArmada NOTIFY sceneChanged)
    Q_PROPERTY(double canvasAspect READ canvasAspect NOTIFY canvasChanged)
    Q_PROPERTY(QString canvasLabel READ canvasLabel NOTIFY canvasChanged)
    Q_PROPERTY(QString streamServer READ streamServer NOTIFY ajustesChanged)
    Q_PROPERTY(bool streamKeyAvailable READ streamKeyAvailable NOTIFY ajustesChanged)
    Q_PROPERTY(bool streamKeySaved READ streamKeySaved NOTIFY ajustesChanged)
    Q_PROPERTY(QString room READ room NOTIFY ajustesChanged)
    /// Las direcciones de los servicios (sala/Direcciones): la única fuente de los enlaces.
    Q_PROPERTY(QObject* direcciones READ direccionesQml CONSTANT)
    /// El área de la sala (ui/SalaViewModel): el cupo, el chat, aprobar a los invitados, el encargo al asistente.
    Q_PROPERTY(QObject* sala READ salaQml CONSTANT)
    Q_PROPERTY(int guests READ guests NOTIFY ajustesChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    /// Arrancó con --test: el MCP no pide aprobaciones (la interfaz lo avisa siempre).
    Q_PROPERTY(bool modoTest READ modoTest CONSTANT)
    /// El área del audio (ui/MezclaViewModel): balística, PFL, retardo y medir la sincronía.
    Q_PROPERTY(QObject* mezcla READ mezclaQml CONSTANT)
    // --- Submodelos -------------------------------------------------------------
    Q_PROPERTY(QObject* scenes READ scenes CONSTANT)
    /// Las salidas (PLAN-SALIDAS) y la activa de esta interfaz: ui/SalidasViewModel
    /// (lista, activa, activaDatos, varias, aire, placa, tope, nueva y los comandos).
    Q_PROPERTY(QObject* salidas READ salidasQml CONSTANT)
    // --- La salida activa de la interfaz (PLAN-SALIDAS, etapa 5) ----------------
    // Cada salida es un operador completo y esta interfaz maneja UNA: la activa. Lo
    // que muestran y hacen Dirección, Escenas y el Mezclador es de ella: la escena
    // al aire y la armada (currentScene, escenaArmada, los roles live del modelo de
    // escenas), take/takeCon/takeScene/armarEscena/tbar, el AFV, el volumen y el mute
    // de cada tira (mixer), el vúmetro del programa y el tally del multiview. Con una
    // sola salida es la por omisión y nada cambia. Lo que no es de esta interfaz (el
    // MCP, el BusEventos, las pantallas de tally de la sala, los monitores "program"
    // y "armada") sigue con la por omisión (conmutador()). Cuál es la activa, su aire y
    // su placa: Studio.salidas.
    Q_PROPERTY(QObject* sources READ sources CONSTANT)
    Q_PROPERTY(QObject* mixer READ mixer CONSTANT)
    Q_PROPERTY(QObject* sourceTypes READ sourceTypes CONSTANT)
    Q_PROPERTY(QObject* settings READ settings CONSTANT)
    Q_PROPERTY(QObject* dashboard READ dashboard CONSTANT)
    Q_PROPERTY(QObject* frames READ frames CONSTANT)
    Q_PROPERTY(QObject* properties READ properties CONSTANT)
    Q_PROPERTY(QObject* conference READ conference CONSTANT)
    Q_PROPERTY(QObject* evento READ evento CONSTANT)
    /// La página Histórico: todos los eventos, su bitácora, configuración y lo que produjeron.
    Q_PROPERTY(QObject* historico READ historicoObj CONSTANT)
    Q_PROPERTY(QObject* grafico READ grafico CONSTANT)
    Q_PROPERTY(QObject* rotulador READ rotulador CONSTANT)
    /// El teleprompter: los discursos, quién está conectado y los controles.
    Q_PROPERTY(QObject* prompter READ prompter CONSTANT)
    /// Los manuales de usuario que arma el asistente (Configuración → Sistema → Manuales).
    Q_PROPERTY(QObject* manuales READ manualesQml CONSTANT)
    /// Pantallas externas (televisores): dónde mostrar y qué mostrar.
    Q_PROPERTY(QObject* pantallas READ pantallas CONSTANT)
    /// Todo lo que se puede mostrar en una ventana, un monitor o una TV, con id
    /// estable (pantallas/Catalogo.hpp). Lo usan el tablero, «sacar a ventana»
    /// y la página Pantallas.
    Q_PROPERTY(QObject* catalogo READ catalogoQml CONSTANT)
    /// Los monitores de la máquina y qué va en cada uno (pantallas/Monitores.hpp).
    Q_PROPERTY(QObject* pantallasConectadas READ pantallasConectadasQml CONSTANT)
    /// Las ventanas sueltas, que se recuerdan (pantallas/Ventanas.hpp).
    Q_PROPERTY(QObject* ventanas READ ventanasQml CONSTANT)
    /// Dónde se muestra cada cosa en esta máquina (ui/EscritorioViewModel): los monitores en
    /// su orden físico, identificarlos, las ventanas abiertas, si se pueden ubicar.
    Q_PROPERTY(QObject* escritorio READ escritorioQml CONSTANT)
    /// La biblioteca de códigos QR y su estudio de diseño.
    Q_PROPERTY(QObject* qr READ qr CONSTANT)
    /// El tema del evento abierto en todas las páginas web (TemaEventoViewModel).
    Q_PROPERTY(QObject* temaEvento READ temaEvento CONSTANT)
    /// La línea de tiempo del evento: escaleta, conducción y multipista (LineaViewModel).
    Q_PROPERTY(QObject* linea READ lineaQml CONSTANT)
    /// Todos los enlaces del aparato, con su estado y su botón de activar (página Enlaces).
    Q_PROPERTY(QObject* enlaces READ enlacesQml CONSTANT)
    /// La libreta de personas (rostros). Existe siempre: sin los modelos
    /// vendoreados avisa por qué, que es mejor que una lista vacía.
    Q_PROPERTY(QObject* personas READ personas CONSTANT)
    /// La atención a quien se conecta (Sala y Asamblea): ui/AtencionViewModel.
    Q_PROPERTY(QObject* atencion READ atencion CONSTANT)
    /// El registro de personas por tótem (Personas → Registro): ui/TotemsViewModel.
    Q_PROPERTY(QObject* totems READ totems CONSTANT)
    /// Las preguntas del publico y la urna (viven en B; esto las trae).
    Q_PROPERTY(QObject* publico READ publico CONSTANT)
    /// Los archivos del operador: musica, videos, imagenes, presentaciones.
    Q_PROPERTY(QObject* medios READ medios CONSTANT)
    /// La playlist de medios (T46).
    Q_PROPERTY(QObject* playlist READ playlistQml CONSTANT)
    /// La biblioteca de carruseles: se arman en Medios y se aplican en las
    /// escenas (ver ui/CarruselViewModel.hpp).
    Q_PROPERTY(QObject* carruseles READ carruseles CONSTANT)
    Q_PROPERTY(QObject* registro READ registro CONSTANT)
    /// Las tarjetas que vienen de Extensiones/ (ver ExtensionesModel.hpp).
    Q_PROPERTY(QObject* extensiones READ extensiones CONSTANT)
    /// La barra de navegacion: donde va, de que tamaño y que muestra.
    Q_PROPERTY(QObject* riel READ riel CONSTANT)
    /// Que seccion de Configuracion esta abierta. Vive aca y no adentro del
    /// QML para que se pueda llevar al operador derecho a una seccion ("andá a
    /// la de audio") en vez de decirle donde tiene que hacer clic.
    Q_PROPERTY(QString seccionConfig READ seccionConfig WRITE setSeccionConfig NOTIFY seccionConfigChanged)
    /// Las cuentas de las plataformas (ui/CuentasViewModel): las guías y el formulario de credenciales.
    Q_PROPERTY(QObject* cuentas READ cuentasQml CONSTANT)
    Q_PROPERTY(QObject* youtube READ youtube CONSTANT)
    Q_PROPERTY(QObject* twitch READ twitch CONSTANT)
    Q_PROPERTY(QObject* facebook READ facebook CONSTANT)
    Q_PROPERTY(QObject* system READ system CONSTANT)
    Q_PROPERTY(QObject* agent READ agent CONSTANT)
    /// El asistente de automatización: otra instancia (su motor, su terminal, su charla)
    /// que atiende las alertas para que no caigan en la charla del evento.
    Q_PROPERTY(QObject* agenteAutomatizacion READ agenteAutomatizacion CONSTANT)
    /// El modelo local (Configuración -> Modelos locales): ver agent/ModelosLocales.hpp.
    Q_PROPERTY(QObject* modelos READ modelos CONSTANT)
    /// La cola única de descargas (Descargas, arriba a la derecha): ver net/Descargas.hpp.
    Q_PROPERTY(QObject* descargas READ descargas CONSTANT)
    /// Las multivistas (pestañas de la página Multiview) y sus ventanas: ui/MultiviewViewModel.
    Q_PROPERTY(QObject* multivistas READ multivistasQml CONSTANT)
    /// Hablarle al asistente (push-to-talk): ver agent/Voz.hpp.
    Q_PROPERTY(QObject* voz READ voz CONSTANT)
    /// «Voz del teleprompter»: el guion dicho en voz alta, siguiendo al teleprompter.
    Q_PROPERTY(QObject* vozPrompter READ vozPrompter CONSTANT)
    /// La matriz de mezcla (fuentes × pistas y buses virtuales): audio/Matriz.hpp.
    Q_PROPERTY(QObject* matriz READ matriz CONSTANT)
    /// El Mezclador (la ventana propia): audio/Mezclador.hpp.
    Q_PROPERTY(QObject* mezclador READ mezcladorQml CONSTANT)
    /// El área de la imagen (ui/ImagenViewModel): los scopes y el legalizador.
    Q_PROPERTY(QObject* imagen READ imagenQml CONSTANT)
    /// El PTZ de cada entrada de video (digital + hardware) y Mejorar imagen:
    /// ptz/ControlCamaras.hpp.
    Q_PROPERTY(QObject* camaras READ camaras CONSTANT)
    /// La página «Tiempos»: el grafo de demoras de cada entrada a cada salida.
    Q_PROPERTY(QObject* tiempos READ tiempos CONSTANT)
    /// El plan de resolución de cada entrada y los colores de alarma de sus tarjetas.
    Q_PROPERTY(QObject* resolucion READ resolucion CONSTANT)
    /// Analizar las caras de una cámara y seguir a una con el PTZ (ui/Seguidor).
    Q_PROPERTY(QObject* seguidor READ seguidor CONSTANT)
    /// Los equipos: detectores, catálogo y acciones (dispositivos/Dispositivos).
    Q_PROPERTY(QObject* dispositivos READ dispositivosQml CONSTANT)
    /// Los grafos de tarjetas de video de cada entrada (video/VideoGrafos).
    Q_PROPERTY(QObject* video READ video CONSTANT)
    /// Los equipos de video de la red (escáner, credenciales, ficha, agregar): red/Red.hpp.
    Q_PROPERTY(QObject* red READ red CONSTANT)
    /// Los plugins de Python (pages/PluginsPage.qml).
    Q_PROPERTY(QObject* plugins READ pluginsQml CONSTANT)
    /// Las salidas de texto (voz a texto con nombre): agent/FlujosTexto.hpp.
    Q_PROPERTY(QObject* flujos READ flujos CONSTANT)
    /// El puesto web del corrector de subtítulos (para QrDialog: pedirQr / qrListo).
    Q_PROPERTY(QObject* puestoCorreccion READ puestoCorreccionQml CONSTANT)
    /// Las tarjetas «Texto a voz»: voces, idiomas y lo que dicen (agent/VocesRack.hpp).
    Q_PROPERTY(QObject* voces READ voces CONSTANT)
    /// La traducción en vivo de las salidas de texto, con su glosario (agent/Traductor.hpp).
    Q_PROPERTY(QObject* traductor READ traductor CONSTANT)
    /// Qué parte de la asistencia con IA no anda o falta configurar (agent/EstadoIA.hpp):
    /// la pastilla «IA» del encabezado y la herramienta ia_estado.
    Q_PROPERTY(QObject* estadoIA READ estadoIA CONSTANT)
    /// La documentación del evento abierto (PDF con carátula, por punto y sección): ui/DocumentosViewModel.
    Q_PROPERTY(QObject* documentos READ documentosQml CONSTANT)
    /// Los PDF en las escenas (fuentes ba_pdf): elegir y pasar páginas. ui/PdfEscena.
    Q_PROPERTY(QObject* pdfEscena READ pdfEscenaQml CONSTANT)
    /// La superresolución de las entradas de red: motores, modelos, laboratorio (video/SuperRes.hpp).
    Q_PROPERTY(QObject* superres READ superres CONSTANT)
    /// El banco de pruebas de superresolución: qué puede usar esta máquina.
    Q_PROPERTY(QObject* bancoSr READ bancoSrObj CONSTANT)
    /// Los roles automaticos y sus corridas (pagina Agentes).
    Q_PROPERTY(QObject* despachador READ despachadorQml CONSTANT)
    Q_PROPERTY(QObject* messages READ messages CONSTANT)
    Q_PROPERTY(QObject* inputs READ inputs CONSTANT)
    /// Pestaña Audio (patchbay de modulos LSP por fuente); null sin CLAP.
    Q_PROPERTY(QObject* audio READ audio CONSTANT)
   public:
    /// `ventanilla`: cómo se le pide al motor (que es del plano de control) y su foto (F3-03).
    StudioViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~StudioViewModel() override;

    /// Lo que sale de la por omisión (las pastillas): de la foto del motor.
    EstadoAireViewModel& aire() { return *aire_; }
    const EstadoAireViewModel& aire() const { return *aire_; }
    QObject* aireQml() const;
    /// El latido del supervisor: {motor_vivo, ui_viva, ui_trabada_ms, ultima_traba_ms, politica}
    /// (LatidoAire::estado). Avisa `latidoChanged` cuando cambia (lo mira el vigía).
    QVariantMap latido() const { return latido_; }
    /// Preferencias chicas de la interfaz (anchos de paneles, lo último elegido),
    /// en Config/interfaz.json: portable, al lado de la app, nunca en el sistema.
    Q_INVOKABLE QVariant uiValor(const QString& clave, const QVariant& porDefecto = {}) const;
    Q_INVOKABLE void uiGuardar(const QString& clave, const QVariant& valor);
    /// Cambia la política del supervisor ({espera_ui_s, matar_si_ui_trabada_sin_aire}); el
    /// error, a `notify`.
    Q_INVOKABLE void latidoPoliticaUi(const QVariantMap& cambios);
    /// Sale algo (transmisión, grabación o señal limpia): BroadcastEngine::haySalida.
    bool alAire() const;
    QObject* cuerpoQml() const;
    QStringList sinSenal() const;
    /// El respaldo de entradas (modelo).
    escenas::RespaldoEntradas& respaldoEntradas() { return *respaldo_; }
    /// Qué hacer si cada entrada se cae estando al aire (propiedad `respaldos`):
    /// {entrada: {escena, al_perder_aire: avisar|armar|cortar, ms}}.
    QVariantMap respaldos() const { return respaldos_; }
    /// Lo pide al modelo (en el dueño); el error, con notify.
    Q_INVOKABLE void setRespaldoEntrada(const QString& entrada, const QVariantMap& r);
    QObject* bancasQml() const;
    QObject* debateQml() const;
    QObject* configuracionesQml() const;
    Configuraciones* configuraciones();
    /// La app misma (el modo en vivo, la caída, cerrar y reiniciar, los eventos).
    AppViewModel& app() { return *app_; }
    QObject* appQml() const;
    asamblea::Debate* debate();
    QObject* dcnQml() const;
    QObject* sesionRemotaQml() const;
    QObject* votacionesQml() const;
    asamblea::Votaciones* votaciones();
    asamblea::Presidencia* presidencia();
    QObject* ccQml() const;
    agent::SubtitulosCC& cc();
    asamblea::Bancas& bancas();
    asamblea::ConferenciaDcn& dcn();
    asamblea::SesionRemota* sesionRemota();
    AppRuntime& runtime() { return rt_; }
    /// La ventanilla del motor (F3-03): las herramientas de la interfaz leen su foto y le piden.
    VentanillaMotor& ventanilla() { return ventanilla_; }
    asamblea::CuerpoViewModel& cuerpo();
    /// El área de la asamblea (sus modelos, la web del evento, los vínculos, el acta).
    AsambleaViewModel& asamblea() { return *asamblea_; }
    QObject* asambleaQml() const;
    agent::Vocabulario& vocabulario();
    /// El área del asistente y la voz (sus modelos, el vocabulario, el rótulo de traducción).
    AsistenteViewModel& asistente() { return *asistente_; }
    QObject* asistenteQml() const;
    control::BusEventos& bus() { return *bus_; }
    control::Botonera& botonera();
    /// El área de las superficies de control (sus modelos, el tema web, la API remota).
    ControlViewModel& control() { return *control_; }
    QObject* controlQml() const;
    /// El muestreador de recursos de la máquina (tarjeta «Sistema», sistema_recursos, alarmas).
    RecursosViewModel* recursos() const { return recursos_.get(); }
    QObject* recursosQml() const;
    PruebaSalaViewModel& pruebaSala() { return *pruebaSala_; }
    PruebaAsambleaViewModel& pruebaAsamblea() { return *pruebaAsamblea_; }
    CapacidadViewModel& capacidad() { return *capacidad_; }
    QObject* capacidadQml() const;
    QObject* pruebaSalaQml() const;
    QObject* pruebaAsambleaQml() const;
    EnlaceViewModel& enlace() { return *enlace_; }
    QObject* enlaceQml() const;
    /// El corte de luz (F1-11): de dónde viene la energía y la política (arranque/EnergiaAire).
    EnergiaAire* energia() const { return energia_.get(); }
    /// La botonera con el gateway (emparejar tablets, credenciales): control/BotoneraGateway.
    control::BotoneraGateway& botoneraGateway();
    control::CarasBotonera& carasBotonera();
    /// El control web (/control/): sus aparatos y el puente con el gateway.
    control::ControlWeb& controlWeb();
    control::ControlWebGateway& controlWebGateway();
    QObject* controlWebQml() const;
    QObject* botoneraQml() const;
    QObject* satelliteQml() const;
    QObject* carasBotoneraQml() const;
    QObject* dskQml() const;
    QObject* grillasQml() const;
    QObject* seguirMicrofonoQml() const;
    asamblea::SeguirMicrofono& seguirMicrofono();
    QObject* ordenDelDiaQml() const;
    asamblea::OrdenDelDia& ordenDelDia();
    QObject* informesQml() const;
    QObject* manualesQml() const;
    asamblea::InformesResultado* informes();
    QObject* quorumQml() const;
    asamblea::Quorum& quorum();
    QObject* accionesQml() const;
    control::Acciones& acciones();
    QObject* notificadorQml() const;
    agent::Notificador& notificador() { return *notificador_; }
    QObject* accesoRemotoQml() const;
    AccesoRemoto* accesoRemoto();
    /// Modo en vivo (T48): protege páginas y ajustes que cortan (ver AppViewModel).
    bool modoVivo() const;
    /// El área de la emisión (los mandos de grabar y transmitir, la salud de los destinos).
    EmisionViewModel& emision() { return *emision_; }
    QObject* emisionQml() const;
    std::filesystem::path datosRaiz() const;
    /// Capítulos de la grabación en curso por punto del orden del día (texto para YouTube).
    Q_INVOKABLE QString capitulosPuntosUi() const;
    control::Dsk& dsk();
    QObject* playlistQml() const;
    QObject* puestoCorreccionQml() const;
    medios::Playlist& playlist() { return *playlist_; }
    /// El cupo de invitados (modelo: sala::CupoSala). La interfaz y el MCP lo piden
    /// por acá; el resultado se muestra como aviso.
    sala::CupoSala& cupoSala();
    /// La configuración de la sala en caliente (modelo: sala::ConfigSala).
    sala::ConfigSala& configSala();
    /// La votación de la sala (modelo).
    sala::VotacionSala& votacion();
    /// La cola de preguntas de la sala (modelo).
    sala::PreguntasSala& preguntas();
    /// El PIN por sesión y el token del MCP (modelo: Config/seguridad.json).
    sala::SeguridadSala& seguridad();
    /// El área de la sala (el cupo, el chat, aprobar a los invitados, el encargo al asistente).
    SalaViewModel& sala() { return *sala_; }
    QObject* salaQml() const;
    bool modoTest() const { return modoTest_; }
    void setModoTest(bool on) { modoTest_ = on; }
    /// El área del audio (los racks, la matriz, el Mezclador, el PFL, el retardo).
    MezclaViewModel& mezcla() { return *mezcla_; }
    QObject* mezclaQml() const;
    /// El área de las escenas (armarlas, el TAKE de la activa, el AFV, la repetición).
    EscenasViewModel& escenas() { return *escenas_; }
    QObject* escenasQml() const;
    control::SatelliteServidor& satellite();
    QObject* busQml() const;
    void setCorteDirectoEditor(bool si);
    double cpuPercent() const { return cpu_; }
    bool gatewayAlive() const { return gatewayAlive_; }
    /// Los de la por omisión, de la última vuelta del dueño (el MCP: no depende de que
    /// alguien mire los vúmetros).
    double programPeakL() const;
    double programPeakR() const;
    QObject* nivelesQml() const;
    double picoActivaL() const;
    double picoActivaR() const;
    QString currentScene() const;
    QString transitionType() const;
    double canvasAspect() const;
    QString canvasLabel() const;
    QString streamServer() const;
    bool streamKeyAvailable() const;
    bool streamKeySaved() const;
    QString room() const;
    QObject* direccionesQml() const;
    sala::Direcciones& direcciones();
    int guests() const;
    QString theme() const;

    QObject* scenes() const;
    QObject* sources() const;
    QObject* mixer() const;
    QObject* sourceTypes() const;
    QObject* settings() const;
    QObject* dashboard() const;
    QObject* frames() const;
    QObject* properties() const;
    QObject* conference() const;
    QObject* evento() const;
    QObject* historicoObj() const;
    HistoricoViewModel* historico() const;
    QObject* grafico() const;
    QObject* rotulador() const;
    QObject* prompter() const;
    QObject* pantallas() const;
    QObject* catalogoQml() const;
    QObject* pantallasConectadasQml() const;
    CatalogoVisible* catalogoVisible() const;
    MonitoresModel* monitoresModelo() const;
    QObject* ventanasQml() const;
    VentanasModel* ventanasModelo() const;
    QObject* qr() const;
    QObject* temaEvento() const;
    QObject* enlacesQml() const;
    EnlacesViewModel* enlacesVm() const { return enlaces_.get(); }
    QObject* personas() const;
    QObject* atencion() const;
    AtencionViewModel* atencionVm() const { return atencion_.get(); }
    QObject* totems() const;
    TotemsViewModel* totemsVm() const { return totems_.get(); }
    QObject* publico() const;
    QObject* medios() const;
    QObject* carruseles() const;
    QrViewModel& qrVm() { return *qr_; }
    QObject* registro() const;
    QObject* extensiones() const;
    QObject* riel() const;
    QString seccionConfig() const { return seccionConfig_; }
    void setSeccionConfig(const QString& s);
    /// De donde sacan sus valores los vinculos (ver graficos/Vinculos.hpp):
    /// la sala, el guion y la escena al aire, todo junto.
    QJsonObject contextoDeVinculos() const;
    graficos::Vinculos* vinculos() { return vinculos_.get(); }
    /// El unico emisor de graficos del proceso. Lo usan la interfaz, el guion,
    /// el rotulador y el MCP: si cada uno tuviera el suyo, lo que uno saca al
    /// aire no se veria en la pantalla del otro.
    graficos::Emisor* emisor() { return emisor_.get(); }
    QObject* cuentasQml() const;
    CuentasViewModel& cuentas() { return *cuentas_; }
    QObject* youtube() const;
    QObject* twitch() const;
    QObject* facebook() const;
    QObject* system() const;
    QObject* agent() const;
    QObject* agenteAutomatizacion() const;
    QObject* modelos() const;
    QObject* descargas() const;
    QObject* multivistasQml() const;
    MultiviewViewModel& multivistas();
    /// El área del escritorio (el catálogo, los monitores, las ventanas, las multivistas).
    EscritorioViewModel& escritorio() { return *escritorio_; }
    QObject* escritorioQml() const;
    QObject* voz() const;
    QObject* vozPrompter() const;
    QObject* matriz() const;
    QObject* mezcladorQml() const;
    QObject* camaras() const;
    QObject* tiempos() const;
    tiempos::TiemposModel* tiemposModel() const;
    QObject* resolucion() const;
    resolucion::ResolucionModel* resolucionModel() const;
    QObject* video() const;
    /// El grafo de video: vive en el dueño del motor (solo el MCP del control y las órdenes).
    video::VideoGrafos* videoGrafos() const;
    /// Lo que la interfaz ve de él y cómo le pide (para los modelos de la interfaz).
    VideoViewModel* videoVista() const;
    QObject* red() const;
    red::Red* redEquipos() const;
    /// Los plugins de Python (docs/funciones/PLUGINS.md).
    plugins::Plugins* plugins() const { return plugins_.get(); }
    QObject* pluginsQml() const;
    QObject* seguidor() const;
    Seguidor* seguidorPtz() const;
    QObject* dispositivosQml() const;
    dispositivos::Dispositivos* equipos() const { return dispositivos_.get(); }
    QObject* flujos() const;
    QObject* voces() const;
    QObject* traductor() const;
    QObject* estadoIA() const;
    QObject* documentosQml() const;
    DocumentosViewModel* documentos() const { return documentos_.get(); }
    QObject* pdfEscenaQml() const;
    PdfEscena* pdfEscena() const { return pdfEscena_.get(); }
    agent::EstadoIA* estadoIAModelo() const;
    QObject* superres() const;
    QObject* bancoSrObj() const;
    video::BancoSuperRes* bancoSr() const;
    video::SuperRes* superresVideo() const;
    /// El área de la imagen (las cámaras, los grafos de video, la superresolución, los scopes).
    ImagenViewModel& imagen() { return *imagen_; }
    QObject* imagenQml() const;
    agent::Traductor* traductorAgent() const;
    agent::ModelosLocales* modelosLocales() const;
    QObject* messages() const;
    QObject* inputs() const;
    QObject* audio() const;
    /// Racks de audio (módulos LSP) de las fuentes.
    audio::RackManager* racks() const;
    agent::AlertEngine& alerts();
    /// El área de las alarmas (ui/AlarmasViewModel): Studio.alarmas en QML.
    QObject* alarmasQml() const;
    agent::Despachador& despachador();
    QObject* despachadorQml() const;

    // --- Comandos ---------------------------------------------------------------
    QString escenaArmada() const;
    /// El conmutador de escenas de la salida por omisión (modelo): la GUI y el MCP
    /// cambian de escena por ahí.
    escenas::Conmutador& conmutador();
    /// Los de todas las salidas (uno por salida; el de la por omisión es conmutador()).
    escenas::Conmutadores& conmutadores();
    /// El área de las salidas (la lista, la activa, el FTB y la placa).
    SalidasViewModel& salidas() { return *salidas_; }
    QObject* salidasQml() const;
    /// La grilla de personas (fuentes ba_grilla): quién va en cada una. nullptr antes de armarse.
    escenas::ControlGrillas* grillasPersonas() { return grillasPersonas_.get(); }
    /// El FTB y la placa de seguridad (modelo): la GUI y el MCP los ponen por ahí.
    emision::SeguridadAire& seguridadAire();
    /// Lo que oye cada invitado (modelo, Config/retornos.json).
    emision::RetornosInvitados& retornos();
    /// El objetivo de loudness guardado (modelo, Config/loudness.json).
    audio::ObjetivoLoudness& objetivoLoudness();

   private:
    /// Le manda el estado de los buses a las pantallas de tally de la sala.
    /// Se llama en cada cambio de escena y de aire: un tally que llega tarde
    /// es peor que no tenerlo, porque alguien se confia.
    void publicarTally();
    /// Pide al dueño los respaldos guardados (lee el archivo) y los pone en `respaldos`.
    void leerRespaldos();

   public:
    /// Copia un texto al portapapeles y lo avisa.
    ///
    /// Vive acá y no en la sala: copiar un enlace no tiene nada que ver con una
    /// videoconferencia. Estaba en ConferenceViewModel y por eso el diálogo de
    /// QR —que lo usan el teleprompter y los rótulos— no podía copiar nada.
    Q_INVOKABLE void copiar(const QString& texto);
    Q_INVOKABLE void save();
    /// Relee escenas, fuentes y mezclador del motor (algo los cambio por
    /// fuera de la vista, p. ej. el servidor MCP).
    void refreshModels();

   signals:
    void latidoChanged();
    /// Lo de la configuración que muestran la raíz y las tarjetas (streamServer, la clave,
    /// room, guests): sale de la parte Ajustes de la foto.
    void ajustesChanged();
    void cpuChanged();
    void gatewayChanged();
    /// Los picos del programa (el vúmetro): a 10 Hz. Lo demás avisa por su lado (Studio.aire,
    /// ajustesChanged, cpuChanged, gatewayChanged), cada cosa cuando cambia.
    void nivelesChanged();
    void sceneChanged();
    /// La LISTA de escenas cambió (se agregó, borró, renombró o cambió de
    /// carpeta). Distinta de `sceneChanged`, que es «cambió la que está al
    /// aire»: una pasa cada pocos segundos en vivo y la otra casi nunca.
    void escenasCambiaron();
    void seccionConfigChanged();
    /// Pide a una página que muestre una parte suya (ui_show_page {page, seccion}).
    void mostrarSeccion(const QString& pagina, const QString& seccion);
    /// qr_mostrar: abrir una ventana con el QR de `texto`, su descripción, una
    /// explicación y un color (verde, azul… o #rrggbb), con el botón de copiar.
    void mostrarQr(const QString& texto, const QString& descripcion, const QString& mensaje, const QString& color);
    /// Pide a una página que abra (o cierre: "cerrar") un panel o diálogo suyo
    /// (ui_abrir_panel {page, panel}): lo que el operador abre con un botón.
    void abrirPanel(const QString& pagina, const QString& panel);
    void canvasChanged();
    void themeChanged();
    /// Aviso para el operador: level = "info" | "ok" | "warn" | "error".
    void notify(const QString& message, const QString& level);
    /// El launcher (o una señal) pidio cerrar.
    void quitRequested();
    void senalChanged();
    void respaldosChanged();

   private:
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_; ///< el motor es del plano de control: se le pide y se lee su foto
    /// Lo que sale de la por omisión (las pastillas): lo leen las salidas, la app y la sala.
    std::unique_ptr<EstadoAireViewModel> aire_;
    /// Lo que cambió en la foto del motor (lo de la raíz: las pastillas, el lienzo, el editor).
    void alCambiarFoto(unsigned partes);
    std::unique_ptr<ScenesModel> scenes_;
    std::unique_ptr<SourcesModel> sources_;
    std::unique_ptr<MixerModel> mixer_;
    std::unique_ptr<SourceTypesModel> types_;
    std::unique_ptr<SettingsModel> settings_;
    std::unique_ptr<DashboardModel> dashboard_;
    std::unique_ptr<FrameService> frames_;
    std::unique_ptr<PropertiesModel> props_;
    std::unique_ptr<ConferenceViewModel> conference_;
    /// El cupo, la configuración, las direcciones, la votación, las preguntas, el PIN y el
    /// túnel (ui/SalaViewModel): usa el cliente de conference_, va después (se destruye antes).
    std::unique_ptr<SalaViewModel> sala_;
    std::unique_ptr<EventoViewModel> evento_;
    std::unique_ptr<graficos::Emisor> emisor_;
    std::unique_ptr<GraficoViewModel> grafico_;
    std::unique_ptr<RotuladorViewModel> rotulador_;
    std::unique_ptr<PrompterViewModel> prompter_;
    std::unique_ptr<PantallasViewModel> pantallas_;
    std::unique_ptr<QrViewModel> qr_;
    std::unique_ptr<TemaEventoViewModel> temaEvento_; ///< usa evento_ y conference_: va después
    std::unique_ptr<sala::ClavesEvento> clavesEvento_; ///< las claves de las superficies, del evento abierto
    /// La urna y las preguntas del público: llegan a la sala y las guarda el evento.
    std::unique_ptr<sala::GuardadoEnEvento> urnaEvento_, preguntasEvento_;
    std::unique_ptr<PersonasViewModel> personas_;
    std::unique_ptr<PublicoViewModel> publico_;
    bool publicoPorWeb_ = false; ///< la cola del público se consulta porque el evento tiene página
    std::unique_ptr<MediosViewModel> medios_;
    std::unique_ptr<medios::Playlist> playlist_;
    std::unique_ptr<CarruselViewModel> carruseles_;
    std::unique_ptr<graficos::Vinculos> vinculos_;
    std::unique_ptr<RegistroViewModel> registro_;
    std::unique_ptr<ExtensionesModel> extensiones_;
    std::unique_ptr<RielConfig> riel_;
    /// El catálogo, los monitores, las ventanas sueltas y las multivistas (ui/EscritorioViewModel).
    std::unique_ptr<EscritorioViewModel> escritorio_;
    QString seccionConfig_;
    /// El audio: los racks, la pestaña Audio, la matriz y el Mezclador (ui/MezclaViewModel).
    std::unique_ptr<MezclaViewModel> mezcla_;
    /// Las cámaras, los grafos de video, la superresolución, los tiempos, la resolución,
    /// los equipos de la red y seguir caras (ui/ImagenViewModel). Los plugins y los
    /// equipos los usan: van después (se destruyen antes).
    std::unique_ptr<ImagenViewModel> imagen_;
#if defined(BA_HAVE_CLAP)
    std::unique_ptr<plugins::Plugins> plugins_;
    std::unique_ptr<dispositivos::Dispositivos> dispositivos_;
    bool modoTest_ = false;
    std::unique_ptr<DocumentosViewModel> documentos_;
    std::unique_ptr<PdfEscena> pdfEscena_;
#endif
    std::unique_ptr<CuentasViewModel> cuentas_; ///< YouTube, Twitch y Facebook (ui/CuentasViewModel)
    std::unique_ptr<AppViewModel> app_; ///< el modo en vivo, la versión, los eventos, la caída (ui/AppViewModel)
    std::unique_ptr<AlarmasViewModel> alarmas_; ///< el área de las alarmas (y su vigía)
    std::unique_ptr<control::BusEventos> bus_;
    /// Lo que escuchan los plugins, pedido al bus (los niveles, si escuchan eso).
    control::InteresEnBus interesPlugins_;
    /// El ritmo de los vúmetros, a pedido: usa el mezclador, las salidas y el bus (va después).
    std::unique_ptr<RelojNiveles> niveles_;
    /// La asamblea y sus modelos (ui/AsambleaViewModel). Después del bus (se desuscriben
    /// de él): lo que usa sus modelos (la grilla, la atención, los tótems) va más abajo.
    std::unique_ptr<AsambleaViewModel> asamblea_;
    /// Las superficies de control (ui/ControlViewModel): usan la asamblea (las bancas de
    /// los botones) y el bus; la línea de tiempo usa sus acciones (va más abajo).
    std::unique_ptr<ControlViewModel> control_;
    /// El tablero web (/control/app/, Qt para WebAssembly): este modelo publicado tal cual
    /// (control/ReplicaPublicador) y el video de sus vistas (control/CuadrosWeb). Usan el
    /// control web (control_) y los cuadros (frames_): se destruyen antes.
    std::unique_ptr<control::ReplicaPublicador> replica_;
    std::unique_ptr<control::CuadrosWeb> cuadrosWeb_;
    /// La bitácora del evento en curso, sus fotos de Config/, el índice de lo que salió y
    /// el cierre en el histórico (ui/BitacoraEvento; H2-H5 de PLAN-EVENTOS-HISTORICO).
    std::unique_ptr<BitacoraEvento> bitacoraEvento_;
    std::unique_ptr<RecursosViewModel> recursos_;
    std::unique_ptr<EnergiaAire> energia_;
    std::unique_ptr<LineaViewModel> linea_; ///< usa evento_, frames_, las acciones (control_) y bus_: va después
    QObject* lineaQml() const;
    std::unique_ptr<agent::Notificador> notificador_;
    /// Las alarmas que salen sin la interfaz (la interfaz trabada con aire, F1-13).
    std::unique_ptr<AvisoFueraDeUi> avisoFueraDeUi_;
    /// Los conectados que se cortan al cerrar: sala + sesión remota.
    int personasConectadas() const;
    /// Las salidas: el conmutador de escenas de cada una, el FTB y la placa, la activa
    /// (ui/SalidasViewModel). Lo que usa sus conmutadores (el respaldo de entradas) va
    /// más abajo.
    std::unique_ptr<SalidasViewModel> salidas_;
    std::unique_ptr<EscenasViewModel> escenas_; ///< los mandos de las escenas (ui/EscenasViewModel): usa las salidas
    /// La interfaz pasa a mostrar lo de la activa: escenas, fuentes, mezcla, tally.
    void reflejarSalidaActiva();
    double peakActivaL_ = -100; ///< el vúmetro del programa de la activa (si no es la por omisión)
    double peakActivaR_ = -100;
    std::unique_ptr<EmisionViewModel> emision_; ///< grabar, transmitir y lo que los rodea (ui/EmisionViewModel)
    /// Lo que está al aire de cada salida, al bus (cuando cambia la foto, el aire o el DSK).
    void publicarSalidas();
    /// Los niveles que alguien pidió al bus (a pedido: ver BusEventos).
    void publicarNivelesEnBus();
    /// Las entradas sin señal, al bus (cuando cambian).
    void publicarSenal();
    /// Lo que se mide cada segundo (la salud de las salidas, el loudness, el orador), al bus.
    void publicarSegundo();
    /// Los vúmetros del programa (una vuelta de niveles_): si se movieron.
    bool medirPrograma();
    /// La transmisión y la grabación de la por omisión, al bus (salida/stream, salida/grabacion).
    void publicarAire();
    /// Usa conference_ (su cliente), la sesión remota y el cuerpo (asamblea_) y el motor: va
    /// después (se destruye antes).
    std::unique_ptr<escenas::ControlGrillas> grillasPersonas_;
    std::unique_ptr<sala::LugaresConGente> lugaresConGente_; ///< los lugares vacíos, en reposo en el motor
    /// Quién mira el programa de cada salida: le pide al motor el retorno a los invitados.
    /// Lee la sala (conference_), la sesión remota (asamblea_) y las pantallas: va después.
    std::unique_ptr<sala::QuienMira> quienMira_;
    /// Los avisos del runtime (AppRuntime::observar): se juntan en una máscara y se
    /// reflejan en una sola pasada por vuelta del bucle (refrescarAvisos).
    int observadorRt_ = 0;
    std::atomic<unsigned> avisosPendientes_{0};
    void refrescarAvisos();
    std::unique_ptr<config::InterfazConfig> interfaz_;        ///< Config/interfaz.json (modelo)
    QVariantMap latido_; ///< lo último del latido (se avisa solo si cambia)
    /// Lee el latido (de otro hilo, atómico) y avisa si cambió.
    void revisarLatido();
    std::unique_ptr<escenas::RespaldoEntradas> respaldo_; ///< usa los grafos de video y el conmutador de la por omisión (del dueño)
    QStringList sinSenal_; ///< lo último que avisó el respaldo (vive en el dueño)
    QVariantMap respaldos_; ///< los respaldos guardados (leerRespaldos)
    /// El grafo de video para el vigía de las alarmas (lo lee su hilo; nulo al irse).
    std::atomic<video::VideoGrafos*> videoParaVigia_{nullptr};
    /// Los asistentes, la voz, las salidas de texto y la barra de mensajes
    /// (ui/AsistenteViewModel). Usa las alarmas (su vigía) y la sala; la atención y los
    /// tótems usan su voz y sus modelos (van más abajo).
    std::unique_ptr<AsistenteViewModel> asistente_;
    std::unique_ptr<InputsViewModel> inputs_;
    std::unique_ptr<ChatOverlayRenderer> chatOverlay_;
    std::unique_ptr<FuenteQrPintor> fuenteQr_; ///< pinta las fuentes «Código QR» de las escenas
    QTimer frameTick_;

    /// El CPU de la app y si el gateway contesta: cada segundo (cpuChanged, gatewayChanged).
    QTimer segundo_;
    void alSegundo();
    double cpu_ = 0;
    ba::app::UsoCpu cpuUso_;
    bool gatewayAlive_ = false;
    double peakL_ = -100;
    double peakR_ = -100;
    /// Lee a los demás servicios: se arma al final del constructor y, declarado
    /// último, se destruye antes que ellos.
    std::unique_ptr<EnlacesViewModel> enlaces_;
    /// La atención a quien se conecta: usa la sala, la voz, los modelos, los
    /// agentes, Personas, el evento y la sesión remota. Declarada última, se
    /// destruye antes que todos ellos.
    std::unique_ptr<AtencionViewModel> atencion_;
    /// Los tótems de registro: usan la atención (su voz), Personas, el evento y
    /// la sala. Después de la atención, se destruyen antes.
    std::unique_ptr<TotemsViewModel> totems_;
    /// La medición del enlace: usa la sala, el evento, lo que está al aire y la grilla.
    /// Antes que la prueba de sala (que la consulta): se destruye después.
    std::unique_ptr<EnlaceViewModel> enlace_;
    /// La prueba de sala: usa la sala, el cupo, los recursos, el evento y el enlace.
    /// Última: se destruye antes que ellos (corta el probador y le avisa a la sala).
    std::unique_ptr<PruebaSalaViewModel> pruebaSala_;
    std::unique_ptr<PruebaAsambleaViewModel> pruebaAsamblea_;
    /// La prueba de capacidad: usa la sala, el cupo, los recursos, el enlace, pinggy y la
    /// grilla. Después de todo: se destruye primero (corta hinchada).
    std::unique_ptr<CapacidadViewModel> capacidad_;
    /// La arma con todo lo de afuera (ui/StudioCapacidad.cpp).
    void crearCapacidad();
};

} // namespace ba::app
