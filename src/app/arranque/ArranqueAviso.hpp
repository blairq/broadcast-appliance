// ============================================================================
// src/app/arranque/ArranqueAviso.hpp — por dónde va el arranque, para el cartel de carga
//
// El motor arranca en el hilo de la interfaz (AppRuntime::start) y mientras
// tanto nada se dibuja: el cartel de carga (GuiMode) se queda quieto. Cada
// etapa avisa acá qué está haciendo y el cartel lo muestra. Sin interfaz
// (--no-ui) nadie escucha y no cuesta nada. Sin Qt a propósito: OperateMode
// también se compila sin él.
// ============================================================================
#pragma once

#include <functional>
#include <string>

namespace ba::app::arranque {

/// Lo que el cartel muestra.
struct Aviso {
    std::string que;      ///< la etapa: «Armando las escenas…»
    std::string detalle;  ///< una línea chica abajo (vacía: nada)
    double parte = -1;    ///< 0..1: cuánto lleva la etapa (una barra propia); < 0: sin barra
    bool paso = true;     ///< etapa nueva (la barra general avanza) o la misma que sigue
};

/// Quién escucha (el cartel de carga). Vacío = nadie.
inline std::function<void(const Aviso&)>& oyente() {
    static std::function<void(const Aviso&)> f;
    return f;
}

/// «Armando las escenas…»: lo que empieza ahora.
inline void avisar(const std::string& que) {
    if (oyente()) oyente()(Aviso{que, {}, -1, true});
}

/// La misma etapa, que sigue (otro texto, detalle o parte; la barra general no
/// avanza). Llamarlo seguido mientras algo largo corre en otro hilo: el cartel
/// se repinta y la ventana no queda «No responde».
inline void seguir(Aviso a) {
    a.paso = false;
    if (oyente()) oyente()(a);
}

} // namespace ba::app::arranque
