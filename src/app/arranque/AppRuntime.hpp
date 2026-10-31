// ============================================================================
// src/app/arranque/AppRuntime.hpp — El Proceso A en marcha, sin importar la interfaz
//
// Proposito:
//   Todo lo que hace ba-app para estar "al aire", comun a la consola
//   (OperateMode) y al dashboard (QML): leer la configuracion (AppSettings) y
//   la linea de comandos, arrancar el motor, armar escenas y fuentes, publicar
//   el retorno hacia B, latir en BA.Health y atender el canal de control.
//
//   Cada accion del operador actualiza el motor Y la configuracion en memoria;
//   `save()` la persiste. Asi lo que se arma en el dashboard sobrevive a un
//   reinicio sin que la interfaz toque JSON.
//
// Modelo de threading (F3-03; el patrón en docs/arquitectura/05-motor-del-plano.md):
//   EL DUEÑO es el hilo del plano de control (arranque/PlanoControl; en la consola, su
//   bucle): arranca en el hilo que llama a start y se muda con cederDueno. El motor y la
//   configuración (cfg_) son suyos: corre el reloj del motor (ticMotor), las órdenes que le
//   encarga la interfaz (emision/VentanillaMotor: la agenda, con el turno) y las
//   herramientas MCP del control, y publica la FOTO que lee la interfaz (publicarFoto,
//   emision/FotoMotor.hpp). Lo demás es «desde cualquier hilo»: ticControl, el documento
//   de aire, salidasPublicadas, avisosPersistencia, latirInterfaz (el latido de la
//   interfaz, en su reloj). `requestStop()` es async-signal-safe (un atomic).
//   Todo lo que toca el motor o cfg_ lo hace CON EL TURNO (emision/TurnoMotor.hpp): el
//   dueño lo toma por orden y por vuelta; la interfaz que todavía entra directo (las áreas
//   sin migrar, por config()/engine() o los métodos de acá) lo toma por evento y se cuenta.
//   Los avisos (observar) corren en el dueño: quien escucha y es de otro hilo se encola
//   (los view models se lo encargan a su bucle). Desde otro hilo, lo que es solo del dueño
//   (guardar app.json, avisar) se le encarga encolado (enDueno).
//
// Estrategia de errores:
//   No lanza. `start` devuelve el codigo de salida si no se puede arrancar;
//   las acciones devuelven bool y dejan el motivo en `lastError()`, que es DEL HILO que
//   llamó (como errno: lo que falló en el control no pisa lo de la interfaz).
// ============================================================================

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <cstdint>
#include <filesystem>
#include <map>
#include <functional>
#include <mutex>
#include <memory>
#include <set>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "arranque/AppSettings.hpp"
#include "emision/LatidoAire.hpp"
#include "emision/BroadcastEngine.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "emision/OperatorFeed.hpp"
#include "emision/SalidasConfig.hpp"
#include "emision/DocumentoAire.hpp"
#include "emision/FotoMotor.hpp"
#include "audio/ObjetivoLoudness.hpp"

namespace ba::app {

class AppRuntime {
   public:
    /// Lee la configuracion y la linea de comandos (sin arrancar nada).
    void configure(int argc, char** argv);

    /// Arranca motor, escenas, salidas, salud y canal de control.
    /// :returns: 0 si quedo en marcha; si no, el codigo de salida del proceso.
    int start();

    /// Una vuelta de trabajo: ticControl, ticMotor y el latido. La consola la llama cada
    /// ~100 ms; con interfaz, cada parte la corre su reloj (arranque/PlanoControl).
    void tick();
    /// Lo que no depende de la interfaz ni del motor: el canal de órdenes del launcher y
    /// el documento de aire (este hilo es su dueño). En el hilo del plano de control.
    void ticControl();
    /// Lo del motor, en el dueño y con el turno (si la interfaz lo tiene, esta vuelta se
    /// saltea): la sección de las salidas del documento, el rearme por la GPU, los
    /// recortes, el audio que llegó tarde y la foto.
    void ticMotor();
    /// El latido de la interfaz (el supervisor sabe si vive) y si hay aire (LatidoAire): en
    /// el reloj de la interfaz, separado del del motor. No toca el motor.
    void latirInterfaz();
    /// Quién vuelca el documento de aire: `despertar` le pide que lo haga en su hilo
    /// (el plano de control encola ticControl). Vacío: el que lo cambia lo vuelca.
    void setDuenoDelAire(std::function<void()> despertar);
    /// [{id, nombre, por_omision}] de las salidas, como estaban en la última vuelta del
    /// motor. Desde cualquier hilo (el MCP en el plano de control).
    nlohmann::json salidasPublicadas() const;

    // --- El hilo dueño y los demás (F3-03) ---------------------------------------
    /// Cómo se le encarga algo al hilo dueño desde otro: la agenda del plano de control
    /// (PlanoControl::encolarConTurno: en orden, con el turno). Vacío (la consola, al
    /// cerrar): en el acto.
    void setEncolarEnDueno(std::function<void(std::function<void()>)> encolar);
    /// El que llama pasa a ser el dueño: el plano de control al arrancar (después de start,
    /// en su hilo) y el hilo principal al cerrar (después de parar el plano).
    void cederDueno();
    /// ¿El que llama es el dueño? (Antes de start, todos lo son.)
    bool esDueno() const;
    /// `fn` en el hilo dueño: en el acto si el que llama es él (o no hay a quién
    /// encolar); si no, encolada (con el turno) y sin esperar. Lo que es solo suyo (cfg_,
    /// guardar, los avisos) se toca por acá cuando la operación vino de otro hilo.
    void enDueno(std::function<void()> fn) const;
    /// `fn` en el hilo dueño SIEMPRE encolada, aunque el que llama sea él: corre después de
    /// lo que está haciendo, con el turno (lo que no tiene que esperar a nadie: el fundido
    /// del AFV, la foto). Sin agenda (la consola), en el acto.
    void despuesEnDueno(std::function<void()> fn) const;

    // --- La foto que lee la interfaz (F3-03, emision/FotoMotor.hpp) -----------------
    /// A quién se le publica (la ventanilla: VentanillaMotor::publicar). Vacío: no se arma.
    void setFoto(std::function<void(std::shared_ptr<const FotoMotor>, unsigned partes)> publicar);
    /// Lo que suman a la foto otros modelos del aire que son del dueño (los conmutadores:
    /// la armada, el AFV, la T-bar, las transiciones de entrada): `aporte` corre en el dueño
    /// con el turno, con las partes que se están rearmando. Devuelve su id.
    int aportarALaFoto(std::function<void(FotoMotor&, unsigned partes)> aporte);
    void dejarDeAportar(int id);
    /// Esas partes cambiaron (FotoMotor::Parte): se rearman en la próxima publicación, que
    /// se le encarga al dueño. Desde cualquier hilo.
    void fotoSucia(unsigned partes) const;
    /// Rearma lo sucio y, si algo cambió, la publica. En el dueño (toma el turno): después
    /// de cada orden de la ventanilla, en cada vuelta del motor y cuando algo la ensucia.
    void publicarFoto();
    /// DESDE CUALQUIER HILO: la última foto publicada (nullptr antes de la primera). Para lo
    /// que mira el motor sin la interfaz (el vigía de las alarmas): es la misma que recibe
    /// la ventanilla, sin esperar a que la interfaz la tome.
    std::shared_ptr<const FotoMotor> ultimaFoto() const;
    /// Lo de la configuración que usan las operaciones de aire (grabar, transmitir, cortar
    /// una escena), para poder hacerlas en el plano de control sin leer cfg_: el dueño la
    /// lee fresca (y deja la copia, también en cada vuelta del motor); los demás, la copia.
    /// Con la interfaz trabada no cambió: los ajustes los cambia ella.
    struct AjustesAire {
        RecordConfig rec;
        EncoderConfig encoder;
        StreamConfig stream;
        std::string claveEnv; ///< la variable de entorno de la clave (cfg_.streamKeyEnv)
        int transicionMs = 300;
        std::filesystem::path carpetaEvento; ///< la del evento abierto ("" = ninguno)
    };
    AjustesAire ajustesAire() const;

    /// Detiene salidas y motor y marca la salud como terminada. Idempotente.
    void stop();

    static void requestStop();
    static bool stopRequested();

    // --- Avisos: quien muestra el estado se entera de lo que cambia ------------
    /// Qué cambió. Lo avisa la operación que lo cambia, no quien la llamó: la
    /// interfaz se entera igual si el pedido vino del operador, del MCP, de un
    /// plugin o del guion del evento, sin que cada llamador se acuerde de avisar.
    enum class Cambio {
        Salidas, ///< transmisión, grabación (programa, por entrada, señal limpia), destinos, o qué salidas hay
        Escenas, ///< la lista de escenas: agregar, copiar, quitar, renombrar, mover, carpeta, protegida
        Aire,    ///< la escena al aire o la transición
        Fuentes, ///< qué fuentes hay en una escena y en qué orden (agregar, quitar, mover, renombrar)
        Capa,    ///< cómo está una fuente: visible, lugar, efectos, recorte o sus ajustes
        Audio,   ///< volumen o silencio de una fuente, o las entradas de audio del equipo
        Ajustes, ///< se aplicó la configuración (Configuración o settings_set del MCP)
    };
    /// Suma a alguien a los avisos; devuelve su id (para `dejarDeObservar`). Se
    /// lo llama en el hilo de la operación (el de la interfaz) y puede llegar
    /// muchas veces seguidas (arrastrar una capa, crear escenas en tanda): el
    /// trabajo pesado, agrupado del lado de quien escucha.
    int observar(std::function<void(Cambio)> fn);
    void dejarDeObservar(int id);
    /// La configuración se aplicó y se guardó (lo llama SettingsModel::apply): los
    /// formularios de Configuración que no tienen cambios a medio hacer se recargan.
    void ajustesAplicados() { avisar(Cambio::Ajustes); }

    // --- Las salidas del aparato (docs/planes/PLAN-SALIDAS.md) ------------------
    // Cada una un operador completo (ver BroadcastEngine, LAS SALIDAS). Se guardan
    // en Config/salidas.json (emision/SalidasConfig.hpp); la por omisión existe
    // siempre. En todo lo que sigue, `salida` "" = la por omisión.
    /// {tope, salidas: [{id, nombre, color, orden, por_omision, andando, escena, codificador}]}.
    nlohmann::json salidas() const;
    /// Una salida nueva, andando (en caliente), hasta el tope. "" o por qué no.
    std::string crearSalida(const std::string& id, const std::string& nombre, const std::string& color = {});
    /// Cambia nombre, color y/o codificador ({nombre, color, codificador}; el codificador
    /// rige desde lo próximo que arranque). "" o por qué no.
    std::string editarSalida(const std::string& id, const nlohmann::json& cambios);
    /// Para todo lo que sale de ella y la saca (la por omisión no). Sus destinos guardados
    /// van a donde diga `susDestinos`: "borrar" o el id de otra salida (con destinos y
    /// sin decirlo, no la borra: SalidasConfig::destinosDeBorrada). "" o por qué no.
    std::string borrarSalida(const std::string& id, const std::string& susDestinos = {});
    /// La codificación con la que sale `salida`: la de la configuración, con el
    /// codificador de video que eligió esa salida (Config/salidas.json) si eligió uno.
    EncoderConfig codificadorDe(const std::string& salida) const;
    /// Cuántas salidas se pueden tener (1…16). "" o por qué no.
    std::string setTopeSalidas(int tope);

    // --- Lo que sale ------------------------------------------------------------
    /// La carpeta del evento en curso (docs/planes/PLAN-EVENTOS-HISTORICO.md, H4): con
    /// uno, lo que se graba va a <evento>/Grabaciones/. Vacía = Recordings/.
    /// La carpeta del evento abierto (lo grabado va ahí). Al ponerla se retoman las
    /// grabaciones que esperaban saber dónde (tras un corte: restoreOutputs).
    void setCarpetaDelEvento(std::function<std::filesystem::path()> fn);
    /// Retoma las grabaciones pendientes de un corte, si las hay. La interfaz la llama
    /// sola sin evento abierto (con evento, la llama setCarpetaDelEvento).
    void retomarGrabaciones();
    /// Dónde va la próxima grabación: <evento>/Grabaciones o la de la configuración.
    /// Desde cualquier hilo (ajustesAire).
    std::string carpetaGrabacion() const;
    /// Dónde buscar grabaciones (para subir, listar): la del evento y Recordings/.
    std::vector<std::string> carpetasDeGrabaciones() const;
    bool startRecording(const std::string& salida = "");
    void stopRecording(const std::string& salida = "");
    /// Graba una entrada a su propio archivo, ademas del programa. Ver
    /// BroadcastEngine::startSourceRecording: cada una es un encode mas.
    bool startSourceRecording(const std::string& source);
    /// Grabar una fuente aparte (ISO) con las reglas: disco para al menos
    /// kDiscoMinimoAparteH y el tope de BroadcastEngine::kMaxIndividuales.
    /// "" = arrancó; si no, por qué no. La GUI y el MCP pasan por acá.
    std::string grabarAparte(const std::string& fuente);
    /// Con menos disco que esto (a este bitrate) la grabación del programa no
    /// arranca sola: la GUI pide confirmar, el MCP no la arranca.
    static constexpr double kDiscoMinimoH = 0.25;
    static constexpr double kDiscoMinimoAparteH = 0.5;
    bool stopSourceRecording(const std::string& source);
    void stopAllSourceRecordings();
    /// La señal limpia (sin DSK ni placa) a un destino guardado (`destino` = su id) o
    /// a Recordings/limpia/ si viene vacío. "" o el error.
    std::string iniciarSenalLimpia(const std::string& destino, const std::string& salida = "");
    /// Corta la señal limpia (y avisa, como todas las salidas).
    void detenerSenalLimpia(const std::string& salida = "");
    /// Prende o apaga la repetición con cfg_.replaySegundos. Devuelve la nota para el operador.
    std::string aplicarReplay();
    /// La repetición de `salida` con `segundos` (0 la apaga): la de la por omisión
    /// queda en la configuración (cfg_.replaySegundos, y se guarda); la de las demás
    /// vive en el motor. "" o por qué no.
    std::string ponerReplay(int segundos, const std::string& salida = "");
    /// Transmite a `server` con `key`; vacios = los de la configuracion/entorno. Otra
    /// `salida` que la por omisión necesita los dos (su clave no se guarda).
    bool startStreaming(const std::string& server = {}, const std::string& key = {}, bool cambiarEnCaliente = false,
                        const std::string& salida = "");
    void stopStreaming(const std::string& salida = "");
    /// Salir al aire a mano (servidor y clave escritos o los de la configuración):
    /// startStreaming y, si arrancó o cambió de destino, avisa a alVivoManual con el
    /// servidor (nunca la clave). La GUI y el MCP pasan por acá: el vivo queda en el
    /// bus y en el histórico venga de donde venga.
    bool salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente = false,
                           const std::string& salida = "");
    void alVivoManual(std::function<void(const std::string& servidor)> f) { alVivoManual_ = std::move(f); }

    // --- Destinos adicionales de la transmisión (Config/destinos.json; la
    // clave de cada uno en el almacén cifrado como "stream-<id>") -------------
    // Cada destino sale con el programa de su `salida` (sin campo = la por omisión).
    /// [{id, nombre, url, respaldo, salida, tiene_clave, activo, reconectando, perdidos, destino}].
    nlohmann::json destinos() const;
    /// Agrega o cambia (`id` existente) un destino: {id?, nombre, url, clave?, respaldo?, salida?}. "" o el error.
    std::string guardarDestino(const nlohmann::json& d, std::string* idOut = nullptr);
    std::string borrarDestino(const std::string& id);
    /// Arranca un destino ("todos": todos los guardados). "" o el error.
    std::string iniciarDestino(const std::string& id);
    /// Para cuánto alcanza el disco: {horas, libre_gb, kbps, volumenes[], carpeta,
    /// kbps_grabacion, copia_mismo_volumen, mismo_volumen[]}. Suma lo que ya se graba en
    /// todas las salidas (y las ISO) y, si la por omisión no graba, lo que sumaría al
    /// arrancarla con su copia; manda el disco que antes se llena (-1 si no se sabe).
    nlohmann::json discoGrabacion() const;
    void detenerDestino(const std::string& id); ///< "todos": todos

    // --- Escenas y fuentes (motor + configuracion) ----------------------------
    bool addScene(const std::string& name);
    bool removeScene(const std::string& name);
    /// Copia una escena entera con sus fuentes. La copia NO queda protegida
    /// aunque el original lo este: para eso se copia.
    bool copyScene(const std::string& from, const std::string& to);
    /// Una escena protegida no se borra ni se renombra (copiarla si).
    bool setSceneProtected(const std::string& name, bool protegida);
    bool sceneProtected(const std::string& name) const;
    bool renameScene(const std::string& from, const std::string& to);
    /// Carpeta de una escena ("" si no tiene).
    std::string sceneFolder(const std::string& name) const;
    /// Mueve la escena a una carpeta ("" la saca). Devuelve false si no existe.
    bool setSceneFolder(const std::string& name, const std::string& folder);
    /// Carpetas que hay, en el orden en que aparecen las escenas.
    std::vector<std::string> sceneFolders() const;
    /// Escenas en el orden en que las ve el operador (el de la configuracion;
    /// las que solo existan en el motor van al final).
    std::vector<std::string> orderedSceneNames() const;
    /// Mueve `name` para que quede JUSTO ANTES de `before` (vacio = al final) y
    /// le pone la carpeta de `before`. Devuelve false si no existe.
    bool moveScene(const std::string& name, const std::string& before);
    /// Al aire en la salida (la de la por omisión queda en app.json; la de todas, en
    /// Runtime/al-aire.json).
    bool setScene(const std::string& name, const std::string& salida = "");
    /// Al aire con otra transición solo esta vez (ver BroadcastEngine::setCurrentSceneCon).
    bool setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida = "");
    /// La escena ya quedó al aire en el motor (la T-bar que llegó): se anota como en
    /// setScene (app.json, el documento de aire, el aviso).
    void anotarEscenaAlAire(const std::string& name, const std::string& salida);
    bool setTransition(const std::string& type, int durationMs);
    bool addSource(const std::string& scene, const SourceSpec& spec);
    bool removeSource(const std::string& scene, const std::string& name);
    bool moveSource(const std::string& scene, const std::string& name, int delta);
    /// Renombra una fuente en el motor y en todas las escenas de la configuración.
    bool renameSource(const std::string& from, const std::string& to);
    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible);
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect);
    /// Transparencia, borde difuso y sombra de la fuente en la escena (y se guarda en la config).
    bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos);
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);
    /// El volumen general de un grupo de audio (VCA; BroadcastEngine::setGananciaGrupo) en la
    /// mezcla de `salida`, y queda guardado: el de la por omisión con la configuración, el de
    /// las demás en Config/salidas.json.
    bool setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida = "");
    /// El fader y el mute de la fuente en la mezcla de `salida` ("" = la por omisión, la
    /// que se guarda en la escena; las demás, en Config/salidas.json: vuelven al arrancar).
    bool setVolume(const std::string& name, float mul, const std::string& salida = "");
    bool setMuted(const std::string& name, bool muted, const std::string& salida = "");

    // --- Configuracion ----------------------------------------------------------
    /// Aplica video/codificacion en caliente (sin salidas activas).
    bool applyVideo(const VideoConfig& v);
    bool applyEncoder(const EncoderConfig& e);
    /// Guarda app.json, gateway.json y launcher.json. Desde otro hilo (el plano de
    /// control), se lo encarga al dueño (la configuración es suya) y devuelve true: lo que
    /// no puede esperar, lo que sale, ya está en el documento de aire.
    bool save();
    /// Reinicia B (el launcher lo relanza con gateway.json nuevo). BLOQUEA hasta
    /// ~4 s (espera que B se cierre): desde la interfaz o el MCP, usar
    /// reiniciarSalaEnElFondo.
    bool restartGateway();
    /// Lo mismo sin bloquear a quien lo pide: corre en un hilo aparte (uno a la
    /// vez) y llama a `listo` DESDE ESE HILO con el resultado ("" = hecho). Si ya
    /// hay un reinicio en curso, `listo` recibe que se espere.
    void reiniciarSalaEnElFondo(std::function<void(const std::string& error)> listo);
    ~AppRuntime();

    // Lo que es del dueño, a mano: desde la interfaz cuenta como LLAMADA DIRECTA (toma el
    // turno por el resto del evento y se anota dónde, emision/TurnoMotor.hpp). Las vistas
    // migradas no las usan: piden por la ventanilla y leen la foto.
    AppConfig& config(std::source_location donde = std::source_location::current()) {
        engine_.turno().entrarDirecto(donde);
        return cfg_;
    }
    const AppConfig& config(std::source_location donde = std::source_location::current()) const {
        engine_.turno().entrarDirecto(donde);
        return cfg_;
    }
    GatewaySettings& gateway(std::source_location donde = std::source_location::current()) {
        engine_.turno().entrarDirecto(donde);
        return gateway_;
    }
    LauncherSettings& launcher(std::source_location donde = std::source_location::current()) {
        engine_.turno().entrarDirecto(donde);
        return launcher_;
    }
    BroadcastEngine& engine(std::source_location donde = std::source_location::current()) {
        engine_.turno().entrarDirecto(donde);
        return engine_;
    }
    /// El latido del supervisor que mira el motor (la UI trabada no corta el aire).
    LatidoAire& latido() { return latido_; }
    /// DESDE CUALQUIER HILO: el canal de niveles (los vúmetros de entradas, buses y
    /// programa; emision/NivelesMotor.hpp). No toca el motor: no es una llamada directa.
    NivelesMotor& niveles() { return engine_.nivelesCanal(); }
    /// DESDE CUALQUIER HILO: arrancó para pruebas (--test/--debug). No cambia después.
    bool modoPrueba() const { return cfg_.modoPrueba; }
    /// El último error DE ESTE HILO (ver «Estrategia de errores»).
    const std::string& lastError() const;
    /// true si el ultimo corte de la transmision / grabacion lo PIDIO alguien
    /// (operador, plataforma, MCP). Si la salida se detiene con esto en false,
    /// se cayo sola (red, servidor, disco lleno) y hay que avisar fuerte.
    /// Hay una clave guardada (cifrada en Credenciales/) de la ultima salida al
    /// aire: sirve para no tipearla de nuevo y para retomar tras una caida.
    bool streamKeySaved() const;
    bool streamStopWanted() const { return streamStopWanted_; }
    bool recordStopWanted() const { return recordStopWanted_; }
    /// La corrida anterior terminó sin cerrarse (corte de luz, cuelgue): hay que
    /// volver como estaba (la escena, la playlist, las salidas).
    bool corteAnterior() const { return corteAnterior_; }
    /// Lo que había al aire en cada salida al arrancar (el documento de aire,
    /// Runtime/al-aire.json, la generación más nueva que sirve): lo que se retomó, con la
    /// sección de la línea de tiempo (la retoma EventoViewModel). Vacío si estaba dañado.
    const salidas::AlAire& aireAnterior() const { return aireAnterior_; }
    /// No se sabe en qué iba la línea de tiempo (el documento, o la línea vieja, volvió
    /// dañado): la del evento abierto vuelve en HOLD preguntando, sin reponer nada.
    bool lineaDesconocida() const { return lineaDesconocida_; }
    /// Al arrancar volvió el negro o la placa de alguna salida (para avisarle al operador).
    bool placaRestaurada() const { return placaRestaurada_; }
    /// EL DOCUMENTO DE AIRE (F3-04, emision/DocumentoAire.hpp): una sola verdad sobre qué
    /// está al aire. La sección de las salidas la pone el runtime (writeOnAirState); la de
    /// la línea, la línea de tiempo (EventoViewModel); lo escribe su dueño.
    salidas::DocumentoAire& documentoAire() { return documento_; }
    /// Algo de lo que sale cambió por fuera del runtime (el negro, la placa: SeguridadAire):
    /// la sección de las salidas se vuelve a poner (el documento compara).
    void aireCambio();
    /// Lo que el operador tiene que saber de la persistencia (el estado de aire dañado,
    /// la carpeta de datos en FAT/exFAT): lo levanta la alarma «persistencia».
    std::vector<std::string> avisosPersistencia() const;
    /// Lo anota en el registro (error) y para la alarma. Lo usa también la línea de
    /// tiempo cuando su estado vivo (linea-aire.json) vuelve dañado.
    void avisarPersistencia(const std::string& texto);
    /// Corte de luz con poca batería (F1-11, arranque/EnergiaAire): EOS ordenado de
    /// las grabaciones (archivos cerrados que se abren) SIN olvidarlas: al-aire.json
    /// las deja pedidas y sin «cerrado», y desde acá un cierre (el sistema que se
    /// apaga por batería) cuenta como corte, no como cierre del operador. Al volver
    /// la energía (o la máquina) se retoma todo. La transmisión sigue. Devuelve las
    /// salidas que cerró.
    std::vector<std::string> cerrarGrabacionesPorEnergia();
    /// Volvió la energía antes de apagarse: las grabaciones que se cerraron vuelven.
    void retomarGrabacionesPorEnergia();
    bool emergenciaEnergia() const { return emergenciaEnergia_; }
    const std::filesystem::path& dataRoot() const { return root_; }
    /// El objetivo de loudness de cada salida (Config/loudness.json), que se lleva al motor
    /// al ponerlo. Liviano: cada quien arma el suyo (el archivo se lee y se escribe entero).
    audio::ObjetivoLoudness objetivoLoudness();
    /// El feed del operador (lobby): de ahí sale el micrófono del push-to-talk.
    OperatorFeed& operatorFeed() { return operator_; }
    std::uint32_t pid() const;

    /// Salud del gateway (B): true si su latido esta fresco.
    bool gatewayAlive() const;
    /// El pid del gateway (de su bloque de salud; 0 si todavía no se presentó).
    std::uint32_t gatewayPid() const;

    // --- Sala: lo que el LOBBY ve/oye del operador --------------------------------
    /// Fuentes (por nombre) con las que el operador aparece ante el lobby;
    /// vacio = placa / sin voz. Se guarda en la configuracion.
    bool setOperatorFeed(const std::string& videoSource, const std::string& audioSource);
    /// Escuchar por monitoreo a quien ocupa el lugar `slot` del lobby.
    void setLobbyMonitor(int slot, bool on) { operator_.setLobbyMonitor(slot, on); }

    /// La definicion guardada de una fuente en una escena (nullptr si no esta).
    SourceSpec* findSpec(const std::string& scene, const std::string& name);

    /// Entrada de audio del equipo (microfono, linea) como fuente global.
    bool addAudioInput(const SourceSpec& spec);
    bool removeAudioInput(const std::string& name);

   private:
    void addGuests();
   public:
    /// El cupo de invitados cambió en caliente (T33): agrega el audio de los lugares
    /// nuevos (los que sobran quedan, mudos hasta que alguien entre).
    void ajustarInvitados(int n);

   private:
    /// A los que observan, en el hilo dueño (ver enDueno).
    void avisar(Cambio c) const;
    /// El último error del hilo que llama (lastError).
    static std::string& errorDelHilo();
    /// Anota en Config/salidas.json el fader y el mute que tiene ahora `fuente` en la
    /// salida `salida` (otra que la por omisión): vuelven al arrancar.
    void guardarMando(const std::string& salida, const std::string& fuente);
    /// Los destinos de destinos.json con su salida y si tienen clave (lee el disco: la
    /// foto lo lee solo cuando cambian las salidas) y, con lo que dice el motor de cada uno
    /// (activo, reconectando, perdidos…), lo que devuelve destinos().
    nlohmann::json destinosGuardados() const;
    nlohmann::json destinosConEstado(const nlohmann::json& guardados) const;
    /// La lista de salidas para otros hilos (salidasPublicadas).
    void publicarSalidas(const EngineStatus& st);
    /// Qué partes de la foto toca cada aviso.
    static unsigned partesDe(Cambio c);
    /// Además de las partes: lo que la foto lee de archivos se vuelve a leer.
    static constexpr unsigned kFotoRelerArchivos = 1u << 8;
    /// Los avisos a los observadores (en el dueño; ver avisar).
    void avisarObservadores(Cambio c) const;
    /// Rearma en `f` las partes `partes` (en el dueño, con el turno): ver FotoMotor.
    void armarFoto(FotoMotor& f, unsigned partes);
    /// La parte Mezcla (FotoMezcla.cpp), y el aviso del motor que la ensucia.
    void armarMezcla(FotoMotor::MezclaAudio& m);
    void anotarCambioAudio(const std::string& fuente, const char* que);
    mutable std::mutex altasMu_;
    std::map<std::string, std::uint64_t> altasAudio_; ///< las altas de cada fuente (CanalAudio::altas)
    mutable std::mutex observadoresMu_; ///< se suman desde la interfaz; se avisa en el dueño
    std::vector<std::pair<int, std::function<void(Cambio)>>> observadores_;
    int proximoObservador_ = 0;
    // La foto (F3-03): la publicación, lo que suman otros, lo sucio y la última publicada.
    std::function<void(std::shared_ptr<const FotoMotor>, unsigned)> publicarFoto_;
    mutable std::mutex aportesMu_;
    std::vector<std::pair<int, std::function<void(FotoMotor&, unsigned)>>> aportes_;
    int proximoAporte_ = 0;
    mutable std::atomic<unsigned> fotoSucia_{FotoMotor::Todas | kFotoRelerArchivos};
    mutable std::atomic<bool> fotoPedida_{false};
    std::shared_ptr<const FotoMotor> foto_; ///< la última publicada (solo el dueño)
    mutable std::mutex ultimaFotoMu_;
    std::shared_ptr<const FotoMotor> ultimaFoto_; ///< la misma, para otros hilos (ultimaFoto)
    /// Lo que la foto lee de archivos (Config/salidas.json, destinos.json): solo cuando
    /// cambian las salidas, no en cada vuelta.
    nlohmann::json fotoListaSalidas_, fotoDestinos_;
    unsigned ticsMotor_ = 0;
    std::atomic<bool> hayAire_{false}; ///< para el latido de la interfaz (ticMotor lo pone)
    std::function<std::filesystem::path()> carpetaDelEvento_;
    bool grabacionesPorRetomar_ = false; ///< restoreOutputs: esperan la carpeta del evento
    void addAudioInputs();
    /// La sección de las salidas del documento de aire: qué salidas estaban activas, la
    /// escena, el negro y la placa, y si el cierre fue ordenado. La lee el arranque para
    /// retomar la emisión tras una caída. No escribe nada hasta aireListo_.
    void writeOnAirState();
    /// Lo que sale ahora de cada salida (escena, transmisión, destinos, grabación, negro y
    /// placa): lo que escribe writeOnAirState y lo que retoma el rearme por la GPU.
    salidas::AlAire aireActual() const;
    /// F3-09: la GPU del video se perdió (motor/GpuPerdida.hpp; lo mira tick). Apaga el
    /// motor, prueba una GPU nueva (o la CPU, a 720p como mucho), lo vuelve a armar con lo
    /// que había y retoma las salidas que estaban saliendo.
    void rearmarPorGpu();
    /// Las ubicaciones de las fuentes son fracciones del lienzo: de nuevo en píxeles (tras
    /// rearmar el motor con otro lienzo).
    void reubicarFuentes();
    /// Lee el documento de aire (dos generaciones; la versión 1 con placa.json y
    /// linea-aire.json aparte, también) al arrancar: deja aireAnterior_ y corteAnterior_, y
    /// si está dañado lo avisa (F1-03).
    void leerAireAnterior();
    /// VOLVER COMO ESTABA, en un solo lugar (F3-04): la escena de cada salida, el negro y
    /// la placa, y lo que salía (restoreOutputs).
    void restaurarAire();
    /// El negro y la placa de cada salida como dice `aire`. true si había alguno.
    bool restaurarPlacas(const salidas::AlAire& aire);
    /// Retoma lo que transmitían y grababan TODAS las salidas (F1-04). `deEstaCorrida`: lo
    /// que retoma es de esta misma corrida (el rearme por la GPU), también con --test.
    void restoreOutputs(bool deEstaCorrida = false);
    /// Crea las salidas guardadas (Config/salidas.json) que el motor todavía no tiene.
    void crearSalidasGuardadas();
    /// Lo común de setScene y setSceneCon después de pedirlo al motor.
    bool anotarAire(bool ok, const std::string& name, const std::string& salida);
    std::filesystem::path archivoSalidas() const { return root_ / "Config" / "salidas.json"; }

    AppConfig cfg_;
    GatewaySettings gateway_;
    LauncherSettings launcher_;
    std::filesystem::path root_; // datos (BA_DATA_ROOT)
    std::filesystem::path bin_;  // binarios
    BroadcastEngine engine_;
    ba::ipc::HealthBlock health_;
    std::mutex healthMu_; ///< latidos desde la UI y desde LatidoAire
    LatidoAire latido_;
    ba::ipc::ControlService control_;
    OperatorFeed operator_; // vive mientras el motor este arriba (stop lo apaga antes que al motor)
    std::thread reinicioSala_;              ///< reiniciarSalaEnElFondo
    std::function<void(const std::string&)> alVivoManual_;
    std::atomic<bool> reiniciandoSala_{false};
    // Atómicos: los lee el vigía de las alarmas en el plano de control.
    std::atomic<bool> streamStopWanted_{true};
    std::atomic<bool> recordStopWanted_{true};
    mutable std::mutex salidasMu_;
    nlohmann::json salidasPublicadas_ = nlohmann::json::array();
    /// Las salidas que no son la por omisión y que se graban a pedido (vuelven tras un corte).
    std::set<std::string> grabandoOtras_;
    /// Lo mismo con su transmisión principal (servidor y clave, cifrados en Credenciales/).
    std::set<std::string> transmitiendoOtras_;
    /// Los destinos (destinos.json) que transmiten, con su salida: vuelven tras un corte.
    std::map<std::string, std::string> destinosAlAire_;
    salidas::AlAire aireAnterior_;
    salidas::DocumentoAire documento_;
    bool aireListo_ = false;        ///< ya volvió como estaba: el documento se puede escribir
    bool lineaDesconocida_ = false; ///< ver lineaDesconocida
    bool placaRestaurada_ = false;
    mutable std::mutex avisosMu_;
    std::vector<std::string> avisosPersistencia_;
    bool corteAnterior_ = false;
    bool cerrando_ = false;           ///< stop(): al-aire.json dice «cerrado»
    bool emergenciaEnergia_ = false;  ///< cerrarGrabacionesPorEnergia: un cierre es un corte
    std::set<std::string> porEnergia_; ///< las salidas cuya grabación cerró el corte (vuelven)
    mutable std::atomic<int> keySaved_{-1}; // cache de streamKeySaved (-1 = sin consultar)
    std::atomic<std::thread::id> dueno_{};  ///< el hilo que hizo start (ver esDueno)
    std::function<void(std::function<void()>)> encolarEnDueno_;
    mutable std::mutex ajustesAireMu_;
    mutable AjustesAire ajustesAire_; ///< la copia para los demás hilos (ver ajustesAire)
    bool safeBackupDone_ = false; // app.json resguardado antes de guardar en modo seguro
    bool started_ = false;
};

/// Ubicacion por defecto del invitado `slot` de `count` (fila inferior).
CanvasRect guestRect(int slot, int count);

} // namespace ba::app
