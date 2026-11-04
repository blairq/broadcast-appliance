// ============================================================================
// src/motor/Trabajos.cpp — ver Trabajos.hpp
// ============================================================================
#include "Trabajos.hpp"

#include "platform/Hilo.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ba::motor {

struct FilaDeTrabajo::Estado {
    std::mutex m;
    std::condition_variable cv; ///< avisa cuando `pendientes` baja
    std::deque<std::function<void()>> trabajos;
    std::size_t pendientes = 0; ///< encargados + el que corre
    bool programada = false;    ///< está en la lista de los hilos o un hilo la está corriendo
};

namespace {

using Estado = FilaDeTrabajo::Estado;

/// La fila que corre en este hilo (para que `esperar` desde adentro no se trabe).
thread_local const Estado* t_filaActual = nullptr;

class Hilos {
   public:
    /// Vive lo que el proceso (nunca se destruye: un hilo que termina un trabajo al salir
    /// no encuentra el candado destruido).
    static Hilos& el() {
        static auto* h = new Hilos;
        return *h;
    }

    void poner(std::shared_ptr<Estado> e) {
        std::lock_guard<std::mutex> l(m_);
        filas_.push_back(std::move(e));
        // Un libre, AVISADO (queda contado: dos encargos seguidos no despiertan al mismo); si
        // no hay ninguno libre, uno más, hasta el tope. Si el tope está ocupado, espera turno.
        if (libres_ > 0) {
            --libres_;
            ++avisos_;
            cv_.notify_one();
        } else if (vivos_ < tope_) {
            ++vivos_;
            maximo_ = std::max(maximo_, vivos_);
            std::thread([this]() { trabajar(); }).detach();
        }
    }

    HilosDeTrabajo cuantos() {
        std::lock_guard<std::mutex> l(m_);
        return {vivos_, ocupados_, maximo_, filas_.size()};
    }

    void ponerTamanos(int base, int tope) {
        std::lock_guard<std::mutex> l(m_);
        base_ = std::clamp(base, 1, 8);
        tope_ = std::clamp(tope, base_, 64); // los que sobran se van al vencer su espera
    }

    void ponerEspera(int ms) {
        std::lock_guard<std::mutex> l(m_);
        espera_ = std::chrono::milliseconds(std::clamp(ms, 50, 60000));
    }

   private:
    void trabajar() {
        ba::platform::nombrarHilo("motor-trabajo");
        std::unique_lock<std::mutex> l(m_);
        for (;;) {
            if (filas_.empty()) {
                ++libres_;
                if (cv_.wait_for(l, espera_, [this]() { return avisos_ > 0; })) {
                    --avisos_; // lo avisó poner (que ya lo descontó de los libres)
                    continue;
                }
                --libres_;
                // Sin trabajo un rato y sobrando: se va (quedan los de base).
                if (vivos_ > base_) {
                    --vivos_;
                    return;
                }
                continue;
            }
            std::shared_ptr<Estado> e = std::move(filas_.front());
            filas_.pop_front();
            ++ocupados_;
            l.unlock();
            correrUno(e);
            l.lock();
            --ocupados_;
        }
    }

    /// Uno de la fila; si quedan, la fila vuelve al final (las otras no esperan a una que
    /// tiene muchos encargados) y sigue en orden: nadie más la toma mientras tanto.
    void correrUno(const std::shared_ptr<Estado>& e) {
        std::function<void()> f;
        {
            std::lock_guard<std::mutex> le(e->m);
            if (e->trabajos.empty()) { // no pasa: por las dudas, que no quede marcada
                e->programada = false;
                e->cv.notify_all();
                return;
            }
            f = std::move(e->trabajos.front());
            e->trabajos.pop_front();
        }
        t_filaActual = e.get();
        try {
            if (f) f();
        } catch (...) {
            // Como el futuro de antes: lo que tira un trabajo no voltea al que sigue.
        }
        t_filaActual = nullptr;
        f = nullptr; // lo que capturaba, fuera antes de avisar que terminó
        bool otraVez = false;
        {
            std::lock_guard<std::mutex> le(e->m);
            --e->pendientes;
            otraVez = !e->trabajos.empty();
            if (!otraVez) e->programada = false;
            e->cv.notify_all();
        }
        if (otraVez) poner(e);
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Estado>> filas_;
    int vivos_ = 0, ocupados_ = 0, maximo_ = 0;
    int libres_ = 0; ///< esperando trabajo y sin avisar
    int avisos_ = 0; ///< avisados por poner que todavía no se despertaron
    int base_ = 2, tope_ = 16;
    std::chrono::milliseconds espera_{5000}; ///< sin trabajo esto, el que sobra se va
};

} // namespace

FilaDeTrabajo::FilaDeTrabajo() : e_(std::make_shared<Estado>()) {}

FilaDeTrabajo::~FilaDeTrabajo() { esperar(); }

void FilaDeTrabajo::encargar(std::function<void()> f) {
    bool poner = false;
    {
        std::lock_guard<std::mutex> l(e_->m);
        e_->trabajos.push_back(std::move(f));
        ++e_->pendientes;
        if (!e_->programada) {
            e_->programada = true;
            poner = true;
        }
    }
    if (poner) Hilos::el().poner(e_);
}

void FilaDeTrabajo::esperar() {
    if (t_filaActual == e_.get()) return; // desde un trabajo de esta fila: se trabaría
    std::unique_lock<std::mutex> l(e_->m);
    e_->cv.wait(l, [this]() { return e_->pendientes == 0; });
}

std::size_t FilaDeTrabajo::pendientes() const {
    std::lock_guard<std::mutex> l(e_->m);
    return e_->pendientes;
}

HilosDeTrabajo hilosDeTrabajo() { return Hilos::el().cuantos(); }

void ponerHilosDeTrabajo(int base, int tope) { Hilos::el().ponerTamanos(base, tope); }

void ponerEsperaDeHilosDeTrabajo(int ms) { Hilos::el().ponerEspera(ms); }

} // namespace ba::motor
