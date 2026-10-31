// ============================================================================
// src/app/emision/TurnoMotor.hpp — quién está adentro del motor ahora (F3-03)
//
// docs/arquitectura/04-plan-unificado.md, F3-03; el patrón, en
// docs/arquitectura/05-motor-del-plano.md. EL MOTOR ES DEL PLANO DE CONTROL: AppRuntime,
// BroadcastEngine y la configuración (cfg_) los usa su hilo («control», el dueño), que corre
// el reloj del motor y las órdenes que le encargan. La interfaz le PIDE (órdenes encoladas,
// emision/VentanillaMotor.hpp) y LEE LA FOTO que el dueño publica (emision/FotoMotor.hpp).
//
// El turno es lo que queda para lo que la interfaz todavía toca directo (las áreas que no
// pasaron a la ventanilla: la mezcla, la imagen, la asamblea, la sala…): un candado
// recursivo que toman los hilos que PARTICIPAN, así nadie ve el motor ni la configuración a
// medio cambiar.
//
//   - EL DUEÑO (el control) lo toma por cada cosa que hace: cada vuelta del reloj del motor,
//     cada orden de la agenda (PlanoControl::encolarConTurno), cada herramienta MCP del
//     control. Siempre CON TOPE (tomarHasta): si la interfaz lo tiene, la orden espera en la
//     agenda y la vuelta se saltea; el control nunca se queda esperando a la interfaz.
//   - LA INTERFAZ (participarPorVuelta) lo toma la primera vez que entra al motor o a la
//     configuración en un evento (un clic, un temporizador, un dibujo de QML) y lo tiene
//     hasta que ese evento termina (soltarVuelta, lo llama el bucle de la interfaz: al
//     volver de cada evento y antes de dormir). Así lo que lee de cfg_ por referencia no
//     cambia debajo de sus pies. Cada vez que entra es una LLAMADA DIRECTA: se cuenta por
//     lugar (directas(), `ba_motor_directas_total{lugar}`, una línea en el registro la
//     primera vez de cada lugar) para que las próximas etapas sepan qué falta migrar.
//   - La consola (sin interfaz) es el dueño y lo toma por cada llamada (participar).
//
// QUIÉN NO: el hilo de salidas del motor, el de los cuadros, los de GStreamer, el pool de la
// foto del MCP. Lo que ellos tocan tiene su candado o se publica como foto (fotoSalidas,
// programaVideo, los picos, las vistas), y quien tiene el turno a veces los ESPERA (apagar
// espera al hilo de salidas): si lo tomaran, sería un abrazo mortal.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace ba::app {

class TurnoMotor {
   public:
    /// Este hilo participa del turno por llamada: el dueño del motor (el control, la consola).
    /// `nombre` («control», «dueño») es lo que dice estado() de quién lo tiene. Si el hilo
    /// participaba por vuelta, suelta lo que tenía y deja de hacerlo.
    static void participar(const char* nombre);
    /// Este hilo (la interfaz) participa POR VUELTA: ver arriba.
    static void participarPorVuelta(const char* nombre);
    static bool participa();
    /// Suelta el turno de la vuelta, si este hilo lo tiene (fin de un evento de la interfaz).
    static void soltarVuelta();

    /// Lo toma al construirse y lo suelta al irse, si el hilo participa por llamada; en el
    /// hilo por vuelta, lo toma para el resto de la vuelta (y cuenta la directa en `donde`).
    class Guarda {
       public:
        explicit Guarda(const TurnoMotor& t, std::source_location donde = std::source_location::current());
        ~Guarda() {
            if (t_ != nullptr) t_->soltar();
        }
        Guarda(const Guarda&) = delete;
        Guarda& operator=(const Guarda&) = delete;

       private:
        const TurnoMotor* t_ = nullptr;
    };

    /// La interfaz entra directo (AppRuntime::config()/engine() desde una vista): en el hilo
    /// por vuelta lo toma para el resto de la vuelta y cuenta la directa en `donde` (el que
    /// llamó). En los demás hilos no hace nada.
    void entrarDirecto(std::source_location donde) const;

    /// Lo toma (esperando lo que haga falta). Recursivo: el que lo tiene vuelve a entrar.
    void tomar(std::source_location donde = std::source_location::current()) const;
    /// Lo toma si se lo dan antes de `ms`; false si no (y no lo tiene).
    bool tomarHasta(int ms) const;
    void soltar() const;
    /// ¿El que llama lo tiene?
    bool loTengo() const { return dueno_.load() == std::this_thread::get_id(); }

    /// {quien (el nombre del hilo que lo tiene, "" si nadie), esperas (las veces que alguien
    /// esperó), espera_max_ms, directas: {total, lugares: {lugar: veces}}}: para el
    /// diagnóstico, el MCP (status) y el test.
    nlohmann::json estado() const;
    /// Las llamadas directas de la interfaz: {total, lugares: {lugar: veces}} (ver arriba).
    nlohmann::json directas() const;
    std::uint64_t totalDirectas() const { return directasTotal_.load(); }

    /// El lugar como se cuenta: «Clase::metodo» (sin el espacio de nombres ni los argumentos).
    static std::string lugar(const std::source_location& l);

   private:
    void anotarEntrada() const;
    void entrarPorVuelta(const std::source_location& donde) const;
    void anotarDirecta(const std::source_location& donde) const;

    mutable std::recursive_timed_mutex mu_;
    mutable std::atomic<std::thread::id> dueno_{};
    mutable std::atomic<const char*> quien_{nullptr};
    mutable int profundidad_ = 0; ///< solo la toca el que lo tiene
    mutable std::atomic<std::uint64_t> esperas_{0};
    mutable std::atomic<std::int64_t> esperaMaxUs_{0};
    mutable std::atomic<std::uint64_t> directasTotal_{0};
    mutable std::mutex directasMu_;
    mutable std::map<std::string, std::uint64_t> directas_;
};

} // namespace ba::app
