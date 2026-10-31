// ============================================================================
// src/core/ipc/MedidaTransporte.hpp — cómo anda un anillo de audio o un canal de video que se lee
//
// Los contadores del transporte entre procesos (docs/arquitectura/04-plan-unificado.md, F0-02):
// sin ellos, «el retorno se corta» o «la pantalla va atrasada» no tienen número y no se sabe
// si la culpa es del que escribe, del que lee o de la máquina. Por cada anillo o canal que lee
// el gateway, en ventanas (de fábrica 10 s):
//   - ocupación: los bloques que había sin leer al mirar (el máximo de la ventana). El nivel
//     de operación del anillo de audio es 1-3 bloques; más es latencia que se acumula;
//   - descartados (overrun): lo que el escritor tiró porque el lector no llegaba (el
//     overrunCount del anillo) o, en video, los cuadros que A publicó y B no llegó a leer
//     (saltos del índice de cuadro);
//   - faltantes (underrun): los bloques de audio que no llegaron (saltos en los sellos QPC
//     de la secuencia, o un mix-minus sin bloque cuando lo pidió el programa);
//   - edad p50/p99: cuánto hace que se selló lo que se lee (QPC del bloque o del cuadro
//     contra el QPC de ahora; los dos procesos usan el mismo reloj, en ns).
//
// La usa UN hilo (el que lee el canal): sin candados. La foto de cada ventana la publica
// ese hilo donde la lean los demás (MediaPump::transporte). Sin dependencias: la incluye
// SharedAudioRing.hpp, que llega hasta los plugins de GStreamer.
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace ba::ipc {

/// Lo que pasó en un anillo o canal en una ventana, más los totales desde que se abrió.
struct FotoTransporte {
    std::string nombre; ///< el canal (BA.Audio.ContentForReturn, BA.Program…)
    std::string tipo;   ///< "audio" | "video"
    double ventanaS = 0;
    std::uint64_t leidos = 0;       ///< bloques o cuadros leídos en la ventana
    std::uint64_t ocupacionMax = 0; ///< audio: bloques sin leer al mirar (el máximo)
    std::uint64_t descartados = 0;  ///< overrun en la ventana
    std::uint64_t faltantes = 0;    ///< underrun en la ventana (solo audio)
    double edadP50Ms = -1, edadP99Ms = -1; ///< -1: nada leído en la ventana
    std::uint64_t descartadosTotal = 0, faltantesTotal = 0;

    /// Algo para mirar: se perdió audio o video, el anillo se llenó más que el nivel de
    /// operación, o lo leído llega viejo. Lo normal no va al registro (no inunda).
    bool anomalia(std::uint64_t ocupacionTope, double edadTopeMs) const {
        return descartados > 0 || faltantes > 0 || ocupacionMax > ocupacionTope || edadP99Ms > edadTopeMs;
    }
};

class MedidaTransporte {
   public:
    /// `periodoNs`: lo que dura un bloque de audio (10 ms) para contar los faltantes por los
    /// sellos. Solo sirve si el escritor sella con la hora del medio (baanillosink: el programa,
    /// los mix-minus); uno que sella al escribir (la voz del operador, la del asistente) escribe
    /// de a ráfagas y los saltos no son faltantes: 0, igual que en video.
    MedidaTransporte(std::string nombre, std::string tipo, std::uint64_t periodoNs = 0)
        : periodoNs_(periodoNs) {
        foto_.nombre = std::move(nombre);
        foto_.tipo = std::move(tipo);
    }

    /// Al mirar el anillo: cuántos bloques había sin leer.
    void mirar(std::uint64_t pendientes) {
        if (pendientes > foto_.ocupacionMax) foto_.ocupacionMax = pendientes;
    }

    /// Un bloque de audio leído, sellado con `qpc`. Un salto en los sellos de más de un bloque
    /// y medio son bloques que no llegaron (la mezcla de A se atrasó o se cortó). Un salto de
    /// más de un segundo (A se reinició, el anillo se reabrió) no se cuenta: no es un hueco
    /// del ritmo, es otro escritor.
    void bloque(std::uint64_t qpc, std::uint64_t ahoraQpc) {
        if (periodoNs_ > 0 && ultimoQpc_ != 0 && qpc > ultimoQpc_) {
            const std::uint64_t d = qpc - ultimoQpc_;
            if (d > periodoNs_ + periodoNs_ / 2 && d < 1'000'000'000ULL) hueco((d + periodoNs_ / 2) / periodoNs_ - 1);
        }
        ultimoQpc_ = qpc;
        leido(qpc, ahoraQpc);
    }

    /// Un cuadro de video leído: su índice (el que pone A al publicar) y su sello.
    void cuadro(std::uint64_t indice, std::uint64_t qpc, std::uint64_t ahoraQpc) {
        if (ultimoIndice_ != 0 && indice > ultimoIndice_ + 1 && indice - ultimoIndice_ < 1000) {
            foto_.descartados += indice - ultimoIndice_ - 1;
            foto_.descartadosTotal += indice - ultimoIndice_ - 1;
        }
        ultimoIndice_ = indice;
        leido(qpc, ahoraQpc);
    }

    /// El contador acumulado de descartes del escritor (SharedAudioRing::descartados()).
    /// Uno que vuelve para atrás es un anillo nuevo: se toma como punto de partida.
    void descartesDelEscritor(std::uint64_t total) {
        if (hayDescartes_ && total > descartesVistos_) {
            foto_.descartados += total - descartesVistos_;
            foto_.descartadosTotal += total - descartesVistos_;
        }
        descartesVistos_ = total;
        hayDescartes_ = true;
    }

    /// Bloques que hacían falta y no estaban (underrun).
    void hueco(std::uint64_t n = 1) {
        foto_.faltantes += n;
        foto_.faltantesTotal += n;
    }

    /// El anillo o canal se reabrió (otro escritor): los sellos y los índices empiezan de nuevo.
    void reabierto() {
        ultimoQpc_ = 0;
        ultimoIndice_ = 0;
        hayDescartes_ = false;
    }

    /// Cierra la ventana (de `ventanaS` segundos): devuelve su foto y empieza otra.
    FotoTransporte cerrar(double ventanaS) {
        FotoTransporte f = foto_;
        f.ventanaS = ventanaS;
        f.edadP50Ms = percentil(0.50);
        f.edadP99Ms = percentil(0.99);
        foto_.leidos = 0;
        foto_.ocupacionMax = 0;
        foto_.descartados = 0;
        foto_.faltantes = 0;
        edades_.fill(0);
        return f;
    }

   private:
    // La edad en milisegundos enteros: el último casillero junta todo lo de 1 s o más.
    static constexpr std::size_t kCasilleros = 1001;

    void leido(std::uint64_t qpc, std::uint64_t ahoraQpc) {
        ++foto_.leidos;
        if (qpc == 0) return; // sin sello: no se sabe la edad
        const std::uint64_t ms = ahoraQpc > qpc ? (ahoraQpc - qpc) / 1'000'000ULL : 0;
        ++edades_[ms < kCasilleros ? ms : kCasilleros - 1];
    }

    double percentil(double p) const {
        std::uint64_t total = 0;
        for (const std::uint64_t n : edades_) total += n;
        if (total == 0) return -1;
        const auto objetivo = static_cast<std::uint64_t>(p * static_cast<double>(total - 1)) + 1;
        std::uint64_t acum = 0;
        for (std::size_t i = 0; i < kCasilleros; ++i) {
            acum += edades_[i];
            if (acum >= objetivo) return static_cast<double>(i);
        }
        return static_cast<double>(kCasilleros - 1);
    }

    std::uint64_t periodoNs_;
    FotoTransporte foto_;
    std::array<std::uint32_t, kCasilleros> edades_{};
    std::uint64_t ultimoQpc_ = 0;
    std::uint64_t ultimoIndice_ = 0;
    std::uint64_t descartesVistos_ = 0;
    bool hayDescartes_ = false;
};

} // namespace ba::ipc
