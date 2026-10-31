// src/app/emision/TurnoMotor.cpp — ver el .hpp

#include "emision/TurnoMotor.hpp"

#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include <QtGlobal>

#include <chrono>
#include <set>

namespace ba::app {

namespace {
thread_local const char* t_participa = nullptr; ///< el nombre del hilo, si participa
thread_local bool t_porVuelta = false;           ///< participa por vuelta (la interfaz)
thread_local const TurnoMotor* t_vuelta = nullptr; ///< el turno que tiene por la vuelta

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

/// Una sola línea por lugar (para que el registro diga qué falta sin llenarse).
bool primeraVez(std::mutex& mu, std::set<std::string>& vistos, const std::string& lugar) {
    const std::lock_guard<std::mutex> l(mu);
    return vistos.insert(lugar).second;
}
} // namespace

std::string TurnoMotor::lugar(const std::source_location& l) {
    // «bool ba::app::BroadcastEngine::addScene(const std::string &)» → «BroadcastEngine::addScene».
    std::string f = l.function_name();
    if (const auto p = f.find('('); p != std::string::npos) f.resize(p);
    if (const auto e = f.rfind(' '); e != std::string::npos) f.erase(0, e + 1);
    for (const char* ns : {"ba::app::", "ba::"})
        if (f.rfind(ns, 0) == 0) f.erase(0, std::char_traits<char>::length(ns));
    return f.empty() ? std::string(l.file_name()) : f;
}

TurnoMotor::Guarda::Guarda(const TurnoMotor& t, std::source_location donde) {
    if (t_participa == nullptr) return;
    if (t_porVuelta) {
        t.entrarPorVuelta(donde);
        return;
    }
    t_ = &t;
    t.tomar(donde);
}

void TurnoMotor::participar(const char* nombre) {
    soltarVuelta();
    t_porVuelta = false;
    t_participa = nombre != nullptr ? nombre : "?";
}

void TurnoMotor::participarPorVuelta(const char* nombre) {
    t_participa = nombre != nullptr ? nombre : "?";
    t_porVuelta = true;
}

bool TurnoMotor::participa() { return t_participa != nullptr; }

void TurnoMotor::soltarVuelta() {
    if (t_vuelta == nullptr) return;
    const TurnoMotor* t = t_vuelta;
    t_vuelta = nullptr;
    t->soltar();
}

void TurnoMotor::entrarDirecto(std::source_location donde) const {
    if (t_participa != nullptr && t_porVuelta) entrarPorVuelta(donde);
}

void TurnoMotor::entrarPorVuelta(const std::source_location& donde) const {
    if (t_vuelta == this) return; // ya lo tiene esta vuelta
    tomar(donde);
    t_vuelta = this;
    anotarDirecta(donde);
}

void TurnoMotor::anotarDirecta(const std::source_location& donde) const {
    const std::string l = lugar(donde);
    ++directasTotal_;
    bool nuevo = false;
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        nuevo = ++directas_[l] == 1;
    }
    ba::diag::Metricas::proceso()
        .contador("ba_motor_directas_total",
                  QT_TRANSLATE_NOOP("metricas", "Veces que la interfaz entró directo al motor (sin la ventanilla), por lugar"),
                  {{"lugar", l}})
        .sumar();
    // La primera vez de cada lugar, al registro: lo que falta migrar a la ventanilla (F3-03).
    if (nuevo)
        ba::diag::registro().linea(ba::diag::Nivel::Info, "motor", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("motor", "la interfaz entró directo al motor: %1 (%2:%3)"),
                                                    {l, std::string(donde.file_name()), std::to_string(donde.line())}));
}

void TurnoMotor::anotarEntrada() const {
    if (profundidad_++ == 0) {
        dueno_.store(std::this_thread::get_id());
        quien_.store(t_participa != nullptr ? t_participa : "otro");
    }
}

void TurnoMotor::tomar(std::source_location donde) const {
    if (!mu_.try_lock()) {
        const std::int64_t desde = ahoraUs();
        mu_.lock();
        const std::int64_t us = ahoraUs() - desde;
        anotarEspera(us, esperas_, esperaMaxUs_);
        // El dueño no espera a la interfaz (toma con tope): si igual esperó, que se sepa dónde.
        static std::mutex mu;
        static std::set<std::string> vistos;
        if (us > 50000 && t_participa != nullptr && !t_porVuelta && primeraVez(mu, vistos, lugar(donde)))
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("motor", "%1 esperó el turno del motor %2 ms en %3"),
                                                        {std::string(t_participa), std::to_string(us / 1000), lugar(donde)}));
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

nlohmann::json TurnoMotor::directas() const {
    nlohmann::json l = nlohmann::json::object();
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        for (const auto& [k, n] : directas_) l[k] = n;
    }
    return {{"total", directasTotal_.load()}, {"lugares", std::move(l)}};
}

nlohmann::json TurnoMotor::estado() const {
    const char* q = quien_.load();
    return {{"quien", q != nullptr ? q : ""},
            {"esperas", esperas_.load()},
            {"espera_max_ms", static_cast<double>(esperaMaxUs_.load()) / 1000.0},
            {"directas", directas()}};
}

} // namespace ba::app
