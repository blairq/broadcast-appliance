// SPDX-License-Identifier: GPL-2.0-or-later
//
// Los temas de las interfaces web de control (assets/web/comun/neuro/temas.css):
// el catálogo que muestran Configuración, Servicios → Templates y el MCP. Uno solo
// para todas las páginas: lo elige la app (AppConfig::temaWeb), el gateway lo
// publica en /comun/tema.json y cada página lo toma de ahí (neuro.js).
//
// Para sumar un tema: el bloque [data-tema="…"] en temas.css y una fila acá.
#pragma once

#include <QtGlobal> // QT_TRANSLATE_NOOP

#include <string>
#include <vector>

namespace ba::app::control {

struct TemaWeb {
    const char* id;
    /// El nombre y la descripción, marcados para traducir: al mostrarlos,
    /// QCoreApplication::translate("control-web", nombre).
    const char* nombre;
    const char* descripcion;
    // Para la muestra en la app: fondo, superficie, texto, acento.
    const char* fondo;
    const char* superficie;
    const char* texto;
    const char* acento;
};

inline const std::vector<TemaWeb>& temasWeb() {
    static const std::vector<TemaWeb> t{
        {"neuro-oscuro", QT_TRANSLATE_NOOP("control-web", "Neumorfismo oscuro"),
         QT_TRANSLATE_NOOP("control-web", "Relieve suave sobre gris carbón. El de fábrica: cansa poco la vista en la sala a oscuras."),
         "#22252c", "#262a32", "#eef0f4", "#ff7a36"},
        {"neuro-claro", QT_TRANSLATE_NOOP("control-web", "Neumorfismo claro"),
         QT_TRANSLATE_NOOP("control-web", "Gris perla (no blanco), relieve difuso y acento azul: para salas con luz."),
         "#dcdfe4", "#e3e6ea", "#2a2f38", "#3b7ff2"},
    };
    return t;
}

inline bool temaWebValido(const std::string& id) {
    for (const auto& t : temasWeb())
        if (id == t.id) return true;
    return false;
}

} // namespace ba::app::control
