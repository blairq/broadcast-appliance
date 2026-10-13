// ============================================================================
// src/app/audio/Analyzer.hpp — analizador de audio (medicion para calibrar)
//
// Corre en el hilo de audio sin reservar memoria ni bloquear y cada 100 ms
// publica una foto (seqlock) que el hilo principal lee cuando quiera:
//   - pico de muestra y RMS por canal, recortes (clipping), DC;
//   - loudness BS.1770-4: momentanea (400 ms), corta (3 s) e integrada (con
//     compuertas absoluta -70 LUFS y relativa -10 LU);
//   - espectro en tercios de octava (25 Hz - 20 kHz, FFT de 4096 con Hann),
//     promedio exponencial (~1 s) y acumulado desde el ultimo reset;
//   - correlacion estereo;
//   - historial de niveles de 100 ms (los ultimos 30 s): de ahi salen el piso
//     de ruido (percentil bajo) y el nivel de voz/programa (percentil alto).
// `report()` arma el resumen para el asistente (con pistas de que mejorar).
// Tambien: generador de señales de prueba (seno, ruido blanco/rosa, barrido).
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::audio {

class Analyzer {
   public:
    static constexpr int kBands = 30;    // tercios de octava 25 Hz .. 20 kHz
    static constexpr int kHistory = 300; // bloques de 100 ms (30 s)

    struct Snapshot {
        double seconds = 0;                // audio analizado desde el reset
        float peakDb[2] = {-120, -120};    // pico de los ultimos 100 ms
        float maxPeakDb[2] = {-120, -120}; // pico maximo desde el reset
        float rmsDb[2] = {-120, -120};     // RMS de 300 ms
        float lufsM = -120, lufsS = -120, lufsI = -120;
        float correlation = 1;
        float dc[2] = {0, 0};
        uint32_t clips = 0;
        float bandsDb[kBands]{};     // promedio exponencial (~1 s)
        float bandsLongDb[kBands]{}; // desde el reset
        float history[kHistory]{};   // nivel (dBFS, RMS) de cada bloque de 100 ms
        int historyCount = 0;        // cuantos validos (<= kHistory), el ultimo es el mas nuevo
    };

    static const std::array<float, kBands>& bandCenters();

    Analyzer();
    void setSampleRate(double sr);
    /// Hilo de audio. `ch` 1 o 2 (con 1, R = L).
    void feed(const float* const* ch, int channels, uint32_t frames);
    /// Pedido de reset (lo aplica el hilo de audio en el proximo feed).
    void requestReset() { resetRequested_ = true; }
    /// Cualquier hilo: la ultima foto publicada.
    Snapshot snapshot() const;
    /// Resumen para el asistente: niveles, loudness, espectro por zonas, piso
    /// de ruido, nivel activo y pistas.
    static nlohmann::json report(const Snapshot& s);

   private:
    void reset();
    void publish();
    void fftFrame();
    // El espectro solo se calcula si alguien lo mira: snapshot() lo marca y el hilo de
    // audio deja de hacer la FFT a los 3 s sin lecturas (niveles y loudness siguen).
    mutable std::atomic<bool> leido_{true};
    int bloquesSinLeer_ = 0;

    double sr_ = 48000;
    std::atomic<bool> resetRequested_{false};
    // K-weighting (2 biquads por canal).
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = {0, 0}, z2[2] = {0, 0};
        double run(double x, int c) {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
    } kShelf_, kHp_;
    // Bloques de 100 ms.
    uint32_t blockLen_ = 4800, blockPos_ = 0;
    double blockK_ = 0;           // suma de K^2 (L+R) del bloque
    double blockSq_[2] = {0, 0};  // suma de x^2 por canal
    double blockSum_[2] = {0, 0}; // suma de x (DC)
    double blockXY_ = 0;          // L*R
    float blockPeak_[2] = {0, 0};
    std::array<double, 30> kRing_{}; // energia K de los ultimos 30 bloques (3 s)
    std::array<double, 30> sqRing_[2]{};
    std::array<double, 4> xyRing_{};
    int ringPos_ = 0, ringCount_ = 0;
    // Integrada: bloques de 400 ms cada 100 ms, histograma de 0.1 LU.
    static constexpr int kHist = 800; // -70 .. +10 LUFS
    std::array<uint32_t, kHist> gateHist_{};
    std::array<double, kHist> gateSum_{};
    // Historial de niveles.
    std::array<float, kHistory> history_{};
    int historyPos_ = 0, historyCount_ = 0;
    // FFT
    static constexpr int kFft = 4096;
    std::vector<float> fftBuf_, window_;
    std::vector<float> re_, im_;
    int fftPos_ = 0;
    std::array<double, kBands> bandAvg_{}, bandLong_{};
    uint64_t bandLongN_ = 0;
    double seconds_ = 0;
    float maxPeak_[2] = {0, 0};
    uint32_t clips_ = 0;

    // Publicacion (seqlock).
    mutable std::atomic<uint32_t> seq_{0};
    Snapshot pub_;
};

/// Generador de señales de prueba.
class Generator {
   public:
    enum class Kind { Silence, Sine, White, Pink, Sweep };
    struct Config {
        Kind kind = Kind::Sine;
        double freq = 1000;     // seno / inicio del barrido
        double freqEnd = 20000; // fin del barrido
        double seconds = 10;    // duracion de un barrido
        double levelDb = -20;   // seno: pico; ruidos: RMS
    };
    static bool parseKind(const std::string& s, Kind& k);
    static const char* kindName(Kind k);

    void setSampleRate(double sr) { sr_ = sr; }
    /// Hilo principal: cambia la señal (se toma en el proximo bloque).
    void configure(const Config& c);
    Config config() const;
    /// Hilo de audio: llena `ch` (1 o 2 canales).
    void render(float* const* ch, int channels, uint32_t frames);

   private:
    double sr_ = 48000;
    std::atomic<uint32_t> seq_{0};
    Config pending_, cur_;
    uint32_t seen_ = 0;
    double phase_ = 0, t_ = 0;
    uint32_t rng_ = 0x12345678u;
    double pink_[7]{};
};

} // namespace ba::app::audio
