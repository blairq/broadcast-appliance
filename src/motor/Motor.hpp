// ============================================================================
// src/motor/Motor.hpp — el motor: entradas, buses y el hilo que los atiende
//
// El dueño del reloj del estudio, las entradas y los buses de audio. La app (y
// más adelante la GUI por la red) le habla con comandos y lee su estado en JSON:
// nada de punteros a GStreamer afuera.
//
// LOS BUSES (cada uno un audiomixer en su pipeline):
//   programa    pista 1: lo que sale. Limitador del máster y loudness.
//   pista2…6    la grabación multipista: cada entrada va a las pistas que diga
//               su ruteo (la Matriz). Se arman cuando alguien las usa.
//   retornoN    lo que oye el invitado N en mix_minus (todo menos su voz).
//   directo     la sala sin demora, para la escucha «directo».
//
// QUÉ SUENA:
//   - Una entrada «global» (micrófonos, invitados, remoto, voces) suena siempre,
//     con su fader y su mute.
//   - Una entrada de escena suena solo si está en la escena al aire
//     (`escenaAlAire`), con un fundido al cambiar. Vale para el programa, las
//     pistas y los retornos.
//
// LOS RETORNOS (lo que oye cada invitado), uno por invitado:
//   - programa:   oye el programa (el gateway usa BA.Audio.ContentForReturn).
//   - mix_minus:  oye el programa SIN su propia voz → BA.Audio.Return.<n>.
//
// EL VIDEO: dos composiciones, PROGRAMA (lo que sale) y PREVIEW (la escena
// armada). `armar` pone una escena en el preview; `take` la pasa al programa
// con una transición (corte o mezcla), y el audio de sus capas la sigue con el
// mismo fundido (`escenaAlAire`).
//
// LA ESCUCHA (auriculares del operador): programa (lo que sale, junto con su
// video), directo (la sala sin demora: solo las globales), pfl (una entrada
// sola) o apagado.
// ============================================================================
#pragma once

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
    };

    explicit Motor(Config c);
    ~Motor();
    Motor(const Motor&) = delete;
    Motor& operator=(const Motor&) = delete;

    /// Arranca el bus PROGRAMA, el puente al gateway y el hilo. "" o el error.
    std::string iniciar();
    void detener();
    bool andando() const { return hilo_.joinable(); }

    // --- Entradas ---
    /// opciones: {"global": bool (suena siempre), "en_escena": bool (de fábrica: global),
    ///            "interna": bool (de la app: no va al multiview),
    ///            "nivel": lineal, "mute": bool, "pistas": [1…6] (de fábrica [1]: el programa)}
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
    std::string ponerNivel(const std::string& id, double lineal);
    std::string ponerMute(const std::string& id, bool mute);
    /// A qué pistas va (1 = el programa; 2…6 = la grabación multipista).
    std::string ponerPistas(const std::string& id, const std::vector<int>& pistas);
    /// Las entradas de escena que están al aire (las demás de escena se apagan).
    void escenaAlAire(const std::vector<std::string>& ids, int fundidoMs);
    /// {estado (none|playing|paused|stopped|ended|error), tiempo_ms, duracion_ms}; null si no es un medio.
    nlohmann::json medio(const std::string& id) const;
    /// Controles de un medio: play, pausa, reiniciar, parar; `ir` con ms.
    std::string controlMedio(const std::string& id, const std::string& accion, std::int64_t ms = -1);

    // --- Escenas y video ---
    /// Define (o cambia) una escena: {"nombre", "capas": [{entrada, x, y, ancho, alto, alfa, orden, recorte, visible}]}.
    /// Si está al aire o armada, se actualiza andando.
    std::string definirEscena(const nlohmann::json& escena);
    std::string quitarEscena(const std::string& nombre);
    /// La escena en el PREVIEW.
    std::string armar(const std::string& nombre);
    /// Lo armado al PROGRAMA: «corte» o «mezcla» de `ms`.
    std::string take(const std::string& transicion = "corte", int ms = 0);
    /// Una escena directo al PROGRAMA (sin pasar por el preview).
    std::string alAire(const std::string& nombre, const std::string& transicion = "corte", int ms = 0);
    /// El DSK del PROGRAMA (capas encima de cualquier escena: zócalos, logo), con fundido.
    /// [{entrada, x, y, ancho, alto, alfa, orden}]. Vacío = sin DSK.
    std::string dsk(const nlohmann::json& capas, int fundidoMs = 0);
    /// T-bar hacia `nombre` (t de 0 a 1); con `soltar` termina. true si quedó al aire.
    bool tbar(const std::string& nombre, double t, bool soltar, std::string* error = nullptr);
    std::string tbarHacia() const;
    /// Una composición de `nombre` aunque no esté al aire (el editor, el DSK): cuenta
    /// pedidos. Devuelve el productor ("" si no existe la escena).
    std::string usarEscena(const std::string& nombre);
    void soltarEscena(const std::string& nombre);
    /// Lo que entregan los buses de video: "programa", "limpio" (el programa sin DSK),
    /// "preview", "multiview"; y cuánto tardan.
    std::string productorDeVideo(const std::string& bus, std::int64_t* latenciaNs = nullptr);

    // --- Retornos ---
    /// modo: programa | mix_minus | apagado; `propia` = la entrada del invitado.
    std::string retorno(int n, const std::string& modo, const std::string& propia);

    // --- La escucha ---
    /// modo: programa | directo | pfl | seleccion | apagado; `id` = la entrada (pfl).
    /// «seleccion»: la suma de las entradas marcadas con ponerEscucha.
    std::string escuchar(const std::string& modo, const std::string& id = "");
    /// Marca (o desmarca) una entrada para la escucha «selección». Con alguna
    /// marcada la escucha pasa a «seleccion»; sin ninguna, se apaga.
    std::string ponerEscucha(const std::string& id, bool on);
    void escuchaDispositivo(const std::string& id);
    void escuchaNivel(double lineal);

    // --- El máster ---
    /// El volumen de todo el programa (0…1) con fundido (el FTB baja el audio).
    void masterVolumen(double lineal, int ms);
    /// Cada bloque del audio del programa (F32 estéreo 48 kHz, en el hilo de datos):
    /// el loudness, los picos, la medición de sincronía. Devuelve un id para sacarlo.
    int escucharPrograma(std::function<void(const float*, std::size_t cuadros)> fn);
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
    bool ponerLimitador(bool activo, double techoDb);
    void ponerTopeEstudio(std::int64_t ms);

    // --- Multiview (M2.4) ---
    /// PREVIEW y PROGRAMA arriba, las entradas con video abajo (hasta 8), cada una con
    /// su tally (rojo: al aire; verde: armada; gris) y su nombre. Se ve con la vista
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
    /// Espera a que el hilo del motor corra todo lo que se encoló hasta ahora.
    /// Para los tests y para hilos que no son la interfaz: la interfaz NUNCA
    /// espera (si lo hace, queda en el registro).
    void esperarCola();
    /// Que la próxima vuelta del bucle (20 ms) publique la foto, sin esperar las
    /// 10 vueltas de siempre: lo pide quien acaba de cambiar algo.
    void pedirFoto() { fotoPedida_.store(true); }
    Reloj& reloj() { return *reloj_; }
    /// Publica el reloj del estudio en la red (GstNetTimeProvider) para que otros
    /// aparatos lo usen (reloj «red»). 0 = dejar de publicar. "" o el error.
    std::string publicarReloj(int puerto);
    const Lienzo& lienzo() const { return cfg_.lienzo; }
    /// Lo que entrega un bus (para la grabación, más adelante): "bus:programa", "bus:pista2"…
    /// y cuánto tarda en salir (ns). "" si no existe.
    std::string productorDeBus(const std::string& bus, std::int64_t* latenciaNs = nullptr);

   private:
    struct Canal {
        std::shared_ptr<Entrada> entrada; ///< compartida con el buzón de cuadros (ver empujarCuadro)
        bool global = false, enEscena = false, interna = false;
        double nivel = 1.0;
        bool mute = false;
        unsigned pistas = 1; ///< bit 0 = programa, bit n-1 = pista n
        bool escucha = false; ///< va a la escucha «selección»
    };
    struct Retorno {
        std::string modo, propia;
        std::unique_ptr<Mezcla> bus;
    };
    void bucle();
    void enSeleccion(const std::string& id, const Canal& c);
    /// Pone (o saca) una entrada en un bus con el estado del canal.
    void enBus(Mezcla& m, const std::string& id, const Canal& c, bool incluir, int fundidoMs);
    /// Lleva un canal a todos los buses donde corresponde.
    void aplicar(const std::string& id, int fundidoMs);
    Mezcla* pista(int n, bool crear);
    void armarRetorno(int n, Retorno& r);
    std::string anillo(const std::string& nombre) const { return cfg_.prefijoAnillos + nombre; }

    Config cfg_;
    std::unique_ptr<Reloj> reloj_;
    GstObject* relojPublicado_ = nullptr; ///< GstNetTimeProvider
    mutable std::recursive_mutex mu_;
    std::mutex sondasM_;
    std::map<std::string, std::unique_ptr<Sondas>> sondasAparte_; ///< las del programa y la mezcla
    std::map<std::string, Canal> canales_;
    std::unique_ptr<Mezcla> programa_, directo_;
    std::unique_ptr<Mezcla> seleccion_; ///< la escucha «selección»
    std::map<int, std::unique_ptr<Mezcla>> pistas_;
    GstElement* puente_ = nullptr; ///< bus:programa → BA.Audio.ContentForReturn
    std::int64_t puenteLatNs_ = -1;
    std::map<int, Retorno> retornos_;
    std::unique_ptr<Escucha> escucha_;
    std::unique_ptr<Composicion> vPrograma_, vPreview_;
    std::unique_ptr<Vistas> vistas_;
    std::unique_ptr<Composicion> vMulti_;
    std::map<std::string, std::unique_ptr<Entrada>> mvAux_; ///< los colores del tally y los nombres
    int mvAncho_ = 1280, mvAlto_ = 720;
    struct CompEscena {
        std::unique_ptr<Composicion> comp;
        int pedidos = 0;
    };
    std::map<std::string, CompEscena> vEscenas_;
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
    /// Entradas que se están desarmando aparte, por id: una nueva con el mismo id
    /// espera a que termine (sus productores inter llevan el mismo nombre).
    std::mutex desarmandoM_;
    std::map<std::string, std::shared_future<void>> desarmando_;

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
    void correrOrdenes();
    // Lo que hacen de verdad las funciones públicas que se encolan.
    std::string agregarEntradaYa(const std::string& id, const nlohmann::json& ajustes, const nlohmann::json& opciones);
    std::string actualizarEntradaYa(const std::string& id, const nlohmann::json& ajustes);
    std::string quitarEntradaYa(const std::string& id);
    std::string propiedadEntradaYa(const std::string& id, const std::string& elemento, const std::string& prop,
                                   const std::string& valor);
    std::string controlEntradaYa(const std::string& id, const nlohmann::json& campos);
    std::string ponerNivelYa(const std::string& id, double lineal);
    std::string ponerMuteYa(const std::string& id, bool mute);
    std::string ponerPistasYa(const std::string& id, const std::vector<int>& pistas);
    void escenaAlAireYa(const std::vector<std::string>& ids, int fundidoMs);
    std::string controlMedioYa(const std::string& id, const std::string& accion, std::int64_t ms);
    nlohmann::json medioYa(const std::string& id) const;
    std::string definirEscenaYa(const nlohmann::json& escena);
    std::string quitarEscenaYa(const std::string& nombre);
    std::string armarYa(const std::string& nombre);
    std::string takeYa(const std::string& transicion, int ms);
    std::string alAireYa(const std::string& nombre, const std::string& transicion, int ms);
    std::string dskYa(const nlohmann::json& capas, int fundidoMs);
    bool tbarYa(const std::string& nombre, double t, bool soltar, std::string* error);
    std::string usarEscenaYa(const std::string& nombre);
    /// El lienzo de una escena interna (el suyo, o el del estudio).
    Lienzo lienzoDe(const Escena& e) const;
    void soltarEscenaYa(const std::string& nombre);
    std::string retornoYa(int n, const std::string& modo, const std::string& propia);
    std::string escucharYa(const std::string& modo, const std::string& id);
    std::string ponerEscuchaYa(const std::string& id, bool on);
    void escuchaDispositivoYa(const std::string& id);
    void escuchaNivelYa(double lineal);
    void masterVolumenYa(double lineal, int ms);
    bool ponerLimitadorYa(bool activo, double techoDb);
    std::string multiviewYa(bool activo, int ancho, int alto);
    std::string verPedirYa(const std::string& clave, int ancho, int alto);
    void verSoltarYa(const std::string& clave);
    std::set<std::string> dskEscenas_;
    // La toma de audio del programa
    GstElement* tomaAudio_ = nullptr;
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
    void armarTomaAudio();
    void escucharEntradaYa(int n, const std::string& id, std::function<void(const float*, std::size_t)> fn);
    std::mutex tomaMu_;
    std::map<int, std::function<void(const float*, std::size_t)>> oyentes_;
    int proximoOyente_ = 1;
    static GstFlowReturn alAudio(GstElement* sink, gpointer d);
    void actualizarMultiview();
    /// Qué entradas está mostrando alguien (composiciones y vistas): las demás
    /// van a 2 cuadros/s (Entrada::ponerEnUso). Con mu_ tomado.
    void actualizarUso();
    std::map<std::string, Escena> escenas_;
    std::string alAire_, armada_;
    std::string escuchaModo_ = "apagado", escuchaId_;
    std::thread hilo_;
    std::atomic<bool> seguir_{false};
};

} // namespace ba::motor
