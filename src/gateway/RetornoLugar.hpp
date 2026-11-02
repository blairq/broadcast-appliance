// ============================================================================
// src/gateway/RetornoLugar.hpp — qué oye cada invitado al aire: su mix-minus o nada
//
// Por cada salida (BA.Audio.ContentForReturn la por omisión, BA.Audio.Salida.<id> las
// demás) el gateway reparte dos cosas (docs/arquitectura/04-plan-unificado.md, F0-01,
// F0-04 y F2-02):
//
//   - A cada invitado al aire de esa salida, SU mix-minus (BA.Audio.Return.<n> o
//     BA.Audio.Return.<id>.<n>: la mezcla de la salida sin su voz). Un invitado al aire
//     NUNCA recibe el programa entero: tiene su propia voz y la oiría con medio segundo
//     de demora (eco). Si su mix-minus no está abierto (la app todavía no lo armó, se
//     cayó, o el operador le apagó el retorno), «retorno apagado»: silencio, y la sala lo
//     publica para que el operador lo vea. Si el operador eligió que oiga el programa
//     entero, la app igual arma su anillo (con su voz adentro): el gateway no adivina.
//   - A los demás (la fila, las pantallas de escena), el programa.
//
// Cada uno al ritmo de lo SUYO. Los mix-minus los escribe baretornos todos juntos, en el
// mismo bloque del mezclador y apenas el bloque está (sin la espera del sink); el programa
// lo escribe un baanillosink con sync=true, ~70 ms más tarde y con su propio temblor. Hasta
// el 2026-11-02 el programa era el metrónomo de todo: por cada bloque del programa, un
// bloque de cada mix-minus, con dos de colchón. Como llegan por caminos distintos, cuando
// uno se atrasaba respecto del otro el colchón no alcanzaba: un mix-minus vacío era un
// hueco (silencio) y uno que se juntaba de más se tiraba para ponerse al día. Medido con
// 40 invitados (hinchada): de 5 a 50 bloques faltantes cada 10 s en cada retorno, ~44 ms
// de edad. Y en una vuelta que tenía varios bloques del programa juntos (el hilo se
// demoró) los mix-minus se tiraban en el primero y faltaban en los siguientes.
//
// Ahora los mix-minus marcan su propio ritmo (VueltasLugares): una «vuelta» es un bloque
// de cada lugar, el mismo instante del medio en todos (el sello del bloque es la hora del
// pipeline: los de un mismo bloque del mezclador tienen el mismo sello). Sale apenas
// llegó el bloque de todos los lugares vivos; si alguno no llega en kEspera (el escritor
// los escribe uno atrás del otro en microsegundos: no llegar es atrasarse de verdad), sale
// sin él (ese lugar: un bloque de silencio) y su bloque, cuando llegue tarde, se tira: el
// lugar no queda corrido respecto de los demás (que oigan lo mismo es lo que los deja
// compartir el Opus: AudioCompartido.hpp). Un lugar que dejó de escribir (kSinEsperar
// vueltas seguidas sin bloque) no frena a los demás. Nada se tira por ir adelante ni por
// llegar de a ráfagas: la ráfaga sale entera y el navegador la absorbe en su búfer.
// Los faltantes se cuentan por los saltos de sello del lugar (MedidaTransporte).
// ============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "MediaRoute.hpp"
#include "ipc/MedidaTransporte.hpp"
#include "ipc/SharedAudioRing.hpp"

namespace ba::gateway::retorno {

/// Si la sesión con ruta `r` oye el mix-minus de su lugar (va al ritmo de los lugares):
/// está al aire y el anillo de su lugar está abierto. `abiertos` vacío: no hay mix-minus.
inline bool vaPorSuLugar(const MediaRoute& r, const std::vector<bool>& abiertos) {
    if (r.kind != MediaRoute::Kind::Program || r.index <= 0) return false;
    const auto i = static_cast<std::size_t>(r.index);
    return i < abiertos.size() && abiertos[i];
}

/// Lo que se le manda a la sesión con ruta `r` junto con el bloque `programa`. `deLugar[n]`:
/// el bloque del mix-minus del lugar n de esta salida en esta vuelta, nullptr si su anillo
/// no está abierto (vacío si no hay mix-minus: sin nombres de lugares, las pruebas).
inline const float* fuente(const MediaRoute& r, const float* programa, const std::vector<const float*>& deLugar,
                           const float* silencio) {
    if (deLugar.empty() || r.kind != MediaRoute::Kind::Program || r.index <= 0) return programa;
    const auto i = static_cast<std::size_t>(r.index);
    if (i >= deLugar.size() || deLugar[i] == nullptr) return silencio; // retorno apagado
    return deLugar[i];
}

/// El ritmo de los mix-minus: arma vueltas (un bloque de cada lugar, el mismo instante del
/// medio) a medida que llegan. Sin hilos: lo usa el hilo del retorno de una salida.
class VueltasLugares {
   public:
    using Reloj = std::chrono::steady_clock;
    /// Cuánto se espera al lugar que falta cuando los demás ya llegaron.
    static constexpr auto kEspera = std::chrono::milliseconds(4);
    /// Vueltas seguidas sin bloque de un lugar para dejar de esperarlo.
    static constexpr int kSinEsperar = 3;
    /// Los floats de un bloque (estéreo): un lugar con otro formato no sirve (silencio).
    static constexpr std::size_t kFloats = std::size_t{ipc::audio_ring::kBlockFrames} * 2;
    /// Lo que dura un bloque, en la hora de los sellos (ns).
    static constexpr std::uint64_t kBloqueNs =
        std::uint64_t{ipc::audio_ring::kBlockFrames} * 1'000'000'000ULL / ipc::audio_ring::kSampleRate;

    /// Un lugar: su anillo (nullptr: cerrado) y su medida.
    struct Lugar {
        ipc::SharedAudioRing* anillo = nullptr;
        ipc::MedidaTransporte* medida = nullptr;
    };

    /// El lugar `s` se abrió o se cerró: lo que tenía guardado ya no va.
    void reiniciar(std::size_t s) {
        if (s < cab_.size()) cab_[s] = Cabeza{};
    }

    /// Mira los anillos (la ocupación y los descartes del escritor van a la medida de cada
    /// uno): una vez por despertada, antes de las vueltas.
    void mirar(const std::vector<Lugar>& lugares) {
        for (const Lugar& l : lugares)
            if (l.anillo != nullptr && l.medida != nullptr) {
                l.medida->mirar(l.anillo->pendientes());
                l.medida->descartesDelEscritor(l.anillo->descartados());
            }
    }

    /// Arma la próxima vuelta: true si hay una lista. Entonces `deLugar[s]` es el bloque del
    /// lugar s (kFloats floats), `silencio` si no llegó a tiempo o nullptr si está cerrado.
    /// Los punteros valen hasta la próxima llamada.
    bool proxima(const std::vector<Lugar>& lugares, const float* silencio, std::vector<const float*>& deLugar,
                 Reloj::time_point ahora, std::uint64_t ahoraQpc) {
        if (cab_.size() < lugares.size()) cab_.resize(lugares.size());
        deLugar.assign(lugares.size(), nullptr);
        for (std::size_t s = 1; s < lugares.size(); ++s)
            if (cab_[s].entregada) cab_[s].hay = cab_[s].entregada = false; // lo de la vuelta anterior
        bool alguna = false, sobra = false;
        Reloj::time_point masVieja = Reloj::time_point::max();
        for (std::size_t s = 1; s < lugares.size(); ++s) {
            ipc::SharedAudioRing* a = lugares[s].anillo;
            if (a == nullptr || a->blockFloats() != kFloats) continue;
            Cabeza& c = cab_[s];
            while (!c.hay && a->pendientes() > 0) {
                if (c.buf.size() < kFloats) c.buf.resize(kFloats);
                ipc::PoppedBlock b;
                if (!a->pop(c.buf.data(), b)) break;
                c.sello = b.qpc;
                c.llegada = ahora;
                // Ya salió la vuelta de ese instante: se tira. Salvo que llegue tarde
                // siempre: entonces va corrido un bloque (mejor que mudo).
                c.tardes = tarde(c.sello) ? c.tardes + 1 : 0;
                c.hay = c.tardes == 0 || c.tardes > kSinEsperar;
            }
            if (!c.hay) continue;
            alguna = true;
            masVieja = std::min(masVieja, c.llegada);
            if (a->pendientes() > 0) sobra = true; // otra vuelta esperando: el que falta se atrasó
        }
        if (!alguna) return false;
        // ¿Falta alguno que se espera? Se le da kEspera, salvo que ya haya otra vuelta atrás.
        if (!sobra && ahora - masVieja < kEspera)
            for (std::size_t s = 1; s < lugares.size(); ++s) {
                const ipc::SharedAudioRing* a = lugares[s].anillo;
                if (a != nullptr && a->blockFloats() == kFloats && !cab_[s].hay && cab_[s].seguidos < kSinEsperar)
                    return false;
            }
        // El instante de esta vuelta: el más viejo de los que llegaron (sin los corridos).
        std::uint64_t ref = UINT64_MAX, refCorridos = UINT64_MAX;
        for (std::size_t s = 1; s < lugares.size(); ++s)
            if (lugares[s].anillo != nullptr && cab_[s].hay) {
                std::uint64_t& r = cab_[s].tardes == 0 ? ref : refCorridos;
                r = std::min(r, cab_[s].sello);
            }
        if (ref == UINT64_MAX) ref = refCorridos;
        for (std::size_t s = 1; s < lugares.size(); ++s) {
            ipc::SharedAudioRing* a = lugares[s].anillo;
            if (a == nullptr) continue; // cerrado: retorno apagado
            Cabeza& c = cab_[s];
            if (c.hay && (c.sello <= ref + kBloqueNs / 2 || c.tardes > 0)) {
                deLugar[s] = c.buf.data();
                c.entregada = true;
                c.seguidos = 0;
                if (lugares[s].medida != nullptr) lugares[s].medida->bloque(c.sello, ahoraQpc);
            } else {
                deLugar[s] = silencio; // no llegó (o va adelante: sale en su vuelta)
                if (!c.hay) ++c.seguidos;
            }
        }
        ultimo_ = std::max(ultimo_, ref);
        return true;
    }

   private:
    struct Cabeza {
        bool hay = false;       ///< hay un bloque leído esperando su vuelta
        bool entregada = false; ///< salió en la última vuelta (se suelta en la próxima)
        std::uint64_t sello = 0;
        Reloj::time_point llegada{};
        int seguidos = 0; ///< vueltas seguidas sin bloque de este lugar
        int tardes = 0;   ///< bloques seguidos que llegaron después de su vuelta
        std::vector<float> buf;
    };

    /// Si la vuelta del instante `sello` ya salió (un bloque que llegó tarde). Un sello a
    /// más de un segundo es otro escritor (la app se reinició): no es tarde, es otro tiempo.
    bool tarde(std::uint64_t sello) const {
        return ultimo_ != 0 && sello < ultimo_ + kBloqueNs / 2 && (sello >= ultimo_ || ultimo_ - sello < 1'000'000'000ULL);
    }

    std::vector<Cabeza> cab_;
    std::uint64_t ultimo_ = 0; ///< el sello de la última vuelta
};

} // namespace ba::gateway::retorno
