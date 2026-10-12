// ============================================================================
// src/core/dsp/LimitadorTP.hpp — limitador del máster con true peak (T12)
//
// Que lo que sale nunca pase el techo (de fábrica −1 dBTP, lo que piden YouTube
// y la EBU R128), contando los picos ENTRE muestras: un DAC o un códec que
// reconstruye la onda los ve aunque ninguna muestra pase el techo.
//
//   - True peak: cada muestra se sobremuestrea ×4 (FIR polifásico, sinc con
//     ventana de Kaiser) y se toma el mayor valor de todos los canales.
//   - Lookahead (de fábrica 5 ms): el audio sale demorado esos milisegundos y la
//     ganancia ya bajó cuando llega el pico. Sin distorsión de ataque.
//   - La ganancia que hace falta se calcula así, y GARANTIZA el techo:
//       1. g(n) = min(1, techo / pico(n))
//       2. h(n) = mínimo de g en las últimas L+1 muestras
//       3. a(n) = promedio de h en las últimas L muestras
//     (cada término del promedio ya es ≤ la ganancia que necesita la muestra que
//     sale ahora, la de hace L). Después, la vuelta (liberación) es lenta: solo
//     puede bajar más, nunca pasar.
//   - Sin señal sobre el techo, la ganancia es 1: pasa tal cual (con la demora).
//
// Demora: L = lookahead × frecuencia (muestras). Quien lo use tiene que
// declararla (el elemento de GStreamer la suma en la consulta LATENCY).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace ba::dsp {

class LimitadorTP {
   public:
    LimitadorTP() = default;
    /// Prepara para `canales` a `frecuencia` Hz. Vacía el estado.
    void preparar(int canales, int frecuencia);
    /// Techo en dBTP (−20…0), lookahead (1…20 ms), liberación (10…2000 ms).
    void ponerTecho(double dbtp);
    void ponerLookahead(double ms);
    void ponerLiberacion(double ms);
    double techo() const { return techoDb_; }
    /// Apagado: no limita, pero sigue demorando lo mismo (la latencia no cambia).
    void ponerActivo(bool si) { activo_ = si; }
    bool activo() const { return activo_; }
    /// Procesa en el lugar `frames` cuadros intercalados (float).
    void procesar(float* datos, std::size_t frames);
    /// La demora que agrega (muestras por canal).
    std::size_t demora() const { return lookahead_; }
    /// La reducción actual (dB, ≤ 0) y la mayor desde la última consulta.
    double reduccionDb() const;
    double reduccionMaxDb();
    /// El true peak de una señal (útil para medir lo que sale): de a bloques, con
    /// estado propio por canal.
    class MedidorTP {
       public:
        void preparar(int canales);
        /// El mayor pico (lineal) del bloque, entre muestras incluidas.
        double medir(const float* datos, std::size_t frames);

       private:
        int canales_ = 0;
        std::vector<std::vector<float>> hist_;
        std::size_t pos_ = 0;
    };

   private:
    void rearmar();
    double picoVerdadero(const float* frame);

    int canales_ = 0, frecuencia_ = 48000;
    double techoDb_ = -1.0, techoLin_ = 0.891, lookaheadMs_ = 5.0, liberacionMs_ = 100.0;
    std::size_t lookahead_ = 240;
    // Sobremuestreo: historia por canal (12 muestras) para el FIR polifásico.
    std::vector<std::vector<float>> hist_;
    std::size_t histPos_ = 0;
    // La demora del audio: L cuadros intercalados.
    std::vector<float> demoraBuf_;
    std::size_t demoraPos_ = 0;
    // Mínimo deslizante de g en una ventana de L+1 (pares índice, valor).
    std::deque<std::pair<std::uint64_t, float>> minimos_;
    // Promedio deslizante de h en L muestras.
    std::vector<float> hBuf_;
    std::size_t hPos_ = 0;
    double hSuma_ = 0;
    std::uint64_t n_ = 0;
    double g_ = 1.0, coefLib_ = 0.0;
    bool activo_ = true;
    double redActual_ = 0, redMax_ = 0;
};

} // namespace ba::dsp
