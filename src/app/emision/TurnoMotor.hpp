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
//   - LA INTERFAZ (participarPorVuelta) NUNCA lo tiene mientras hace algo suyo:
//       · a un método del motor (o de AppRuntime) con su Guarda entra POR LLAMADA, como el
//         dueño: lo toma al entrar y lo suelta al salir. AppRuntime::engine() no lo toma
//         (cada método del motor tiene el suyo).
//       · a lo que se lee POR REFERENCIA (AppRuntime::config(), gateway(), launcher()) entra
//         por la vuelta: lo toma la primera vez y lo tiene hasta que ese evento termina
//         (soltarVuelta, lo llama el bucle de la interfaz: al volver de cada evento y antes
//         de dormir), así lo que lee de cfg_ no cambia debajo de sus pies. Y entre cada
//         oyente de la foto (VentanillaMotor::escuchar): un oyente que lo tomó no se lo
//         pasa al siguiente. Lo que lo tiene mucho se anota (la tenencia, abajo).
//     Mientras tanto el dueño no la espera: la agenda reintenta y el reloj del motor mide
//     igual lo que no necesita el turno (NivelesMotor::medir). Cada vez que entra es una
//     LLAMADA DIRECTA: se cuenta por lugar, una vez por evento (directas(),
//     `ba_motor_directas_total{lugar}`, una línea en el registro la primera vez de cada
//     lugar) para que las próximas etapas sepan qué falta migrar.
//   - La consola (sin interfaz) es el dueño y lo toma por cada llamada (participar).
//
// QUIÉN NO: el hilo de salidas del motor, el de los cuadros, los de GStreamer, el pool de la
// foto del MCP. Lo que ellos tocan tiene su candado o se publica como foto (fotoSalidas,
// programaVideo, los picos, las vistas), y quien tiene el turno a veces los ESPERA (apagar
// espera al hilo de salidas): si lo tomaran esperando, sería un abrazo mortal. Si uno de
// ellos igual entra a un método con Guarda (un error: lo que usa no es suyo), lo toma CON
// TOPE (si está libre, nadie le cambia el motor debajo; si no, sigue sin él) y la entrada
// se cuenta por lugar (estado().sin_participar, una línea en el registro la primera vez;
// con `estricto`, el modo de prueba, como error).
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
    /// Las entradas de hilos que no participan van al registro como error (--test).
    static void estricto(bool si);
    /// Suelta el turno de la vuelta, si este hilo lo tiene (fin de un evento de la interfaz,
    /// o entre dos oyentes de la foto: un punto donde nadie guarda referencias a cfg_).
    static void soltarVuelta();

    /// Lo toma al construirse y lo suelta al irse (también en el hilo por vuelta, donde
    /// además cuenta la directa en `donde`).
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

    /// La interfaz entra directo a lo que se lee POR REFERENCIA (AppRuntime::config(),
    /// gateway(), launcher() desde una vista): en el hilo por vuelta lo toma para el resto de
    /// la vuelta y cuenta la directa en `donde` (el que llamó). En los demás hilos no hace
    /// nada. Lo que dura, se anota (ver «Tenencia»).
    void entrarDirecto(std::source_location donde) const;
    /// La interfaz entra directo a algo que toma el turno POR LLAMADA (AppRuntime::engine():
    /// cada método del motor tiene su Guarda): solo se cuenta, una vez por lugar y evento.
    void contarDirecta(std::source_location donde) const;

    /// TENENCIA: cuánto tuvo la interfaz el turno por la vuelta. Desde kTenenciaLargaUs va a
    /// `ba_motor_turno_interfaz_ms{lugar}`; desde kTenenciaAvisoUs, una línea en el registro
    /// la primera vez de cada lugar (lo que frena la agenda: pasarlo a la foto o la ventanilla).
    static constexpr std::int64_t kTenenciaLargaUs = 20000;
    static constexpr std::int64_t kTenenciaAvisoUs = 250000;

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
    void anotarSinParticipar(const std::source_location& donde) const;
    void anotarTenencia(std::int64_t us) const;

    mutable std::recursive_timed_mutex mu_;
    mutable std::atomic<std::thread::id> dueno_{};
    mutable std::atomic<const char*> quien_{nullptr};
    mutable int profundidad_ = 0; ///< solo la toca el que lo tiene
    mutable std::atomic<std::uint64_t> esperas_{0};
    mutable std::atomic<std::int64_t> esperaMaxUs_{0};
    mutable std::atomic<std::uint64_t> directasTotal_{0};
    mutable std::mutex directasMu_;
    mutable std::map<std::string, std::uint64_t> directas_;
    mutable std::map<std::string, std::uint64_t> sinParticipar_; ///< (con directasMu_)
    mutable std::int64_t tenenciaMaxUs_ = 0;   ///< (con directasMu_)
    mutable std::string tenenciaMaxLugar_;     ///< (con directasMu_)
    mutable std::atomic<std::uint64_t> tenenciasLargas_{0};
};

} // namespace ba::app
