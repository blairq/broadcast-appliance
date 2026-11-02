// ============================================================================
// src/motor/ReglasEntrada.hpp — reglas puras de una entrada: el vigía de «no llegan
// datos» y cuándo se corrigen sus horas
//
// Sin GStreamer (las prueba test_reglas_entrada). Entrada.cpp las aplica.
//
// UN LUGAR DE LA CONFERENCIA NO SE REARMA. Lo que lee de un anillo del gateway (el audio
// y el video de cada invitado, el remoto, la grilla) lo escribe el gateway mientras la
// conexión de esa persona está viva: callada o con el micrófono apagado igual llegan
// bloques (de silencio). Si se va, el gateway lo dice (el lugar queda sin gente y la
// entrada en reposo): esa es la única fuente de verdad. Rearmar no arregla nada (vuelve a
// abrir el mismo anillo) y si no llega nada es que alguien se atrasó al leer: se avisa.
// Medido el 2026-11-01 con 20/30/40 invitados: 19, 71 y 149 rearmados de gente conectada.
//
// LAS HORAS DE UN ANILLO SON LAS DEL ESTUDIO (el gateway sella cada bloque al
// publicarlo): si llegan viejas es que se leyeron tarde, no que la fuente tenga su
// propio reloj. «Realinearlas» las corría segundos hacia adelante y el mezclador las
// guardaba hasta su hora; mientras, el hilo común que lee el audio de todos esperaba.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace ba::motor {

/// El vigía de «no llegan datos sin error» de una entrada.
class VigiaDatos {
   public:
    enum class Que {
        Nada,      ///< sigue igual
        Llegan,    ///< llegó algo desde la última mirada
        Vuelven,   ///< llegó algo después de haber avisado que no llegaba
        Avisar,    ///< un lugar (anillo del gateway) sin datos el plazo: se avisa una vez
        Rearmar,   ///< otra entrada sin datos el plazo: se desarma y se vuelve a armar
    };

    /// Empieza a contar (armada o despertada): `datos` = lo que pasó hasta ahora. Lo que
    /// nunca dio nada desde acá no se rearma (no es «dejó de llegar»).
    void empezar(std::uint64_t datos, std::uint64_t ahoraNs) {
        base_ = vistos_ = datos;
        desdeNs_ = ahoraNs;
        avisado_ = false;
    }

    /// `datos`: lo que pasó hasta ahora (crece mientras llega algo); `plazoNs`: sin datos
    /// tanto tiempo se actúa; `lugar`: lo lee de un anillo del gateway (no se rearma).
    Que mirar(std::uint64_t datos, std::uint64_t ahoraNs, std::uint64_t plazoNs, bool lugar) {
        if (datos != vistos_) {
            vistos_ = datos;
            desdeNs_ = ahoraNs;
            const bool vuelven = avisado_;
            avisado_ = false;
            return vuelven ? Que::Vuelven : Que::Llegan;
        }
        if (vistos_ == base_ || ahoraNs - desdeNs_ < plazoNs) return Que::Nada;
        if (lugar) {
            if (avisado_) return Que::Nada;
            avisado_ = true;
            return Que::Avisar;
        }
        base_ = vistos_; // se rearma: hasta que vuelva a llegar algo, nada más
        return Que::Rearmar;
    }

    bool avisado() const { return avisado_; }

   private:
    std::uint64_t base_ = 0, vistos_ = 0, desdeNs_ = 0;
    bool avisado_ = false;
};

/// ¿Se corrigen las horas de una rama que llega corrida `medianaMs` del estudio? Solo las
/// de una fuente con SUS horas (empieza en 0, un emisor sin hora de captura), estables y
/// corridas más de un segundo. Las de un anillo del gateway (`horaDelEstudio`) nunca.
inline bool realinearHoras(bool horaDelEstudio, bool estable, double medianaMs) {
    return !horaDelEstudio && estable && std::fabs(medianaMs) > 1000.0;
}

} // namespace ba::motor
