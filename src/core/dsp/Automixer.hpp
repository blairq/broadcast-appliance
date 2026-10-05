// Automixer.hpp
//
// Proposito:
//   Automixer de mesa redonda (spec 7.3 "Mesa redonda", estilo Dugan). Es un
//   objeto COMPARTIDO (no un filtro por canal): recibe el nivel lineal de cada
//   microfono y devuelve la ganancia dinamica que cada canal debe aplicar, de
//   modo que el que esta hablando pase al frente y el resto se atenuen. La
//   ganancia de cada canal es su energia suavizada dividida por la suma de las
//   energias suavizadas de TODOS los canales (normalizacion Dugan), con:
//
//     * un piso en el denominador (todos en silencio -> ganancias todas cero,
//       nunca NaN);
//     * un piso de ganancia minima para el "ultimo micro abierto", sostenido
//       durante una ventana de hold despues de que el canal cae (para que no
//       se corte de golpe al terminar una frase);
//     * una atenuacion por cantidad de micros abiertos, para que la salida
//       SUMADA se mantenga acotada (mas micros abiertos -> a cada uno le toca
//       menos).
//
// Modelo de threading:
//   Es una tabla COMPARTIDA de niveles: `setLevel()` la escribe el thread de
//   audio (un solo escritor) y `gainFor()` la lee el thread de control/UI (o
//   otro thread de audio) en paralelo. Por eso los niveles suavizados viven en
//   `std::atomic<float>`: la lectura es lock-free y no hay data race. No hay
//   asignacion, no hay locks bloqueantes ni I/O en `setLevel()`/`gainFor()`:
//   todo el estado vive en arrays de tamano fijo (capacidad fija en la
//   construccion, sin crecimiento de heap despues). `setParams()` se llama
//   desde el thread de control y suelta los cambios suavizados (one-pole) para
//   evitar clicks, igual que `ChannelStrip::setParams()`.
//
// Estrategia de errores:
//   No lanza. `setLevel()`/`gainFor()` con `ch` fuera de [0, maxChannels) es un
//   no-op (devuelve 0 en gainFor). Los parámetros se claman a rangos seguros en
//   `setParams()` (fs>0, tiempos >=0, umbral >=0, maxOpen >=1, targetSummed >0)
//   para que la mesa quede siempre estable y sin NaN. `gainFor()` devuelve
//   siempre un valor finito en [0, 1] (con piso de denominador y atenuacion
//   acotada).
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace ba::dsp {

/// Parámetros del automixer de mesa redonda (spec 7.3). Valores por defecto =
/// mesa "neutra" con tiempos de broadcast razonables. Todos los tiempos en ms.
struct AutomixerParams {
    // --- Muestreo ---
    double fs = 48000.0; // frecuencia de muestreo (para los one-poles)

    // --- Deteccion de "micro abierto" ---
    // Un canal cuenta como "abierto" cuando su energia suavizada supera este
    // umbral (lineal). -40 dBFS ~= 0.01 en lineal: por debajo se considera
    // ruido de fondo / silencio.
    double openThresholdDb = -40.0;

    // --- Suavizado de la energia (one-pole, ataque rapido / liberacion lenta)
    // Mismo estilo de envolvente que ChannelStrip: ataque corto para seguir el
    // habla, liberacion larga para no "parpadear" entre silabas.
    double attackMs = 5.0;
    double releaseMs = 200.0;

    // --- Hold del "ultimo micro abierto" ---
    // Despues de que el canal cae bajo el umbral, se le sostiene un piso de
    // ganancia minima durante esta ventana (ms) para no cortarlo de golpe al
    // terminar una frase. 0 = sin hold.
    double holdMs = 300.0;
    // Piso de ganancia minima que se sostiene durante el hold (lineal, 0..1).
    // -20 dBFS ~= 0.1: el micro que acaba de hablar queda audible pero atenuado.
    double holdFloorDb = -20.0;

    // --- Atenuacion por cantidad de micros abiertos ---
    // La salida SUMADA se acota: a mas micros abiertos, a cada uno le toca
    // menos. `targetSummedGain` es la ganancia SUMADA objetivo cuando hay
    // `maxOpen` micros abiertos; con menos, la atenuacion es proporcional.
    int maxOpenMics = 4;
    double targetSummedGain = 1.0;
};

/// Automixer de mesa redonda (spec 7.3, estilo Dugan). Objeto COMPARTIDO de
/// niveles: `setLevel()` (audio, un escritor) y `gainFor()` (control/UI,
/// lectores) en paralelo, lock-free gracias a `std::atomic<float>`. RT-safe:
/// sin asignacion, sin locks bloqueantes ni I/O en el camino de audio.
class Automixer {
   public:
    /// Construye la mesa con capacidad FIJA de `maxChannels` canales (sin
    /// crecimiento de heap despues). `maxChannels <= 0` se toma como 1. Estado
    /// inicial: todos los canales en silencio, sin hold activo.
    ///
    /// :param maxChannels: capacidad maxima de canales (por defecto 8).
    explicit Automixer(int maxChannels = 8);

    /// Reporta el nivel lineal (0..1, se toma el valor absoluto) del canal `ch`
    /// desde el thread de AUDIO. Actualiza la energia suavizada de ese canal
    /// con un one-pole de ataque rapido / liberacion lenta (mismo estilo de
    /// envolvente que ChannelStrip). `ch` fuera de [0, maxChannels) es no-op.
    /// No asigna, no bloquea, no hace I/O.
    ///
    /// :param ch: indice de canal en [0, maxChannels).
    /// :param level: nivel lineal del canal (se usa |level|).
    void setLevel(int ch, float level);

    /// Devuelve la ganancia dinamica (lineal, en [0, 1]) que debe aplicar el
    /// canal `ch` AHORA. Es `energia[ch] / sum(energia[*])` (Dugan) con piso
    /// de denominador (todos en silencio -> 0, nunca NaN), piso de ganancia
    /// minima del "ultimo micro abierto" sostenido durante el hold, y
    /// atenuacion por cantidad de micros abiertos (mas abiertos -> a cada uno
    /// le toca menos, salida sumada acotada). `ch` fuera de rango devuelve 0.
    /// Lock-free: lee los atomicos sin bloquear. No asigna, no hace I/O.
    ///
    /// :param ch: indice de canal en [0, maxChannels).
    /// :returns: ganancia lineal en [0, 1], siempre finita.
    float gainFor(int ch) const;

    /// Como `setLevel()` pero para un BLOQUE de `frames` muestras con nivel
    /// `rms`: la envolvente avanza `frames` muestras de una vez y el hold se
    /// cuenta en muestras PROPIAS del canal. Es lo que usa un canal que corre
    /// en su propio hilo (p. ej. el elemento de GStreamer `baautomixer`, un
    /// pipeline por micrófono): varios escritores, uno por canal, sin pisarse
    /// el tiempo (el `tick_` global cuenta llamadas de todos).
    void setLevelBlock(int ch, float rms, int frames);

    /// Limpia todo el estado dinamico: energias suavizadas a cero y hold
    /// inactivo. No toca los parámetros actuales.
    void reset();

    /// Recalcula los coeficientes one-pole a partir de `p` y arma el plan de
    /// suavizado de la atenuacion: los cambios de atenuacion (por cantidad de
    /// micros abiertos) se interpolan con un one-pole a lo largo de `smoothMs`
    /// para que un cambio de preset no produzca clicks. `setParams()` se llama
    /// desde el thread de control, no en paralelo con `setLevel()`.
    ///
    /// :param p: parámetros objetivo a aplicar.
    /// :param smoothMs: duracion del fade de atenuacion en ms (0 = inmediato;
    ///                  <0 se toma como 0).
    void setParams(const AutomixerParams& p, double smoothMs = 5.0);

    /// Cantidad de canales con capacidad (para tests/monitoreo).
    int maxChannels() const;

   private:
    // --- Capacidad fija (sin heap despues de la construccion) ---
    // Solo se escribe en el constructor, antes de que exista cualquier otro
    // thread, por eso es el unico escalar compartido que no necesita ser
    // atomico.
    int maxChannels_ = 8;

    // --- Parametros clamped (valores seguros) ---
    //
    // setParams() corre en el thread de CONTROL y setLevel()/gainFor() en el de
    // AUDIO, asi que todo parametro que el camino de audio lea tiene que ser
    // atomico: un `double` plano escrito por un thread y leido por otro es una
    // carrera de datos (UB), no solo un valor viejo. Se usa memory_order_relaxed
    // porque no hay invariante ENTRE parametros que proteger: cada uno se aplica
    // por su cuenta y un cambio de preset puede quedar a medias durante una
    // muestra sin consecuencia audible (el suavizado one-pole lo absorbe).
    std::atomic<double> fs_{48000.0};
    std::atomic<double> openThresholdDb_{-40.0};
    std::atomic<double> holdMs_{300.0};
    std::atomic<double> holdFloorDb_{-20.0};
    std::atomic<int> maxOpenMics_{4};
    std::atomic<double> targetSummedGain_{1.0};

    // Solo los toca setParams() (control); el camino de audio usa los
    // coeficientes ya calculados, no estos. No hace falta que sean atomicos.
    double attackMs_ = 5.0;
    double releaseMs_ = 200.0;

    // --- Coeficientes one-pole (0..1) precalculados en setParams() ---
    std::atomic<float> atk_{1.0f};     // ataque de la envolvente (rapido)
    std::atomic<float> rel_{0.0f};     // liberacion de la envolvente (lenta)
    std::atomic<float> attFade_{1.0f}; // fade de la atenuacion (smoothMs)

    // --- Estado dinamico COMPARTIDO (lock-free) ---
    // Energia suavizada por canal (lineal). std::atomic<float> para que
    // gainFor() (lectores) y setLevel() (escritor) no tengan data race.
    // Tamano fijo: se reserva en la construccion.
    // unique_ptr, no `new[]` crudo: la clase no tenia destructor y ambos arrays
    // se filtraban en cada instancia. Ademas deja la clase move-only y borra la
    // copia, que con punteros crudos habria compartido el estado en silencio.
    std::unique_ptr<std::atomic<float>[]> env_;

    // Estado de hold por canal: momento (en "ticks" de setLevel) en que el
    // canal dejo de estar abierto. -1 = nunca / no en hold.
    std::unique_ptr<std::atomic<long long>[]> holdUntil_;

    // Tick global: cuenta las llamadas a setLevel() (proxy de tiempo en el
    // dominio del thread de audio). Se usa para el hold sin reloj de sistema.
    std::atomic<long long> tick_ = 0;

    // Hold de `setLevelBlock()`: muestras que le quedan a cada canal (solo lo
    // escribe el hilo de ese canal; gainFor lo lee).
    std::unique_ptr<std::atomic<long long>[]> holdLeft_;

    // --- Atenuacion por cantidad de micros abiertos (suavizada) ---
    // Valor actual (lineal) y objetivo. Se interpola con attFade_ (one-pole
    // sobre smoothMs) para que un cambio de preset no produzca clicks.
    // Atomico: lo escribe setLevel() (audio) y lo lee gainFor() (control/UI).
    std::atomic<float> attenuation_{1.0f};
    // Solo lo usa setLevel() como variable de paso; no cruza threads.
    float attenuationTarget_ = 1.0f;

    // --- Utilidades internas ---
    // dB -> lineal.
    static double dbToLin(double db);
    // Coeficiente one-pole (0..1) para un tiempo en ms a una fs dada.
    static float onePole(double ms, double fs);
    // Atenuacion objetivo para una cantidad de micros abiertos: acota la
    // salida sumada (mas abiertos -> a cada uno le toca menos).
    float attenuationForOpenCount(int openCount) const;
};

} // namespace ba::dsp
