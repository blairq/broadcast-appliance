// SPDX-License-Identifier: GPL-2.0-or-later
//
// LoudnessPrograma: el loudness de lo que SALE (la mezcla 0, la que va a la
// transmisión y la grabación), según ITU-R BS.1770-4 / EBU R128:
//   - momentáneo (400 ms), corto (3 s), integrado (compuertas -70 LUFS y -10 LU);
//   - true peak (sobremuestreo x4, FIR polifásico);
//   - LRA (EBU Tech 3342: percentiles 10 y 95 del corto, compuerta relativa -20 LU).
// Corre en el hilo de audio de libobs (barato: dos biquads y 4x12 productos
// por muestra y canal) y publica una foto que cualquier hilo lee.
#pragma once

#include <nlohmann/json.hpp>

#include "dsp/PonderacionK.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ba::app::audio {

class LoudnessPrograma {
   public:
    struct Foto {
        double m = -120, s = -120, i = -120; ///< LUFS
        double tp = -120;                    ///< dBTP (máximo desde el reset)
        double tpReciente = -120;            ///< dBTP de los últimos 400 ms
        double lra = 0;                      ///< LU
        double segundos = 0;                 ///< desde el reset
        double grDb = 0;                     ///< reducción del limitador del máster (T12)
    };
    struct Objetivo {
        std::string perfil = "r128"; ///< r128 | a85 | plataforma16 | plataforma14 | custom
        double lufs = -23, tp = -1;
        /// Control lento de ganancia hacia `lufs` antes del limitador del máster
        /// (dsp/AgcLoudness, en balimitador): opcional, de fábrica no.
        bool agc = false;
    };

    explicit LoudnessPrograma(double tasa = 48000);
    /// Hilo de audio: bloques planos float (1 o 2 canales).
    void alimentar(const float* const* datos, int canales, std::uint32_t cuadros);
    void reiniciar() { pedirReset_ = true; }
    Foto foto() const;
    Objetivo objetivo() const;
    /// Cambia el objetivo (perfil de fábrica o custom con lufs/tp). "" o el error.
    std::string ponerObjetivo(const nlohmann::json& o);
    void ponerGr(double db) { gr_ = db; }
    /// {m, s, i, tp, lra, segundos, gr_db, objetivo{perfil, lufs, tp}, fuera_de_objetivo}.
    nlohmann::json json() const;

   private:
    void resetInterno();
    void cerrarBloque(double energia);
    double tasa_;
    ba::dsp::PonderacionK k_; ///< la ponderación K y los bloques de 100 ms
    std::deque<double> bloques_; ///< energía media de cada bloque de 100 ms (los últimos 30)
    // Histogramas de 0,1 LU entre -70 y +30 LUFS (como libebur128): el integrado y
    // el LRA salen de acá sin guardar cada bloque de una sesión de horas.
    static constexpr int kBins = 1000;
    std::array<std::uint64_t, kBins> hist400_{}; ///< bloques de 400 ms (cada 100 ms)
    std::array<std::uint64_t, kBins> histCorto_{}; ///< corto de 3 s (cada 100 ms)
    // True peak: historial por canal para el FIR polifásico.
    static constexpr int kTaps = 12;
    std::array<std::array<float, kTaps>, 2> hist_{};
    int posHist_ = 0;
    std::array<std::array<float, kTaps>, 4> fir_{};
    double tpMax_ = 0, tpVentana_ = 0;
    double segundos_ = 0;
    std::atomic<bool> pedirReset_{false};
    std::atomic<double> gr_{0};
    mutable std::mutex mu_;
    Foto foto_;
    Objetivo obj_;
};

} // namespace ba::app::audio
