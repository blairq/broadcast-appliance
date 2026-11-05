// ============================================================================
// src/motor/Composicion.hpp — un bus de video del motor (PROGRAMA, PREVIEW…)
//
// Un pipeline propio por bus:
//
//   intersrc <id>:video ! <recorte> ─► <mezclador> (pad: x, y, ancho, alto, alfa, orden)
//   intersrc <id>:video ! <recorte> ─►     │
//   …                                      ▼
//                         caps del lienzo ! intersink video:<nombre>
//
//   <recorte>: videocrop (CPU), gltransformation (GL) o d3d12convert (D3D12);
//   <mezclador>: compositor, glvideomixer o d3d12compositor. Sin queue por capa:
//   la cola es la del intersrc (tira lo viejo).
//
// - Reloj y base_time del estudio; en vivo aunque no haya capas (force-live),
//   con la grilla de cuadros fija sobre la hora del estudio
//   (start-time-selection=set: la misma para el mezclador de la escena y el del
//   DSK) y espera la latencia del estudio + margen (min-upstream-latency): una
//   entrada lenta que se enchufa andando no llega tarde.
// - ESCENA = un conjunto de capas (qué entrada, dónde, de qué tamaño, recorte,
//   alfa, orden). `mostrar` pone una escena con una TRANSICIÓN:
//     LO NUEVO SE ARMA FUERA DEL AIRE: las capas de la escena que entra se crean con
//     alfa 0 (no se ven) y la que está sigue entera hasta que TODAS las nuevas dieron
//     su primer cuadro (o pasó video.esperaCorteMs: corta con lo que haya y lo avisa
//     en el registro, nombrando las que no llegaron). Recién ahí arranca la transición.
//     Antes la nueva entraba visible encima de la vieja y cada capa aparecía cuando
//     llegaba su primer cuadro: al aire se veía la escena armándose de a una (con las
//     entradas que descansan, los invitados según demanda o una placa que se rearma,
//     ese primer cuadro tarda distinto en cada una).
//     corte   la nueva aparece entera de una; la vieja se saca en el mismo tic.
//     mezcla  la nueva entra encima con su alfa de 0 a 1 en `ms`; después se saca la vieja.
//     empuje  la nueva entra desde la derecha empujando a la vieja hacia la izquierda.
//     cortina la nueva se va descubriendo de izquierda a derecha (recorte + ancho del
//             pad juntos: no se deforma).
//   Mostrar la MISMA escena otra vez (mismo nombre) actualiza las capas en el
//   lugar: posición, tamaño, recorte y alfa cambian andando, sin rearmar.
// - CAPAS ANIMADAS POR CLAVES (Capa::claves): una capa que va de un lugar a otro con
//   su propio reloj (la cámara que entra dentro de un zócalo con la línea de tiempo
//   del gráfico, ver src/app/graficos/VideoEnGrafico.hpp). La mueve tic(), como las
//   transiciones; el reloj se corre lo que tarda en salir lo que se compone (la edad
//   de la salida), así el ms 0 cae en el cuadro que sale con el primero del gráfico.
// - SOMBRA y BORDE DIFUSO de una capa (Capa::sombra): basombra (o su par de GL o
//   D3D12) después del recorte; sale un cuadro más grande (un margen alrededor
//   para la sombra) y la caja de la capa en el mezclador se agranda lo mismo,
//   así la imagen queda donde estaba. La rama lo lleva desde que alguna vez lo
//   necesitó (sacarlo sería rearmarla: un parpadeo).
// - Una capa cuya entrada no da video no se ve (el pad no tiene datos): la placa
//   «sin señal» la pone la entrada (M5).
// - CON OTRO MEZCLADOR (Opciones::mezclador, la «Mezcla de GStreamer» del grafo de
//   video): cualquier módulo de GStreamer de varias entradas (compositor,
//   d3d12compositor, glvideomixer, glmosaic, smpte, shapewipe, glstereomix…) en vez
//   del de siempre. Cada capa va a SU pad (Capa::pad: «sink_1», «video_sink»…; a
//   pedido o fijo) con las propiedades de ese pad tal cual (xpos, alpha, zorder…);
//   lo de la escena (posición, recorte, sombra, transiciones) no se usa. Cada rama
//   le lleva la imagen en la memoria que el módulo acepta (la del motor si puede: no
//   baja de la placa) y, si sus pads no ubican la imagen, al tamaño del lienzo (smpte
//   y shapewipe piden las dos iguales). Lo que sale vuelve a la memoria del motor.
// ============================================================================
#pragma once

#include "Edad.hpp"
#include "Escena.hpp"

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

class Composicion {
   public:
    /// Qué hacer con una capa de la escena que entra (Opciones::esperarEntrada):
    ///   Normal     lista cuando pasó su primer cuadro;
    ///   Esperar    todavía no, aunque pase cuadros (la placa mientras llega su señal; una
    ///              escena interna incompleta);
    ///   NoEsperar  no va a tener cuadro nunca (una entrada sin video: un micrófono).
    enum class Espera { Normal, Esperar, NoEsperar };
    struct Opciones {
        bool gpu = false; ///< glvideomixer (en la placa) en vez de compositor (CPU); también si gpuVideo()
        /// DSK y clean feed: la escena sale también limpia (`video:<nombre>:limpio`) y
        /// un segundo compositor le pone encima las capas del DSK (zócalos, logo), que
        /// no cambian con la escena.
        bool dsk = false;
        /// Filtros después del mezclador (ver Escena::filtros); no con `dsk`.
        nlohmann::json filtros = nlohmann::json::array();
        /// Otro mezclador (ver Escena::mezclador); no con `dsk`.
        nlohmann::json mezclador = nlohmann::json::object();
        /// ¿Esa entrada todavía está llegando? (recién armada o despertada, en la placa
        /// esperando su primera señal: Entrada::esperandoSenal). Una capa así no cuenta como
        /// lista para sacar al aire la escena que entra aunque ya pase cuadros (los de la
        /// placa «sin señal»). Con `productor` de otra composición (una escena interna,
        /// «video:escena:<n>»), si esa todavía no está completa (completa()). La llama
        /// tic, en el hilo del motor (lo pone el Motor).
        /// Una entrada sin video (NoEsperar) no frena el corte.
        std::function<Espera(const std::string& entrada, const std::string& productor)> esperarEntrada;
        /// Las de una escena interna: su lienzo, sus filtros y su mezclador.
        static Opciones de(const Escena& e) {
            Opciones op;
            op.filtros = e.filtros;
            op.mezclador = e.mezclador;
            return op;
        }
    };
    Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre, Opciones op);
    Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre)
        : Composicion(reloj, lienzo, std::move(nombre), Opciones{}) {}
    ~Composicion();
    Composicion(const Composicion&) = delete;
    Composicion& operator=(const Composicion&) = delete;

    const std::string& nombre() const { return nombre_; }
    const Lienzo& lienzo() const { return lienzo_; }
    /// Los filtros de después del mezclador, andando: false si cambió algo que no se
    /// cambia en vivo (otros elementos o propiedades fijas): hay que rearmarla.
    bool ponerFiltros(const nlohmann::json& filtros);
    /// Las propiedades del otro mezclador, andando: false si es otro módulo (o cambió
    /// una que no se cambia en vivo): hay que rearmarla.
    bool ponerMezclador(const nlohmann::json& mezclador);
    std::string productor() const { return "video:" + nombre_; }
    /// Las entradas que tiene ahora (capas, las que salen en una transición y el DSK).
    std::set<std::string> entradasEnUso() const;
    /// Lo que leen sus capas (para saber qué composiciones internas están en uso).
    std::set<std::string> productoresEnUso() const;
    bool anda() const { return pipeline_ != nullptr; }
    const std::string& error() const { return error_; }

    /// Pone una escena: transición «corte» o «mezcla» con `ms`. Misma escena: actualiza en el lugar.
    void mostrar(const Escena& e, const std::string& transicion = "corte", int ms = 0);
    const std::string& escena() const { return escena_.nombre; }
    /// ¿Hay una transición en curso?
    bool enTransicion() const;
    /// ¿La escena que entra se está armando fuera del aire (todavía se ve la anterior
    /// entera)? Pasa a false en el tic en que corta o arranca su transición.
    bool armando() const;
    /// ¿Lo que muestra está completo (ninguna capa de su escena esperando su primer cuadro
    /// o su señal, ni una escena armándose)? Para la que la usa como capa (una escena
    /// interna): sus capas en reposo se esperan al sacarla al aire. Si su candado está
    /// tomado (o una escena se contiene a sí misma), dice que no: se espera un tic más.
    bool completa() const;
    /// T-bar: lleva a mano una mezcla hacia `e` (t de 0 a 1). Con `soltar` termina:
    /// pasada la mitad queda la nueva; si no, vuelve la de antes. Devuelve true si
    /// la nueva quedó.
    bool manual(const Escena& e, double t, bool soltar);
    /// Hacia qué escena va la T-bar ("" = ninguna).
    std::string manualHacia() const;

    /// Las capas del DSK (encima de cualquier escena): las nuevas entran con un
    /// fundido de `ms`, las que no están más salen igual. false si este bus no tiene DSK.
    bool ponerDsk(const std::vector<Capa>& capas, int ms = 0);
    std::string productorLimpio() const { return "video:" + nombre_ + ":limpio"; }

    void tic();
    nlohmann::json estado() const;
    /// Su pipeline con una referencia (para el análisis por elemento); nullptr si no anda.
    GstElement* pipelineRef() const;
    /// Cuánto después de su hora sale lo que entrega (ns).
    std::int64_t latenciaSalidaNs() const;

   private:
    struct Rama {
        std::string entrada, productor;
        std::string clave; ///< la de su capa (Capa::claveCapa): una entrada puede estar dos veces
        GstElement* src = nullptr;
        GstElement* recorte = nullptr; ///< videocrop (CPU), gltransformation (GL) o d3d12convert (D3D12: aplica la meta de recorte)
        GstElement* subir = nullptr;   ///< glupload / d3d12upload (gpu)
        GstElement* conv = nullptr;    ///< glcolorconvert (gl)
        GstElement* fijar = nullptr;   ///< glshader (gl: obliga a gltransformation a dibujar, ver crearRama)
        GstElement* sombra = nullptr;  ///< basombra / basombragl / basombrad3d12 (si la capa tiene sombra o borde)
        GstElement* aRgba = nullptr;   ///< videoconvert antes de basombra (CPU)
        GstElement* puente = nullptr;  ///< con otro mezclador: lo que le lleva la imagen a su memoria (un bin)
        bool padPedido = true;         ///< el pad del mezclador es a pedido (se suelta); si no, es fijo
        int margen = 0;                ///< lo que se agrandó la caja de cada lado (la sombra)
        // D3D12: la ventana que se marca en cada cuadro (meta de recorte, en píxeles de la
        // fuente) y el tamaño de la fuente (lo escribe el hilo de datos).
        std::atomic<int> recIzq{0}, recArr{0}, recDer{0}, recAba{0}, fuenteW{0}, fuenteH{0};
        GstPad* pad = nullptr;
        int generacion = 0;
        std::atomic<bool> listo{false}; ///< ya pasó su primer cuadro (lo pone el hilo de datos)
        /// La edad con que llegan sus cuadros a la composición (a la salida del intersrc): lo que
        /// esperaron desde que salieron de la entrada es el colchón de esa entrada.
        MedidorEdad llega;
        /// GL: llegó el tamaño de la fuente (al negociar, o porque cambió): tic() vuelve a
        /// calcular el recorte, que va en fracciones de ese tamaño. Lo pone el hilo de datos.
        std::atomic<bool> tamanoNuevo{false};
        double alfaHacia = 1.0;
        int orden = 0;
        GstElement* mx = nullptr;   ///< el compositor donde está (el de la escena o el del DSK)
        Capa capa;                  ///< cómo tiene que quedar (para las transiciones)
        // Fundido propio (las del DSK)
        double alfaDesde = 0, alfa = 0;
        std::int64_t fundeDesdeNs = 0, fundeNs = 0;
        bool saliendo = false;
    };
    void aplicarLatencia();
    void ponerCapa(Rama& r, const Capa& c, double alfa);
    /// Los números de la sombra a su elemento, con la caja (en el lienzo) donde se ve la imagen.
    static void ponerSombra(Rama& r, const SombraCapa& s, int margen, int cajaAncho, int cajaAlto, bool contener);
    /// GPU: el recorte (y lo que saca la cortina por la derecha) con gltransformation (GL)
    /// o con la meta de recorte que aplica d3d12convert (D3D12), y la caja en el lienzo
    /// (con «contener» y recorte, la calcula a mano).
    /// false si hay recorte pero todavía no se sabe el tamaño de la fuente (la rama no
    /// negoció): cuando llegue, tic() lo pone (Rama::tamanoNuevo).
    bool recorteGl(Rama& r, const Capa& c, int extraDer, int ancho, int alto);
    /// D3D12: marca en cada cuadro la ventana de la rama (y anota el tamaño de la fuente).
    static GstPadProbeReturn marcarRecorte(GstPad* pad, GstPadProbeInfo* info, gpointer d);
    /// GL: avisa (Rama::tamanoNuevo) cada vez que la fuente negocia un tamaño.
    static GstPadProbeReturn avisarTamano(GstPad* pad, GstPadProbeInfo* info, gpointer d);
    Rama* crearRama(const Capa& c, int gen, GstElement* mx = nullptr);
    /// Con otro mezclador: intersrc → puente a su memoria → SU pad (Capa::pad).
    Rama* crearRamaLibre(const Capa& c, int gen);
    /// Las propiedades de su pad, tal cual (con otro mezclador).
    static void ponerPropiedadesPad(GstPad* pad, const nlohmann::json& props);
    /// Antes de una escena nueva: las capas de la actual bajan a su orden (las nuevas van en 5000 + orden).
    void bajarViejas();
    void soltar(Rama& r);
    /// Antes de soltar el último pad del mezclador de la escena, el ancla (ver ancla_).
    void anclar();
    /// Llegó una capa: el ancla sobra (el mezclador ya tiene otro pad).
    void soltarAncla();
    void sacarGeneracion(int gen);
    /// Termina la transición en curso: lo nuevo como tiene que quedar y lo viejo, afuera.
    void terminarTransicion();
    /// Las capas de la escena que entra que todavía no dieron su primer cuadro.
    std::vector<std::string> nuevasSinCuadro() const;
    /// La transición en curso en el punto `t` (0…1).
    void animar(double t);
    /// Una capa animada por claves, en el punto de ahora (si se movió desde la última vez).
    void animarRama(Rama& r);
    /// La capa en el punto de ahora de su animación (sin claves, la misma).
    Capa capaAhora(const Capa& c) const;
    /// El ancho de lo que llega a una rama (antes del recorte); 0 si no se sabe aún.
    static int anchoDeOrigen(const Rama& r);
    static GstPadProbeReturn alPrimerCuadro(GstPad* pad, GstPadProbeInfo* info, gpointer d);

    Reloj& reloj_;
    MedidorEdad edadSalida_; ///< la edad de lo que sale (medida)
    Lienzo lienzo_;
    std::string firmaFiltros_; ///< los filtros de después del mezclador (Piezas::firmaDe)
    std::vector<std::string> avisosFiltros_;
    std::string nombre_, error_;
    bool gpu_ = false; ///< en la GPU (GL o D3D12)
    bool gl_ = false;  ///< en GL (si no, D3D12)
    /// Con otro mezclador (Opciones::mezclador), y su firma (Piezas::firmaDe).
    bool libre_ = false;
    std::string firmaMezclador_;
    /// Dónde se le pone la latencia del estudio al mezclador: «min-upstream-latency» (los
    /// agregadores), «latency» (glvideomixer, que es un bin) o "" (smpte, shapewipe).
    std::string propLatencia_ = "min-upstream-latency";
    bool agregador_ = true; ///< respeta la hora de lo que entra (los agregadores; smpte no)
    /// Un mezclador que no es un agregador (smpte) cuenta su propio tiempo desde 0: lo que
    /// sale lleva la hora de lo último que le entró (la anotan las sondas de sus pads).
    std::atomic<std::int64_t> ptsEntrada_{-1};
    static GstPadProbeReturn anotarHora(GstPad* pad, GstPadProbeInfo* info, gpointer d);
    static GstPadProbeReturn ponerHora(GstPad* pad, GstPadProbeInfo* info, gpointer d);
    GstElement* pipeline_ = nullptr;
    GstElement* mezclador_ = nullptr;
    /// EL ANCLA: un pad del mezclador sin nada enchufado, mientras la escena no tiene capas.
    /// Un agregador de video (compositor, glvideomixer, d3d12compositor) que se queda sin
    /// pads se REINICIA (GstVideoAggregator: la posición a -1, la cuenta de cuadros a 0) y
    /// vuelve a sacar cuadros desde su start-time, la hora en que arrancó: una ráfaga de
    /// cuadros vencidos para ponerse al día y, abajo, el DSK no daba más nada (el programa
    /// congelado al cortar a una escena vacía; también al cambiarle la fuente a la única capa).
    /// Con el ancla nunca se queda sin pads; uno sin datos no lo frena (en vivo, a la hora
    /// del cuadro sale lo que hay: el fondo, como sin pads).
    GstPad* ancla_ = nullptr;
    GstElement* dskMx_ = nullptr;          ///< el compositor del DSK (nullptr sin DSK)
    std::map<std::string, Rama*> dsk_;     ///< las capas del DSK por nombre
    mutable std::mutex mu_;
    unsigned serieRamas_ = 0; ///< sufijo de los nombres de elementos (únicos aunque dos claves se limpien igual)
    std::map<std::string, Rama*> ramas_; ///< clave: <generación>/<clave de la capa>
    int generacion_ = 0;
    Escena escena_;
    // La transición en curso
    int genVieja_ = -1;
    std::string transicion_;
    std::int64_t transDesdeNs_ = 0, transNs_ = 0;
    /// La escena que entra se está armando fuera del aire (sus capas con alfa 0): la que
    /// sale se sigue viendo entera hasta que todas tengan cuadro o pase el tope.
    bool esperando_ = false;
    std::int64_t esperaDesdeNs_ = 0;
    std::function<Espera(const std::string&, const std::string&)> esperarEntrada_; ///< Opciones::esperarEntrada
    bool manual_ = false;     ///< la T-bar: la transición no avanza sola
    double manualT_ = 0;
    Escena escenaAntes_;      ///< la que estaba (para volver si se suelta antes de la mitad)
    std::int64_t latenciaNs_ = -1, salidaNs_ = 0, consultaNs_ = 0;
    /// Cuánto después de su hora sale un cuadro del mezclador (la edad de la salida, p50):
    /// lo que se le corre el reloj a las capas animadas por claves.
    std::int64_t retrasoClavesNs_ = 0;
    LatenciaAparte latencia_;
};

} // namespace ba::motor
