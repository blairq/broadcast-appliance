// ============================================================================
// src/gst-plugins/ipc/comun.cpp — ver comun.hpp
// ============================================================================

#include "comun.hpp"
#include "tiempos.hpp"

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

    Vueltas vueltas() const { return {n_.load(), ns_.load(), tarde_.load(), muyTarde_.load(), maxNs_.load()}; }

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
        if (critico_) prioridadDeAudio();
        diferirAvisos(true); // ver comun.hpp: los repartidores, una vez por vuelta
        // Una vuelta por período, sin correr para alcanzar las perdidas (cada vuelta lee
        // todo lo que haya: no se pierde nada por saltear una).
        ba::platform::Ritmo ritmo(periodo_);
        std::vector<std::shared_ptr<Lectura>> vuelta;
        for (;;) {
            {
                std::lock_guard<std::mutex> g(mu_);
                vuelta = lista_;
            }
            const auto inicio = std::chrono::steady_clock::now();
            const std::uint64_t t0 = tiempos::activos() ? tiempos::ahora() : 0;
            for (const auto& l : vuelta) {
                std::lock_guard<std::mutex> ll(l->mu);
                if (l->activa) l->leer();
            }
            despertarPendientes();
            if (!vuelta.empty()) {
                const auto dur = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - inicio).count());
                const auto per = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(periodo_).count());
                n_.fetch_add(1, std::memory_order_relaxed);
                ns_.fetch_add(dur, std::memory_order_relaxed);
                if (dur > per) tarde_.fetch_add(1, std::memory_order_relaxed);
                if (dur * 5 > per * 12) muyTarde_.fetch_add(1, std::memory_order_relaxed);
                if (dur > maxNs_.load(std::memory_order_relaxed)) maxNs_.store(dur, std::memory_order_relaxed);
            }
            if (t0 != 0 && !vuelta.empty()) {
                tiempos::vuelta.anotar(tiempos::ahora() - t0);
                tiempos::volcarCada5s();
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
    std::atomic<std::uint64_t> n_{0}, ns_{0}, tarde_{0}, muyTarde_{0}, maxNs_{0}; ///< ver Vueltas
};

Lector& lector(Medio m) {
    // Sin destruir al salir (el hilo no se espera: ver registrar).
    static Lector* audio = new Lector("anillos-audio", std::chrono::milliseconds(5), true);
    static Lector* video = new Lector("anillos-video", std::chrono::microseconds(16667), false);
    return m == Medio::Audio ? *audio : *video;
}

} // namespace

void prioridadDeAudio() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

void registrar(Medio m, const std::shared_ptr<Lectura>& l) { lector(m).registrar(l); }
Vueltas vueltas(Medio m) { return lector(m).vueltas(); }
void quitar(Medio m, const std::shared_ptr<Lectura>& l) { lector(m).quitar(l); }

} // namespace ba::ipc_gst
