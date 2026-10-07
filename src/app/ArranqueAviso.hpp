// ============================================================================
// src/app/ArranqueAviso.hpp — por dónde va el arranque, para el cartel de carga
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

/// Quién escucha (el cartel de carga). Vacío = nadie.
inline std::function<void(const std::string&)>& oyente() {
    static std::function<void(const std::string&)> f;
    return f;
}

/// «Armando las escenas…»: lo que empieza ahora.
inline void avisar(const std::string& que) {
    if (oyente()) oyente()(que);
}

} // namespace ba::app::arranque
