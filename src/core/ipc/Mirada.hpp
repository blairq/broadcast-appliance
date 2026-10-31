// ============================================================================
// src/core/ipc/Mirada.hpp — cuándo volver a mirar un canal sin nada nuevo (F2-14)
//
// Nadie avisa que hay un cuadro (o un bloque de audio) nuevo en un canal de memoria
// compartida: el lector sondea. Mirar cada 4 ms fijos eran 250 despertadas por segundo
// por canal y por proceso, aunque el productor publicara 30 veces (docs/arquitectura/
// 04-plan-unificado.md, F2-14; 02 C8). Y mirar al ritmo del productor (cada 33 ms contra
// alguien que publica cada 33 ms) es peor: las dos cadencias baten y se pierde casi la
// mitad de los cuadros (medido: 13,6 de 30), con lo que llega parejo pero tarde.
//
// Lo que hace esto: el productor deja en la cabecera del canal CUÁNDO publicó el último
// (su reloj monotónico, el mismo en los dos procesos) y cada cuánto publica (lo que dice o
// lo que se le midió). El lector duerme hasta el próximo esperado con un margen, no un
// período desde que miró él: el plazo cuelga de la hora del productor, así que no bate.
// El margen sale del temblor del productor (Temblor): uno parejo se mira justo después
// de su hora; uno que tiembla, un poco antes, para no sumarle su temblor a la demora.
// Si al despertar no llegó (el productor se atrasó), mira de a pasos cortos que crecen
// con el atraso, hasta 4 ms (nunca peor que antes); quieto hace más de 1 s (un lugar
// vacío, un invitado callado), de a `pasoQuieto`.
//
// Es la mitad de C8 (el paso S). La otra mitad, el timbre entre procesos, es F3-13
// (ipc/Timbre.hpp): con un productor que toca, el lector duerme hasta el toque y esto
// queda para los que no tocan (uno viejo, o un macOS anterior a 14.4).
// ============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace ba::ipc::mirada {

using Reloj = std::chrono::steady_clock;

/// Después del próximo esperado (menos el adelanto por el temblor): lo que tarda el
/// productor en escribir y anunciar. Más es demora; menos, despertarse antes de que esté.
inline constexpr Reloj::duration kMargen = std::chrono::microseconds(500);
/// Sin saber el ritmo del productor (todavía no publicó dos): como antes.
inline constexpr Reloj::duration kSinRitmo = std::chrono::milliseconds(4);
/// Atrasado: el paso más corto y el más largo (el de antes: nunca más despertadas que con 4 ms).
inline constexpr Reloj::duration kPasoMin = std::chrono::milliseconds(1);
inline constexpr Reloj::duration kPasoMax = std::chrono::milliseconds(4);
/// Tanto sin publicar = quieto.
inline constexpr Reloj::duration kQuieto = std::chrono::seconds(1);
/// Con un productor que toca el timbre (ipc/Timbre.hpp, F3-13) el lector no mira: duerme
/// hasta el toque. Este es el plazo por si el productor se murió sin tocar (el lector mira
/// entonces si el canal quedó viejo: stale(), que mira de a 1 s).
inline constexpr Reloj::duration kVigia = std::chrono::seconds(1);

/// La hora monotónica en ns, la que el productor deja en la cabecera (steady_clock: el
/// mismo reloj para los dos procesos: QPC en Windows, CLOCK_MONOTONIC en los demás).
inline std::uint64_t ahoraNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Reloj::now().time_since_epoch()).count());
}

inline Reloj::time_point enReloj(std::uint64_t ns) {
    return Reloj::time_point(std::chrono::duration_cast<Reloj::duration>(std::chrono::nanoseconds(ns)));
}

/// El temblor de un productor, visto por UN lector: cuánto se corre cada publicación de la
/// esperada (la anterior más el período), en media móvil del desvío absoluto. Un productor
/// parejo (el motor, que publica con el reloj del estudio) tiembla décimas de ms y el lector
/// se despierta en el momento; uno que tiembla (un navegador, la red) hace que se despierte
/// antes, lo justo (el doble del temblor, a lo sumo un cuarto de período), y espere de a 1 ms.
class Temblor {
   public:
    /// Llegó una publicación nueva del productor (su hora de envío).
    void publicado(std::uint64_t envioNs, std::uint64_t periodoNs) {
        if (envioNs == anteriorNs_) return;
        if (anteriorNs_ != 0 && periodoNs != 0 && envioNs > anteriorNs_ &&
            envioNs - anteriorNs_ < static_cast<std::uint64_t>(std::chrono::nanoseconds(kQuieto).count())) {
            // Contra la grilla más cercana: un lector que se saltea uno (lee el último) no ve
            // dos períodos como un desvío de uno.
            const auto p = static_cast<std::int64_t>(periodoNs);
            const std::int64_t desvio = (static_cast<std::int64_t>(envioNs - anteriorNs_) + p / 2) % p - p / 2;
            const std::int64_t abs = desvio < 0 ? -desvio : desvio;
            temblorNs_ += (abs - temblorNs_) / 8;
        }
        anteriorNs_ = envioNs;
    }
    /// Cuánto antes del esperado conviene despertarse.
    Reloj::duration adelanto(std::uint64_t periodoNs) const {
        const std::int64_t a = std::min<std::int64_t>(2 * temblorNs_, static_cast<std::int64_t>(periodoNs / 4));
        return std::chrono::duration_cast<Reloj::duration>(std::chrono::nanoseconds(std::max<std::int64_t>(0, a)));
    }

   private:
    std::uint64_t anteriorNs_ = 0;
    std::int64_t temblorNs_ = 0;
};

/// Cuándo volver a mirar. `ultimoNs`: cuándo publicó el productor lo último (0 = nunca);
/// `periodoNs`: cada cuánto publica (0 = no se sabe); `pasoQuieto`: el paso si lleva quieto
/// más de kQuieto; `adelanto`: cuánto antes del esperado (Temblor).
inline Reloj::time_point proxima(Reloj::time_point ahora, std::uint64_t ultimoNs, std::uint64_t periodoNs,
                                 Reloj::duration pasoQuieto, Reloj::duration adelanto = Reloj::duration::zero()) {
    if (ultimoNs == 0 || periodoNs == 0) return ahora + kSinRitmo;
    const Reloj::time_point esperado = enReloj(ultimoNs + periodoNs) - adelanto + kMargen;
    if (ahora < esperado) return esperado;
    if (ahora - enReloj(ultimoNs) > kQuieto) return ahora + pasoQuieto;
    // Recién pasado el esperado, de a 1 ms (lo normal es que esté por llegar); cuanto más
    // atrasado, más largo el paso (un productor que bajó el ritmo no despierta de más).
    return ahora + std::clamp<Reloj::duration>((ahora - esperado) / 2, kPasoMin, kPasoMax);
}

/// El período medido de lo que publica un productor: media móvil de los intervalos entre
/// publicaciones (1/8 de peso al nuevo), sin los huecos de más de kQuieto (una pausa no es
/// un ritmo). `anteriorNs` 0 = la primera publicación.
inline std::uint64_t periodoMedido(std::uint64_t periodoNs, std::uint64_t anteriorNs, std::uint64_t ahoraNs) {
    if (anteriorNs == 0 || ahoraNs <= anteriorNs) return periodoNs;
    const std::uint64_t dt = ahoraNs - anteriorNs;
    if (dt > static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(kQuieto).count()))
        return periodoNs;
    if (periodoNs == 0) return dt;
    const auto p = static_cast<std::int64_t>(periodoNs);
    return static_cast<std::uint64_t>(p + (static_cast<std::int64_t>(dt) - p) / 8);
}

} // namespace ba::ipc::mirada
