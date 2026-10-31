// ============================================================================
// src/motor/Candado.hpp — el candado del motor, con vigía
//
// La interfaz no puede esperar al motor: una espera larga es la app trabada, y
// en macOS puede ser una traba para siempre (GL necesita el hilo principal para
// soltar sus buffers, y si el hilo principal está esperando un cambio de estado
// de GStreamer, se esperan los dos). Mientras eso se termina de separar, esto
// lo MIDE: cada vez que el hilo de la interfaz espera el candado (o lo tiene)
// más de 50 ms queda en el registro con el lugar exacto; y cualquier hilo que
// lo tenga más de 200 ms, también. Un aviso por lugar cada 10 s.
//
// Es un mutex COMÚN (F3-14; era recursive_mutex): una reentrada (el mismo hilo que
// ya lo tiene lo vuelve a pedir) no pasa callada, es un error con el lugar en el
// registro y el proceso se cae con su volcado (con un mutex común sería un abrazo
// para siempre). Lo que corre «con el candado tomado» (las órdenes de la cola, los
// ...Ya del motor) no lo vuelve a pedir: lo exige (BA_CON_CANDADO), y si falta queda
// como error en el registro (y se cuenta: faltasDelCandado, para los tests).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace ba::motor {

/// Marca el hilo que llama como el de la interfaz (lo llama la app una vez, al
/// arrancar la interfaz). Sin marcar, solo se vigila cuánto se tiene el candado.
void marcarHiloInterfaz();
/// ¿Es este el hilo de la interfaz?
bool enHiloInterfaz();
/// Cuántas veces la interfaz tomó el candado del motor fuera de arrancarlo y
/// apagarlo (iniciar/detener). Tiene que ser cero: lo mira el test.
int tomasDeLaInterfaz();
/// Estricto (la app con --test): cada toma de la interfaz queda como ERROR en el
/// registro, aunque sea de un instante.
void vigiaEstricto(bool si);

/// El mutex del motor: común, y sabe qué hilo lo tiene (para nombrar una reentrada en
/// vez de abrazarse, y para exigirlo donde se corre con él tomado).
class MutexMotor {
   public:
    void lock() {
        m_.lock();
        dueno_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    void unlock() {
        dueno_.store(std::thread::id(), std::memory_order_relaxed);
        m_.unlock();
    }
    /// ¿Lo tiene este hilo? (Solo el hilo que lo tiene puede ver su propio id: sin carrera.)
    bool esMio() const { return dueno_.load(std::memory_order_relaxed) == std::this_thread::get_id(); }

   private:
    std::mutex m_;
    std::atomic<std::thread::id> dueno_{};
};

/// Cuántas veces algo que tiene que correr con el candado tomado corrió sin él. Cero.
int faltasDelCandado();
/// Lo que corre con el candado tomado lo exige (no lo pide: ver arriba).
void exigirCandado(const MutexMotor& m, const char* donde);

class Candado {
   public:
    Candado(MutexMotor& m, const char* donde);
    ~Candado();
    Candado(const Candado&) = delete;
    Candado& operator=(const Candado&) = delete;

   private:
    MutexMotor& m_;
    const char* donde_;
    bool interfaz_;
    std::int64_t pedidoUs_;
    std::int64_t tomadoUs_;
};

/// Un tramo con nombre de algo que corre con el candado (las vueltas del bucle
/// del motor): si tarda, dice cuál fue.
class Tramo {
   public:
    explicit Tramo(const char* nombre);
    ~Tramo();
    Tramo(const Tramo&) = delete;
    Tramo& operator=(const Tramo&) = delete;

   private:
    const char* nombre_;
    std::int64_t desdeUs_;
};

/// Avisa (con el límite de uno por lugar cada 10 s) si `ms` pasó de `umbralMs`.
void avisarDemora(const char* donde, std::int64_t ms, std::int64_t umbralMs, const char* que);

} // namespace ba::motor

/// El candado del motor con el nombre de la función que lo pide.
#define BA_CANDADO(m) ::ba::motor::Candado candado_motor_(m, __func__)
/// Esto corre con el candado del motor ya tomado (lo tomó quien llama: la cola de órdenes).
#define BA_CON_CANDADO(m) ::ba::motor::exigirCandado(m, __func__)
