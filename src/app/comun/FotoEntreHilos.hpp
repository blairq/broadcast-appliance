// ============================================================================
// src/app/comun/FotoEntreHilos.hpp — lo que un hilo copia y otro lee (F3-03)
//
// El plano de control (arranque/PlanoControl) no puede tocar objetos de la interfaz:
// lo que necesita de ellos (la sala, los chats, las entradas para el vigía de las
// alarmas) lo copia la interfaz en su hilo, cada tanto, a una foto; el control lee la
// última copia. Con la interfaz trabada la foto queda quieta (y `edadMs` lo dice):
// lo que no puede esperar a la interfaz no se lee de una foto, se lee directo de algo
// seguro entre hilos.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>

namespace ba::app {

template <class T>
class FotoEntreHilos {
   public:
    void poner(T v) {
        const std::lock_guard<std::mutex> l(mu_);
        v_ = std::move(v);
        cuando_ = std::chrono::steady_clock::now();
        puesta_ = true;
    }
    T leer() const {
        const std::lock_guard<std::mutex> l(mu_);
        return v_;
    }
    /// Cuánto hace que se puso (-1 si nunca).
    std::int64_t edadMs() const {
        const std::lock_guard<std::mutex> l(mu_);
        if (!puesta_) return -1;
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cuando_).count();
    }

   private:
    mutable std::mutex mu_;
    T v_{};
    std::chrono::steady_clock::time_point cuando_{};
    bool puesta_ = false;
};

} // namespace ba::app
