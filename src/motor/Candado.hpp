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
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>

namespace ba::motor {

/// Marca el hilo que llama como el de la interfaz (lo llama la app una vez, al
/// arrancar la interfaz). Sin marcar, solo se vigila cuánto se tiene el candado.
void marcarHiloInterfaz();
/// ¿Es este el hilo de la interfaz?
bool enHiloInterfaz();

class Candado {
   public:
    Candado(std::recursive_mutex& m, const char* donde);
    ~Candado();
    Candado(const Candado&) = delete;
    Candado& operator=(const Candado&) = delete;

   private:
    std::recursive_mutex& m_;
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
