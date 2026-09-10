// Biquad.hpp
//
// Proposito:
//   Biquad de segundo orden (IIR) con las formulas del "Audio EQ Cookbook" de
//   Robert Bristow-Johnson (RBJ), en forma directa II transpuesta. Es la
//   primitiva de filtrado del channel strip nativo (spec 7.2): lo usan el HPF
//   (cascada de high-pass), el EQ parametrico de 4 bandas (peaking/shelf) y el
//   de-esser (band-pass de sidechain). Normaliza por A0 (a0=1) para que el
//   process() sea solo multiplications y sumas.
//
// Modelo de threading:
//   NO thread-safe: cada instancia tiene estado interno (x1, x2) que muta en
//   process(). Un biquad pertenece a UN ChannelStrip y lo procesa un solo
//   thread de audio a la vez. setParams() se llama desde el thread de control
//   (cambio de preset); el contrato es que no se llame en paralelo con
//   process() del mismo biquad (el suavizado de ganancia del strip evita clicks
//   sin necesidad de locks).
//
// Estrategia de errores:
//   No lanza. Clamps defensivos: fs debe ser > 0 (si no, se usa 48000) y freq
//   debe ser > 0 y < fs/2 (se recorta al Nyquist con un margen); un Q <= 0 se
//   toma como 0.5. Con esos clamps el filtro queda siempre numericamente
//   estable (polos dentro del circulo unitario) para cualquier senal de
//   prueba. No hay asignacion ni I/O en el camino de process().
#pragma once

namespace ba::dsp {

/// Tipos de filtro del cookbook RBJ que soporta `Biquad`.
enum class BiquadType {
    LowPass,    // paso bajo (Butterworth, Q=0.7071 por defecto del cookbook)
    HighPass,   // paso alto (Butterworth)
    BandPass,   // pasa banda (ganancia pico constante 0 dB)
    Peaking,    // EQ parametrico de pico (boost/cut)
    LowShelf,   // estanteria baja
    HighShelf   // estanteria alta
};

/// Biquad IIR de segundo orden, forma directa II transpuesta, coeficientes
/// normalizados por A0 (a0 == 1) segun el Audio EQ Cookbook de RBJ.
///
/// Mantene su propio estado interno (x1, x2), por lo que es un objeto con
/// estado: un `Biquad` distinto por canal de audio. `setParams()` recalcula
/// los coeficientes (no toca el estado); `reset()` limpia el estado.
class Biquad {
public:
    /// Recalcula los coeficientes para el tipo/frecuencia/ganancia/Q dados.
    ///
    /// Aplica las formulas exactas del Audio EQ Cookbook de RBJ (bilinear
    /// transform, A0-normalizado). No modifica el estado interno (x1, x2), de
    /// modo que cambiar parametros en caliente no reintroduce un click por
    /// reinicio de estado; el suavizado de ganancia lo hace el ChannelStrip.
    ///
    /// :param type: tipo de filtro a sintetizar.
    /// :param fs: frecuencia de muestreo en Hz (debe ser > 0; si no, 48000).
    /// :param freq: frecuencia central / de esquina en Hz (> 0, < fs/2).
    /// :param gainDb: ganancia en dB. Solo relevante para Peaking y shelves;
    ///                 para LP/HP/BP se ignora (0 dB por construccion).
    /// :param q: factor Q. Para LP/HP el cookbook usa Q=0.7071 (Butterworth)
    ///           salvo que se indique otro; para Peaking/BP/Shelf es el Q dado.
    void setParams(BiquadType type, double fs, double freq, double gainDb, double q);

    /// Procesa una muestra: y[n] = b0*x[n] + b1*x1 + b2*x2 - a1*y1 - a2*y2
    /// (forma directa II transpuesta, a0=1). Muta el estado interno.
    ///
    /// :param x: muestra de entrada.
    /// :returns: muestra de salida filtrada.
    float process(float x);

    /// Limpia el estado interno (x1 = x2 = 0) sin tocar los coeficientes.
    /// Evita transitorios al (re)iniciar el filtro.
    void reset();

private:
    // Coeficientes normalizados (a0 == 1). b0..b2 feedforward, a1..a2 feedback.
    float b0_ = 1.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    // Estado directo II transpuesto: x1 = x[n-1], x2 = x[n-2].
    float x1_ = 0.0f;
    float x2_ = 0.0f;
};

} // namespace ba::dsp
