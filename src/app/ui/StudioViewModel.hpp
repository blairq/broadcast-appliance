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
#include <QImage>
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
#include "comun/UsoCpu.hpp"

#include <deque>
#include <map>
#include <memory>

namespace ba::app::agent {
class AlertEngine;
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
namespace ba::app::video {
class VideoGrafos;
class SuperRes;
class BancoSuperRes;
} // namespace ba::app::video
namespace ba::app::sala {
class CupoSala;
class VotacionSala;
class PreguntasSala;
class SeguridadSala;
}
namespace ba::app::escenas {
class Conmutador;
class RespaldoEntradas;
class AjustesEscenas;
}
namespace ba::app::config {
class ModoVivoConfig;
class InterfazConfig;
}
namespace ba::app::emision {
class RetornosInvitados;
class SeguridadAire;
}
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
class Quorum;
}

class AppRuntime;
class AccesoRemoto;
class ConferenceViewModel;
class EventoViewModel;
class HistoricoViewModel;
class GraficoViewModel;
class RotuladorViewModel;
class PrompterViewModel;
class PantallasViewModel;
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
class YouTubeViewModel;
class TwitchViewModel;
class FacebookViewModel;
class SystemViewModel;
class Configuraciones;
class OperatorMessages;
class InputsViewModel;
class ChatOverlayRenderer;
class FuenteQrPintor;
class DashboardModel;
class FrameService;
class MixerModel;
class PropertiesModel;
class ScenesModel;
class SettingsModel;
class SourcesModel;
class SourceTypesModel;

class StudioViewModel : public QObject {
    Q_OBJECT
    // --- Estado ---------------------------------------------------------------
    Q_PROPERTY(bool recording READ recording NOTIFY statusChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY statusChanged)
    /// Lo que se corta si se cierra (o se actualiza) la app: {transmitiendo, grabando, personas, hay}.
    Q_PROPERTY(QVariantMap impactoDeCerrar READ impactoDeCerrar NOTIFY statusChanged)
    /// Las entradas de video que se quedaron sin señal (el multiview las marca).
    Q_PROPERTY(QStringList sinSenal READ sinSenal NOTIFY senalChanged)
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
    /// La botonera (superficie de control de tablets y hardware): control/Botonera.
    Q_PROPERTY(QObject* botonera READ botoneraQml CONSTANT)
    /// Companion Satellite manejando la botonera (control/SatelliteServidor).
    Q_PROPERTY(QObject* satellite READ satelliteQml CONSTANT)
    /// Las caras de los botones (foto o video corto): control/CarasBotonera.
    Q_PROPERTY(QObject* carasBotonera READ carasBotoneraQml CONSTANT)
    /// Las capas DSK encima del programa (control/Dsk): logo, intérprete LSA, zócalo fijo.
    Q_PROPERTY(QObject* dsk READ dskQml CONSTANT)
    /// Seguir al micrófono abierto: propone (o arma) la banca que pidió la palabra.
    Q_PROPERTY(QObject* seguirMicrofono READ seguirMicrofonoQml CONSTANT)
    /// El orden del día de la sesión y el uso de la palabra (asamblea/OrdenDelDia).
    Q_PROPERTY(QObject* ordenDelDia READ ordenDelDiaQml CONSTANT)
    /// Quórum, mayorías y resultados oficiales de las votaciones (asamblea/Quorum).
    Q_PROPERTY(QObject* quorum READ quorumQml CONSTANT)
    /// El catálogo de acciones y los atajos de teclado (control/Acciones).
    Q_PROPERTY(QObject* acciones READ accionesQml CONSTANT)
    /// Avisos fuera del programa (webhook, Telegram, mail) para los disparadores.
    Q_PROPERTY(QObject* notificador READ notificadorQml CONSTANT)
    /// El túnel de pinggy: estado, token (solo guardado/falta) y DNS (net/AccesoRemoto).
    Q_PROPERTY(QObject* accesoRemoto READ accesoRemotoQml CONSTANT)
    /// Modo en vivo (T48): transmitiendo o con el evento en vivo; protege páginas y ajustes que cortan.
    Q_PROPERTY(bool modoVivo READ modoVivo NOTIFY statusChanged)
    /// La caída de la que se acaba de volver (T24): {causa, componente, hace_s, aire_vuelta_s?} o vacío.
    Q_PROPERTY(QVariantMap caidaReciente READ caidaReciente NOTIFY caidaChanged)
    /// El tally para el multiview: {aire: [fuentes en el programa (con las capas DSK)], armada: [las del preview]}.
    Q_PROPERTY(QVariantMap tally READ tally NOTIFY tallyChanged)
    /// Los medios que suenan/se ven al aire: [{fuente, restante_s, duracion_s}] (T46).
    Q_PROPERTY(QVariantList mediosAlAire READ mediosAlAire NOTIFY mediosChanged)
    /// Salud de cada destino de transmisión (T27): [{id, activo, kbps, perdidos_red, perdidos_enc, congestion, reconexiones, kbps_historial[]}].
    Q_PROPERTY(QVariantList saludSalidas READ saludSalidasUi NOTIFY saludChanged)
    /// El botón de cada escena del editor corta directo (si no, la arma en preview).
    Q_PROPERTY(bool corteDirectoEditor READ corteDirectoEditor WRITE setCorteDirectoEditor NOTIFY escenasAjustesChanged)
    /// La transmision se corto y se esta reintentando.
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY statusChanged)
    /// Por que se corto, en castellano. Vacio si no hubo problema.
    Q_PROPERTY(QString outputProblem READ outputProblem NOTIFY statusChanged)
    Q_PROPERTY(QString recordTime READ recordTime NOTIFY statusChanged)
    Q_PROPERTY(QString streamTime READ streamTime NOTIFY statusChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statusChanged)
    Q_PROPERTY(QString encoder READ encoder NOTIFY statusChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statusChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY statusChanged)
    Q_PROPERTY(int streamKbps READ streamKbps NOTIFY statusChanged)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY statusChanged)
    Q_PROPERTY(bool gatewayAlive READ gatewayAlive NOTIFY statusChanged)
    Q_PROPERTY(double programPeakL READ programPeakL NOTIFY statusChanged)
    Q_PROPERTY(double programPeakR READ programPeakR NOTIFY statusChanged)
    Q_PROPERTY(QString currentScene READ currentScene NOTIFY sceneChanged)
    Q_PROPERTY(QString transitionType READ transitionType NOTIFY sceneChanged)
    /// La escena ARMADA (la que sale con el proximo TAKE). Vacia = ninguna.
    Q_PROPERTY(QString escenaArmada READ escenaArmada NOTIFY sceneChanged)
    Q_PROPERTY(double canvasAspect READ canvasAspect NOTIFY canvasChanged)
    Q_PROPERTY(QString canvasLabel READ canvasLabel NOTIFY canvasChanged)
    Q_PROPERTY(QString streamServer READ streamServer NOTIFY statusChanged)
    Q_PROPERTY(bool streamKeyAvailable READ streamKeyAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool streamKeySaved READ streamKeySaved NOTIFY statusChanged)
    Q_PROPERTY(QString room READ room NOTIFY statusChanged)
    Q_PROPERTY(QString joinUrl READ joinUrl NOTIFY statusChanged)
    Q_PROPERTY(int guests READ guests NOTIFY statusChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    /// Arrancó con --test: el MCP no pide aprobaciones (la interfaz lo avisa siempre).
    Q_PROPERTY(bool modoTest READ modoTest CONSTANT)
    /// Balística de los medidores (T31): "pico", "ppm" (EBU tipo IIb) o "vu".
    Q_PROPERTY(QString balistica READ balistica WRITE setBalistica NOTIFY balisticaChanged)
    /// La fuente que se escucha por el monitor (PFL), "" si ninguna.
    Q_PROPERTY(QString pfl READ pfl NOTIFY pflChanged)
    // --- Submodelos -------------------------------------------------------------
    Q_PROPERTY(QObject* scenes READ scenes CONSTANT)
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
    /// Pantallas externas (televisores): dónde mostrar y qué mostrar.
    Q_PROPERTY(QObject* pantallas READ pantallas CONSTANT)
    /// La biblioteca de códigos QR y su estudio de diseño.
    Q_PROPERTY(QObject* qr READ qr CONSTANT)
    /// La libreta de personas (rostros). Existe siempre: sin los modelos
    /// vendoreados avisa por qué, que es mejor que una lista vacía.
    Q_PROPERTY(QObject* personas READ personas CONSTANT)
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
    Q_PROPERTY(QObject* youtube READ youtube CONSTANT)
    Q_PROPERTY(QObject* twitch READ twitch CONSTANT)
    Q_PROPERTY(QObject* facebook READ facebook CONSTANT)
    Q_PROPERTY(QObject* system READ system CONSTANT)
    Q_PROPERTY(QObject* agent READ agent CONSTANT)
    /// El modelo local (Configuración -> Modelos locales): ver agent/ModelosLocales.hpp.
    Q_PROPERTY(QObject* modelos READ modelos CONSTANT)
    /// La cola única de descargas (Descargas, arriba a la derecha): ver net/Descargas.hpp.
    Q_PROPERTY(QObject* descargas READ descargas CONSTANT)
    /// Las multivistas (pestañas de la página Multiview): [{id, nombre, fuentes, distribucion,
    /// separacion, fondo, borde, colorBorde, nombres, tamNombres, tally, vumetros}]. Config/multiview.json.
    Q_PROPERTY(QVariantList multiviews READ multiviews NOTIFY multiviewChanged)
    Q_PROPERTY(QString multiviewActual READ multiviewActual WRITE setMultiviewActual NOTIFY multiviewChanged)
    /// La de la pestaña elegida.
    Q_PROPERTY(QVariantMap multiview READ multiview NOTIFY multiviewChanged)
    /// Los ids de las multivistas que tienen una ventana abierta.
    Q_PROPERTY(QStringList multiviewsConVentana READ multiviewsConVentana NOTIFY multiviewVentanasChanged)
    /// Hablarle al asistente (push-to-talk): ver agent/Voz.hpp.
    Q_PROPERTY(QObject* voz READ voz CONSTANT)
    /// «Voz del teleprompter»: el guion dicho en voz alta, siguiendo al teleprompter.
    Q_PROPERTY(QObject* vozPrompter READ vozPrompter CONSTANT)
    /// La matriz de mezcla (fuentes × pistas y buses virtuales): audio/Matriz.hpp.
    Q_PROPERTY(QObject* matriz READ matriz CONSTANT)
    /// El Mezclador (la ventana propia): audio/Mezclador.hpp.
    Q_PROPERTY(QObject* mezclador READ mezcladorQml CONSTANT)
    /// El PTZ de cada entrada de video (digital + hardware) y Mejorar imagen:
    /// ptz/ControlCamaras.hpp.
    Q_PROPERTY(QObject* camaras READ camaras CONSTANT)
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
    explicit StudioViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~StudioViewModel() override;

    bool recording() const { return recording_; }
    /// Cuando arranco la grabacion, en ms desde la epoca. 0 = no esta grabando
    /// (o arranco antes de que la app supiera mirarlo).
    Q_INVOKABLE qint64 recordStartMs() const { return recording_ ? recSinceMs_ : 0; }
    /// El latido del supervisor: {motor_vivo, ui_viva, ui_trabada_ms, ultima_traba_ms, politica}.
    Q_INVOKABLE QVariantMap latidoUi() const;
    /// Preferencias chicas de la interfaz (anchos de paneles, lo último elegido),
    /// en Config/interfaz.json: portable, al lado de la app, nunca en el sistema.
    Q_INVOKABLE QVariant uiValor(const QString& clave, const QVariant& porDefecto = {}) const;
    Q_INVOKABLE void uiGuardar(const QString& clave, const QVariant& valor);
    /// Cambia la política del supervisor ({espera_ui_s, matar_si_ui_trabada_sin_aire}). "" o el error.
    Q_INVOKABLE QString latidoPoliticaUi(const QVariantMap& cambios);
    /// Las alarmas de los disparadores que esperan que alguien las reconozca.
    Q_INVOKABLE QVariantList alarmasActivasUi() const;
    /// El foco del teclado está en un campo de texto (las teclas T/Enter no son TAKE).
    Q_INVOKABLE bool focoEnTexto() const;
    // --- FTB y placa de seguridad (encima de todo lo que sale) ---------------
    /// Fundido a negro; `audio`: "bajar" (de fábrica) o "mantener". "" o el error.
    Q_INVOKABLE QString ftbUi(bool activar, int ms = 800, const QString& audio = "bajar");
    /// La placa con `texto` (y una imagen opcional). "" o el error.
    Q_INVOKABLE QString placaUi(bool activar, const QString& texto = {}, const QString& imagen = {}, int ms = 600);
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    Q_INVOKABLE QVariantMap estadoPlacaUi() const;
    /// Los textos de placa de siempre (y los que se usaron).
    Q_INVOKABLE QStringList textosPlaca() const;
    /// Vuelve a poner el FTB y la placa como estaban (Runtime/placa.json): tras una caída.
    void restaurarPlaca();
    /// Hay aire: transmitiendo o grabando.
    Q_INVOKABLE bool alAire() const { return streaming_ || recording_; }
    /// {transmitiendo, grabando, personas, hay}. `personas`: los de la sala (lobby,
    /// programa, fila de oradores) y los integrantes de la sesión remota.
    QVariantMap impactoDeCerrar() const;
    /// El aviso para confirmar: `accion` vacía = cerrar la ventana; si no, lo que
    /// cierra la app (p. ej. "actualizar"). "" si no se corta nada.
    Q_INVOKABLE QString avisoDeCerrar(const QString& accion = {}) const;
    /// «Apretar para hablar» desde la app (asamblea::Presidencia): mientras `activo`, el
    /// remoto oye la voz del operador en su celular. "" o el error.
    Q_INVOKABLE QString hablarARemoto(const QString& integrante, bool activo);
    /// La presidencia calla a todos (asamblea::Presidencia); lo que falle se avisa.
    Q_INVOKABLE void silenciarTodos();
    /// «Hablo yo» (asamblea::Presidencia): calla a todos y abre el micrófono de la
    /// presidencia en la central; con false lo cierra. Lo que falle se avisa.
    Q_INVOKABLE void hablaPresidencia(bool activo);
    Q_PROPERTY(bool presidenciaHabla READ presidenciaHabla NOTIFY presidenciaHablaChanged)
    bool presidenciaHabla() const;
    bool corteDirectoEditor() const;
    QObject* cuerpoQml() const;
    QStringList sinSenal() const;
    /// El respaldo de entradas (modelo).
    escenas::RespaldoEntradas& respaldoEntradas() { return *respaldo_; }
    /// Qué hacer si la entrada se cae estando al aire: {escena, al_perder_aire: avisar|armar|cortar, ms}.
    Q_INVOKABLE QVariantMap respaldoEntrada(const QString& entrada) const;
    Q_INVOKABLE QString setRespaldoEntrada(const QString& entrada, const QVariantMap& r);
    QObject* bancasQml() const;
    QObject* debateQml() const;
    QObject* configuracionesQml() const;
    Configuraciones* configuraciones() { return configuraciones_.get(); }
    asamblea::Debate* debate() { return debate_.get(); }
    QObject* dcnQml() const;
    QObject* sesionRemotaQml() const;
    QObject* votacionesQml() const;
    asamblea::Votaciones* votaciones() { return votaciones_.get(); }
    asamblea::Presidencia* presidencia() { return presidencia_.get(); }
    QObject* ccQml() const;
    agent::SubtitulosCC& cc() { return *cc_; }
    asamblea::Bancas& bancas() { return *bancas_; }
    asamblea::ConferenciaDcn& dcn() { return *dcn_; }
    asamblea::SesionRemota* sesionRemota() { return sesionRemota_.get(); }
    AppRuntime& runtime() { return rt_; }
    /// Aprueba lo que el invitado escribió de sí mismo (o un `texto` corregido)
    /// como el cargo de su zócalo. "" o el error.
    Q_INVOKABLE QString aprobarInfoInvitado(const QString& participante, const QString& texto = {});
    /// Chat: los recientes con {key, source, author, text, destacado}; destacar y sacar.
    Q_INVOKABLE QVariantList chatRecientesUi() const;
    Q_INVOKABLE bool destacarChatUi(const QString& key);
    Q_INVOKABLE void quitarDestacadoChatUi(const QString& key);
    /// Suma un mensaje del chat (por su key) a la cola de preguntas. "" o el error.
    Q_INVOKABLE QString preguntaDesdeChatUi(const QString& key);
    /// «Traducción automática»: mientras salga una traducción en pantalla o en CC, una capa DSK lo rotula.
    Q_INVOKABLE bool rotuloTraduccionActivo() const;
    Q_INVOKABLE void setRotuloTraduccion(bool activo);
    nlohmann::json rotuloTraduccionEstado() const;
    asamblea::CuerpoViewModel& cuerpo() { return *cuerpo_; }
    /// Vocabulario del reconocedor de voz (agent/Vocabulario): grupos, términos,
    /// probar una frase y traer los nombres del Cuerpo.
    Q_INVOKABLE QVariantMap vocabularioUi() const;
    Q_INVOKABLE QString vocabularioAgregarUi(const QString& termino, const QString& variantes, const QString& grupo);
    Q_INVOKABLE bool vocabularioQuitarUi(const QString& termino);
    Q_INVOKABLE void vocabularioGrupoUi(const QString& grupo, bool activo);
    /// {texto, reemplazos: [{original, reemplazo}]}.
    Q_INVOKABLE QVariantMap vocabularioProbarUi(const QString& texto);
    /// Pone los nombres y apellidos del Cuerpo en el grupo «cuerpo» (agent::Vocabulario::cargarCuerpo)
    /// y avisa cuántos. Devuelve cuántos.
    Q_INVOKABLE int vocabularioDelCuerpoUi();
    agent::Vocabulario& vocabulario() { return *vocabulario_; }
    control::BusEventos& bus() { return *bus_; }
    control::Botonera& botonera() { return *botonera_; }
    /// La botonera con el gateway (emparejar tablets, credenciales): control/BotoneraGateway.
    control::BotoneraGateway& botoneraGateway() { return *botoneraGw_; }
    control::CarasBotonera& carasBotonera() { return *carasBotonera_; }
    /// La API remota HTTP de la botonera (T59): {activo, accesos, url, aviso?}.
    Q_INVOKABLE QVariantMap apiRemotaUi() const;
    Q_INVOKABLE void setApiRemotaUi(bool activo);
    /// Un acceso nuevo para `perfil`: {dispositivo, token, url_ejemplo} (el token se ve UNA vez) o {error}.
    Q_INVOKABLE QVariantMap crearAccesoHttpUi(const QString& perfil, const QString& nombre);
    /// http://<ip de la red local>:<puerto de la sala> (vacío si no hay red).
    QString urlApiRemota() const;
    QObject* botoneraQml() const;
    QObject* satelliteQml() const;
    QObject* carasBotoneraQml() const;
    QObject* dskQml() const;
    QObject* seguirMicrofonoQml() const;
    asamblea::SeguirMicrofono& seguirMicrofono() { return *seguirMic_; }
    QObject* ordenDelDiaQml() const;
    asamblea::OrdenDelDia& ordenDelDia() { return *ordenDia_; }
    QObject* quorumQml() const;
    asamblea::Quorum& quorum() { return *quorum_; }
    QObject* accionesQml() const;
    control::Acciones& acciones() { return *acciones_; }
    QObject* notificadorQml() const;
    agent::Notificador& notificador() { return *notificador_; }
    QObject* accesoRemotoQml() const;
    AccesoRemoto* accesoRemoto() { return accesoRemoto_.get(); }
    bool modoVivo() const;
    QVariantMap caidaReciente() const { return caida_; }
    QVariantMap tally() const { return tally_; }
    QVariantList mediosAlAire() const { return mediosAlAire_; }
    QVariantList saludSalidasUi() const;
    /// La serie de un destino en los últimos `minutos` (de a un punto por segundo).
    nlohmann::json saludSerie(const std::string& destino, int minutos) const;
    nlohmann::json saludActual() const { return saludActual_; }
    /// El pico de audio de una fuente (para el vúmetro de cada celda del multiview).
    Q_INVOKABLE double picoDe(const QString& fuente) const;
    Q_INVOKABLE void cerrarCartelCaida() { caida_.clear(); emit caidaChanged(); }
    /// Las últimas `n` caídas que anotó el supervisor (Runtime/caidas.jsonl).
    nlohmann::json caidas(int n) const;
    std::filesystem::path datosRaiz() const;
    /// ¿Hay que desbloquear la página `id` (del riel) para entrar? (en vivo y en la lista)
    Q_INVOKABLE bool paginaBloqueada(const QString& id) const;
    /// La desbloquea hasta que termine el vivo; el motivo queda en el registro.
    Q_INVOKABLE void desbloquearPagina(const QString& id, const QString& motivo);
    nlohmann::json modoVivoEstado() const;
    std::string setModoVivoAjustes(const nlohmann::json& a);
    /// Los oradores remotos con el tiempo que hablaron (de la sala): [{nombre, segundos}].
    nlohmann::json remotosPalabra() const;
    /// Capítulos de la grabación en curso por punto del orden del día (texto para YouTube).
    Q_INVOKABLE QString capitulosPuntosUi() const;
    control::Dsk& dsk() { return *dsk_; }
    /// Lo que se puede poner en una capa DSK: escenas y fuentes, por nombre.
    Q_INVOKABLE QStringList nombresParaDsk() const;
    /// El disco en apuros (BroadcastEngine::discoEstado): {apuros, atraso_s, registro_trabado_ms,
    /// prioridad, automatica, modo, pausadas, graba_programa, desde_s}.
    Q_INVOKABLE QVariantMap apurosDiscoUi() const;
    /// Darle el disco a la grabación del programa (pausa ISO, repetición, registro) o devolverlo.
    Q_INVOKABLE void priorizarProgramaUi(bool si);
    /// "preguntar", "auto" o "nunca". "" o el error.
    Q_INVOKABLE QString modoDiscoUi(const QString& modo);
    /// Grabación ISO (cada entrada a su archivo): [{fuente, archivo, mb, audioPropio, activa, pausada}].
    Q_INVOKABLE QVariantList isosUi() const;
    /// Graba o deja de grabar una fuente aparte (chequea el disco y el tope). "" o el error.
    Q_INVOKABLE QString isoUi(const QString& fuente, bool grabar);
    /// Grabar aparte la fuente de un invitado de la sala (id): avisa el resultado.
    Q_INVOKABLE void isoInvitadoUi(const QString& id);
    /// La fuente de un invitado de la sala («Invitado N» por su lugar). "" si no tiene.
    Q_INVOKABLE QString fuenteDeInvitado(const QString& participante) const;
    /// Todas las de video de la escena al aire (true) o ninguna (false). Devuelve los errores.
    Q_INVOKABLE QString isoTodasUi(bool grabar);
    /// La señal limpia: {activa, destino, archivo, cuadros, perdidos, reconectando}.
    Q_INVOKABLE QVariantMap limpiaUi() const;
    /// La arranca (a `destino`, id de un destino guardado, o a disco si vacío) o la corta. "" o el error.
    Q_INVOKABLE QString limpiaActivarUi(bool activar, const QString& destino);
    /// La grabación (T29): {activa, archivo, copia, copia_activa, calidad, encoder, pistas, encoder_respaldo_activo}
    /// y la repetición {replay: {activa, segundos, ultimo}}.
    Q_INVOKABLE QVariantMap grabacionUi() const;
    QObject* playlistQml() const;
    QObject* puestoCorreccionQml() const;
    medios::Playlist& playlist() { return *playlist_; }
    /// El cupo de invitados (modelo: sala::CupoSala). La interfaz y el MCP lo piden
    /// por acá; el resultado se muestra como aviso.
    sala::CupoSala& cupoSala() { return *cupoSala_; }
    /// La votación de la sala (modelo).
    sala::VotacionSala& votacion() { return *votacion_; }
    /// La cola de preguntas de la sala (modelo).
    sala::PreguntasSala& preguntas() { return *preguntas_; }
    /// El PIN por sesión y el token del MCP (modelo: Config/seguridad.json).
    sala::SeguridadSala& seguridad() { return *seguridad_; }
    /// Desde la interfaz: "" o el error inmediato (el resultado llega como aviso).
    Q_INVOKABLE QString cupoUi(int n);
    QString balistica() const;
    bool modoTest() const { return modoTest_; }
    void setModoTest(bool on) { modoTest_ = on; }
    void setBalistica(const QString& b);
    QString pfl() const;
    /// PFL exclusivo: escucha `fuente` por el monitor (o la apaga si ya era esa). "" o el error.
    Q_INVOKABLE QString pflUi(const QString& fuente);
    /// Retardo del audio de una fuente respecto de su video (ms, como audio_sincro).
    Q_INVOKABLE int retardoUi(const QString& fuente) const;
    Q_INVOKABLE QString setRetardoUi(const QString& fuente, int ms);
    /// Medir con aplauso: mide `segundos` sobre el programa y llama a `listo` con el
    /// resultado (ver BroadcastEngine::terminarMedicionSincro). "" o el error al empezar.
    QString medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo);
    /// Lo mismo para la GUI: el resultado llega por medicionSincroLista(resultado).
    Q_INVOKABLE QString medirSincroUi(int segundos);
    /// Un scope (onda|parade|vector|histograma) de `fuente` ("" = programa):
    /// {imagen: data URL PNG, legalidad: {...}, legalizador} o {error}.
    Q_INVOKABLE QVariantMap scopeUi(const QString& fuente, const QString& tipo, int ancho, int alto) const;
    /// Legalizar para TV: no | pal | ntsc | rango. "" o el error.
    Q_INVOKABLE QString legalizarUi(const QString& modo);
    /// «Repetir»: guarda los últimos segundos y, cuando el clip está escrito, lo arma
    /// en la escena «Repetición» (lista para el take). "" o el error.
    Q_INVOKABLE QString repetirUi();
    /// Pone `archivo` en la escena «Repetición» (la crea si falta) y la deja armada.
    QString armarRepeticion(const QString& archivo);
    /// Espera (hasta `ms`) a que haya un clip nuevo distinto de `antes` y llama a `listo` con él ("" si no llegó).
    void esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo);
    control::SatelliteServidor& satellite() { return *satellite_; }
    /// Empareja una tablet: código de un uso (2 min) y el QR. Contesta con
    /// emparejamientoListo({url, qr (data URL de un SVG), codigo, vence_ms, huella, aviso, error}).
    Q_INVOKABLE void emparejarTabletUi(const QString& perfil, const QString& nombre, int venceDias);
    /// Una persona aprueba (o no) la tablet que pidió emparejarse.
    Q_INVOKABLE void aprobarTabletUi(const QString& solicitud, bool aprobar);
    QObject* busQml() const;
    void setCorteDirectoEditor(bool si);
    /// Reconoce una (id del evento) o todas (0).
    Q_INVOKABLE int reconocerAlarmaUi(qint64 id);
    /// Le saca (o le devuelve) el sonido a un disparador: sus alarmas siguen apareciendo, sin sonar.
    Q_INVOKABLE bool silenciarAlarmaUi(const QString& disparador, bool silenciar);
    /// Hay una alarma crítica sin reconocer (la franja roja de arriba).
    Q_INVOKABLE bool alarmaCritica() const;
    /// El sonido de las alarmas (volumen, dispositivo, probar).
    Q_INVOKABLE QObject* alarmaSonora() const;
    bool streaming() const { return streaming_; }
    bool reconnecting() const { return reconnecting_; }
    QString outputProblem() const { return outputProblem_; }
    QString recordTime() const;
    QString streamTime() const;
    double fps() const { return fps_; }
    QString encoder() const { return encoder_; }
    int droppedFrames() const { return dropped_; }
    int totalFrames() const { return total_; }
    int streamKbps() const { return kbps_; }
    double cpuPercent() const { return cpu_; }
    bool gatewayAlive() const { return gatewayAlive_; }
    double programPeakL() const { return peakL_; }
    double programPeakR() const { return peakR_; }
    QString currentScene() const;
    QString transitionType() const;
    double canvasAspect() const;
    QString canvasLabel() const;
    QString streamServer() const;
    bool streamKeyAvailable() const;
    bool streamKeySaved() const;
    QString room() const;
    QString joinUrl() const;
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
    HistoricoViewModel* historico() const { return historico_.get(); }
    QObject* grafico() const;
    QObject* rotulador() const;
    QObject* prompter() const;
    QObject* pantallas() const;
    QObject* qr() const;
    QObject* personas() const;
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
    QObject* youtube() const;
    QObject* twitch() const;
    QObject* facebook() const;
    QObject* system() const;
    QObject* agent() const;
    QObject* modelos() const;
    QObject* descargas() const;
    QVariantMap multiview() const;
    QVariantList multiviews() const;
    QString multiviewActual() const;
    void setMultiviewActual(const QString& id);
    Q_INVOKABLE QVariantMap multiviewDe(const QString& id) const;
    /// Cambia lo que venga en `cambios` (lo demás queda) y lo guarda: la elegida / la de `id`.
    Q_INVOKABLE void setMultiview(const QVariantMap& cambios);
    Q_INVOKABLE void setMultiviewDe(const QString& id, const QVariantMap& cambios);
    /// Una pestaña nueva (de fábrica o copia de la elegida); queda elegida. Devuelve su id.
    Q_INVOKABLE QString nuevaMultiview(const QString& nombre, bool copiarActual);
    Q_INVOKABLE void renombrarMultiview(const QString& id, const QString& nombre);
    /// false si es la última (siempre queda una).
    Q_INVOKABLE bool borrarMultiview(const QString& id);
    Q_INVOKABLE void restablecerMultiview();
    /// Los monitores: [{nombre, ancho, alto, principal}].
    Q_INVOKABLE QVariantList monitores() const;
    /// Abre la ventana de esa multivista (cierra la que tuviera) en `monitor` ("" =
    /// el principal, en una ventana), en pantalla completa si se pide.
    Q_INVOKABLE void abrirMultiviewEnVentana(const QString& id, const QString& monitor, bool pantallaCompleta);
    /// Una ventana por multivista: una por monitor (los que no son el principal), en
    /// pantalla completa; si sobran o no hay otros monitores, ventanas escalonadas.
    /// Devuelve cuántas abrió.
    Q_INVOKABLE int abrirTodasLasMultiviews();
    Q_INVOKABLE int cerrarVentanasMultiview();
    QStringList multiviewsConVentana() const;
    QObject* voz() const;
    QObject* vozPrompter() const;
    QObject* matriz() const;
    QObject* mezcladorQml() const;
    QObject* camaras() const;
    QObject* video() const;
    video::VideoGrafos* videoGrafos() const { return video_.get(); }
    QObject* red() const;
    red::Red* redEquipos() const { return red_.get(); }
    /// Los plugins de Python (docs/funciones/PLUGINS.md).
    plugins::Plugins* plugins() const { return plugins_.get(); }
    QObject* pluginsQml() const;
    QObject* seguidor() const;
    Seguidor* seguidorPtz() const { return seguidor_.get(); }
    QObject* dispositivosQml() const;
    dispositivos::Dispositivos* equipos() const { return dispositivos_.get(); }
    QObject* flujos() const;
    QObject* voces() const;
    QObject* traductor() const;
    QObject* estadoIA() const;
    agent::EstadoIA* estadoIAModelo() const { return estadoIA_.get(); }
    QObject* superres() const;
    QObject* bancoSrObj() const;
    video::BancoSuperRes* bancoSr() const { return bancoSr_.get(); }
    video::SuperRes* superresVideo() const { return superres_.get(); }
    agent::Traductor* traductorAgent() const { return traductor_.get(); }
    agent::ModelosLocales* modelosLocales() const { return modelos_.get(); }
    QObject* messages() const;
    QObject* inputs() const;
    QObject* audio() const;
#if defined(BA_HAVE_CLAP)
    /// Racks de audio (modulos LSP) de las fuentes. nullptr si no hay libobs/CLAP.
    audio::RackManager* racks() const { return racks_.get(); }
#endif
    agent::AlertEngine& alerts() { return *alerts_; }
    agent::Despachador& despachador() { return *despachador_; }
    QObject* despachadorQml() const;

    // --- Comandos ---------------------------------------------------------------
    /// Pasos para conectar una plataforma (misma fuente que el MCP: AccountGuides).
    Q_INVOKABLE QVariantMap accountGuide(const QString& platform) const;
    /// Guarda la credencial de una plataforma desde el formulario seguro: campos
    /// del formulario, {"importFile": ruta} o {"fromDownloads": true}. "" o el error.
    Q_INVOKABLE QString saveAccountCredential(const QString& platform, const QVariantMap& values);
    Q_INVOKABLE void toggleRecording();
    /// Graba aunque quede poco disco (lo confirmó el operador).
    Q_INVOKABLE void grabarIgual();
    /// {libre_gb, horas, kbps, carpeta} y el remux: {archivo, destino, pct, andando, ok, error}.
    Q_INVOKABLE QVariantMap discoUi() const;
    /// Lo que oye el invitado del slot: "mix_minus" (el programa sin su voz) o "programa".
    Q_INVOKABLE QString retornoInvitadoUi(int slot) const;
    Q_INVOKABLE QString setRetornoInvitadoUi(int slot, const QString& modo);
    /// El loudness de lo que sale: {m, s, i, tp, lra, segundos, gr_db, objetivo, fuera_de_objetivo}.
    Q_INVOKABLE QVariantMap loudnessUi() const;
    Q_INVOKABLE void resetLoudnessUi();
    /// {perfil: r128|a85|plataforma16|plataforma14|custom, lufs?, tp?}. "" o el error.
    Q_INVOKABLE QString objetivoLoudnessUi(const QVariantMap& o);
    Q_INVOKABLE QVariantMap remuxUi() const;
    Q_INVOKABLE void startStreaming(const QString& server, const QString& key);
    Q_INVOKABLE void stopStreaming();
    /// Pasa la transmisión a otro destino estando al aire (lo confirmó el operador).
    Q_INVOKABLE void cambiarDestinoStream(const QString& server, const QString& key);
    // Destinos adicionales (respaldo, otras plataformas): comparten los codificadores.
    Q_INVOKABLE QVariantList destinosUi() const;
    Q_INVOKABLE QString guardarDestinoUi(const QVariantMap& d);
    Q_INVOKABLE QString borrarDestinoUi(const QString& id);
    Q_INVOKABLE QString iniciarDestinoUi(const QString& id);
    Q_INVOKABLE void detenerDestinoUi(const QString& id);
    // --- Bus de preview y TAKE ------------------------------------------------
    // Una mesa de switcher tiene DOS buses: lo que esta al aire (programa) y lo
    // que va a salir (preview). El operador arma el proximo plano, lo MIRA, y
    // recien ahi hace TAKE. Sin eso, cada cambio de plano es a ciegas y en
    // vivo: lo que esta roto se descubre cuando ya salio.
    //
    // `takeScene` sigue estando y sigue yendo directo al aire: hay momentos en
    // los que hay que cortar YA y pedir dos pasos seria peor.
    Q_INVOKABLE void takeScene(const QString& name);
    /// Arma una escena en el bus de preview (NO la pone al aire).
    Q_INVOKABLE void armarEscena(const QString& name);
    /// Pone al aire la escena armada, con la transicion configurada.
    ///
    /// Y deja armada la que SALIO del aire, como cualquier mesa: despues de un
    /// take, el bus de preview queda con el plano anterior, listo para volver.
    Q_INVOKABLE void take();
    /// AUTO con otra transición solo esta vez («cut» = CUT). `ms` < 0: la duración global.
    Q_INVOKABLE void takeCon(const QString& tipo, int ms);
    /// T-bar hacia la escena armada: `t` de 0 a 1; `soltar` termina (pasada la mitad, queda al aire).
    Q_INVOKABLE void tbar(double t, bool soltar);
    /// La transición con la que ENTRA una escena (vacío = la global). {tipo, ms}.
    Q_INVOKABLE QVariantMap transicionEntrada(const QString& escena) const;
    Q_INVOKABLE QString setTransicionEntrada(const QString& escena, const QString& tipo, int ms);
    /// Audio que sigue al video (AFV): la fuente sube cuando su escena entra al aire y baja cuando sale.
    Q_INVOKABLE bool afv(const QString& fuente) const;
    Q_INVOKABLE void setAfv(const QString& fuente, bool sigue);
    /// Las fuentes con AFV, para enlazar en la vista (afv() es una llamada: no se
    /// reevalúa y el botón no se enteraba de un cambio hecho en otra vista o por MCP).
    Q_PROPERTY(QStringList afvFuentes READ afvFuentes NOTIFY afvChanged)
    QStringList afvFuentes() const;
    QString escenaArmada() const;
    /// El conmutador de escenas (modelo): la GUI y el MCP cambian de escena por ahí.
    escenas::Conmutador& conmutador() { return *conmutador_; }
    /// El FTB y la placa de seguridad (modelo): la GUI y el MCP los ponen por ahí.
    emision::SeguridadAire& seguridadAire() { return *seguridadAire_; }
    /// Lo que oye cada invitado (modelo, Config/retornos.json).
    emision::RetornosInvitados& retornos() { return *retornos_; }
    /// El objetivo de loudness guardado (modelo, Config/loudness.json).
    audio::ObjetivoLoudness& objetivoLoudness() { return *objetivoLoudness_; }

    /// Las escenas AGRUPADAS POR CARPETA, en el orden que puso el operador:
    /// `[{carpeta: "", escenas: ["a", "b"]}, {carpeta: "Bloque 1", ...}]`.
    ///
    /// Esto se arma acá y no en QML porque desde QML no se puede recorrer un
    /// QAbstractListModel. Devuelve SOLO la agrupación, sin decir cuál está al
    /// aire ni cuál armada: esas dos salen de `currentScene` y `escenaArmada`,
    /// que ya avisan cuando cambian. Metiéndolas acá habría que reconstruir la
    /// lista entera en cada corte, y la lista no cambió.
    Q_INVOKABLE QVariantList escenasPorCarpeta() const;

    /// Muestra un cartel grande en CADA monitor con su número y su nombre,
    /// unos segundos. Es la única forma de saber cuál es «DP-1» sin ponerse a
    /// mover ventanas a ver cuál se mueve.
    Q_INVOKABLE void identificarPantallas() { emit identificarPedido(); }

    /// Los monitores donde YA hay una ventana nuestra, por nombre. Lo usa una
    /// ventana que se abre para nacer en uno libre en vez de encima de otra.
    Q_INVOKABLE QStringList ventanasAbiertas() const;

    /// Si en este escritorio un programa puede decir DONDE va una ventana.
    ///
    /// En Wayland no: ubicarlas es del compositor. Y lo peor es que pedirlo no
    /// falla —Qt hasta informa la pantalla nueva— pero la ventana no se mueve.
    /// Con esto la interfaz puede hacer lo único que sí funciona (pantalla
    /// completa en un monitor) en vez de un botón que no hace nada.
    Q_PROPERTY(bool puedeUbicarVentanas READ puedeUbicarVentanas CONSTANT)
    static bool puedeUbicarVentanas();

   private:
    /// Le manda el estado de los buses a las pantallas de tally de la sala.
    /// Se llama en cada cambio de escena y de aire: un tally que llega tarde
    /// es peor que no tenerlo, porque alguien se confia.
    void publicarTally();

   public:
    Q_INVOKABLE void addScene(const QString& name);
    Q_INVOKABLE void removeScene(const QString& name);
    Q_INVOKABLE void renameScene(const QString& from, const QString& to);
    /// Copia una escena entera con sus fuentes. Devuelve el nombre que le
    /// quedo (le agrega un numero si el que se pidio ya existia), o vacio si
    /// no se pudo.
    Q_INVOKABLE QString copyScene(const QString& from, const QString& to = {});
    Q_INVOKABLE void setSceneProtected(const QString& name, bool protegida);
    Q_INVOKABLE bool sceneProtected(const QString& name) const;
    /// Mueve la escena a una carpeta ("" la saca). Solo ordena la lista.
    Q_INVOKABLE void setSceneFolder(const QString& name, const QString& folder);
    /// Carpetas que ya existen (para ofrecerlas al mover una escena).
    Q_INVOKABLE QStringList sceneFolders() const;
    /// Reordena: deja `name` justo antes de `before` (vacio = al final), en la
    /// carpeta de `before`.
    Q_INVOKABLE void moveScene(const QString& name, const QString& before);
    /// El operador le encarga al asistente atender a alguien del lobby: que le
    /// pida sus datos por chat y despues le presente todo junto para admitirlo.
    /// Devuelve el error o "".
    Q_INVOKABLE QString assistantIntake(const QString& id, const QString& name, bool yaAdentro);

    /// Copia un texto al portapapeles y lo avisa.
    ///
    /// Vive acá y no en la sala: copiar un enlace no tiene nada que ver con una
    /// videoconferencia. Estaba en ConferenceViewModel y por eso el diálogo de
    /// QR —que lo usan el teleprompter y los rótulos— no podía copiar nada.
    Q_INVOKABLE void copiar(const QString& texto);
    /// Lo engancha McpService (necesita el nucleo MCP, que vive afuera).
    void setIntakeSink(std::function<QString(const QString&, const QString&, bool)> fn) {
        intakeSink_ = std::move(fn);
    }
    /// Escena que muestra el lienzo del editor ("" = ninguna). Se renderiza
    /// aunque no este al aire, asi una escena vacia se ve vacia.
    Q_INVOKABLE void setScenePreview(const QString& name);
    Q_INVOKABLE void save();
    /// Relee escenas, fuentes y mezclador del motor (algo los cambio por
    /// fuera de la vista, p. ej. el servidor MCP).
    void refreshModels();

   signals:
    void presidenciaHablaChanged();
    void saludChanged();
    void tallyChanged();
    void mediosChanged();
    void caidaChanged();
    void emparejamientoListo(const QVariantMap& r);
    void statusChanged();
    void sceneChanged();
    /// La LISTA de escenas cambió (se agregó, borró, renombró o cambió de
    /// carpeta). Distinta de `sceneChanged`, que es «cambió la que está al
    /// aire»: una pasa cada pocos segundos en vivo y la otra casi nunca.
    void escenasCambiaron();
    /// Alguien pidió identificar los monitores (ver `identificarPantallas`).
    void identificarPedido();
    void seccionConfigChanged();
    /// Pide a una página que muestre una parte suya (ui_show_page {page, seccion}).
    void mostrarSeccion(const QString& pagina, const QString& seccion);
    void multiviewChanged();
    void multiviewVentanasChanged();
    /// qr_mostrar: abrir una ventana con el QR de `texto`, su descripción, una
    /// explicación y un color (verde, azul… o #rrggbb), con el botón de copiar.
    void mostrarQr(const QString& texto, const QString& descripcion, const QString& mensaje, const QString& color);
    /// Pide a una página que abra (o cierre: "cerrar") un panel o diálogo suyo
    /// (ui_abrir_panel {page, panel}): lo que el operador abre con un botón.
    void abrirPanel(const QString& pagina, const QString& panel);
    void canvasChanged();
    void medicionSincroLista(const QVariantMap& resultado);
    void balisticaChanged();
    void pflChanged();
    void afvChanged();
    void themeChanged();
    /// Aviso para el operador: level = "info" | "ok" | "warn" | "error".
    void notify(const QString& message, const QString& level);
    /// El launcher (o una señal) pidio cerrar.
    void quitRequested();
    void alarmasCambio();
    void escenasAjustesChanged();
    void placaChanged();
    void senalChanged();
    /// Se pidió transmitir a otro destino estando al aire: que el operador confirme.
    void pedirCambioDestino(const QString& actual, const QString& nuevoServer, const QString& nuevoKey);
    void destinosChanged();
    /// Queda muy poco disco para grabar: que el operador confirme.
    void pedirConfirmarGrabacion(double horas);

   private slots:
    void onTick();

   private:
    AppRuntime& rt_;
    std::function<QString(const QString&, const QString&, bool)> intakeSink_;
    std::unique_ptr<ScenesModel> scenes_;
    std::unique_ptr<SourcesModel> sources_;
    std::unique_ptr<MixerModel> mixer_;
    std::unique_ptr<SourceTypesModel> types_;
    std::unique_ptr<SettingsModel> settings_;
    std::unique_ptr<DashboardModel> dashboard_;
    std::unique_ptr<FrameService> frames_;
    std::unique_ptr<PropertiesModel> props_;
    std::unique_ptr<ConferenceViewModel> conference_;
    std::unique_ptr<sala::CupoSala> cupoSala_; ///< usa el cliente de conference_: va después (se destruye antes)
    std::unique_ptr<sala::VotacionSala> votacion_; ///< ídem
    std::unique_ptr<sala::PreguntasSala> preguntas_; ///< ídem
    std::unique_ptr<sala::SeguridadSala> seguridad_; ///< ídem
    std::unique_ptr<AccesoRemoto> accesoRemoto_; ///< usa el cliente de conference_: va después (se destruye antes)
    std::unique_ptr<EventoViewModel> evento_;
    std::unique_ptr<graficos::Emisor> emisor_;
    std::unique_ptr<GraficoViewModel> grafico_;
    std::unique_ptr<RotuladorViewModel> rotulador_;
    std::unique_ptr<PrompterViewModel> prompter_;
    // El scope de una fuente que no es el programa: el último cuadro que llegó
    // (scopeUi lo pide aparte; la interfaz no espera el cuadro).
    mutable bool scopeCapturando_ = false;
    mutable QString scopeFuente_, scopeError_;
    mutable QImage scopeCuadro_;
    std::unique_ptr<PantallasViewModel> pantallas_;
    std::unique_ptr<QrViewModel> qr_;
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
    QString seccionConfig_;
#if defined(BA_HAVE_CLAP)
    std::unique_ptr<audio::RackManager> racks_;
    std::unique_ptr<audio::Matriz> matriz_;
    std::unique_ptr<audio::Mezclador> mezclador_;
    std::unique_ptr<ptz::ControlCamaras> camaras_;
    std::unique_ptr<video::VideoGrafos> video_;
    std::unique_ptr<red::Red> red_;
    std::unique_ptr<plugins::Plugins> plugins_;
    std::unique_ptr<Seguidor> seguidor_;
    std::unique_ptr<dispositivos::Dispositivos> dispositivos_;
    std::unique_ptr<agent::FlujosTexto> flujos_;
    std::unique_ptr<PuestoCorreccion> puestoCorreccion_;
    bool modoTest_ = false;
    std::unique_ptr<agent::VocesRack> voces_;
    std::unique_ptr<agent::Traductor> traductor_;
    std::unique_ptr<agent::EstadoIA> estadoIA_;
    std::unique_ptr<agent::Subtitulos> subtitulos_;
    std::unique_ptr<agent::SubtitulosCC> cc_;
    std::unique_ptr<video::SuperRes> superres_;
    std::unique_ptr<video::BancoSuperRes> bancoSr_;
    std::unique_ptr<audio::AudioViewModel> audio_;
#endif
    std::unique_ptr<YouTubeViewModel> youtube_;
    std::unique_ptr<TwitchViewModel> twitch_;
    std::unique_ptr<FacebookViewModel> facebook_;
    std::unique_ptr<SystemViewModel> system_;
    std::unique_ptr<Configuraciones> configuraciones_;
    std::unique_ptr<agent::AlertEngine> alerts_;
    std::unique_ptr<agent::AlarmaSonora> alarma_;
    std::unique_ptr<asamblea::CuerpoViewModel> cuerpo_;
    std::unique_ptr<agent::Vocabulario> vocabulario_;
    std::unique_ptr<control::BusEventos> bus_;
    /// bitacora.jsonl del evento en curso (docs/planes/PLAN-EVENTOS-HISTORICO.md, H2).
    std::unique_ptr<evento::Bitacora> bitacora_;
    std::unique_ptr<HistoricoViewModel> historico_;
    /// Lo que no va del bus a la bitácora (cambia en vivo desde Ajustes del histórico).
    std::shared_ptr<std::vector<std::string>> busExcluir_;
    int bitacoraOyente_ = 0, bitacoraBus_ = 0;
    void armarBitacora();
    /// Las fotos de Config/ por evento (H3): la última vista y la revisión cada 10 s.
    std::unique_ptr<evento::FotosConfig> fotos_;
    nlohmann::json fotoUltima_;
    /// Saca la foto de ahora y anota en la bitácora lo que cambió desde la
    /// última. `momento`: "inicio" | "cierre" | "" (solo revisar), del evento
    /// `carpeta` (vacío = el en curso).
    void revisarConfig(const std::string& carpeta, const std::string& momento);
    /// grabaciones.json y transmisiones.json del evento en curso (H4): cada
    /// grabación con su archivo y desde/hasta; cada vivo con su URL y caídas.
    void indexarSalida(const std::string& tema, const nlohmann::json& valor);
    /// Lo que queda al cerrar un evento (H5): asamblea/, registro/ y, en otro
    /// hilo, sellos.json. Los hilos se cancelan y esperan al cerrar la app.
    void cerrarEnHistorico(const std::string& carpeta);
    struct Sellador {
        std::thread hilo;
        std::shared_ptr<std::atomic<bool>> listo;
    };
    std::vector<Sellador> selladores_;
    std::atomic<bool> cancelarSellos_{false};
    std::unique_ptr<control::CarasBotonera> carasBotonera_;
    std::unique_ptr<control::Botonera> botonera_;
    std::unique_ptr<control::BotoneraGateway> botoneraGw_;
    std::unique_ptr<control::SatelliteServidor> satellite_;
    std::unique_ptr<control::Dsk> dsk_;
    std::unique_ptr<control::Acciones> acciones_;
    std::unique_ptr<agent::Notificador> notificador_;
    std::unique_ptr<config::ModoVivoConfig> modoVivoCfg_; ///< Config/modo-vivo.json (modelo)
    QSet<QString> desbloqueadas_;
    bool estabaEnVivo_ = false;
    /// Los conectados que se cortan al cerrar: sala + sesión remota.
    int personasConectadas() const;
    bool eventoEnVivo_ = false;
    QVariantMap caida_;
    QVariantMap tally_;
    QVariantList mediosAlAire_;
    struct Salud {
        std::uint64_t bytesAntes = 0;
        int perdidosAntes = 0;
        int reconexiones = 0;
        bool reconectando = false;
        qint64 reconectandoDesde = 0;
        qint64 reconexionMs = 0; ///< lo que tardó la última reconexión
        std::deque<nlohmann::json> serie; ///< 600 puntos (10 min)
    };
    std::map<std::string, Salud> salud_;
    nlohmann::json saludActual_ = nlohmann::json::array();
    std::uint64_t saltadosAntes_ = 0;
    void muestrearSalud();
    std::unique_ptr<escenas::Conmutador> conmutador_; ///< el aire y la armada (modelo)
    std::unique_ptr<emision::SeguridadAire> seguridadAire_;     ///< FTB y placa (modelo)
    std::unique_ptr<emision::RetornosInvitados> retornos_;      ///< (modelo)
    std::unique_ptr<audio::ObjetivoLoudness> objetivoLoudness_; ///< (modelo)
    qint64 caidaMs_ = 0;
    std::unique_ptr<asamblea::SeguirMicrofono> seguirMic_;
    std::unique_ptr<asamblea::OrdenDelDia> ordenDia_;
    std::unique_ptr<asamblea::Quorum> quorum_;
    std::unique_ptr<agent::RotuloTraduccionConfig> rotulo_; ///< Config/rotulo-traduccion.json (modelo)
    QTimer rotuloApagar_;
    void rotularTraduccion(const QString& flujo, const QString& origen);
    /// Publica en el bus lo que cambió (cada vuelta del reloj, 10 Hz).
    void publicarEventos();
    int busTicks_ = 0;
    std::unique_ptr<asamblea::Bancas> bancas_;
    std::unique_ptr<asamblea::Debate> debate_;
    nlohmann::json centralAntesDelDebate_; ///< modo y máximo de abiertos para devolverle a la central
    std::unique_ptr<asamblea::ConferenciaDcn> dcn_;
    std::unique_ptr<asamblea::SesionRemota> sesionRemota_;
    std::unique_ptr<asamblea::Votaciones> votaciones_; ///< usa sesionRemota_, dcn_, quorum_ y bus_: va después (se destruye antes)
    /// Usa sesionRemota_, bancas_, debate_, dcn_, votaciones_, cuerpo_, quorum_, ordenDia_, conference_ y bus_.
    std::unique_ptr<asamblea::Presidencia> presidencia_;
    qint64 retornoVistoMs_ = 0; ///< la última vez que alguien miraba el retorno (ver onTick)
    /// Los avisos del runtime (AppRuntime::observar): se juntan en una máscara y se
    /// reflejan en una sola pasada por vuelta del bucle (refrescarAvisos).
    int observadorRt_ = 0;
    std::atomic<unsigned> avisosPendientes_{0};
    void refrescarAvisos();
    /// Las caras de los botones ya achicadas (ruta|fecha -> data: URL).
    std::map<QString, std::string> imagenesBotonera_;
    /// El orador presencial cuyo tiempo se lleva en la central (banca@desde) y los avisos ya dados.
    std::string dcnOrador_;
    int dcnAvisos_ = 0;
    int dcnTic_ = 0;
    void tiemposCentral();
    std::unique_ptr<escenas::AjustesEscenas> ajustesEscenas_; ///< Config/escenas.json (modelo)
    std::unique_ptr<config::InterfazConfig> interfaz_;        ///< Config/interfaz.json (modelo)
    QHash<QString, QString> infoAprobada_; ///< participante -> cargo aprobado por el operador
    unsigned vigiaTicks_ = 0;
    std::unique_ptr<escenas::RespaldoEntradas> respaldo_; ///< usa video_ y conmutador_
    void revisarSenales();
    std::unique_ptr<agent::AgentViewModel> agent_;
    std::unique_ptr<agent::Roles> roles_;
    std::unique_ptr<agent::Despachador> despachador_;
    std::unique_ptr<agent::ModelosLocales> modelos_;
    std::unique_ptr<agent::Voz> voz_;
    std::unique_ptr<agent::VozPrompter> vozPrompter_;
    std::unique_ptr<OperatorMessages> messages_;
    std::unique_ptr<InputsViewModel> inputs_;
    std::unique_ptr<ChatOverlayRenderer> chatOverlay_;
    std::unique_ptr<FuenteQrPintor> fuenteQr_; ///< pinta las fuentes «Código QR» de las escenas
    QTimer tick_;
    QTimer frameTick_;

    bool recording_ = false;
    bool streaming_ = false;
    bool reconnecting_ = false;
    QString outputProblem_;
    std::chrono::steady_clock::time_point recSince_{};
    /// Lo mismo pero en reloj de PARED. `recSince_` es monotono —sirve para
    /// contar cuanto lleva grabando y no se mueve si alguien cambia la hora—
    /// pero no se puede comparar con el recorrido del guion, que va en reloj de
    /// pared. Para los capitulos hacen falta los dos en la misma escala.
    qint64 recSinceMs_ = 0;
    std::chrono::steady_clock::time_point streamSince_{};
    double fps_ = 0;
    QString encoder_;
    int dropped_ = 0;
    int total_ = 0;
    int kbps_ = 0;
    std::uint64_t lastBytes_ = 0;
    std::chrono::steady_clock::time_point lastBytesAt_{};
    double cpu_ = 0;
    ba::app::UsoCpu cpuUso_;
    bool gatewayAlive_ = false;
    double peakL_ = -100;
    double peakR_ = -100;
    QVariantList multiviews_;
    QString multiviewActual_;
    bool multiviewLeido_ = false;
    void leerMultiviews() const;
    int indiceMultiview(const QString& id) const;
    void guardarMultiviews();
};

} // namespace ba::app
