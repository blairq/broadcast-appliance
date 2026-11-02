// ============================================================================
// src/gst-plugins/ipc/comun.cpp — ver comun.hpp
// ============================================================================

#include "comun.hpp"

#include "platform/Hilo.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::ipc_gst {

namespace {

class Lector {
   public:
    Lector(const char* nombre, std::chrono::steady_clock::duration periodo, bool critico)
        : nombre_(nombre), periodo_(periodo), critico_(critico) {}

    void registrar(const std::shared_ptr<Lectura>& l) {
        l->activa = true;
        std::lock_guard<std::mutex> g(mu_);
        if (std::find(lista_.begin(), lista_.end(), l) == lista_.end()) lista_.push_back(l);
        // El hilo arranca con la primera y no se va (vive lo que el proceso: al salir no se
        // lo espera, podría estar empujando en un pipeline que se está desarmando).
        if (!andando_) {
            andando_ = true;
            std::thread([this]() { bucle(); }).detach();
        }
    }

    void quitar(const std::shared_ptr<Lectura>& l) {
        {
            std::lock_guard<std::mutex> g(mu_);
            lista_.erase(std::remove(lista_.begin(), lista_.end(), l), lista_.end());
        }
        l->activa = false; // sin esperar: ver comun.hpp
    }

   private:
    void bucle() {
        ba::platform::nombrarHilo(nombre_);
#if defined(__APPLE__)
        if (critico_) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
        if (critico_) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        // Una vuelta por período, sin correr para alcanzar las perdidas (cada vuelta lee
        // todo lo que haya: no se pierde nada por saltear una).
        ba::platform::Ritmo ritmo(periodo_);
        std::vector<std::shared_ptr<Lectura>> vuelta;
        for (;;) {
            {
                std::lock_guard<std::mutex> g(mu_);
                vuelta = lista_;
            }
            for (const auto& l : vuelta) {
                std::lock_guard<std::mutex> ll(l->mu);
                if (l->activa) l->leer();
            }
            vuelta.clear(); // las que se quitaron se van acá, no en la próxima vuelta
            ritmo.esperar();
        }
    }

    const char* nombre_;
    const std::chrono::steady_clock::duration periodo_;
    const bool critico_;
    std::mutex mu_;
    std::vector<std::shared_ptr<Lectura>> lista_; ///< bajo mu_
    bool andando_ = false;                        ///< bajo mu_
};

Lector& lector(Medio m) {
    // Sin destruir al salir (el hilo no se espera: ver registrar).
    static Lector* audio = new Lector("anillos-audio", std::chrono::milliseconds(5), true);
    static Lector* video = new Lector("anillos-video", std::chrono::microseconds(16667), false);
    return m == Medio::Audio ? *audio : *video;
}

} // namespace

void registrar(Medio m, const std::shared_ptr<Lectura>& l) { lector(m).registrar(l); }
void quitar(Medio m, const std::shared_ptr<Lectura>& l) { lector(m).quitar(l); }

} // namespace ba::ipc_gst
