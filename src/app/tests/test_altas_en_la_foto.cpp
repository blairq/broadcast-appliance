// ============================================================================
// src/app/tests/test_altas_en_la_foto.cpp — las altas esperan la vuelta del motor (arranque/AltasEnLaFoto.hpp)
//
//   1. Un alta sola espera una vuelta (la respuesta llega con el alta en la foto).
//   2. Varias en un lote (subir el cupo de invitados, copiar una escena) esperan UNA, al
//      cerrar el lote más externo; los lotes anidados no suman esperas.
//   3. Desde el hilo de la interfaz no se espera nunca (ni sola ni en lote).
//   4. El lote es por hilo: el de otro hilo no se lleva las altas de este.
//   5. La foto: lo que publica el «motor» de mentira en su vuelta ya está al volver.
// ============================================================================
#include "arranque/AltasEnLaFoto.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using ba::app::AltasEnLaFoto;

namespace {
int fallas = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        if (!(c)) {                                                         \
            std::printf("FALLA: %s (%s, linea %d)\n", msg, #c, __LINE__); \
            ++fallas;                                                       \
        } else {                                                            \
            std::printf("ok: %s\n", msg);                                   \
        }                                                                   \
    } while (0)

/// Un motor de mentira: las altas se encolan y su hilo las corre en su vuelta, y publica la
/// foto (los nombres) antes de soltar a los que esperan (como Motor::esperarCola).
class MotorDeMentira {
   public:
    MotorDeMentira() : hilo_([this]() { correr(); }) {}
    ~MotorDeMentira() {
        {
            const std::lock_guard<std::mutex> l(mu_);
            seguir_ = false;
        }
        cv_.notify_all();
        hilo_.join();
    }
    void agregar(const std::string& n) {
        const std::lock_guard<std::mutex> l(mu_);
        cola_.push_back(n);
    }
    void esperarCola() {
        std::unique_lock<std::mutex> l(mu_);
        const std::uint64_t quiero = ++pedidas_;
        cv_.notify_all();
        cv_.wait(l, [&]() { return vueltas_ >= quiero || !seguir_; });
    }
    std::set<std::string> foto() const {
        const std::lock_guard<std::mutex> l(mu_);
        return foto_;
    }

   private:
    void correr() {
        std::unique_lock<std::mutex> l(mu_);
        while (seguir_) {
            cv_.wait_for(l, std::chrono::milliseconds(20), [&]() { return !seguir_ || pedidas_ > vueltas_; });
            for (const std::string& n : cola_) foto_.insert(n);
            cola_.clear();
            vueltas_ = pedidas_;
            cv_.notify_all();
        }
    }
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> cola_;
    std::set<std::string> foto_;
    std::uint64_t pedidas_ = 0, vueltas_ = 0;
    bool seguir_ = true;
    std::thread hilo_;
};
} // namespace

int main() {
    // La «interfaz»: un hilo marcado (como motor::marcarHiloInterfaz).
    std::atomic<std::thread::id> interfaz{};
    MotorDeMentira motor;
    const AltasEnLaFoto altas([&motor]() { motor.esperarCola(); },
                              [&interfaz]() { return interfaz.load() != std::this_thread::get_id(); });

    // 1. Un alta sola.
    motor.agregar("mic");
    altas.alta();
    CHECK(altas.esperas() == 1, "un alta espera una vuelta");
    CHECK(motor.foto().count("mic") == 1, "al volver, el alta ya está en la foto");

    // 2. Un lote (con otro adentro): una sola espera, al cerrar el de afuera.
    {
        const AltasEnLaFoto::Lote lote(altas);
        for (int i = 1; i <= 4; ++i) {
            motor.agregar("invitado" + std::to_string(i));
            altas.alta();
        }
        {
            const AltasEnLaFoto::Lote adentro(altas); // copyScene → addSource adentro de otro lote
            motor.agregar("camara");
            altas.alta();
        }
        CHECK(altas.esperas() == 1, "adentro del lote no se espera (tampoco al cerrar uno anidado)");
    }
    CHECK(altas.esperas() == 2, "al cerrar el lote, UNA espera para todas sus altas");
    {
        const auto f = motor.foto();
        CHECK(f.count("invitado1") + f.count("invitado4") + f.count("camara") == 3, "y todas ya están en la foto");
    }
    { const AltasEnLaFoto::Lote vacio(altas); }
    CHECK(altas.esperas() == 2, "un lote sin altas no espera");

    // 3. Desde la interfaz, nunca.
    interfaz = std::this_thread::get_id();
    motor.agregar("desde-la-interfaz");
    altas.alta();
    {
        const AltasEnLaFoto::Lote lote(altas);
        altas.alta();
    }
    CHECK(altas.esperas() == 2, "el hilo de la interfaz no espera al motor (ni sola ni en lote)");
    interfaz = std::thread::id{};

    // 4. El lote es por hilo: un alta en otro hilo, con un lote abierto en este, espera igual.
    {
        const AltasEnLaFoto::Lote lote(altas);
        std::thread([&]() {
            motor.agregar("otro-hilo");
            altas.alta();
        }).join();
        CHECK(altas.esperas() == 3, "el lote de un hilo no frena las altas de otro");
    }
    CHECK(altas.esperas() == 3, "y el lote de este, sin altas propias, no espera al cerrar");

    // 5. Sin quien espere (antes de que haya motor): no hace nada.
    const AltasEnLaFoto sinMotor({}, {});
    sinMotor.alta();
    CHECK(sinMotor.esperas() == 0, "sin motor, un alta no espera");

    if (fallas == 0) std::printf("== test_altas_en_la_foto: OK ==\n");
    return fallas == 0 ? 0 : 1;
}
