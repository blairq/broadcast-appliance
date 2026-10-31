// src/app/emision/TurnoMotor.cpp — ver el .hpp

#include "emision/TurnoMotor.hpp"

#include "diag/Metricas.hpp"

#include <QtGlobal>

#include <chrono>

namespace ba::app {

namespace {
thread_local const char* t_participa = nullptr; ///< el nombre del hilo, si participa

std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Lo que se esperó para entrar (solo cuando hubo que esperar: entrar libre no cuesta nada).
void anotarEspera(std::int64_t us, std::atomic<std::uint64_t>& esperas, std::atomic<std::int64_t>& maxUs) {
    ++esperas;
    std::int64_t m = maxUs.load();
    while (us > m && !maxUs.compare_exchange_weak(m, us)) {
    }
    ba::diag::Metricas::proceso()
        .histograma("ba_motor_turno_espera_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que se esperó el turno del motor (la interfaz o el plano de control adentro), ms"),
                    {{"hilo", t_participa != nullptr ? t_participa : "otro"}})
        .observar(static_cast<double>(us) / 1000.0);
}
} // namespace

void TurnoMotor::participar(const char* nombre) { t_participa = nombre != nullptr ? nombre : "?"; }

bool TurnoMotor::participa() { return t_participa != nullptr; }

void TurnoMotor::anotarEntrada() const {
    if (profundidad_++ == 0) {
        dueno_.store(std::this_thread::get_id());
        quien_.store(t_participa != nullptr ? t_participa : "otro");
    }
}

void TurnoMotor::tomar() const {
    if (!mu_.try_lock()) {
        const std::int64_t desde = ahoraUs();
        mu_.lock();
        anotarEspera(ahoraUs() - desde, esperas_, esperaMaxUs_);
    }
    anotarEntrada();
}

bool TurnoMotor::tomarHasta(int ms) const {
    if (!mu_.try_lock()) {
        const std::int64_t desde = ahoraUs();
        if (!mu_.try_lock_for(std::chrono::milliseconds(ms > 0 ? ms : 0))) return false;
        anotarEspera(ahoraUs() - desde, esperas_, esperaMaxUs_);
    }
    anotarEntrada();
    return true;
}

void TurnoMotor::soltar() const {
    if (--profundidad_ == 0) {
        quien_.store(nullptr);
        dueno_.store(std::thread::id());
    }
    mu_.unlock();
}

nlohmann::json TurnoMotor::estado() const {
    const char* q = quien_.load();
    return {{"quien", q != nullptr ? q : ""},
            {"esperas", esperas_.load()},
            {"espera_max_ms", static_cast<double>(esperaMaxUs_.load()) / 1000.0}};
}

} // namespace ba::app
