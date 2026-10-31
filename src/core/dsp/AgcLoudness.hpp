// ============================================================================
// src/core/dsp/AgcLoudness.hpp — llevar el programa al objetivo de loudness, despacio
//
// El objetivo de `loudness.json` (−23 LUFS la EBU, −16 las plataformas) hasta
// acá solo se medía: si el programa salía 4 LU abajo, el operador tenía que
// darse cuenta y subir el máster. Esto lo hace solo, como un operador
// prolijo: mira el loudness de CORTO plazo (3 s, BS.1770 con la ponderación
// K) de lo que entra y mueve la ganancia hacia lo que falta para el objetivo,
// pero:
//
//   - despacio: como mucho 1 dB por segundo (no «bombea» con cada frase);
//   - poco: ±6 dB como máximo (no levanta un silencio ni aplasta un aplauso);
//   - congelado en silencio: un bloque de 100 ms 20 LU abajo del objetivo (una
//     pausa, el aire entre dos oradores) deja la ganancia donde estaba y no
//     entra a la ventana del corto: no sube a buscar el ruido de fondo.
//
// Va ANTES del limitador del máster (balimitador): el limitador sigue
// cuidando los picos. Sin demora propia (no hace falta lookahead para algo que
// se mueve a 1 dB/s). Sin memoria nueva en `procesar()`.
// ============================================================================
#pragma once

#include <array>
#include <cstddef>

#include "dsp/PonderacionK.hpp"

namespace ba::dsp {

class AgcLoudness {
   public:
    struct Opciones {
        double objetivoLufs = -23;
        double velocidadDbS = 1.0; ///< lo más rápido que se mueve la ganancia
        double rangoDb = 6.0;      ///< ± lo más que corrige
        double silencioLu = 20.0;  ///< congelado con un bloque este tanto abajo del objetivo
    };

    void preparar(int canales, int tasa);
    void ponerOpciones(const Opciones& o) { o_ = o; }
    void ponerObjetivo(double lufs) { o_.objetivoLufs = lufs; }
    /// Apagado: ganancia 0 dB (vuelve despacio, a la misma velocidad: sin saltos).
    void ponerActivo(bool si) { activo_ = si; }
    bool activo() const { return activo_; }
    /// En el lugar, `frames` cuadros intercalados.
    void procesar(float* datos, std::size_t frames);
    /// La ganancia de ahora (dB).
    double gananciaDb() const { return gananciaDb_; }
    /// El corto (3 s de lo que no es silencio) de lo que entra, LUFS (−120 hasta tenerlos).
    double cortoLufs() const { return cortoLufs_; }
    bool congelado() const { return congelado_; }

   private:
    Opciones o_;
    PonderacionK k_;
    int canales_ = 2;
    double tasa_ = 48000;
    bool activo_ = false;
    std::array<double, 30> bloques_{}; ///< energía de los últimos 30 bloques de 100 ms
    std::size_t nBloques_ = 0, posBloque_ = 0;
    double cortoLufs_ = -120;
    double gananciaDb_ = 0;
    double deseadaDb_ = 0;
    bool congelado_ = true;
};

} // namespace ba::dsp
