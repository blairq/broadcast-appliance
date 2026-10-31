// ============================================================================
// src/app/emision/PintorAire.cpp — ver PintorAire.hpp
// ============================================================================
#include "emision/PintorAire.hpp"

#include "platform/Hilo.hpp"

namespace ba::app::fuentes {

PintorAire::PintorAire(std::string nombre, std::chrono::milliseconds periodo, std::function<void()> pintar)
    : nombre_(std::move(nombre)), periodo_(periodo), pintar_(std::move(pintar)) {
    hilo_ = std::thread([this]() { bucle(); });
}

PintorAire::~PintorAire() {
    {
        std::lock_guard<std::mutex> l(mu_);
        fin_ = true;
    }
    cv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

void PintorAire::despertar() {
    {
        std::lock_guard<std::mutex> l(mu_);
        despierto_ = true;
    }
    cv_.notify_all();
}

void PintorAire::bucle() {
    ba::platform::nombrarHilo(nombre_);
    auto siguiente = std::chrono::steady_clock::now();
    for (;;) {
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait_until(l, siguiente, [this]() { return fin_ || despierto_; });
            if (fin_) return;
            despierto_ = false;
        }
        // El próximo, desde ahora: una vuelta lenta no se recupera pintando de más.
        siguiente = std::chrono::steady_clock::now() + periodo_;
        pintar_();
    }
}

} // namespace ba::app::fuentes
