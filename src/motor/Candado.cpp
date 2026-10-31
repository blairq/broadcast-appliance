#include "Candado.hpp"

#include <gst/gst.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>

#include "Reloj.hpp" // registrar
#include "i18n/I18n.hpp"

// Marca para lupdate un texto que se traduce después (sin Qt en el motor).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::motor {
namespace {

std::atomic<std::thread::id> hiloInterfaz{};
std::atomic<int> tomas{0};
std::atomic<bool> estricto{false};
std::atomic<int> faltas{0};

/// Arrancar y apagar el motor sí corren en el hilo de la interfaz (detrás del
/// cartel de carga y al cerrar).
bool permitida(const char* donde) {
    const std::string d = donde;
    return d == "iniciar" || d == "detener";
}
std::mutex avisosM;
std::map<std::string, std::int64_t> ultimoAviso; // lugar -> cuándo se avisó (us)

} // namespace

void marcarHiloInterfaz() { hiloInterfaz.store(std::this_thread::get_id()); }

bool enHiloInterfaz() { return hiloInterfaz.load() == std::this_thread::get_id(); }

int tomasDeLaInterfaz() { return tomas.load(); }

void vigiaEstricto(bool si) { estricto = si; }

void avisarDemora(const char* donde, std::int64_t ms, std::int64_t umbralMs, const char* que) {
    if (ms <= umbralMs) return;
    const std::int64_t ahora = g_get_monotonic_time();
    {
        std::lock_guard<std::mutex> l(avisosM);
        std::int64_t& ult = ultimoAviso[std::string(que) + donde];
        if (ult != 0 && ahora - ult < 10'000'000) return;
        ult = ahora;
    }
    registrar(Nivel::Aviso, ba::i18n::llenar(ba::i18n::traducir("motor", "%1 %2 ms en %3"),
                                             {ba::i18n::traducir("motor", que), std::to_string(ms), donde}));
}

int faltasDelCandado() { return faltas.load(); }

void exigirCandado(const MutexMotor& m, const char* donde) {
    if (m.esMio()) return;
    ++faltas;
    const std::int64_t ahora = g_get_monotonic_time();
    {
        std::lock_guard<std::mutex> l(avisosM);
        std::int64_t& ult = ultimoAviso[std::string("sin candado:") + donde];
        if (ult != 0 && ahora - ult < 10'000'000) return;
        ult = ahora;
    }
    registrar(Nivel::Error,
              ba::i18n::llenar(ba::i18n::traducir("motor", "%1 corrió sin el candado del motor (tiene que ir por la cola de órdenes)"),
                               {donde}));
}

Candado::Candado(MutexMotor& m, const char* donde)
    : m_(m), donde_(donde), interfaz_(enHiloInterfaz()), pedidoUs_(g_get_monotonic_time()) {
    // Una reentrada: con un mutex común, este hilo esperaría para siempre al que lo tiene,
    // que es él mismo. Se dice dónde y se cae (con su volcado): la causa está en la pila.
    if (m_.esMio()) {
        const std::string e = ba::i18n::llenar(
            ba::i18n::traducir("motor", "reentrada del candado del motor en %1: este hilo ya lo tenía"), {donde_});
        registrar(Nivel::Error, e);
        std::fprintf(stderr, "%s\n", e.c_str());
        std::abort();
    }
    if (interfaz_ && !permitida(donde_)) {
        ++tomas;
        if (estricto.load()) {
            const std::int64_t ahora = g_get_monotonic_time();
            bool avisar = false;
            {
                std::lock_guard<std::mutex> l(avisosM);
                std::int64_t& ult = ultimoAviso[std::string("estricto:") + donde_];
                avisar = ult == 0 || ahora - ult > 10'000'000;
                if (avisar) ult = ahora;
            }
            if (avisar)
                registrar(Nivel::Error,
                          ba::i18n::llenar(ba::i18n::traducir("motor", "la interfaz tomó el candado del motor en %1 (no "
                                                                       "tiene que tomarlo nunca: ver "
                                                                       "docs/planes/PLAN-MOTOR-SIN-ESPERAS.md)"),
                                           {donde_}));
        }
    }
    m_.lock();
    tomadoUs_ = g_get_monotonic_time();
}

Candado::~Candado() {
    const std::int64_t ahora = g_get_monotonic_time();
    m_.unlock();
    const std::int64_t esperaMs = (tomadoUs_ - pedidoUs_) / 1000;
    const std::int64_t adentroMs = (ahora - tomadoUs_) / 1000;
    if (interfaz_) {
        avisarDemora(donde_, esperaMs, 50, QT_TRANSLATE_NOOP("motor", "la interfaz esperó el candado del motor"));
        avisarDemora(donde_, adentroMs, 50, QT_TRANSLATE_NOOP("motor", "la interfaz tuvo el candado del motor"));
    } else {
        avisarDemora(donde_, adentroMs, 200, QT_TRANSLATE_NOOP("motor", "el candado del motor estuvo tomado"));
    }
}

Tramo::Tramo(const char* nombre) : nombre_(nombre), desdeUs_(g_get_monotonic_time()) {}

Tramo::~Tramo() {
    avisarDemora(nombre_, (g_get_monotonic_time() - desdeUs_) / 1000, 100, QT_TRANSLATE_NOOP("motor", "la vuelta del motor se demoró"));
}

} // namespace ba::motor
