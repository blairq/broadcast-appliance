// ============================================================================
// src/gateway/Labios.hpp — el video del programa en sincronía con su audio, para quien MIRA (F2-11)
//
// El programa le llega a B por dos caminos que tardan distinto: el video por su canal
// (BA.Program / BA.Salida.<id>), apenas la composición lo arma y la GPU lo baja; el audio
// por su anillo (BA.Audio.ContentForReturn / BA.Audio.Salida.<id>), a la hora que le toca
// con toda la latencia de la mezcla. B empujaba cada uno apenas lo leía: en la pantalla de
// la sala, en el celular de un espectador o en la consola de la presidencia, la boca se
// movía 70 a 230 ms antes de que se oyera (docs/arquitectura/03-hardware-y-sonido.md P14;
// BT.1359 tolera 45 ms de audio atrasado).
//
// Los dos traen la hora de lo que muestran o suenan, en el mismo reloj (QPC): el cuadro su
// `contenidoQpc` (Vistas, en A) y cada bloque de audio su sello (baanillosink). Lo que tarda
// el audio desde su hora hasta que B lo empuja es cuánto después de su sello lo escribe A en
// el anillo (la cabecera lo dice: SharedAudioRing::retrasoDelEscritorNs) más lo que tarda B
// en leerlo, suavizado (EdadAudio). Cada cuadro se demora hasta que tenga esa misma edad:
// sale a `contenidoQpc + edad del audio`. Así cada cuadro conserva su lugar en
// el tiempo (no se corre la cadencia) y la diferencia que queda es el temblor de cada lado.
//
// Solo para quien MIRA (T4): al invitado que conversa (con su mix-minus) no se le demora
// nada; para él gana la latencia aunque el audio llegue un poco después que la imagen.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace ba::gateway::labios {

/// Lo más que se demora un cuadro: más es otra cosa (un audio trabado, un sello malo) y
/// juntar cuadros en memoria no lo arregla.
inline constexpr std::uint64_t kTopeNs = 400'000'000ULL;
/// Lo más que se guardan a la espera (a 60 cps, los 400 ms del tope y algo más).
inline constexpr std::size_t kTopeCuadros = 32;

/// Cuándo empujar a quienes miran un cuadro cuya hora es `contenidoQpc` y llegó a las
/// `ahoraQpc`, si el audio se empuja con `edadAudioNs` de edad (< 0 = no se sabe). Sin
/// hora o sin la del audio, ya; nunca antes de ahora ni más tarde que el tope.
inline std::uint64_t salida(std::uint64_t contenidoQpc, std::uint64_t ahoraQpc, std::int64_t edadAudioNs) {
    if (contenidoQpc == 0 || edadAudioNs <= 0 || contenidoQpc > ahoraQpc + kTopeNs) return ahoraQpc;
    const std::uint64_t objetivo = contenidoQpc + static_cast<std::uint64_t>(edadAudioNs);
    return std::clamp(objetivo, ahoraQpc, ahoraQpc + kTopeNs);
}

/// Lo que tarda el audio, suavizado: cada medida (ns) entra con 1/16 de peso, y una que se
/// va lejos (más de 50 ms de lo que se venía midiendo: un bloque leído a destiempo, A que se
/// reinicia) cuenta poco hasta que se repite.
class EdadAudio {
   public:
    void medir(std::int64_t ns) {
        if (ns <= 0 || ns > static_cast<std::int64_t>(kTopeNs)) return;
        if (ns_ < 0) {
            ns_ = ns;
            return;
        }
        const std::int64_t d = ns - ns_;
        ns_ += (d > 50'000'000 || d < -50'000'000) ? d / 64 : d / 16;
    }
    /// -1 si todavía no hay.
    std::int64_t ns() const { return ns_; }

   private:
    std::int64_t ns_ = -1;
};

/// Los cuadros que esperan su hora (`T`: lo que se empuja; con CuadroSaliente, una
/// referencia al mismo buffer ya convertido). Los de un mismo productor llegan en orden.
template <typename T>
class Espera {
   public:
    /// Guarda `cuadro` para empujarlo a las `cuandoQpc`. Si no entra, el más viejo sale ya
    /// por `fuera` (mejor tarde que nunca: no se tira un cuadro por esperar).
    template <typename F>
    void guardar(std::uint64_t cuandoQpc, T cuadro, F&& fuera) {
        if (cola_.size() >= kTopeCuadros) {
            fuera(cola_.front().second);
            cola_.pop_front();
        }
        // Monótona: si la edad del audio bajó, un cuadro no se adelanta al anterior.
        if (!cola_.empty()) cuandoQpc = std::max(cuandoQpc, cola_.back().first);
        cola_.emplace_back(cuandoQpc, std::move(cuadro));
    }
    /// Los que ya tienen su hora, a `fn` (en orden). Cuántos.
    template <typename F>
    std::size_t sacarListos(std::uint64_t ahoraQpc, F&& fn) {
        std::size_t n = 0;
        while (!cola_.empty() && cola_.front().first <= ahoraQpc) {
            fn(cola_.front().second);
            cola_.pop_front();
            ++n;
        }
        return n;
    }
    /// La hora del próximo (0 = ninguno).
    std::uint64_t proximo() const { return cola_.empty() ? 0 : cola_.front().first; }
    std::size_t size() const { return cola_.size(); }
    void vaciar() { cola_.clear(); }

   private:
    std::deque<std::pair<std::uint64_t, T>> cola_;
};

} // namespace ba::gateway::labios
