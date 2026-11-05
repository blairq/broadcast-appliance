// ============================================================================
// src/app/arranque/ArranqueAviso.hpp — por dónde va el arranque, para el cartel de carga
//
// El motor arranca en el hilo de control (GuiMode: AppRuntime::start en el plano de
// control, el dueño del motor), no en el de la interfaz: mientras tanto la interfaz
// sigue con su bucle y el cartel de carga se dibuja solo. Cada etapa avisa acá qué está
// haciendo y el cartel lo muestra (el oyente lo lleva al hilo del cartel). Sin interfaz
// (--no-ui) nadie escucha y no cuesta nada. Sin Qt a propósito: AppRuntime también se
// compila sin él.
//
// Hilos: se avisa desde el que arranca el motor y se pone/saca el oyente desde la
// interfaz; el oyente se copia con un candado (nunca se llama con el candado tomado).
// ============================================================================
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace ba::app::arranque {

/// Lo que el cartel muestra.
struct Aviso {
    std::string que;      ///< la etapa: «Armando las escenas…»
    std::string detalle;  ///< una línea chica abajo (vacía: nada)
    double parte = -1;    ///< 0..1: cuánto lleva la etapa (una barra propia); < 0: sin barra
    bool paso = true;     ///< etapa nueva (la barra general avanza) o la misma que sigue
};

using Oyente = std::function<void(const Aviso&)>;

namespace detalle {
struct Estado {
    std::mutex mu;
    Oyente oyente;
};
inline Estado& estado() {
    static Estado e;
    return e;
}
inline void llamar(const Aviso& a) {
    Oyente f;
    {
        const std::lock_guard<std::mutex> l(estado().mu);
        f = estado().oyente;
    }
    if (f) f(a);
}
} // namespace detalle

/// Quién escucha (el cartel de carga). Vacío = nadie. Desde cualquier hilo.
inline void ponerOyente(Oyente f) {
    const std::lock_guard<std::mutex> l(detalle::estado().mu);
    detalle::estado().oyente = std::move(f);
}

/// «Armando las escenas…»: lo que empieza ahora.
inline void avisar(const std::string& que) { detalle::llamar(Aviso{que, {}, -1, true}); }

/// La misma etapa, que sigue (otro texto, detalle o parte; la barra general no
/// avanza). Llamarlo seguido mientras algo largo corre en otro hilo: el cartel
/// muestra que avanza (el reloj, cuántos plugins lleva).
inline void seguir(Aviso a) {
    a.paso = false;
    detalle::llamar(a);
}

} // namespace ba::app::arranque
