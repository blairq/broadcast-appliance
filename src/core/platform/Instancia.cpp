// ============================================================================
// src/core/platform/Instancia.cpp — el lugar de un launcher (ver Instancia.hpp)
// ============================================================================

#include "platform/Instancia.hpp"

#include "i18n/I18nTexto.hpp"

#include <string>

namespace ba::platform {

bool LugarDeInstancia::tomar(bool prueba, LugarDeInstancia& out, std::string* err) {
    out.lock_.release();
    out.lugar_ = -1;
    for (int i = prueba ? 1 : 0; i < kLugares; ++i) {
        if (SingleInstance::acquire("lugar-" + std::to_string(i), out.lock_, nullptr,
                                    SingleInstance::Alcance::Maquina)) {
            out.lugar_ = i;
            return true;
        }
    }
    if (err != nullptr)
        *err = ba::i18n::llenar(
            ba::i18n::traducir("ipc", "ya hay %1 copias del programa abiertas en esta máquina (cada una con su carpeta de datos)"),
            {std::to_string(prueba ? kLugares - 1 : kLugares)});
    return false;
}

} // namespace ba::platform
