// SPDX-License-Identifier: GPL-2.0-or-later
// src/tablero-web/Pagina.cpp — ver el .hpp
#include "Pagina.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#endif

Pagina& Pagina::la() {
    static Pagina p;
    return p;
}

QString Pagina::alternarPantallaCompleta() {
#ifdef __EMSCRIPTEN__
    // puente.js: window.baPuente.pantallaCompleta() → "" o el motivo.
    emscripten::val puente = emscripten::val::global("baPuente");
    if (puente.isUndefined()) return tr("No se pudo.");
    return QString::fromStdString(puente.call<std::string>("pantallaCompleta"));
#else
    setPantallaCompleta(!completa_);
    return {};
#endif
}

QString Pagina::preferencia(const QString& clave) const {
#ifdef __EMSCRIPTEN__
    emscripten::val puente = emscripten::val::global("baPuente");
    if (puente.isUndefined()) return {};
    return QString::fromStdString(puente.call<std::string>("leerLocal", clave.toStdString()));
#else
    Q_UNUSED(clave);
    return {};
#endif
}

void Pagina::guardarPreferencia(const QString& clave, const QString& valor) {
#ifdef __EMSCRIPTEN__
    emscripten::val puente = emscripten::val::global("baPuente");
    if (!puente.isUndefined()) puente.call<void>("guardarLocal", clave.toStdString(), valor.toStdString());
#else
    Q_UNUSED(clave);
    Q_UNUSED(valor);
#endif
}
