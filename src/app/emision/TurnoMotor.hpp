// ============================================================================
// src/app/emision/TurnoMotor.hpp — el turno del motor: la interfaz y el plano de control, de a uno (F3-03)
//
// docs/arquitectura/04-plan-unificado.md, F3-03. BroadcastEngine no es seguro entre hilos,
// y tampoco lo que lleva el aire de afuera (lo que sale en AppRuntime, los conmutadores de
// escenas, el negro y la placa): era todo del hilo de la interfaz. Con el plano de control
// (arranque/PlanoControl), las acciones de aire del MCP (TAKE, grabar, transmitir, FTB,
// placa) corren en SU hilo: con la interfaz trabada (un QML pesado, un diálogo, un disco
// lento) igual se hacen. El turno es lo que deja entrar a los dos sin pisarse.
//
// QUÉ ES: quién está adentro del motor ahora. Un candado recursivo que toma cada método de
// la API de BroadcastEngine al entrar (Guarda) y cada operación compuesta de afuera
// (AppRuntime: grabar, transmitir, cortar; escenas::Conmutador: poner, take, T-bar) por todo
// lo que dura, así nadie ve una a medias. La interfaz lo tiene solo mientras está ADENTRO
// del motor: trabada en otra cosa, no lo tiene, y el control entra.
//
// QUIÉN LO TOMA: los hilos que PARTICIPAN (participar(): el dueño del motor —la interfaz o
// el bucle de la consola— y el del plano de control). Los demás no: el hilo de salidas del
// motor, el de los cuadros, los de GStreamer, el pool de la foto del MCP. Lo que ellos
// tocan tiene su candado o se publica como foto (fotoSalidas, programaVideo, los picos), y
// quien tiene el turno a veces los ESPERA (apagar espera al hilo de salidas; soltar una
// vista espera a que termine su aviso en curso): si lo tomaran, sería un abrazo mortal.
//
// EL CONTROL NUNCA ESPERA A LA INTERFAZ: lo toma con tope (tomarHasta) y, si no se lo dan
// (la interfaz trabada ADENTRO del motor, un rearme de varios segundos), contesta «el motor
// está ocupado» y no hace nada después. La interfaz sí espera al control (lo que el
// control hace adentro es corto: encolar una salida, cortar una escena).
//
// Lo que falta (F3-03): que el motor sea DEL plano de control —la interfaz le pide y lee
// fotos— en vez de turnarse con ella; ahí el turno sobra.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

namespace ba::app {

class TurnoMotor {
   public:
    /// Este hilo participa del turno (para siempre): el dueño del motor y el plano de
    /// control. `nombre` («interfaz», «control») es lo que dice estado() de quién lo tiene.
    static void participar(const char* nombre);
    static bool participa();

    /// Lo toma al construirse y lo suelta al irse, si el hilo participa (si no, nada).
    class Guarda {
       public:
        explicit Guarda(const TurnoMotor& t) : t_(participa() ? &t : nullptr) {
            if (t_ != nullptr) t_->tomar();
        }
        ~Guarda() {
            if (t_ != nullptr) t_->soltar();
        }
        Guarda(const Guarda&) = delete;
        Guarda& operator=(const Guarda&) = delete;

       private:
        const TurnoMotor* t_;
    };

    /// Lo toma (esperando lo que haga falta). Recursivo: el que lo tiene vuelve a entrar.
    void tomar() const;
    /// Lo toma si se lo dan antes de `ms`; false si no (y no lo tiene).
    bool tomarHasta(int ms) const;
    void soltar() const;
    /// ¿El que llama lo tiene?
    bool loTengo() const { return dueno_.load() == std::this_thread::get_id(); }

    /// {quien (el nombre del hilo que lo tiene, "" si nadie), esperas (las veces que alguien
    /// esperó), espera_max_ms}: para el diagnóstico y el test.
    nlohmann::json estado() const;

   private:
    void anotarEntrada() const;

    mutable std::recursive_timed_mutex mu_;
    mutable std::atomic<std::thread::id> dueno_{};
    mutable std::atomic<const char*> quien_{nullptr};
    mutable int profundidad_ = 0; ///< solo la toca el que lo tiene
    mutable std::atomic<std::uint64_t> esperas_{0};
    mutable std::atomic<std::int64_t> esperaMaxUs_{0};
};

} // namespace ba::app
