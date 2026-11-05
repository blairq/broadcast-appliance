// ============================================================================
// src/app/arranque/AltasEnLaFoto.hpp — la respuesta de un alta llega con el alta ya en la foto
//
// EL CONTRATO DE LA VENTANILLA (emision/VentanillaMotor.hpp): cuando llega la respuesta de
// una orden, lo que la orden cambió ya está en la foto. Las ALTAS en el motor (agregar una
// fuente a una escena, un micrófono, el audio y el video de un invitado, renombrar una
// fuente, que en el motor es una entrada nueva) se ENCOLAN: el motor las corre en su vuelta
// (≤ 20 ms) y lo que la app saca de su foto —qué fuentes tienen audio (el canal del
// mezclador, el rack de efectos), el tamaño de lo que sale, el estado de cada entrada— no
// estaba todavía cuando la ventanilla publicaba la foto al terminar la orden. Lo primero que
// se hacía con lo recién agregado (ponerle efectos, el asistente con *_add y enseguida otra
// herramienta) decía «no tiene audio» o «no existe».
//
// `alta()` espera esa vuelta (Motor::esperarCola: la foto del motor se publica en la misma
// vuelta, antes de soltar a los que esperan). Desde el dueño (o una herramienta en su hilo),
// NUNCA desde el hilo de la interfaz: ahí no espera (`sePuede` dice false) y la foto llega
// en la vuelta siguiente, como antes.
//
// Varias altas seguidas (subir el cupo de invitados, copiar una escena con sus fuentes) van
// en un `Lote`: esperan UNA vuelta, al cerrar el lote más externo. El lote es por hilo.
//
// Sin Qt ni el motor: se prueba solo (test_altas_en_la_foto).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace ba::app {

class AltasEnLaFoto {
   public:
    /// `esperar`: la vuelta del motor (Motor::esperarCola). `sePuede`: false si el que llama
    /// no puede esperar (el hilo de la interfaz). Vacíos: no espera / siempre se puede.
    AltasEnLaFoto(std::function<void()> esperar, std::function<bool()> sePuede)
        : esperar_(std::move(esperar)), sePuede_(std::move(sePuede)) {}

    /// Hubo un alta en el motor: la espera ya (o al cerrar el lote, si hay uno abierto).
    void alta() const {
        if (enLote() > 0) {
            pendiente() = true;
            return;
        }
        esperarSiSePuede();
    }

    /// Las altas de adentro esperan una sola vez, al cerrar el más externo.
    class Lote {
       public:
        explicit Lote(const AltasEnLaFoto& a) : a_(a) { ++enLote(); }
        ~Lote() {
            if (--enLote() == 0 && pendiente()) {
                pendiente() = false;
                a_.esperarSiSePuede();
            }
        }
        Lote(const Lote&) = delete;
        Lote& operator=(const Lote&) = delete;

       private:
        const AltasEnLaFoto& a_;
    };

    /// Cuántas veces esperó la vuelta del motor (para el diagnóstico y el test).
    std::uint64_t esperas() const { return esperas_.load(); }

   private:
    void esperarSiSePuede() const {
        if (!esperar_ || (sePuede_ && !sePuede_())) return;
        esperar_();
        ++esperas_;
    }
    static int& enLote() {
        thread_local int n = 0;
        return n;
    }
    static bool& pendiente() {
        thread_local bool p = false;
        return p;
    }

    std::function<void()> esperar_;
    std::function<bool()> sePuede_;
    mutable std::atomic<std::uint64_t> esperas_{0};
};

} // namespace ba::app
