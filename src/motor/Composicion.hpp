// ============================================================================
// src/motor/Composicion.hpp — un bus de video del motor (PROGRAMA, PREVIEW…)
//
// Un pipeline propio por bus:
//
//   intersrc <id>:video ! queue ! videocrop ─► compositor (pad: x, y, ancho, alto, alfa, orden)
//   intersrc <id>:video ! queue ! videocrop ─►     │
//   …                                              ▼
//                         caps del lienzo ! intersink video:<nombre>
//
// - Reloj y base_time del estudio; en vivo aunque no haya capas (force-live),
//   arranca con la primera que llega (start-time-selection=first) y espera la
//   latencia del estudio + margen (min-upstream-latency): una entrada lenta que
//   se enchufa andando no llega tarde.
// - ESCENA = un conjunto de capas (qué entrada, dónde, de qué tamaño, recorte,
//   alfa, orden). `mostrar` pone una escena con una TRANSICIÓN:
//     corte   la nueva aparece entera; la vieja se saca recién cuando la nueva
//             ya dio su primer cuadro (nada de un cuadro negro en el medio).
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

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

/// Sombra (o resplandor) y borde difuso de una capa, en píxeles del lienzo (ver basombra).
struct SombraCapa {
    double alfa = 0;            ///< opacidad de la sombra (0 = sin sombra)
    std::uint32_t color = 0xFF000000; ///< ARGB
    double difuminado = 20, distancia = 10, angulo = 45, tamano = 0;
    double borde = 0;           ///< borde difuso de la imagen (aparte de la sombra)
    double radio = 0;           ///< esquinas redondeadas
    bool hace() const { return alfa > 0 || borde > 0 || radio > 0; }
    /// Lo que se agranda la caja de cada lado para que la sombra entre (de a 8: que
    /// mover un deslizador no renegocie en cada paso).
    int margen() const;
    bool operator==(const SombraCapa&) const = default;
    static SombraCapa desdeJson(const nlohmann::json& j);
    nlohmann::json aJson() const;
};

/// Un punto de la animación de una capa (Capa::claves): a los `ms` la capa está ahí y así.
/// Entre dos claves, lo de en medio (con `suave`, arrancando y frenando despacio).
struct ClaveCapa {
    int ms = 0;
    double x = 0, y = 0, ancho = 0, alto = 0; ///< en el lienzo (ancho/alto 0 = el del lienzo)
    double alfa = 1.0;
    double recIzq = 0, recArr = 0, recDer = 0, recAba = 0; ///< recorte, en píxeles de la entrada
    bool suave = true; ///< el tramo que LLEGA a esta clave
    bool operator==(const ClaveCapa&) const = default;
};

struct Capa {
    std::string entrada;           ///< la entrada del motor (lee <entrada>:video); también el nombre de la capa
    int x = 0, y = 0;
    int ancho = 0, alto = 0;       ///< 0 = el del lienzo
    double alfa = 1.0;
    int orden = 0;                 ///< más alto = más arriba
    int recIzq = 0, recArr = 0, recDer = 0, recAba = 0; ///< recorte en píxeles de la entrada
    bool visible = true;
    /// true: la imagen entra en su recuadro sin deformarse, centrada (como hacía OBS);
    /// false: se estira al recuadro.
    bool contener = false;
    std::string productor;         ///< si no está vacío, lee este productor (p. ej. «video:programa» en el multiview)
    SombraCapa sombra;
    /// Cómo se la reconoce en la escena (su rama y sus elementos): la entrada; si la
    /// misma entrada está más de una vez (dos bancas de una cámara recortada distinto),
    /// «entrada#2», «entrada#3»... (Escena::desdeJson). Vacía = la entrada.
    std::string clave;
    const std::string& claveCapa() const { return clave.empty() ? entrada : clave; }
    /// Con otro mezclador (Escena::mezclador): el pad al que va («sink_1», «video_sink»;
    /// vacío = uno nuevo a pedido) y sus propiedades ({"xpos": 10, "alpha": 0.5}).
    std::string pad;
    nlohmann::json propiedadesPad = nlohmann::json::object();
    /// ANIMADA POR CLAVES: la capa va de clave en clave (ordenadas por ms) con el reloj del
    /// motor, y pasada la última queda ahí. `clavesDesdeNs` es el ms 0, en el reloj monótono
    /// (g_get_monotonic_time en ns: el de la app y el del motor son el mismo); 0 = todavía no
    /// arrancó (el gráfico no dio su primer cuadro): queda en la primera clave.
    std::vector<ClaveCapa> claves;
    std::int64_t clavesDesdeNs = 0;
};

/// La capa a los `ms` de su animación (Capa::claves): dónde, de qué tamaño, con qué alfa y
/// recorte; lo demás, como está. Sin claves, la misma.
Capa capaEn(const Capa& c, double ms);

struct Escena {
    std::string nombre;
    std::vector<Capa> capas;
    /// Solo las escenas internas (un compositor, la Cámara 360): un lienzo propio
    /// (0 = el del estudio) y filtros después del mezclador (la lista de Piezas:
    /// [{id, elemento, propiedades}]; en la GPU, con su par de GL o D3D12).
    int lienzoAncho = 0, lienzoAlto = 0;
    nlohmann::json filtros = nlohmann::json::array();
    /// Solo las internas: otro módulo de varias entradas en vez del mezclador de siempre
    /// ({"elemento", "propiedades"}; vacío = el de siempre). Ver Capa::pad.
    nlohmann::json mezclador = nlohmann::json::object();
    static Escena desdeJson(const nlohmann::json& j);
    /// Una clave distinta para cada capa (Capa::clave): la misma entrada dos veces, o una
    /// entrada que se llama justo como la clave de otra («cam#2»). desdeJson ya la llama;
    /// quien arma una Escena a mano, también.
    void asignarClaves();
    nlohmann::json aJson() const;
};

class Composicion {
   public:
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
    void sacarGeneracion(int gen);
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
