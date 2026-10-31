// ChannelStrip.hpp
//
// Proposito:
//   Channel strip nativo de audio (spec 7.2): una cadena de procesamiento por
//   entrada de microfono, RT-safe. Hoy solo la usan OperatorEngine (el autotest
//   --headless-selftest) y su test: los micrófonos del programa se procesan en
//   los racks (audio/AudioRack, barack). Cadena fija:
//
//     HPF (12/18/24 dB/oct, cascada de high-pass)
//       -> EQ parametrico de 4 bandas (biquads RBJ)
//       -> de-esser (compresion de banda partida, sidechain 5-9 kHz)
//       -> expander/gate (con histéresis y hold)
//       -> compresor (peak, knee suave)
//       -> limitador (con lookahead)
//
//   Todo el estado vive en miembros fijos (arrays de biquads, buffers de
//   lookahead de tamano fijo): NO hay asignacion, locks ni I/O en el camino de
//   `process()`. `setParams()` (cambio de preset) se llama desde el thread de
//   control y suelta los cambios de ganancia suavizados a lo largo de
//   `smoothMs` (one-pole) para evitar clicks.
//
// Modelo de threading:
//   Un `ChannelStrip` pertenece a UN canal de audio y lo procesa un solo
//   thread (el de audio). `process()` muta estado interno y NO es
//   thread-safe. `setParams()` se llama desde el thread de control; el
//   contrato es que no se solape con `process()` del mismo strip. El
//   suavizado de ganancia (no locks) es lo que evita clicks al cambiar de
//   preset en caliente.
//
// Estrategia de errores:
//   No lanza. `process()` con `interleaved==nullptr` o `n<=0` no hace nada.
//   `n` es el numero de FRAMES (no muestras); el buffer es estéreo
//   interleaved (2*n floats). Los parámetros se claman a rangos seguros en
//   `setParams()` (fs>0, frecuencias >0, ratios >=1, tiempos >=0) para que la
//   cadena quede siempre estable y sin NaN. `lastGainDb()` devuelve la última
//   ganancia dinámica aplicada (gate+comp+lim) en dB, para tests.
#pragma once

#include "Biquad.hpp"

namespace ba::dsp {

/// Parámetros de un channel strip (spec 7.2/7.3). Valores por defecto =
/// cadena "neutra" (solo comp y lim activos, como en el spec).
struct StripParams {
    // --- HPF ---
    double hpfFreq = 100.0;
    double hpfSlopeDbOct = 18.0; // 12/18/24 -> 1/2/3 biquads high-pass en cascada

    // --- EQ parametrico de 4 bandas ---
    struct Band {
        BiquadType type = BiquadType::Peaking;
        double freq = 1000.0;
        double gainDb = 0.0;
        double q = 1.0;
        bool enabled = false;
    };
    Band eq[4];

    // --- De-esser (banda partida, sidechain) ---
    bool deEsserEnabled = false;
    double deEsserFreq = 6500.0; // centro de la banda de sidechain (5-9 kHz)
    double deEsserRatio = 4.0;
    double deEsserThresholdDb = -30.0;

    // --- Expander / gate ---
    bool gateEnabled = false;
    double gateThresholdDb = -45.0;
    double gateRangeDb = 12.0; // atenuacion maxima cuando esta cerrado
    double gateAttackMs = 1.0;
    double gateReleaseMs = 100.0;

    // --- Compresor (peak, knee suave) ---
    bool compEnabled = true;
    double compRatio = 2.5;
    double compThresholdDb = -20.0;
    double compAttackMs = 10.0;
    double compReleaseMs = 150.0;
    double compKneeDb = 6.0;

    // --- Limitador (lookahead) ---
    bool limEnabled = true;
    double limCeilingDb = -1.0;
    int limLookaheadSamples = 64;

    // --- Muestreo ---
    double fs = 48000.0;
};

/// Channel strip nativo (spec 7.2). Cadena fija HPF -> EQ4 -> de-esser ->
/// gate -> comp -> lim, RT-safe (sin asignacion/locks/I/O en process()).
class ChannelStrip {
   public:
    /// Construye el strip con la cadena de biquads en memoria fija (sin heap
    /// en el camino de process). Estado inicial neutro: HPF 100 Hz 18 dB/oct,
    /// EQ desactivado, de-esser/gate desactivados, comp y lim activos.
    ChannelStrip();

    /// Recalcula los coeficientes de toda la cadena a partir de `p` y arma el
    /// plan de suavizado: los cambios de ganancia (EQ, de-esser, gate, comp,
    /// lim) se interpolan con un one-pole a lo largo de `smoothMs` para que un
    /// cambio de preset no produzca clicks. `setParams()` se llama desde el
    /// thread de control, no en paralelo con `process()`.
    ///
    /// :param p: parámetros objetivo a aplicar.
    /// :param smoothMs: duracion del fade de ganancia en ms (0 = cambio
    ///                  inmediato; <0 se toma como 0).
    void setParams(const StripParams& p, double smoothMs = 5.0);

    /// Procesa `n` FRAMES de audio estéreo interleaved (2*n floats) in-place.
    /// `in==out` esta permitido. No asigna, no bloquea, no hace I/O.
    ///
    /// :param interleaved: puntero a 2*n floats (L,R,L,R,...); se modifica in-place.
    /// :param n: numero de FRAMES (no de muestras). <=0 o nullptr no hace nada.
    void process(float* interleaved, int n);

    /// Limpia todo el estado interno (biquads, envolventes, buffers de
    /// lookahead). No toca los coeficientes actuales.
    void reset();

    /// Última ganancia dinámica aplicada (gate + comp + lim, en dB) sobre la
    /// última muestra procesada. Solo para tests/monitoreo.
    float lastGainDb() const;

   private:
    // --- HPF: hasta 3 high-pass en cascada (12/18/24 dB/oct) ---
    // UN BIQUAD POR CANAL. Un biquad tiene estado interno (z1/z2): pasar L y R
    // por la MISMA instancia hace que el filtro vea un flujo L,R,L,R y mezcle
    // el estado de los dos canales, con lo que la respuesta deja de ser la
    // pedida y los canales salen distintos entre si. El de-esser ya lo hacia
    // bien (deEsserBpL_/deEsserBpR_); el HPF y el EQ no.
    Biquad hpfL_[3];
    Biquad hpfR_[3];
    int hpfStages_ = 2;

    // --- EQ parametrico: 4 biquads fijos ---
    Biquad eqL_[4];
    Biquad eqR_[4];
    bool eqEnabled_[4] = {false, false, false, false};

    // --- De-esser: band-pass de sidechain POR CANAL + ganancia de compresion ---
    // Dos biquads (L y R) porque el biquad tiene estado interno y no puede
    // compartirse entre canales.
    Biquad deEsserBpL_;
    Biquad deEsserBpR_;
    bool deEsserEnabled_ = false;
    double deEsserRatio_ = 4.0;
    double deEsserThresholdDb_ = -30.0;
    float deEsserEnv_ = 0.0f;        // envolvente (peak) del sidechain (comun L/R)
    float deEsserGain_ = 1.0f;       // ganancia de compresion actual (lineal)
    float deEsserGainTarget_ = 1.0f; // objetivo (para suavizado)

    // --- Expander / gate ---
    bool gateEnabled_ = false;
    double gateThresholdDb_ = -45.0;
    double gateRangeDb_ = 12.0;
    double gateAttackMs_ = 1.0;
    double gateReleaseMs_ = 100.0;
    float gateEnv_ = 0.0f;  // envolvente del nivel de entrada
    float gateGain_ = 1.0f; // ganancia actual (lineal)
    float gateGainTarget_ = 1.0f;

    // --- Compresor (peak, knee suave) ---
    bool compEnabled_ = true;
    double compRatio_ = 2.5;
    double compThresholdDb_ = -20.0;
    double compKneeDb_ = 6.0;
    double compAttackMs_ = 10.0;
    double compReleaseMs_ = 150.0;
    float compEnv_ = 0.0f;
    float compGain_ = 1.0f;
    float compGainTarget_ = 1.0f;

    // --- Limitador (lookahead) ---
    bool limEnabled_ = true;
    double limCeilingDb_ = -1.0;
    int limLookahead_ = 64;
    float limGain_ = 1.0f; // ganancia de limitacion actual (lineal)

    // --- Suavizado de ganancias (one-pole sobre smoothMs) ---
    // Coeficiente de interpolacion por muestra en [0,1]: cuanto mas grande,
    // mas rapido converge. Se aplica a cada ganancia dinamica:
    //   gain += (target - gain) * fade.
    float fade_ = 0.0f;

    // --- Buffers de lookahead de tamano fijo (sin heap) ---
    // El limitador con lookahead necesita ver las próximas
    // limLookaheadSamples muestras. Se usa un buffer circular de tamano fijo
    // (kMaxLookahead) para cada canal.
    static constexpr int kMaxLookahead = 256;
    float limBufL_[kMaxLookahead] = {0};
    float limBufR_[kMaxLookahead] = {0};
    int limBufPos_ = 0;  // indice de escritura circular
    int limBufFill_ = 0; // muestras validas en el buffer (0..kMaxLookahead)

    // --- Muestreo ---
    double fs_ = 48000.0;

    // --- Utilidades internas ---
    // dB -> lineal.
    static double dbToLin(double db);
    // lineal -> dB (con piso para evitar -inf).
    static double linToDb(double lin);
    // Coeficiente one-pole (0..1) para un tiempo en ms a una fs dada.
    static float onePole(double ms, double fs);
};

} // namespace ba::dsp
