// ============================================================================
// src/motor/Motor.hpp — el motor: entradas, buses y el hilo que los atiende
//
// El dueño del reloj del estudio, las entradas y los buses de audio. La app (y
// más adelante la GUI por la red) le habla con comandos y lee su estado en JSON:
// nada de punteros a GStreamer afuera.
//
// LOS BUSES (cada uno un audiomixer en su pipeline):
//   programa    pista 1: lo que sale. Limitador del máster (el loudness lo mide la app).
//   pista2…6    la grabación multipista: cada entrada va a las pistas que diga
//               su ruteo (la Matriz). Se arman cuando alguien las usa.
//   retornoN    lo que oye el invitado N en mix_minus (todo menos su voz), uno
//               por salida: retornoN de la por omisión, retornoN:<id> de las demás.
//   directo     la sala sin demora, para la escucha «directo».
//   seleccion   las entradas marcadas para la escucha (el PFL de la app).
//   afl         una entrada sola con el fader de una salida (la escucha «afl»).
//
// DOS CLASES DE BUSES (docs/arquitectura/04-plan-unificado.md, T4 y F2-01):
//   - de PROGRAMA (programa, pistas): esperan la latencia del estudio (más su margen),
//     para ir junto con el video: lo que se graba, se transmite y se ve en las pantallas.
//   - de OÍDO (retornos, directo, seleccion, afl): esperan una latencia FIJA y corta
//     (latenciaOidoNs: 20 ms a sus entradas, ~30 ms desde la captura), porque quien conversa o se escucha necesita oír ya, no
//     en sincronía con un video. Lo que llega más tarde que eso (una cámara IP con
//     audio) NO ENTRA a ese bus; el motor lo informa por entrada («entra_al_oido» en el
//     estado y «oido» con las que quedan afuera) y la interfaz lo muestra.
//
// QUÉ SUENA:
//   - Una entrada «global» (micrófonos, invitados, remoto, voces) suena siempre,
//     con su fader y su mute.
//   - Una entrada de escena suena solo si está en la escena al aire
//     (`escenaAlAire`), con un fundido al cambiar. Vale para el programa, las
//     pistas y los retornos.
//
// LOS RETORNOS (lo que oye cada invitado), uno por invitado Y POR SALIDA (F2-02):
// el invitado n en la salida X oye la mezcla de X (sus faders, su escena al aire) en
// BA.Audio.Return.<n> (la por omisión) o BA.Audio.Return.<X>.<n> (ipc/CanalesSalida.hpp).
// El motor no sabe en qué salida está cada invitado (lo decide la sala): arma el de
// todas, y el gateway lee el de la salida que le toca a cada uno.
//   - mix_minus:  oye el programa SIN su propia voz (de fábrica).
//   - programa:   oye el programa entero, con su voz (lo pidió y usa auriculares).
//   - apagado:    no oye nada: sin anillo.
// Los dos primeros arman su anillo: el gateway nunca le manda el programa común a un
// invitado al aire (tiene su voz: eco); sin su anillo, le apaga el retorno
// (gateway/RetornoLugar.hpp).
//
// EL VIDEO: dos composiciones, PROGRAMA (lo que sale) y PREVIEW (la escena
// armada). `armar` pone una escena en el preview; `take` la pasa al programa
// con una transición (corte o mezcla), y el audio de sus capas la sigue con el
// mismo fundido (`escenaAlAire`).
//
// LA ESCUCHA (auriculares del operador): programa (lo que sale, junto con su
// video: espera la latencia del estudio), y las de oído, que no esperan al video
// (F2-03): directo (la sala sin demora: solo las globales), pfl (una entrada sola,
// antes del fader y del bus), afl (una entrada con el fader de una salida),
// seleccion (las marcadas) o apagado. Directo, afl y seleccion usan el mando (fader,
// mute) de la salida que se elige al escuchar.
//
// LAS SALIDAS (docs/planes/PLAN-SALIDAS.md, etapa 1). El aparato tiene N salidas
// y cada una es un operador completo: su PROGRAMA y su PREVIEW, lo que tiene al
// aire y armado, su DSK, su T-bar y su mezcla (su bus, con su máster, su
// limitador y su toma para el loudness). Las entradas, las escenas y la cadena de
// cada entrada son de todas: cada entrada se procesa UNA vez y su audio entra al
// bus de cada salida con el fader, el mute y el «en escena» de ESA salida (su
// Mando). Una escena al aire en la salida B suena en el bus de B y no en el de A.
// Ninguna salida es especial, pero lo que no dice a cuál apunta va a la salida
// por omisión («principal», la que existe desde el arranque) y conserva los
// nombres de siempre: video:programa, video:programa:limpio, video:preview,
// bus:programa; vistas «programa», «preview», «limpio». Las demás llevan su id:
// video:programa:<id>, video:programa:<id>:limpio, video:preview:<id>,
// bus:programa:<id>; vistas «programa:<id>», «preview:<id>», «limpio:<id>».
// El audio de cada salida va al gateway: la por omisión a BA.Audio.ContentForReturn
// (como siempre), las demás a BA.Audio.Salida.<id> (etapa 2).
// LA MATRIZ (F3-08): los buses virtuales de la Matriz de la app son buses de OÍDO del
// motor (bus:matriz-<id>), cada envío con su ganancia por canal (nivel y pan). Uno con
// placa (una salida virtual) sale por ella como la escucha (su id estable, el Vigía, se
// rearma si la placa va y vuelve, BuffersAudio); el productor de uno sin placa (una
// entrada virtual) lo lee una entrada (`intersrc producer-name=bus:matriz-<id>`). Antes
// los mezclaba un hilo de la app con las tomas de cada entrada (escucharEntrada) y los
// sacaba con Qt Multimedia: un colchón y un dominio de reloj más.
//
// POR AHORA siguen a la salida por omisión: las pistas 2-6 y el multiview (su
// PROGRAMA y su PREVIEW son los de la por omisión; el tally rojo es «al aire en
// ALGUNA salida»). Los retornos son de cada salida; la escucha, de la que se elige.
// ============================================================================
#pragma once

#include "Candado.hpp"
#include "Sondas.hpp"

#include "Composicion.hpp"
#include "Entrada.hpp"
#include "Escucha.hpp"
#include "Mezcla.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"
#include "Vistas.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ba::motor {

class Motor {
   public:
    struct Config {
        Lienzo lienzo;
        std::string reloj = "sistema", relojServidor;
        int relojPuerto = 0, relojDominio = 0;
        std::int64_t topeMs = -1; ///< -1 = automático (250 ms)
        /// Cómo empiezan los anillos del gateway (las pruebas usan otro, para no
        /// pisar los de una app andando). "" = sin gateway: no se escriben.
        std::string prefijoAnillos = "BA.Audio.";
        /// Un sink en vez de la salida de audio del sistema para la escucha (las
        /// pruebas: «fakesink sync=true», para no sonar por los parlantes).
        std::string sinkEscucha;
        bool video = true;    ///< las composiciones (PROGRAMA y PREVIEW)
        bool videoGpu = false; ///< glvideomixer en vez de compositor
        /// Las salidas que codifica un `ba-emisor` aparte (F3-01): su programa va también
        /// al anillo BA.Audio.Emisor.<id> (el del gateway es de un solo lector).
        std::set<std::string> emisores;
    };

    explicit Motor(Config c);
    ~Motor();
    Motor(const Motor&) = delete;
    Motor& operator=(const Motor&) = delete;

    /// Arranca el bus PROGRAMA, el puente al gateway y el hilo. "" o el error.
    std::string iniciar();
    void detener();
    bool andando() const { return hilo_.joinable(); }

    // --- Las salidas ---
    /// La que existe desde el arranque y a la que va lo que no dice a cuál apunta.
    static constexpr const char* kSalidaPorOmision = "principal";
    /// Una salida nueva (andando, si el motor anda): sin nada al aire ni armado;
    /// su mezcla arranca con los faders y mutes de la por omisión. El id no puede
    /// tener «:» ni «@» (van en los productores y en las claves de las vistas).
    std::string crearSalida(const std::string& id, const std::string& nombre);
    /// Saca una salida andando (la por omisión no se borra).
    std::string borrarSalida(const std::string& id);
    /// {id: {nombre, por_omision, al_aire, armada, tbar_hacia, …}} de la foto (no espera).
    nlohmann::json salidas() const;
    const std::string& salidaPorOmision() const { return porOmision_; }

    // --- Entradas ---
    /// opciones: {"global": bool (suena siempre), "en_escena": bool (de fábrica: global),
    ///            "interna": bool (de la app: no va al multiview),
    ///            "nivel": lineal, "mute": bool, "pistas": [1…6] (de fábrica [1]: el programa)}
    /// El nivel, el mute y el «en escena» de las opciones valen para todas las salidas.
    /// En todo lo que sigue, `salida` "" = la por omisión.
    std::string agregarEntrada(const std::string& id, const nlohmann::json& ajustes,
                               const nlohmann::json& opciones = nlohmann::json::object());
    std::string actualizarEntrada(const std::string& id, const nlohmann::json& ajustes);
    std::string quitarEntrada(const std::string& id);
    /// Una propiedad de un elemento de la cadena de la entrada (ba_f_<filtro>), en vivo.
    std::string propiedadEntrada(const std::string& id, const std::string& elemento, const std::string& prop,
                                 const std::string& valor);
    /// Las propiedades legibles de ese elemento (vacío si no está).
    nlohmann::json propiedadesEntrada(const std::string& id, const std::string& elemento) const;
    /// El canal de control «ba/control» de la entrada (ver Entrada::control).
    std::string controlEntrada(const std::string& id, const nlohmann::json& campos);
    /// El fader y el mute de la entrada en la mezcla de una salida. Los de la por
    /// omisión valen también para las pistas, los retornos y la escucha «selección».
    std::string ponerNivel(const std::string& id, double lineal, const std::string& salida = "");
    std::string ponerMute(const std::string& id, bool mute, const std::string& salida = "");
    /// A qué pistas va (1 = el programa de cada salida; 2…6 = la grabación multipista).
    std::string ponerPistas(const std::string& id, const std::vector<int>& pistas);
    /// Las entradas de escena que están al aire en una salida (las demás de escena se apagan en ella).
    void escenaAlAire(const std::vector<std::string>& ids, int fundidoMs, const std::string& salida = "");
    /// {estado (none|playing|paused|stopped|ended|error), tiempo_ms, duracion_ms}; null si no es un medio.
    nlohmann::json medio(const std::string& id) const;
    /// Controles de un medio: play, pausa, reiniciar, parar; `ir` con ms.
    std::string controlMedio(const std::string& id, const std::string& accion, std::int64_t ms = -1);

    // --- Escenas y video ---
    /// Define (o cambia) una escena: {"nombre", "capas": [{entrada, x, y, ancho, alto, alfa, orden, recorte, visible}]}.
    /// Si está al aire o armada, se actualiza andando.
    std::string definirEscena(const nlohmann::json& escena);
    std::string quitarEscena(const std::string& nombre);
    /// La escena en el PREVIEW de la salida.
    std::string armar(const std::string& nombre, const std::string& salida = "");
    /// Lo armado al PROGRAMA de la salida: «corte» o «mezcla» de `ms`.
    std::string take(const std::string& transicion = "corte", int ms = 0, const std::string& salida = "");
    /// Una escena directo al PROGRAMA de la salida (sin pasar por el preview).
    std::string alAire(const std::string& nombre, const std::string& transicion = "corte", int ms = 0,
                       const std::string& salida = "");
    /// El DSK del PROGRAMA de la salida (capas encima de cualquier escena: zócalos, logo), con fundido.
    /// [{entrada, x, y, ancho, alto, alfa, orden}]. Vacío = sin DSK.
    std::string dsk(const nlohmann::json& capas, int fundidoMs = 0, const std::string& salida = "");
    /// T-bar de la salida hacia `nombre` (t de 0 a 1); con `soltar` termina. true si quedó al aire.
    bool tbar(const std::string& nombre, double t, bool soltar, std::string* error = nullptr, const std::string& salida = "");
    std::string tbarHacia(const std::string& salida = "") const;
    /// Una composición de `nombre` aunque no esté al aire (el editor, el DSK): cuenta
    /// pedidos. Devuelve el productor ("" si no existe la escena). Con `mini`, otra
    /// composición de la misma escena a kMiniFps (las miniaturas de la línea de tiempo,
    /// vista «mini:<escena>»): el mezclador trabaja 5 veces por segundo, no 30.
    std::string usarEscena(const std::string& nombre, bool mini = false);
    void soltarEscena(const std::string& nombre, bool mini = false);
    static constexpr int kMiniFps = 5;
    /// Lo que entregan los buses de video: "programa", "limpio" (el programa sin DSK),
    /// "preview" (de `salida`, o de la que diga la clave: "programa:<id>"…), "multiview";
    /// y cuánto tardan.
    std::string productorDeVideo(const std::string& bus, std::int64_t* latenciaNs = nullptr, const std::string& salida = "");

    // --- Retornos ---
    /// modo: programa | mix_minus | apagado; `propia` = la entrada del invitado.
    std::string retorno(int n, const std::string& modo, const std::string& propia);

    // --- La escucha ---
    /// modo: programa | directo | pfl | afl | seleccion | apagado; `id` = la entrada (pfl,
    /// afl) o la salida (programa; "" = la por omisión). `salida`: de qué salida son el
    /// fader y el mute en directo, afl y seleccion ("" = la por omisión).
    /// «seleccion»: la suma de las entradas marcadas con ponerEscucha.
    std::string escuchar(const std::string& modo, const std::string& id = "", const std::string& salida = "");
    /// Marca (o desmarca) una entrada para la escucha «selección». Con alguna
    /// marcada la escucha pasa a «seleccion»; sin ninguna, se apaga.
    std::string ponerEscucha(const std::string& id, bool on);
    void escuchaDispositivo(const std::string& id);
    void escuchaNivel(double lineal);

    // --- La Matriz (F3-08: ver arriba) ---
    /// Lo que manda una entrada a un bus de la Matriz: la ganancia de cada canal (lineal).
    struct EnvioMatriz {
        std::string entrada;
        double izq = 1.0, der = 1.0;
    };
    /// Arma (o acomoda: solo lo que cambió) el bus `id` con esos envíos. `dispositivo` no
    /// vacío: sale por esa placa (su id estable; "default" = la del sistema); si no está,
    /// muda hasta que vuelva (sin ir a la del sistema).
    std::string matrizBus(const std::string& id, const std::vector<EnvioMatriz>& envios, const std::string& dispositivo);
    std::string quitarMatrizBus(const std::string& id);
    /// Lo que entrega un bus de la Matriz (para la entrada de una entrada virtual).
    static std::string productorMatriz(const std::string& id) { return "bus:matriz-" + id; }

    // --- El máster ---
    /// El volumen de todo el programa de la salida (0…1) con fundido (el FTB baja el audio).
    void masterVolumen(double lineal, int ms, const std::string& salida = "");
    /// Cada bloque del audio del programa de la salida (F32 estéreo 48 kHz, en el hilo
    /// de datos): el loudness, los picos, la medición de sincronía. Devuelve un id para
    /// sacarlo. Una salida que todavía no existe empieza a llegar cuando se crea.
    int escucharPrograma(std::function<void(const float*, std::size_t cuadros)> fn, const std::string& salida = "");
    void dejarDeEscuchar(int id);
    /// El audio de UNA entrada después de su cadena (lo mismo que va a la mezcla),
    /// estéreo intercalado a 48 kHz, en el hilo de GStreamer, apenas llega (sin
    /// esperar al reloj): la matriz de la app lo usa para sus buses virtuales.
    /// Con la entrada silenciada llegan ceros (como lo que va a la mezcla).
    int escucharEntrada(const std::string& id, std::function<void(const float*, std::size_t cuadros)> fn);
    void dejarDeEscucharEntrada(int toma);
    /// ¿Sigue andando esa toma? (un motor rearmado no tiene las del anterior:
    /// los números no se repiten entre motores).
    bool tomaViva(int toma) const;
    bool ponerLimitador(bool activo, double techoDb, const std::string& salida = "");
    /// El loudness de lo que sale por `salida`: el AGC lento hacia `objetivoLufs`
    /// (opcional) y el techo del limitador (dBTP); la por omisión, también el techo de
    /// las pistas 2-6. En la cola del motor: "" o el error.
    std::string ponerLoudness(bool agc, double objetivoLufs, double techoDb, const std::string& salida = "");
    void ponerTopeEstudio(std::int64_t ms);

    // --- Multiview (M2.4) ---
    /// PREVIEW y PROGRAMA (los de la salida por omisión) arriba, las entradas con video
    /// abajo (hasta 8), cada una con su tally (rojo: al aire en alguna salida; verde:
    /// armada en la por omisión; gris) y su nombre. Se ve con la vista
    /// «multiview» o se consume como «video:multiview».
    std::string multiview(bool activo, int ancho = 1280, int alto = 720);

    // --- Imágenes para la interfaz ---
    /// "programa", "preview" o "entrada:<id>" al tamaño pedido (cuenta pedidos).
    std::string verPedir(const std::string& clave, int ancho, int alto);
    void verSoltar(const std::string& clave);
    bool verUltimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const;
    /// Cada cuadro de una vista ya pedida (en el hilo de datos). fn vacía = sacar.
    void verAvisar(const std::string& clave, std::function<void(const Cuadro&)> fn);
    /// Un cuadro para una entrada tipo=app.
    bool empujarCuadro(const std::string& id, const std::uint8_t* bgra, int ancho, int alto, int stride);

    nlohmann::json estado() const;
    /// Análisis por elemento de una entrada (Sondas.hpp, docs/planes/PLAN-TIEMPOS.md T3):
    /// empezar pone las sondas; terminar las saca y devuelve cuánto agrega cada elemento y
    /// en qué memoria va (GPU o sistema), con los cruces. Pueden esperar el candado de la
    /// entrada (un rearmado): llamarlas fuera del hilo de la interfaz.
    /// `entrada`: una entrada, o «programa» (la composición del programa) / «programa:audio» (la mezcla).
    std::string analizarEmpezar(const std::string& entrada);
    /// Si la latencia del estudio puede bajar sola (sin nada al aire). En vivo solo sube.
    void permitirBajarLatencia(bool si) { permitirBajar_ = si; }
    /// Bajarla ahora a lo que piden las entradas sincronizadas (el operador lo pide).
    bool bajarLatenciaEstudio() { return reloj_ && reloj_->bajarLatencia(); }
    nlohmann::json analizarTerminar(const std::string& entrada);
    /// La última foto de `estado()` que publicó el bucle del motor (cada ~100 ms).
    /// NO toma el candado: es lo que lee la interfaz (los vúmetros la leen cada
    /// 100 ms; con `estado()` cualquier espera del candado congelaba la app).
    /// Vacía (json nulo) hasta la primera vuelta del bucle.
    std::shared_ptr<const nlohmann::json> instantanea() const;
    /// Los niveles para los vúmetros (el Mezclador): cada entrada con audio (lo que
    /// entrega, después de su rack, vaya a donde vaya) y cada bus (programa,
    /// pista<n>, retorno<n>), por canal, pico y RMS en dB. La publica el bucle cada
    /// ~40 ms; como la foto, NO toma el candado (la interfaz nunca espera).
    struct NivelAudio {
        std::string id;
        bool bus = false;
        float pico[2] = {-100, -100};
        float rms[2] = {-100, -100};
    };
    std::shared_ptr<const std::vector<NivelAudio>> niveles() const;
    /// Las órdenes que se encolaron y fallaron (ver orden): `que` es la función.
    /// Se llama desde el hilo del motor.
    void alFallar(std::function<void(const std::string& que, const std::string& error)> fn);
    /// Espera a que el hilo del motor corra todo lo que se encoló hasta ahora (y avise
    /// lo que falló: alFallar ya se llamó). Para los tests y para hilos que no son la
    /// interfaz: la interfaz NUNCA espera (si lo hace, queda en el registro).
    void esperarCola();
    /// Que la próxima vuelta del bucle (20 ms) publique la foto, sin esperar las
    /// 10 vueltas de siempre: lo pide quien acaba de cambiar algo.
    void pedirFoto() { fotoPedida_.store(true); }
    Reloj& reloj() { return *reloj_; }
    /// Publica el reloj del estudio en la red (GstNetTimeProvider) para que otros
    /// aparatos lo usen (reloj «red»). 0 = dejar de publicar. "" o el error.
    std::string publicarReloj(int puerto);
    const Lienzo& lienzo() const { return cfg_.lienzo; }
    /// Lo que entrega un bus (para la grabación, más adelante): "programa" (de `salida`,
    /// o "programa:<id>"), "pista2"… y cuánto tarda en salir (ns). "" si no existe.
    std::string productorDeBus(const std::string& bus, std::int64_t* latenciaNs = nullptr, const std::string& salida = "");

   private:
    struct Canal {
        std::shared_ptr<Entrada> entrada; ///< compartida con el buzón de cuadros (ver empujarCuadro)
        bool global = false, interna = false;
        unsigned pistas = 1; ///< bit 0 = el programa de cada salida, bit n-1 = pista n
        bool escucha = false; ///< va a la escucha «selección»
    };
    /// Lo que una salida hace con una entrada en su mezcla.
    struct Mando {
        double nivel = 1.0;
        bool mute = false;
        bool enEscena = false; ///< está en la escena al aire de ESA salida
    };
    /// La toma del audio del programa de una salida (escucharPrograma). Compartida con
    /// su desarme aparte: GStreamer puede llamar a alAudio hasta que se pasa a NULL.
    struct TomaPrograma {
        Motor* motor = nullptr;
        std::string salida;
        GstElement* pipeline = nullptr;
    };
    /// Una salida (ver LAS SALIDAS arriba): todo lo del aire que antes era único.
    struct Salida {
        std::string nombre;
        std::unique_ptr<Mezcla> mezcla;                   ///< bus:programa[:<id>]
        std::unique_ptr<Composicion> programa, preview;   ///< video:programa[:<id>], video:preview[:<id>]
        std::string alAire, armada;
        std::set<std::string> dskEscenas;                 ///< las escenas que pidió su DSK
        std::map<std::string, Mando> mandos;              ///< por entrada
        std::shared_ptr<TomaPrograma> toma;
        GstElement* puente = nullptr; ///< bus → BA.Audio.ContentForReturn (la por omisión) o BA.Audio.Salida.<id>
        GstElement* puenteEmisor = nullptr; ///< bus → BA.Audio.Emisor.<id> (con emisor aparte, F3-01)
        std::int64_t puenteLatNs = -1;      ///< la de los dos puentes
        /// El mix-minus de cada invitado en esta salida (buses de oído): retorno<n>[:<id>]
        /// → BA.Audio.Return[.<id>].<n>. Uno por cada retorno de `retornos_` que no está apagado.
        std::map<int, std::unique_ptr<Mezcla>> retornos;
        // El loudness (ponerLoudness): queda acá para que un bus rearmado lo vuelva a tener.
        bool agc = false;
        double objetivoLufs = -23, techoDb = -1;
    };
    /// Lo que pidió el invitado n (vale en todas las salidas: sus buses están en cada Salida).
    struct Retorno {
        std::string modo, propia;
        /// Si la entrada `id` (que va al programa) suena en este retorno: todas, salvo su
        /// propia voz en mix_minus.
        bool oye(const std::string& id) const { return modo == "programa" || id != propia; }
    };
    void bucle();
    void enSeleccion(const std::string& id, const Canal& c);
    /// Pone (o saca) una entrada en un bus con el estado del canal y el mando de una salida.
    void enBus(Mezcla& m, const std::string& id, const Canal& c, const Mando& k, bool incluir, int fundidoMs);
    /// Lleva un canal a todos los buses donde corresponde (los de todas las salidas).
    void aplicar(const std::string& id, int fundidoMs);
    /// Lo mismo, solo en lo que depende de una salida: su bus, sus retornos, la escucha
    /// si la sigue a ella y, si es la por omisión, lo que por ahora la sigue (las pistas).
    void aplicarEnSalida(Salida& s, const std::string& id, int fundidoMs);
    /// La entrada en los buses de oído de la escucha (directo, selección, afl) con el
    /// mando `k` de la salida que sigue la escucha. Sin fundidos: no tocan el aire.
    void aplicarEscucha(const std::string& id, const Canal& c, const Mando& k);
    /// "" = la por omisión; nullptr si no hay una con ese id.
    Salida* laSalida(const std::string& id);
    const Salida* laSalida(const std::string& id) const;
    Salida& porOmision() { return salidas_.at(porOmision_); }
    const Mando& mandoPorOmision(const std::string& id) const;
    std::string noHaySalida(const std::string& id) const;
    /// Arma lo que anda de una salida (su bus, sus composiciones, su puente, su toma).
    std::string armarSalida(const std::string& id, Salida& s);
    /// Lo desarma: aparte (en caliente: devuelve cuándo termina) o ya (al apagar, con
    /// los desarmes ya esperados).
    std::shared_future<void> desarmarSalida(Salida& s, bool aparte);
    /// El programa de la salida al anillo BA.Audio.<canal> (con la latencia de los puentes). nullptr si no.
    GstElement* armarPuente(Salida& s, const std::string& canal);
    // LOS «...Ya»: corren con el candado tomado (las órdenes de la cola, el bucle); no lo
    // piden, lo exigen (BA_CON_CANDADO, Candado.hpp). Las públicas los encolan.
    std::string crearSalidaYa(const std::string& id, const std::string& nombre);
    std::string borrarSalidaYa(const std::string& id);
    std::string productorDeVideoYa(const std::string& bus, std::int64_t* latenciaNs, const std::string& salida);
    nlohmann::json estadoYa() const;
    Mezcla* pista(int n, bool crear);
    /// El retorno del invitado n en la salida `sid` (lo arma de nuevo con lo que pide
    /// retornos_[n]; si está apagado, lo saca). false si no arrancó.
    bool armarRetorno(int n, const std::string& sid, Salida& s);
    /// Un bus de oído (latencia fija corta: ver DOS CLASES DE BUSES) sin medidores.
    std::unique_ptr<Mezcla> busDeOido(const std::string& nombre, std::string anillo = "");
    /// La salida cuyo mando usa la escucha de oído (directo, afl, seleccion).
    Salida* salidaDeEscucha() { return laSalida(escuchaSalida_); }
    /// Cuánto espera la escucha PFL de una entrada: lo de oído, o lo que tarda su audio
    /// si tarda más (una cámara IP: sin esto, en los auriculares no se oiría nada).
    std::int64_t latenciaPfl(const Entrada& e) const;
    /// Si el audio de la entrada llega a tiempo a los buses de oído (sin medir: sí).
    static bool entraAlOido(const Entrada& e);
    std::string anillo(const std::string& nombre) const { return cfg_.prefijoAnillos + nombre; }

    Config cfg_;
    std::unique_ptr<Reloj> reloj_;
    GstObject* relojPublicado_ = nullptr; ///< GstNetTimeProvider
    mutable MutexMotor mu_; ///< el candado del motor (Candado.hpp): común, sin reentradas
    std::mutex sondasM_;
    std::atomic<bool> permitirBajar_{false};
    std::map<std::string, std::unique_ptr<Sondas>> sondasAparte_; ///< las del programa y la mezcla
    std::map<std::string, Canal> canales_;
    std::map<std::string, Salida> salidas_; ///< la por omisión existe siempre (desde el constructor)
    std::string porOmision_ = kSalidaPorOmision;
    std::unique_ptr<Mezcla> directo_;
    std::unique_ptr<Mezcla> seleccion_; ///< la escucha «selección»
    std::unique_ptr<Mezcla> afl_;       ///< la escucha «afl»
    std::map<int, std::unique_ptr<Mezcla>> pistas_;
    double techoPistas_ = -1.0; ///< el techo de sus limitadores (lo pone ponerLoudness)
    std::map<int, Retorno> retornos_;
    std::unique_ptr<Escucha> escucha_;
    std::unique_ptr<Vistas> vistas_;
    std::unique_ptr<Composicion> vMulti_;
    std::map<std::string, std::unique_ptr<Entrada>> mvAux_; ///< los colores del tally y los nombres
    int mvAncho_ = 1280, mvAlto_ = 720;
    struct CompEscena {
        std::unique_ptr<Composicion> comp;
        int pedidos = 0;
    };
    std::map<std::string, CompEscena> vEscenas_;
    std::map<std::string, CompEscena> vMinis_; ///< las de la línea de tiempo (a kMiniFps)
    mutable std::mutex instantaneaM_; ///< solo para copiar el puntero: nunca se tiene haciendo algo
    std::shared_ptr<const nlohmann::json> instantanea_;
    void publicarNiveles();
    mutable std::mutex nivelesM_; ///< como instantaneaM_
    std::shared_ptr<const std::vector<NivelAudio>> niveles_;
    std::atomic<bool> fotoPedida_{true};
    void publicarInstantanea();
    /// Las composiciones que se están desarmando aparte (ver soltarEscena).
    std::mutex desarmesM_;
    std::vector<std::shared_future<void>> desarmes_;
    void desarmarAparte(std::unique_ptr<Composicion> c);
    std::shared_future<void> desarmarAparte(std::function<void()> f);
    /// Entradas que se están desarmando aparte, por id (las salidas, por «salida:<id>»:
    /// las entradas no llevan «:»): una nueva con el mismo id espera a que termine
    /// (sus productores inter llevan el mismo nombre).
    std::mutex desarmandoM_;
    std::map<std::string, std::shared_future<void>> desarmando_;
    void esperarDesarme(const std::string& clave);

    // --- El buzón de cuadros de la app (ver empujarCuadro) ---
    struct Buzon {
        std::vector<std::uint8_t> px; ///< BGRA, filas pegadas
        int ancho = 0, alto = 0;
        bool hay = false;
    };
    std::mutex buzonesM_;
    std::condition_variable buzonesCv_;
    std::map<std::string, Buzon> buzones_;
    std::map<std::string, std::shared_ptr<Entrada>> deApp_; ///< bajo buzonesM_: las que reciben cuadros
    std::set<std::string> noDeApp_; ///< bajo buzonesM_: las que existen y no reciben cuadros
    std::map<std::string, std::shared_ptr<Entrada>> todas_; ///< bajo buzonesM_: índice para leer sin mu_
    std::thread hiloCuadros_;
    void cuadros();
    void indexarDeApp(const std::string& id, const std::shared_ptr<Entrada>& e);
    /// Suelta una referencia a una entrada: si era la última, se desarma en otro
    /// hilo (su destructor pasa sus pipelines a NULL: ni la interfaz ni el bucle).
    void soltarEntrada(std::shared_ptr<Entrada>&& e);

    // --- La cola de órdenes (ver orden en Motor.cpp) ---
    std::mutex colaM_;
    std::condition_variable colaCv_;
    std::deque<std::pair<const char*, std::function<std::string()>>> cola_;
    std::function<void(const std::string&, const std::string&)> alFallar_; ///< bajo colaM_
    std::atomic<std::thread::id> hiloId_{};
    bool enHiloDelMotor() const;
    /// Encola `f` para el hilo del motor (o la corre ya, desde ese hilo o con el
    /// motor parado). Devuelve su error solo si la corrió ya.
    std::string orden(const char* que, std::function<std::string()> f);
    /// A la cola siempre, sin correrla nunca directo (desde los trabajadores).
    void encolar(const char* que, std::function<std::string()> f);
    /// Lo que deja una vuelta de órdenes para después de soltar el candado: las que
    /// fallaron (a alFallar) y los que esperan la cola (esperarCola), en ese orden.
    struct DespuesDeLasOrdenes {
        std::vector<std::pair<const char*, std::string>> fallas;
        std::vector<std::shared_ptr<std::promise<void>>> esperan;
    };
    /// Corre lo encolado (con el candado).
    DespuesDeLasOrdenes correrOrdenes();
    /// Avisa las fallas y suelta a los que esperan (sin el candado).
    void despuesDeLasOrdenes(DespuesDeLasOrdenes& d);
    std::vector<std::shared_ptr<std::promise<void>>> esperanCola_; ///< bajo mu_ (las órdenes de esperarCola)
    // Lo que hacen de verdad las funciones públicas que se encolan.
    std::string agregarEntradaYa(const std::string& id, const nlohmann::json& ajustes, const nlohmann::json& opciones);
    std::string actualizarEntradaYa(const std::string& id, const nlohmann::json& ajustes);
    std::string quitarEntradaYa(const std::string& id);
    std::string propiedadEntradaYa(const std::string& id, const std::string& elemento, const std::string& prop,
                                   const std::string& valor);
    std::string controlEntradaYa(const std::string& id, const nlohmann::json& campos);
    std::string ponerNivelYa(const std::string& id, double lineal, const std::string& salida);
    std::string ponerMuteYa(const std::string& id, bool mute, const std::string& salida);
    std::string ponerPistasYa(const std::string& id, const std::vector<int>& pistas);
    void escenaAlAireYa(const std::vector<std::string>& ids, int fundidoMs, const std::string& salida);
    std::string controlMedioYa(const std::string& id, const std::string& accion, std::int64_t ms);
    nlohmann::json medioYa(const std::string& id) const;
    std::string definirEscenaYa(const nlohmann::json& escena);
    std::string quitarEscenaYa(const std::string& nombre);
    std::string armarYa(const std::string& nombre, const std::string& salida);
    std::string takeYa(const std::string& transicion, int ms, const std::string& salida);
    std::string alAireYa(const std::string& nombre, const std::string& transicion, int ms, const std::string& salida);
    std::string dskYa(const nlohmann::json& capas, int fundidoMs, const std::string& salida);
    bool tbarYa(const std::string& nombre, double t, bool soltar, std::string* error, const std::string& salida);
    std::string usarEscenaYa(const std::string& nombre, bool mini = false);
    /// El lienzo de una escena interna (el suyo, o el del estudio); `mini`, a kMiniFps.
    Lienzo lienzoDe(const Escena& e, bool mini = false) const;
    void soltarEscenaYa(const std::string& nombre, bool mini = false);
    std::string retornoYa(int n, const std::string& modo, const std::string& propia);
    std::string escucharYa(const std::string& modo, const std::string& id, const std::string& salida);
    std::string ponerEscuchaYa(const std::string& id, bool on);
    void escuchaDispositivoYa(const std::string& id);
    void escuchaNivelYa(double lineal);
    std::string matrizBusYa(const std::string& id, const std::vector<EnvioMatriz>& envios, const std::string& dispositivo);
    std::string quitarMatrizBusYa(const std::string& id);
    /// Un bus de la Matriz: su mezcla y, si sale por una placa, su salida (una Escucha).
    struct BusMatriz {
        std::unique_ptr<Mezcla> mezcla;
        std::unique_ptr<Escucha> salida;
        std::string dispositivo;
        std::map<std::string, std::pair<double, double>> envios; ///< entrada → (izq, der)
    };
    std::map<std::string, BusMatriz> matriz_; ///< bajo mu_
    void masterVolumenYa(double lineal, int ms, const std::string& salida);
    bool ponerLimitadorYa(bool activo, double techoDb, const std::string& salida);
    std::string ponerLoudnessYa(bool agc, double objetivoLufs, double techoDb, const std::string& salida);
    std::string multiviewYa(bool activo, int ancho, int alto);
    std::string verPedirYa(const std::string& clave, int ancho, int alto);
    void verSoltarYa(const std::string& clave);
    struct TomaEntrada {
        GstElement* pipeline = nullptr;
        std::string id;
        std::atomic<bool> mute{false}; ///< la entrada está silenciada: llegan ceros
        std::function<void(const float*, std::size_t)> fn;
        std::mutex fnM;       ///< se tiene mientras se llama a fn
        bool apagada = false; ///< bajo fnM: ya no se llama a fn
    };
    std::map<int, std::shared_ptr<TomaEntrada>> tomasEntrada_; ///< bajo mu_ (fn se llama sin él)
    /// Las vivas, para tomaViva y dejarDeEscucharEntrada sin el candado del motor.
    mutable std::mutex tomasVivasM_;
    std::map<int, std::shared_ptr<TomaEntrada>> tomasVivas_;
    /// La toma del programa de una salida, si alguien la escucha y no está armada.
    void armarTomaAudio(const std::string& salida);
    void escucharEntradaYa(int n, const std::string& id, std::function<void(const float*, std::size_t)> fn);
    std::mutex tomaMu_;
    struct Oyente {
        std::string salida; ///< el id (ya resuelto: nunca "")
        std::function<void(const float*, std::size_t)> fn;
    };
    std::map<int, Oyente> oyentes_; ///< bajo tomaMu_
    bool hayOyentes(const std::string& salida); ///< toma tomaMu_
    int proximoOyente_ = 1;
    static GstFlowReturn alAudio(GstElement* sink, gpointer d);
    void actualizarMultiview();
    /// Qué entradas está mostrando alguien (composiciones y vistas): las demás
    /// van a 2 cuadros/s (Entrada::ponerEnUso). Con mu_ tomado.
    void actualizarUso();
    std::map<std::string, Escena> escenas_;
    std::string escuchaModo_ = "apagado";
    std::string escuchaId_; ///< la entrada (pfl, afl) o la salida (programa; "" = la por omisión)
    std::string escuchaSalida_; ///< de qué salida son el fader y el mute en directo, afl y seleccion ("" = la por omisión)
    std::thread hilo_;
    std::atomic<bool> seguir_{false};
};

} // namespace ba::motor
