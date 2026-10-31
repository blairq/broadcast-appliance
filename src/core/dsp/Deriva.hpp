// ============================================================================
// src/core/dsp/Deriva.hpp — seguir el reloj de una placa y remuestrear al del estudio
//
// EL PROBLEMA. Una placa USB cuenta muestras con su propio cristal: a «48 kHz»
// entrega 48 000·(1 + ε) por segundo del reloj del estudio, con ε de 20 a 100
// ppm. Contando muestras, su audio se corre respecto del video y de las otras
// placas 50 ms cada 8-17 minutos. Hasta acá, cuando el corrimiento pasaba 50 ms
// se reanclaba (motor/Piezas.cpp): un hueco o un recorte de 50 ms en el
// programa, la grabación y el retorno, cada tantos minutos.
//
// LO QUE HACE. Un lazo de seguimiento (DLL de segundo orden, el de Fons
// Adriaensen: zita-ajbridge, JACK) filtra la hora de llegada de cada bloque y
// estima el período real de la placa (y con eso sus ppm). Con esa razón maneja
// un remuestreador variable (libsamplerate, dsp/Resampler) que entrega
// exactamente 48 000 muestras por segundo del estudio; un segundo término, más
// lento, corrige la fase acumulada para que la latencia quede fija. El sello de
// lo que sale es la hora de captura (la del primer bloque más lo que salió).
// El reanclaje queda como red de seguridad: si la fase se va más de 50 ms (la
// placa se cortó, se desconectó y volvió) se vuelve a empezar, y se cuenta.
//
// Al enganchar el lazo es ancho (1 Hz, se acomoda en segundos) y se angosta a
// 0,02 Hz (constante de tiempo ~8 s): el temblor de la llegada (los bloques de
// la placa llegan con milisegundos de diferencia) no llega a la razón.
//
// Sin E/S ni memoria nueva en `procesar()` (después de `configurar()`): corre
// en el hilo de streaming de la placa.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp/Resampler.hpp"

namespace ba::dsp {

class Deriva {
   public:
    struct Opciones {
        int frecuencia = 48000;
        int canales = 2;
        Quality calidad = Quality::SincFastest;
        double anchoHz = 0.02;        ///< el lazo ya enganchado
        double anchoInicialHz = 1.0;  ///< al enganchar (se angosta solo)
        double engancheS = 15;        ///< desde ahí la fase queda fija y se corrige
        double faseS = 30;            ///< constante de tiempo de la corrección de fase
        double reanclarS = 0.05;      ///< red de seguridad: fase más allá de esto
        double huecoS = 0.25;         ///< un bloque que llega con este atraso o adelanto: corte
        double topePpm = 1000;        ///< la corrección no pasa de esto (una placa sana: < 200)
    };

    void configurar(const Opciones& o);
    /// Vuelve a empezar (otro formato, un flush). No cuenta como reanclaje.
    void reiniciar();

    /// Lo que entra salió de la placa terminando en `llegadaS` (hora del estudio,
    /// segundos). Escribe lo remuestreado en `out` (hasta `cap` cuadros) y devuelve
    /// cuántos. `ptsS`: hora de captura del primer cuadro que sale. `corte`: true
    /// si hubo que reanclar (o es el primero): el sello no sigue al anterior.
    std::size_t procesar(const float* in, std::size_t n, double llegadaS, float* out, std::size_t cap, double& ptsS,
                         bool& corte);

    /// Lo que la placa se aparta del reloj del estudio (positivo: va rápida).
    double ppm() const { return ppm_; }
    /// Cuántas veces se reancló (sin contar el primer bloque).
    std::uint64_t reanclajes() const { return reanclajes_; }
    /// Cuánto se apartó la fase de la fijada al enganchar, en ms (lo que llega a A/V).
    double desfaseMs() const { return desfaseS_ * 1000.0; }
    bool enganchado() const { return enganchado_; }
    /// Lo que el remuestreador retiene (lo que sale está así de atrasado respecto
    /// de su sello): para la consulta de latencia. 0 hasta enganchar.
    double latenciaS() const;
    /// La razón que se está usando (salida/entrada).
    double razon() const { return razon_; }
    /// Cuadros de salida como mucho para `n` de entrada (para dimensionar `out`).
    std::size_t capacidadPara(std::size_t n) const;

   private:
    void empezar(std::size_t n, double llegadaS);

    Opciones o_;
    Resampler rs_;
    bool iniciado_ = false;
    bool enganchado_ = false;
    bool faseFijada_ = false;
    bool corteProximo_ = false; ///< se reancló al final del bloque anterior
    double periodo_ = 1.0 / 48000; ///< segundos por muestra de la placa (lo que estima el lazo)
    double fin_ = 0;               ///< hora filtrada del final del último bloque
    double t0_ = 0;                ///< hora de captura del primer cuadro desde el último anclaje
    double desde_ = 0;             ///< segundos de placa desde que se empezó (para el ancho del lazo)
    double salidas_ = 0;           ///< cuadros que salieron desde el anclaje
    double fase0_ = 0;             ///< la fase fijada al enganchar
    double desfaseS_ = 0;
    double ppm_ = 0;
    double razon_ = 1;
    std::uint64_t reanclajes_ = 0;
};

} // namespace ba::dsp
