// ============================================================================
// src/app/emision/ApiMotor.hpp — Lo que la app usa del motor de emisión
//
// La API de BroadcastEngine que llaman la interfaz (ui/), el MCP, el arranque y las
// áreas: escenas, fuentes, audio por fuente, medios, capas, salidas, vistas. Es una
// interfaz abstracta: BroadcastEngine (emision/BroadcastEngine.hpp) la implementa con
// los mismos métodos de siempre (mismo nombre, misma firma, mismos argumentos por
// omisión), y AppRuntime::engine() la devuelve. Así, tocar BroadcastEngine.hpp (sus
// piezas, su estado, lo que solo usa el dueño) recompila emision/ y el arranque, no a
// todos los que llaman al motor (Ola 4 del diseño físico, docs/local/PLAN-DISENO-FISICO.md).
//
// Qué queda solo en BroadcastEngine: el ciclo de vida y lo que usa el dueño por su
// puntero (startup, shutdown, atenderSalidas, medidores, nivelesCanal, publishReturnFeeds…)
// y lo que solo usan las piezas de emision/. Un método nuevo que use alguien de afuera
// se declara acá (virtual … = 0) y en BroadcastEngine (… override).
//
// Los comentarios de cada método (el turno, los hilos, «LAS SALIDAS», la estrategia de
// errores) son los de BroadcastEngine.hpp: ver su cabecera.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Entero y no declarado: quien mide el loudness de una salida (loudness(), loudnessDe())
// lo usa siempre, y así sigue llegándole por acá como antes por BroadcastEngine.hpp.
#include "audio/LoudnessPrograma.hpp"
#include "emision/ConfigAire.hpp"
#include "emision/MotorFwd.hpp"
#include "json/JsonFwd.hpp"

namespace ba::motor {
class Motor;
struct Cuadro;
} // namespace ba::motor

namespace ba::app::escenas {
class TablaEscenas;
} // namespace ba::app::escenas

namespace ba::app {

/// La API del motor que usa la app. La implementa BroadcastEngine. Ver el header.
class ApiMotor {
   public:
    ApiMotor(const ApiMotor&) = delete;
    ApiMotor& operator=(const ApiMotor&) = delete;

    /// Por qué no anda la escena interna de un compositor o una Mezcla de GStreamer ("" si anda),
    /// de la foto del motor (no espera su candado).
    virtual std::string errorDeCompositor(const std::string& nombre) const = 0;
    /// El motor de GStreamer, para las piezas que trabajan sobre él (el grafo de
    /// video, los racks, la escucha). nullptr antes de startup.
    virtual motor::Motor* motor() = 0;
    /// La foto de las salidas (transmisiones, grabación, limpia) que publica su hilo:
    /// no espera a nadie (la página Latencias del pipeline la lee seguido).
    virtual nlohmann::json salidasAhora() const = 0;

    // --- Las salidas (ver LAS SALIDAS arriba) -------------------------------
    static constexpr const char* kSalidaPorOmision = "principal";
    /// Una salida nueva, andando: su programa, su preview y su mezcla en el motor
    /// (con los volúmenes y mutes que tiene ahora la por omisión, sin nada al aire),
    /// su loudness y su retorno al gateway si está publicado. false + lastError.
    virtual bool crearSalida(const std::string& id, const std::string& nombre) = 0;
    /// Para todo lo que sale de ella (transmisiones, grabación, repetición, limpia),
    /// suelta su retorno y la saca del motor. La por omisión no se borra.
    virtual bool borrarSalida(const std::string& id) = 0;
    /// Los ids, la por omisión primera y las demás en el orden en que se crearon.
    virtual std::vector<std::string> salidaIds() const = 0;
    virtual bool existeSalida(const std::string& id) const = 0;
    /// El id de la salida ("" = la por omisión), o "" si no existe.
    virtual std::string idDeSalida(const std::string& salida) const = 0;

    // --- Escenas ------------------------------------------------------------
    // Cada escena tiene un id estable (escenas/RefEscena.hpp). Todo lo que recibe una
    // escena entiende su NOMBRE o su ID; el motor la conoce por el id, así que
    // renombrarla (aunque esté al aire) no corta ni toca el preview.
    /// `id` vacío: uno nuevo (lo dice sceneId después).
    virtual bool createScene(const std::string& name, const std::string& id = {}) = 0;
    /// Elimina una escena (no la actual si es la unica). Las salidas que la tenían
    /// al aire pasan a otra con un corte.
    virtual bool removeScene(const std::string& name) = 0;
    virtual bool renameScene(const std::string& from, const std::string& to) = 0;
    /// Nombres de las escenas en orden de creacion.
    virtual std::vector<std::string> sceneNames() const = 0;
    /// El id de una escena pedida por nombre o id ("" si no está), y su nombre.
    virtual std::string sceneId(const std::string& escena) const = 0;
    virtual std::string sceneName(const std::string& escena) const = 0;
    /// Las escenas (id, nombre), en orden de creación: para resolver lo guardado.
    virtual escenas::TablaEscenas tablaEscenas() const = 0;
    /// Pone `name` al aire en la salida con la transicion configurada (setTransition).
    virtual bool setCurrentScene(const std::string& name, const std::string& salida = "") = 0;
    /// Escena al aire en la salida ("" si no hay o no existe la salida). Una copia: la
    /// referencia de antes se la podía cambiar el otro que tiene el turno.
    virtual std::string currentScene(const std::string& salida = "") const = 0;
    /// Lo mismo, el id ("" si no hay).
    virtual std::string currentSceneId(const std::string& salida = "") const = 0;
    /// Tipo ("cut", "fade", "swipe", "slide", "fade_to_color", "wipe") y duracion.
    virtual bool setTransition(const std::string& type, int durationMs) = 0;
    /// Pone `name` al aire con OTRA transición solo esta vez (`ms` < 0 = la global).
    virtual bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida = "") = 0;
    /// T-bar: lleva la transición a mano hacia `name` (t de 0 a 1). Con `soltar`,
    /// termina (si pasó la mitad, queda al aire; si no, vuelve).
    virtual bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida = "") = 0;
    /// En medio de una transición manual (y hacia qué escena).
    virtual std::string manualHacia(const std::string& salida = "") const = 0;
    /// La escena en el PREVIEW (el bus de armada) de la salida ("" = nada armado).
    /// Lo pide el conmutador (escenas::Conmutador): las vistas solo lo miran.
    virtual bool armar(const std::string& name, const std::string& salida = "") = 0;

    // --- FTB y placa de seguridad (por encima de todo lo que sale) -------------
    /// Fundido a negro de la salida; con `bajarAudio`, el audio del programa baja con él.
    virtual bool ftb(bool activar, int ms, bool bajarAudio, const std::string& salida = "") = 0;
    /// La placa ("Cuarto intermedio", "Volvemos enseguida"…): fondo, texto e imagen opcional.
    virtual bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms, const std::string& salida = "") = 0;
    // --- DSK: capas globales encima del programa (1..4) ------------------------
    /// Arma el contenido de la capa `capa` (1..4): escenas o fuentes, por nombre,
    /// apiladas en ese orden. `recuadro` (opcional): {posicion, tamano, margen}.
    virtual bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro,
                  const std::string& salida = "") = 0;
    /// Sube o baja la capa con un fundido de `ms`.
    virtual bool dskMostrar(int capa, bool visible, int ms, const std::string& salida = "") = 0;
    /// [{capa, visible, opacidad}] para las 4.
    virtual nlohmann::json dskEstado(const std::string& salida = "") const = 0;
    /// Sale algo hacia afuera desde ALGUNA salida: alguna transmisión (la principal o
    /// una extra), la grabación o la señal limpia. Sin esto, lo que pasa en el programa
    /// no lo ve nadie: ni la PTZ ni las alertas ni los candados tratan nada como «al aire».
    /// `contarGrabacion` en false: solo lo que se transmite (el modo en vivo
    /// puede no contar la grabación, Config/modo-vivo.json).
    virtual bool haySalida(bool contarGrabacion = true) const = 0;
    /// Lo mismo, de una salida sola ("" = la por omisión).
    virtual bool haySalidaEn(const std::string& salida, bool contarGrabacion = true) const = 0;

    /// LA FUENTE «BANCA CON LA PALABRA» (tipo ba_banca, ajuste `ranura` 1, 2…): en la
    /// escena muestra la cámara de la banca de esa ranura (1 = quien tiene la
    /// palabra; en un debate, 1 y 2). La app dice qué hay en cada ranura; sin nada,
    /// la capa no se dibuja. Con `entera`, la capa toma la toma ENTERA de la cámara
    /// (antes del PTZ digital) y la recorta ella: una sola cámara muestra varias
    /// bancas a la vez sin moverse, y el plano general sigue igual.
    struct RanuraBanca {
        std::string entrada;          ///< la cámara ("" = nadie en esta ranura)
        bool entera = false;          ///< recortar la toma entera (posición digital)
        float recorte[4] = {0, 0, 0, 0}; ///< izquierda, arriba, derecha, abajo (fracciones)
    };
    static constexpr const char* kTipoBanca = "ba_banca";
    /// Cuántas ranuras hay: 1 (quien habla, o el primero de un debate) y 2 (el segundo; el
    /// debate es de a dos). Si el debate admite más, se sube acá y las Propiedades lo siguen.
    static constexpr int kRanurasBanca = 2;
    /// Cambia lo que muestra cada ranura y vuelve a publicar las escenas que las usan.
    /// `forzar`: aunque no hayan cambiado (el tamaño de la cámara llegó después y el
    /// recorte, que va en píxeles, recién ahora se puede calcular).
    virtual void setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar = false) = 0;
    /// La primera escena que muestra la ranura `ranura` ("" si ninguna).
    virtual std::string escenaDeRanura(int ranura) const = 0;
    /// Las fuentes que SE VEN en el programa de una salida: las visibles de su escena
    /// al aire y lo que muestran sus capas DSK que están arriba.
    virtual std::vector<std::string> fuentesEnPrograma(const std::string& salida) const = 0;
    /// Las que se ven en el programa de ALGUNA salida (el tally: al aire en alguna).
    virtual std::vector<std::string> fuentesEnPrograma() const = 0;
    /// `fuente` se ve en el programa de alguna salida y esa salida sale (haySalidaEn):
    /// lo que le pase se ve en una transmisión o queda en una grabación.
    virtual bool fuenteAlAire(const std::string& fuente) const = 0;
    // --- Video en vivo dentro de un gráfico (graficos/VideoEnGrafico.hpp) --------------
    /// Las capas de video que acompañan al gráfico de la fuente `grafico` (ba_grafico):
    /// en cada escena donde está ese gráfico, cada fuente nombrada se anima con sus claves
    /// (si ya está en la escena, esa; si no, se suma mientras dure), arriba o debajo del
    /// gráfico. Quedan quietas en su primera clave hasta empezarVideoDeGrafico. No se
    /// guardan con la escena. `capas`: VideoEnGrafico::aJson. "" o el error.
    virtual std::string ponerVideoDeGrafico(const std::string& grafico, const nlohmann::json& capas) = 0;
    /// El ms 0 de sus claves (g_get_monotonic_time en ns): el primer cuadro del gráfico.
    virtual void empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs) = 0;
    /// Las saca: cada fuente vuelve a como estaba (o se va, si se había sumado).
    virtual void quitarVideoDeGrafico(const std::string& grafico) = 0;
    /// Un rótulo de texto con nombre (fuera de las escenas, para una capa DSK).
    virtual bool textoGlobal(const std::string& nombre, const std::string& texto) = 0;
    /// {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    virtual nlohmann::json estadoPlaca(const std::string& salida = "") const = 0;
    /// DESDE CUALQUIER HILO (el vigía de las alarmas en el plano de control, F3-03): el
    /// pico del programa de la por omisión (dBFS, los dos canales) desde la última vez
    /// que se pidió, sin tocar el del vúmetro (programPeaksDb).
    virtual float picoProgramaVigiaDb() = 0;
    /// DESDE CUALQUIER HILO: la foto de lo que sale que publica el hilo de salidas
    /// ({principal, grabacion, salidas: {id: {principal, grabacion, …}}}; ver status()).
    virtual nlohmann::json fotoSalidas() const = 0;
    /// DESDE CUALQUIER HILO: el loudness del programa de la por omisión (tiene su candado).
    virtual const audio::LoudnessPrograma& loudnessPrograma() const = 0;

    /// Subtítulos CEA-608 en la transmisión (todavía no: M7). Devuelve a cuántas salidas se mandó.
    virtual int enviarCaption(const std::string& texto, double segundos) = 0;

    /// El loudness de lo que sale por la salida: M/S/I, true peak, LRA (EBU R128).
    /// El de la por omisión vive lo que el BroadcastEngine (se puede guardar la referencia).
    virtual audio::LoudnessPrograma& loudness(const std::string& salida = "") = 0;
    /// El mismo, o nullptr si no hay una salida con ese id ("" = la por omisión).
    virtual audio::LoudnessPrograma* loudnessDe(const std::string& salida) = 0;
    /// Lleva el objetivo de loudness de cada salida (el suyo: Config/loudness.json; una
    /// nueva arranca con el de la por omisión) al motor: el AGC lento si está pedido y el
    /// techo del limitador (el del objetivo; −2 dBTP si esa salida transmite AAC por
    /// debajo de 128 kbps, AES TD1004). Al cambiar un objetivo, al crear una salida y al
    /// empezar a transmitir.
    virtual void aplicarLoudness() = 0;
    /// El programa de la salida en imagen, para las alarmas: brillo medio (0–255) y cuántos
    /// segundos lleva sin cambiar. `valido` false si todavía no llegó ningún cuadro (o no
    /// hay esa salida). DESDE CUALQUIER HILO.
    struct ProgramaVideo {
        double luma = 0, quietoSeg = 0;
        bool valido = false;
    };
    virtual ProgramaVideo programaVideo(const std::string& salida = "") const = 0;
    /// El tope de la latencia del estudio (ms; -1 = automático).
    virtual void ponerTopeEstudio(long long ms) = 0;
    /// La política térmica (F4-05): dormir las vistas del motor que nadie mira (las vistas
    /// previas, las capturas que quedaron vivas) hasta que alguien las lea. Vale también
    /// para el motor que se rearme.
    virtual void soltarVistasSinMirar(bool on) = 0;
    /// {latencia_ms, tope_ms, automatico, tarde: [entradas que pasan el tope]}.
    virtual nlohmann::json latenciaEstudio() const = 0;
    /// Bajar la latencia del estudio ahora a lo que piden las entradas sincronizadas.
    virtual bool bajarLatenciaEstudio() = 0;
    virtual bool remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) = 0;
    /// {archivo, destino, pct, andando, ok, error}.
    virtual nlohmann::json remuxEstado() const = 0;

    // --- Fuentes -------------------------------------------------------------
    // Los nombres de fuente son unicos. Agregar a una escena una fuente cuyo
    // nombre ya existe REUTILIZA esa fuente.
    virtual bool addSource(const std::string& scene, const SourceSpec& spec) = 0;
    virtual bool addSource(const SourceSpec& spec) = 0;
    /// Saca la fuente de la escena (si no queda en ninguna, se libera).
    virtual bool removeSource(const std::string& scene, const std::string& name) = 0;
    /// Sube (delta > 0) o baja (delta < 0) la fuente en el orden de la escena.
    virtual bool moveSource(const std::string& scene, const std::string& name, int delta) = 0;
    /// Fuentes de la escena, de abajo hacia arriba.
    virtual std::vector<std::string> sceneSources(const std::string& scene) const = 0;
    virtual bool setSourceVisible(const std::string& scene, const std::string& name, bool visible) = 0;
    virtual bool sourceVisible(const std::string& scene, const std::string& name) const = 0;
    /// Ubica la fuente en `rect` (fracciones del lienzo) dentro de la escena.
    virtual bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) = 0;
    /// Recorta la fuente EN LA ESCENA: izquierda, arriba, derecha, abajo (fracciones, 0..1).
    virtual bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo) = 0;
    /// Transparencia, borde difuso y sombra de la fuente EN LA ESCENA.
    virtual bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) = 0;
    virtual EfectosCapa sourceEfectos(const std::string& scene, const std::string& name) const = 0;
    /// Renombra una fuente (en todas las escenas).
    virtual bool renameSource(const std::string& from, const std::string& to) = 0;
    /// Tipo de la fuente ("" si no existe).
    virtual std::string sourceTypeId(const std::string& name) const = 0;
    /// TRAER (la tarjeta del grafo): lo que se ve de la entrada `entrada` en las
    /// escenas viene de otra parte: un compositor (`compositor`, el nombre de su
    /// fuente) o lo que publica otra entrada (`productor`, el de su basalida o
    /// «<fuente>:video»). Ambos vacíos = lo suyo. Rearma las escenas que la usan.
    virtual void setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor) = 0;
    /// ¿Existe la fuente?
    virtual bool sourceExists(const std::string& name) const = 0;
    /// Lo que mide el motor de la entrada (tamaño, latencia, filtros, error...); null si no existe.
    virtual nlohmann::json estadoEntrada(const std::string& name) const = 0;
    /// Lo visible de una escena: (fuente, rectángulo en fracciones del lienzo), de abajo hacia arriba.
    virtual std::vector<std::pair<std::string, CanvasRect>> itemsVisibles(const std::string& scene) const = 0;
    /// El reloj del estudio: sistema, ntp, ptp o red. Si cambia con el motor andando, se rearma.
    virtual bool ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) = 0;
    /// Publica el reloj del estudio en la red (0 = no). "" o el error.
    virtual std::string publicarReloj(int puerto) = 0;
    /// El canal de control «ba/control» de una entrada. "" o el error.
    virtual std::string controlEntrada(const std::string& name, const nlohmann::json& campos) = 0;
    /// Settings actuales de la fuente como JSON.
    virtual std::string sourceSettingsJson(const std::string& name) const = 0;
    /// Compatibilidad: en el motor todas las fuentes andan siempre (no hace nada).
    virtual void holdShowing(const std::string& name, bool on) = 0;
    /// Tamaño actual del video de una fuente (0x0 si todavia no llego imagen).
    virtual bool sourceSize(const std::string& name, int& width, int& height) const = 0;
    /// Reemplaza los settings de la fuente.
    virtual bool updateSourceSettings(const std::string& name, const std::string& settingsJson) = 0;
    /// La captura sin que espere quien llama (la interfaz nunca espera): prepara
    /// acá —lee escenas y fuentes, que son del dueño del motor (con el turno)—, espera el
    /// cuadro en otro hilo y llama a `listo` DESDE ESE HILO (quien la usa en la
    /// interfaz la vuelve a su hilo). `error` vacío = hay imagen (RGBA).
    using Captura = std::function<void(const std::string& error, std::vector<std::uint8_t> rgba, int ancho, int alto)>;
    virtual void capturarAparte(const std::string& name, int maxWidth, Captura listo) = 0;
    /// La imagen actual de una fuente o escena ("" = el programa, con sus capas), con ancho
    /// de a lo sumo `maxWidth` (0 = el del lienzo), RGBA 8 bits, en dos partes (las
    /// miniaturas de Entradas, que se cargan aparte para no trabar la interfaz):
    /// `prepararCaptura` lee las escenas, las fuentes y el estado del motor, que
    /// son del dueño (con el turno; sin él era una carrera que tumbaba la app);
    /// `capturarVista` arma o reusa la vista y espera el cuadro, y se
    /// puede llamar desde cualquier hilo. "" o el error.
    virtual std::string prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                bool& esEscena) const = 0;
    virtual std::string capturarVista(const std::string& clave, int w, int h, bool esEscena, std::vector<std::uint8_t>& rgba,
                              int& width, int& height) = 0;
    /// Los mismos: son fijos (no miran el motor), así que se leen desde cualquier hilo.
    static std::vector<SourceType> tiposDeFuente();
    /// Invitados: la fuente de video va en escenas; la de AUDIO es global.
    virtual bool addGuestAudio(int slot) = 0;
    /// Entrada de audio del equipo (micrófono, línea) como fuente GLOBAL.
    virtual bool addGlobalAudio(const SourceSpec& spec) = 0;
    virtual bool removeGlobalAudio(const std::string& name) = 0;
    /// Nombres de las entradas de audio del equipo (sin los invitados).
    virtual std::vector<std::string> globalAudioInputs() const = 0;

    // --- Audio por fuente (mezclador) ------------------------------------------
    /// Fuentes con audio (escenas + globales), en orden.
    virtual std::vector<std::string> audioSources() const = 0;
    /// Todas las fuentes que maneja el motor, sin repetir.
    virtual std::vector<std::string> sourceNames() const = 0;
    /// El volumen (el fader) de la fuente en la mezcla de una salida. El de la por
    /// omisión es el de siempre (se guarda con la fuente); el de las demás arranca
    /// como el de la por omisión al crearlas y después es suyo.
    virtual bool setSourceVolume(const std::string& name, float mul, const std::string& salida = "") = 0;
    /// El volumen de la fuente (su fader), SIN la ganancia de su grupo.
    virtual float sourceVolume(const std::string& name, const std::string& salida = "") const = 0;
    /// El grupo de audio de una fuente: su nombre sin los números y con los espacios en orden
    /// («Invitado 3 (audio)» -> «Invitado (audio)»). El mismo que agrupa el mezclador.
    static std::string grupoDeAudio(const std::string& nombre);
    /// El volumen general de un grupo (un VCA): multiplica el volumen de cada fuente del grupo
    /// sin tocar su fader, en el mismo control de nivel (no agrega nada al camino del audio).
    /// En 1 (0 dB), como si no estuviera. Una fuente que se suma después al grupo ya lo trae.
    /// Es de la mezcla de cada salida ("" = la por omisión; una nueva arranca sin ninguno).
    virtual void setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida = "") = 0;
    virtual float gananciaGrupo(const std::string& grupo, const std::string& salida = "") const = 0;
    virtual bool setSourceMuted(const std::string& name, bool muted, const std::string& salida = "") = 0;
    virtual bool sourceMuted(const std::string& name, const std::string& salida = "") const = 0;
    /// Pico mas reciente de la fuente en dBFS (-100 = silencio): lo que entrega la
    /// entrada, vaya o no al Programa (el mismo de los vúmetros del Mezclador).
    virtual float sourcePeakDb(const std::string& name) const = 0;
    /// Los auriculares del operador tienen dos cosas distintas:
    /// - la ESCUCHA FIJA: lo que la matriz manda ahí a propósito (la voz del
    ///   asistente, la del teleprompter). Varias juntas; se guarda (audio-matriz.json).
    ///   Suena con el fader y el mute de la salida de la escucha (setSalidaEscucha).
    /// - el SOLO: escuchar UN canal un rato, solo él (la escucha del motor en «pfl» o
    ///   «afl», F2-03). PFL: lo que entrega la entrada, antes del fader y del bus; AFL: con
    ///   el fader y el mute de una salida (la que muestra el Mezclador). Uno solo a la vez;
    ///   mientras dura, la escucha fija espera sin tocarse (vuelve al soltarlo) y no se guarda.
    virtual bool setSourceMonitoring(const std::string& name, bool on) = 0;
    /// Si suena en los auriculares (escucha fija o solo).
    virtual bool sourceMonitoring(const std::string& name) const = 0;
    /// Solo la escucha fija (lo que muestra y guarda la matriz).
    virtual bool sourceEscuchaFija(const std::string& name) const = 0;
    /// El solo: `modo` "pfl" o "afl" (`salida`: de cuál es el fader del AFL; "" = la por
    /// omisión). `name` "" lo suelta. Falla si la fuente o la salida no existen.
    virtual bool setPfl(const std::string& name, const std::string& modo = "pfl", const std::string& salida = "") = 0;
    virtual std::string pfl() const = 0;
    /// "pfl" o "afl" (lo del solo prendido; "" sin solo).
    virtual std::string pflModo() const = 0;
    /// La salida de la escucha fija: de cuál son su fader y su mute (su id; "" al pedirla =
    /// la por omisión). No se guarda. Falla si no existe.
    virtual bool setSalidaEscucha(const std::string& salida) = 0;
    virtual std::string salidaEscucha() const = 0;
    /// Atrasa (ms > 0) o adelanta la fuente para que su audio coincida con su video.
    virtual bool setSourceSyncOffset(const std::string& name, int ms) = 0;
    virtual int sourceSyncOffset(const std::string& name) const = 0;
    /// A qué pistas va la fuente (1 = el programa; 2..6 = la grabación multipista).
    virtual bool setSourcePistas(const std::string& name, const std::vector<int>& pistas) = 0;
    virtual std::vector<int> sourcePistas(const std::string& name) const = 0;

    // --- Medios ------------------------------------------------------------------
    struct MediaInfo {
        std::string name;
        std::string state; ///< playing | paused | stopped | ended | error | none
        std::int64_t durationMs = 0;
        std::int64_t timeMs = 0;
    };
    /// Fuentes reproducibles y su estado.
    virtual std::vector<MediaInfo> mediaSources() const = 0;
    /// Reproduce un archivo como fuente GLOBAL de audio (reemplaza lo que sonaba con ese nombre).
    virtual bool playMedia(const std::string& name, const std::string& file, bool loop = false, float volume = 1.0f) = 0;
    /// Para lo que este sonando y libera la fuente.
    virtual bool stopMedia(const std::string& name) = 0;
    /// Rebobina y arranca una fuente de medios que YA existe.
    virtual bool restartMedia(const std::string& name) = 0;
    /// Lleva un medio a `ms`.
    virtual bool seekMedia(const std::string& name, std::int64_t ms) = 0;
    /// Para un medio sin sacarlo.
    virtual bool pararMedia(const std::string& name) = 0;

    // --- Descubrir capacidades -------------------------------------------------
    struct ListItem {
        std::string name;
        std::string value;
        bool disabled = false;
    };
    /// true si el tipo de fuente es uno que el motor sabe armar.
    virtual bool sourceTypeExists(const std::string& typeId) const = 0;
    /// Nombre de la placa de video (para el diagnóstico).
    /// Con qué dibuja (no depende del estado del motor: desde cualquier hilo).
    static std::string graphicsDeviceName();
    struct EncoderInfo {
        std::string id, name, codec;
        bool hardware = false;
    };
    /// Codificadores de video disponibles (los de la placa marcados). De las fábricas de
    /// GStreamer, no del estado del motor: desde cualquier hilo.
    static std::vector<EncoderInfo> videoEncoders();
    /// Cambia resolucion/fps. Solo sin salidas activas: rearma el motor con todo lo que había.
    /// Con `conElMotorApagado` (F3-09, la GPU perdida) también con salidas: las para, corre
    /// eso con el motor viejo ya apagado (rearmar la GPU) y arma el nuevo con el lienzo que
    /// devuelve; las salidas las retoma quien llama.
    virtual bool resetVideo(const VideoConfig& video, const std::function<VideoConfig()>& conElMotorApagado = {}) = 0;
    /// Una copia (ver currentScene).
    virtual VideoConfig videoConfig() const = 0;
    /// El tamaño más grande con que el gateway le manda el programa a una sesión
    /// (Configuración → Sala, gateway.programaW/H: 1280×720 de fábrica). El retorno se
    /// publica a ese tamaño (el de la salida si es más chica), escalado y en BGRA en la
    /// GPU (F2-13): bajarlo a 1920×1080 para que el gateway lo achicara en la CPU era
    /// copiar y recorrer 2,25 veces lo que se manda. Antes de publishReturnFeeds; si
    /// cambia andando, el canal se rehace (el gateway se vuelve a suscribir solo).
    virtual void ponerTopeRetorno(int ancho, int alto) = 0;
    /// ¿Alguien mira el retorno de la salida (invitados en la sala, integrantes
    /// remotos, quien mire BA.Salida.<id> en el gateway)? Sin nadie, la vista que lo
    /// baja en cada cuadro se suelta (el canal queda publicado); con alguien, vuelve.
    /// La por omisión arranca en uso; las demás, sin nadie.
    virtual void retornoEnUso(bool si, const std::string& salida = "") = 0;

    /// Una escena que no es el programa, para las pantallas de la sala: la pone
    /// en el canal `k` (BA.Escena.<k>, 0..ipc::kCanalesEscena-1) y la manda al
    /// Proceso B mientras esté ahí; vacía, lo suelta. La escena se compone aparte
    /// y sigue andando aunque no esté al aire. Dos pantallas con la misma escena
    /// comparten canal: eso lo decide quien reparte los canales (Pantallas).
    /// Lo que se le encargó al motor y falló después: las órdenes que pueden
    /// tardar van en cola y vuelven enseguida (la interfaz no espera), así que
    /// su error llega por acá, desde el hilo del motor.
    /// Un aviso del motor de algo que pasó después de pedirlo: TIPADO (quien lo muestra
    /// decide por el código y el nivel, nunca comparando el texto, que sale traducido).
    /// Definido en emision/MotorFwd.hpp (se nombra sin incluir el motor).
    using AvisoMotor = ::ba::app::AvisoMotor;
    virtual void alFallarMotor(std::function<void(const AvisoMotor&)> fn) = 0;
    virtual bool escenaACanal(int k, const std::string& escena) = 0;
    virtual std::string escenaDeCanal(int k) const = 0;
    /// Las escenas (por nombre, sin repetir) que se están MOSTRANDO además del aire y el
    /// preview de las salidas. Es el registro del motor, no hay otro:
    ///   - `vistas`: las que pidió la interfaz (monitores y ventanas de escena, el multiview,
    ///     las distribuciones de pantallas, las miniaturas vivas: todo pasa por
    ///     enableScenePreview);
    ///   - `canales`: las de los canales de escena (BA.Escena.<k>), que SALEN de la máquina:
    ///     las pantallas de la sala y los retornos de escena a invitados e integrantes.
    /// Para la sala (sala/CapasRemotos): quien sale en una de estas no se pausa.
    struct EscenasMostradas {
        std::vector<std::string> vistas, canales;
        bool operator==(const EscenasMostradas&) const = default;
    };
    /// Los que no son Pantallas (las vistas de la sesión remota) toman un canal con
    /// dueño: el que ya tiene esa escena o uno libre (-1 si no hay). Pantallas no
    /// suelta un canal con dueños; el último dueño que lo suelta, lo apaga.
    virtual int tomarCanalEscena(const std::string& escena, const std::string& dueno) = 0;
    /// Suelta los canales de `dueno` salvo los de `salvo` (los que acaba de volver a tomar:
    /// así un reparto nuevo no apaga y prende lo que sigue igual).
    virtual void soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo = {}) = 0;
    virtual bool canalEscenaConDuenos(int k) const = 0;

    /// Lo que de verdad pasó con una orden de salida que corre en el hilo de salidas
    /// (startRecording, startStreaming): que devuelvan true es «encolada», no «hecha». El
    /// resultado llega después a quien pidió, por `AlResultado`, DESDE EL HILO DE SALIDAS
    /// (quien lo recibe se lo encarga a su hilo). `que`: "grabar" o "transmitir".
    /// Definidos en emision/MotorFwd.hpp (se nombran sin incluir el motor).
    using ResultadoOrden = ::ba::app::ResultadoOrden;
    using AlResultado = ::ba::app::AlResultado;

    /// Transmite. Si YA está transmitiendo: al mismo destino no hace nada
    /// (ultimoInicio = YaIgual); a OTRO destino falla (YaOtroDestino), salvo
    /// `cambiarEnCaliente`, que corta y arranca al nuevo. Con true, lo que pasó llega por
    /// `alTerminar` (ver ResultadoOrden).
    virtual bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente = false,
                        const std::string& salida = "", AlResultado alTerminar = {}) = 0;
    enum class Inicio { Iniciado, YaIgual, YaOtroDestino, Error };
    /// Lo que hizo el último startStreaming DE ESTE HILO (como lastError).
    virtual Inicio ultimoInicio() const = 0;
    /// El destino al aire, enmascarado (servidor y los últimos 4 de la clave).
    virtual std::string destinoActivo(const std::string& salida = "") const = 0;

    // --- Destinos adicionales (respaldo, otras plataformas) --------------------
    /// Un destino extra (su id es único entre todas las salidas) con el programa de `salida`.
    /// `serverRespaldo`: el punto de ingest de respaldo (misma clave); sale a los dos a la vez.
    virtual bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                    const std::string& salida = "", const std::string& serverRespaldo = "") = 0;
    virtual void stopExtra(const std::string& id) = 0; ///< esté en la salida que esté
    virtual void stopExtras() = 0;                     ///< los de todas las salidas
    /// {id: {activo, reconectando, perdidos, cuadros, bytes, destino, salida}}, de todas las salidas.
    virtual nlohmann::json extrasEstado() const = 0;
    /// Detiene la transmision (no toca la grabacion).
    virtual void stopStreaming(const std::string& salida = "") = 0;

    /// Arranca la grabacion del programa (MKV en tramos; MP4/MOV se pasan al terminar). Con
    /// true (encolada), lo que pasó llega por `alTerminar` (ver ResultadoOrden).
    virtual bool startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida = "",
                        AlResultado alTerminar = {}) = 0;
    virtual void stopRecording(const std::string& salida = "") = 0;
    /// {activa, archivo, copia, copia_activa, calidad, encoder, pistas, tramos}.
    virtual nlohmann::json grabacionEstado(const std::string& salida = "") const = 0;

    // --- Repetición ---------------------------------------------------------------
    /// Guarda los últimos `segundos` del programa (0 = apagar). `dir`: donde quedan los clips.
    virtual bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida = "") = 0;
    /// Guarda el clip; llega por `ultimoReplay()` cuando termina de escribir.
    virtual bool guardarReplay(const std::string& salida = "") = 0;
    virtual std::string ultimoReplay(const std::string& salida = "") const = 0;
    virtual nlohmann::json replayEstado(const std::string& salida = "") const = 0;
    /// Configuración → Grabación: cuánto espera lo grabado para entrar al disco
    /// hasta «apuros», y cuánto todo al día hasta salir.
    virtual void ponerUmbralesDisco(double apurosS, int recuperadoS) = 0;
    virtual std::string setModoDisco(const std::string& modo) = 0; ///< "" o el error (se guarda en Config/disco.json)
    /// Darle (o devolverle) el disco al programa ahora.
    virtual void priorizarPrograma(bool si) = 0;
    /// {apuros, atraso_s, registro_trabado_ms, prioridad, automatica, modo, pausadas[], desde_s,
    ///  restante: {horas, libre_gb, kbps, volumenes[{volumen, libre_gb, kbps, horas, que[]}]} (lo que
    ///  escribe AHORA en todas las salidas: grabación, copia, señal limpia e ISO, sumado por disco),
    ///  mismo_volumen: [{salida, grabacion, copia}] (copias que van al mismo disco que su grabación)}
    virtual nlohmann::json discoEstado() const = 0;
    /// Lo que escribe en disco la grabación de una salida (video + audio de cada pista):
    /// lo usa al arrancarla y quien calcula de antemano para cuánto alcanza el disco.
    /// `conPistas` false: solo la del programa (el emisor aparte todavía no graba las 2-6).
    static int kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool conPistas = true);
    /// Para probar (modo --test): la grabación del programa, como con el disco trabado `ms`.
    virtual void simularDiscoTrabado(int ms) = 0;

    // --- Medir la sincronía labios/voz con un aplauso -----------------------------
    /// En el programa de `salida` ("" = la por omisión): una cámara contra su micrófono.
    virtual bool empezarMedicionSincro(const std::string& salida = "") = 0;
    /// {ok, retardo_ms, dispersion_ms, aplausos, confiable, problema, golpes_audio, golpes_video}.
    virtual nlohmann::json terminarMedicionSincro() = 0;
    virtual bool midiendoSincro() const = 0;
    /// Las pistas (máscara: bit 1..5 = pistas 2..6) que usa la grabación del programa.
    virtual int pistasGrabacionMask() const = 0;

    // --- Grabacion POR ENTRADA ------------------------------------------------
    // Cada entrada a su propio archivo, además del programa: con su video y su
    // audio (o el del programa si no tiene). Cada una es un encode más.
    struct GrabacionIndividual {
        std::string source;
        std::string path;
        bool audioPropio = false;
        bool activa = false;
        std::uint64_t bytes = 0; ///< lo que ya fue al archivo (sin preguntarle al disco)
        bool pausada = false;    ///< el disco, para el programa
    };
    static constexpr int kMaxIndividuales = 8;
    virtual bool startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc) = 0;
    /// La señal limpia (el programa sin DSK, placa ni negro) a un destino de red o a un archivo.
    virtual bool startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                        const EncoderConfig& enc, const std::string& salida = "") = 0;
    virtual void stopCleanFeed(const std::string& salida = "") = 0;
    virtual nlohmann::json cleanFeedEstado(const std::string& salida = "") const = 0;
    virtual bool stopSourceRecording(const std::string& source) = 0;
    virtual void stopAllSourceRecordings() = 0;
    virtual std::vector<GrabacionIndividual> sourceRecordings() const = 0;

    // --- Vista previa y medicion (para la UI) -------------------------------
    /// La vista previa del programa a `width` x `height` (BGRA, la escala el motor).
    virtual bool enablePreview(int width, int height) = 0;
    /// Vista previa CONTINUA de una escena (o fuente) aunque no esté al aire, por canal
    /// ("editor", "armada"…): varios canales a la vez.
    virtual bool enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) = 0;
    virtual bool enableScenePreview(const std::string& name, int maxWidth) = 0;
    /// Lo mismo para un bus de una salida (PLAN-SALIDAS, etapa 5): su programa (`bus`
    /// "programa") o su preview ("preview"), tal como salen, en el canal `canal` (se
    /// lee con renderScenePreview y se suelta con disableScenePreview, como las demás).
    virtual bool enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida, int maxWidth) = 0;
    virtual void disableScenePreview(const std::string& canal) = 0;
    virtual void disableScenePreview() = 0;
    /// El último cuadro de ese canal (BGRA) si es más nuevo que `serial`: comparte los
    /// bytes con la vista del motor (motor::Pixeles), no los copia (F3-11). DESDE CUALQUIER
    /// HILO (la interfaz los lee a 60 Hz sin el turno: F3-03); pedir y soltar una vista, no.
    virtual bool renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial) = 0;
    /// DESDE CUALQUIER HILO.
    virtual std::string scenePreviewName(const std::string& canal) const = 0;
    /// El ultimo frame de vista previa (BGRA) si es mas nuevo que `serial`, sin copiarlo
    /// (como renderScenePreview). DESDE CUALQUIER HILO.
    virtual bool latestPreview(motor::Cuadro& out, std::uint64_t& serial) const = 0;
    /// Estado actual (para la UI y el HealthBlock).
    virtual EngineStatus status() const = 0;
    /// El último error DE ESTE HILO (ver «Estrategia de errores»).
    virtual const std::string& lastError() const = 0;

    /// EL TURNO (emision/TurnoMotor.hpp): lo toma cada método de la API; quien hace varias
    /// cosas seguidas que tienen que verse juntas (AppRuntime, los conmutadores) lo toma por
    /// todo lo que dura, y el plano de control lo pide con tope (TurnoMotor::tomarHasta).
    virtual const TurnoMotor& turno() const = 0;

    /// Lo que oye el invitado del slot: "mix_minus" (de fábrica) o "programa".
    virtual void setRetornoInvitado(int slot, const std::string& modo) = 0;
    virtual std::string retornoInvitado(int slot) const = 0;

    // --- Lo que lee la foto del motor (arranque/PublicadorFoto) ------------------
    /// Las fuentes que muestran las capas que están arriba (tally, interlock de PTZ).
    virtual std::vector<std::string> fuentesDsk(const std::string& salida = "") const = 0;
    /// Alguna escena tiene una fuente «Banca con la palabra» (así se sabe si dar la
    /// palabra tiene que mover el recorte o lo hace la fuente).
    virtual bool hayFuenteBanca() const = 0;
    /// El archivo que se está grabando (o el último) de la salida.
    virtual std::string archivoGrabacion(const std::string& salida = "") const = 0;
    /// El reloj pedido y, si no arrancó (PTP sin permisos, NTP que no sincroniza), por qué
    /// (la tarjeta de preparación, F4-07).
    virtual std::pair<std::string, std::string> relojEstado() const = 0;
    virtual EscenasMostradas escenasMostradas() const = 0;
    /// {principal: {...}, extras: {id: {...}}, saltados_encoder, lag_render}.
    virtual nlohmann::json saludSalidas() const = 0;
    /// Alguna salida perdió un codificador en el aire y sigue con otro (la lista negra
    /// del motor; las salidas se rearman solas en el hilo de salidas).
    virtual bool encoderRespaldoActivo() const = 0;
    /// Tipos de fuente que el motor sabe armar.
    std::vector<SourceType> inputTypes() const { return tiposDeFuente(); }

   protected:
    ApiMotor() = default;
    ~ApiMotor() = default;
};

} // namespace ba::app
