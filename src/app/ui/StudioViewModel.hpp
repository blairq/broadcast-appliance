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

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace ba::app::agent {
class AlertEngine;
class AlarmaSonora;
class AgentViewModel;
class Despachador;
class ModelosLocales;
class Roles;
class Voz;
class VozPrompter;
class FlujosTexto;
class Traductor;
class Subtitulos;
class VocesRack;
} // namespace ba::app::agent

namespace ba::app::audio {
class RackManager;
class Matriz;
class AudioViewModel;
} // namespace ba::app::audio

namespace ba::app::ptz {
class ControlCamaras;
} // namespace ba::app::ptz
namespace ba::app::video {
class VideoGrafos;
class SuperRes;
} // namespace ba::app::video
namespace ba::app::red {
class Red;
} // namespace ba::app::red
namespace ba::app::plugins {
class Plugins;
} // namespace ba::app::plugins
namespace ba::app::dispositivos {
class Dispositivos;
} // namespace ba::app::dispositivos

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
class EventoViewModel;
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
class OperatorMessages;
class InputsViewModel;
class ChatOverlayRenderer;
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
    /// Hablarle al asistente (push-to-talk): ver agent/Voz.hpp.
    Q_PROPERTY(QObject* voz READ voz CONSTANT)
    /// «Voz del teleprompter»: el guion dicho en voz alta, siguiendo al teleprompter.
    Q_PROPERTY(QObject* vozPrompter READ vozPrompter CONSTANT)
    /// La matriz de mezcla (fuentes × pistas y buses virtuales): audio/Matriz.hpp.
    Q_PROPERTY(QObject* matriz READ matriz CONSTANT)
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
    /// Las tarjetas «Texto a voz»: voces, idiomas y lo que dicen (agent/VocesRack.hpp).
    Q_PROPERTY(QObject* voces READ voces CONSTANT)
    /// La traducción en vivo de las salidas de texto, con su glosario (agent/Traductor.hpp).
    Q_PROPERTY(QObject* traductor READ traductor CONSTANT)
    /// La superresolución de las entradas de red: motores, modelos, laboratorio (video/SuperRes.hpp).
    Q_PROPERTY(QObject* superres READ superres CONSTANT)
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
    bool corteDirectoEditor() const { return corteDirectoEditor_; }
    void setCorteDirectoEditor(bool si);
    /// Reconoce una (id del evento) o todas (0).
    Q_INVOKABLE int reconocerAlarmaUi(qint64 id);
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
    QObject* voz() const;
    QObject* vozPrompter() const;
    QObject* matriz() const;
    QObject* camaras() const;
    QObject* video() const;
    video::VideoGrafos* videoGrafos() const { return video_.get(); }
    QObject* red() const;
    red::Red* redEquipos() const { return red_.get(); }
    /// Los plugins de Python (docs/PLUGINS.md).
    plugins::Plugins* plugins() const { return plugins_.get(); }
    QObject* pluginsQml() const;
    QObject* seguidor() const;
    Seguidor* seguidorPtz() const { return seguidor_.get(); }
    QObject* dispositivosQml() const;
    dispositivos::Dispositivos* equipos() const { return dispositivos_.get(); }
    QObject* flujos() const;
    QObject* voces() const;
    QObject* traductor() const;
    QObject* superres() const;
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
    QString escenaArmada() const;

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
    void statusChanged();
    void sceneChanged();
    /// La LISTA de escenas cambió (se agregó, borró, renombró o cambió de
    /// carpeta). Distinta de `sceneChanged`, que es «cambió la que está al
    /// aire»: una pasa cada pocos segundos en vivo y la otra casi nunca.
    void escenasCambiaron();
    /// Alguien pidió identificar los monitores (ver `identificarPantallas`).
    void identificarPedido();
    void seccionConfigChanged();
    void canvasChanged();
    void themeChanged();
    /// Aviso para el operador: level = "info" | "ok" | "warn" | "error".
    void notify(const QString& message, const QString& level);
    /// El launcher (o una señal) pidio cerrar.
    void quitRequested();
    void alarmasCambio();
    void escenasAjustesChanged();
    void placaChanged();
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
    std::unique_ptr<EventoViewModel> evento_;
    std::unique_ptr<graficos::Emisor> emisor_;
    std::unique_ptr<GraficoViewModel> grafico_;
    std::unique_ptr<RotuladorViewModel> rotulador_;
    std::unique_ptr<PrompterViewModel> prompter_;
    std::unique_ptr<PantallasViewModel> pantallas_;
    std::unique_ptr<QrViewModel> qr_;
    std::unique_ptr<PersonasViewModel> personas_;
    std::unique_ptr<PublicoViewModel> publico_;
    std::unique_ptr<MediosViewModel> medios_;
    std::unique_ptr<CarruselViewModel> carruseles_;
    std::unique_ptr<graficos::Vinculos> vinculos_;
    std::unique_ptr<RegistroViewModel> registro_;
    std::unique_ptr<ExtensionesModel> extensiones_;
    std::unique_ptr<RielConfig> riel_;
    QString seccionConfig_;
#if defined(BA_HAVE_CLAP)
    std::unique_ptr<audio::RackManager> racks_;
    std::unique_ptr<audio::Matriz> matriz_;
    std::unique_ptr<ptz::ControlCamaras> camaras_;
    std::unique_ptr<video::VideoGrafos> video_;
    std::unique_ptr<red::Red> red_;
    std::unique_ptr<plugins::Plugins> plugins_;
    std::unique_ptr<Seguidor> seguidor_;
    std::unique_ptr<dispositivos::Dispositivos> dispositivos_;
    std::unique_ptr<agent::FlujosTexto> flujos_;
    std::unique_ptr<agent::VocesRack> voces_;
    std::unique_ptr<agent::Traductor> traductor_;
    std::unique_ptr<agent::Subtitulos> subtitulos_;
    std::unique_ptr<video::SuperRes> superres_;
    std::unique_ptr<audio::AudioViewModel> audio_;
#endif
    std::unique_ptr<YouTubeViewModel> youtube_;
    std::unique_ptr<TwitchViewModel> twitch_;
    std::unique_ptr<FacebookViewModel> facebook_;
    std::unique_ptr<SystemViewModel> system_;
    std::unique_ptr<agent::AlertEngine> alerts_;
    std::unique_ptr<agent::AlarmaSonora> alarma_;
    bool corteDirectoEditor_ = false;
    unsigned vigiaTicks_ = 0;
    std::unique_ptr<agent::AgentViewModel> agent_;
    std::unique_ptr<agent::Roles> roles_;
    std::unique_ptr<agent::Despachador> despachador_;
    std::unique_ptr<agent::ModelosLocales> modelos_;
    std::unique_ptr<agent::Voz> voz_;
    std::unique_ptr<agent::VozPrompter> vozPrompter_;
    std::unique_ptr<OperatorMessages> messages_;
    std::unique_ptr<InputsViewModel> inputs_;
    std::unique_ptr<ChatOverlayRenderer> chatOverlay_;
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
    void* cpuInfo_ = nullptr; // os_cpu_usage_info_t*
    bool gatewayAlive_ = false;
    double peakL_ = -100;
    double peakR_ = -100;
};

} // namespace ba::app
