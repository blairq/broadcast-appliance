// Resampler.hpp
//
// Proposito:
//   Envoltorio RAII fino sobre libsamplerate (Secret Rabbit Code, Erik de
//   Castro Lopo, MIT) para el re-muestreo adaptativo de la cadena de audio
//   (spec 7.4). Convierte un buffer de audio de una frecuencia de muestreo a
//   otra (p. ej. 44.1 kHz -> 48 kHz) conservando el tono y el contenido
//   espectral, con calidad configurable. Es la primitiva que permite que el
//   appliance acepte dispositivos a 44.1 kHz y los normalice a la frecuencia
//   de trabajo del sistema sin que el usuario tenga que tocar nada.
//
// Modelo de threading:
//   NO thread-safe: una instancia pertenece a UN thread y no se comparte entre
//   threads. El estado interno de libsamplerate (el `SRC_STATE*` que se crea en
//   `configure()`) se muta en cada llamada a `resample()`, de modo que dos
//   threads que compartan la instancia se pisarian entre si. En la cadena de
//   audio cada thread de render tiene su propio `Resampler`.
//
// Caminos de RT vs. control:
//   `configure()` es el camino de CONTROL: se llama una vez (o al cambiar de
//   frecuencia/canal/calidad) y alli se crea el estado de libsamplerate, que es
//   donde ocurre la unica asignacion de memoria del objeto. `resample()` es el
//   camino de RT (estado estacionario): NO hace ninguna asignacion de memoria
//   despues de `configure()`; solo recorre el buffer y escribe en la salida.
//
// Estrategia de errores:
//   No lanza. `resample()` devuelve `false` ante un error de libsamplerate
//   (p. ej. buffer de salida demasiado chico) y deja `*outFrames` en 0; ante
//   un buffer de salida insuficiente devuelve `false` y deja `*outFrames` con
//   la cantidad de frames que HUBIERA escrito (para que el llamador sepa cuanto
//   ampliar). `configure()` aplica clamps defensivos: `inRate`/`outRate` deben
//   ser > 0 y `channels` >= 1; si no, se corrigen a valores validos (48000 y
//   1) para que el objeto quede siempre en un estado util.
#pragma once

#include <cstddef>

// Estado opaco de libsamplerate. <samplerate.h> lo define como
// `typedef struct SRC_STATE_tag SRC_STATE;`; se adelanta la declaración del
// tag en el namespace GLOBAL (no dentro de `ba::dsp`) para que el puntero
// `SRC_STATE_tag*` coincida con el tipo real sin exponer el header C en esta
// cabecera. Así `ba_core` puede enlazar `samplerate` en PRIVATE: ningún header
// de ba_core necesita <samplerate.h>.
struct SRC_STATE_tag;

namespace ba::dsp {

/// Niveles de calidad del re-muestreo, mapeados 1:1 a los interpoladores de
/// libsamplerate. A mayor calidad, mejor fidelidad espectral y mas CPU.
///   - `Linear`     -> SRC_LINEAR: interpolacion lineal, la mas barata.
///   - `SincMedium` -> SRC_SINC_MEDIUM_QUALITY: Sinc de calidad media.
///   - `SincBest`   -> SRC_SINC_BEST_QUALITY: Sinc de mejor calidad (el
///                     default recomendado para audio de difusion).
enum class Quality {
    Linear,
    SincMedium,
    SincBest
};

/// Re-muestreador de audio (cambio de frecuencia de muestreo) sobre
/// libsamplerate. Objeto RAII: el destructor libera el estado interno.
///
/// Flujo de uso:
///   1. `Resampler r;` (estado vacio, sin memoria de libsamplerate).
///   2. `r.configure(inRate, outRate, channels, quality);` (camino de control,
///      crea el estado; unica asignacion de memoria).
///   3. `r.resample(in, inFrames, out, maxOutFrames, &outFrames);` (camino de
///      RT, sin asignaciones; se llama una y otra vez por bloque de audio).
///
/// El buffer de entrada `in` debe estar en formato INTERLEAVED (canal 0, canal
/// 1, ... por frame) cuando `channels > 1`; con `channels == 1` es plano. La
/// salida `out` se escribe en el mismo formato intercalado.
class Resampler {
public:
    /// Constructor: deja el objeto en estado vacio (sin estado de
    /// libsamplerate). No asigna memoria hasta `configure()`.
    Resampler();

    /// Destructor: libera el estado interno de libsamplerate, si existe.
    ~Resampler();

    /// Configura (o re-configura) el re-muestreador para una pareja de
    /// frecuencias, numero de canales y calidad.
    ///
    /// Es el camino de CONTROL: se llama una vez por cambio de configuracion.
    /// Aqui se crea el `SRC_STATE*` de libsamplerate (la unica asignacion de
    /// memoria del objeto). Si ya habia un estado previo, se destruye antes de
    /// crear el nuevo.
    ///
    /// :param inRate: frecuencia de muestreo de ENTRADA en Hz. Debe ser > 0;
    ///                si no, se corrige a 48000.
    /// :param outRate: frecuencia de muestreo de SALIDA en Hz. Debe ser > 0;
    ///                 si no, se corrige a 48000.
    /// :param channels: numero de canales (>= 1). Si no, se corrige a 1.
    /// :param q: calidad del interpolador (ver `Quality`).
    void configure(int inRate, int outRate, int channels, Quality q);

    /// Re-muestrea un bloque de audio de `inRate` a `outRate`.
    ///
    /// Es el camino de RT (estado estacionario): NO hace ninguna asignacion de
    /// memoria despues de `configure()`; solo recorre `in` y escribe en `out`.
    ///
    /// :param in: buffer de entrada en float, formato intercalado (canal 0,
    ///            canal 1, ... por frame) cuando `channels > 1`; plano si
    ///            `channels == 1`. Debe apuntar a `inFrames * channels` floats.
    /// :param inFrames: numero de frames de entrada (cada frame tiene
    ///                   `channels` muestras).
    /// :param out: buffer de salida en float, mismo formato intercalado que
    ///             `in`. Debe tener espacio para `maxOutFrames * channels`
    ///             floats.
    /// :param maxOutFrames: capacidad (en frames) del buffer `out`.
    /// :param outFrames: [out] numero de frames realmente escritos en `out`.
    ///                   Ante un error se deja en 0; ante buffer insuficiente
    ///                   se deja con la cantidad que habria escrito.
    /// :returns: `true` si re-muestreo todo el bloque con exito; `false` si
    ///           fallo (error de libsamplerate o buffer de salida insuficiente).
    bool resample(const float* in, size_t inFrames, float* out,
                  size_t maxOutFrames, size_t* outFrames);

private:
    // Estado interno de libsamplerate. Se crea en configure() y se libera en el
    // destructor. `SRC_STATE_tag` es el tag global que <samplerate.h> typedefa
    // como `SRC_STATE`; se declara opaca arriba para no exponer el header C.
    SRC_STATE_tag* state_ = nullptr;
    int channels_ = 1;
    // Ratio de conversión (outRate / inRate) fijado en configure(); se pasa en
    // cada SRC_DATA.src_ratio al llamar a src_process().
    double ratio_ = 1.0;
};

} // namespace ba::dsp
