// ============================================================================
// src/app/pantallas/Tarjetas.hpp — los tipos de tarjeta del tablero
//
// UNA sola tabla para dos usuarios: el tablero (DashboardModel: la barra de
// tarjetas, los títulos) y el catálogo de cosas que se pueden mostrar
// (pantallas/Catalogo: «sacar a ventana» y los monitores). Antes vivía adentro
// de DashboardModel.cpp y quien quisiera listar las tarjetas tenía que
// copiarla.
//
// Header solo (sin .cpp): DashboardModel se compila suelto en su test y no
// tiene por qué arrastrar el catálogo entero.
// ============================================================================
#pragma once

#include <QtGlobal> // QT_TRANSLATE_NOOP

namespace ba::app::tarjetas {

struct Tipo {
    const char* id;
    /// Marcado para traducir: al mostrarlo, QCoreApplication::translate("tarjetas", titulo).
    const char* titulo;
    const char* icono; ///< el de la barra de tarjetas (un signo, como los del riel)
};

inline constexpr Tipo kTipos[] = {
    // Primera a proposito: es la que se mira todo el tiempo.
    {"direccion", QT_TRANSLATE_NOOP("tarjetas", "Dirección"), "◎"},
    {"aire", QT_TRANSLATE_NOOP("tarjetas", "Aire"), "●"},
    {"outputs", QT_TRANSLATE_NOOP("tarjetas", "Salidas"), "⇪"},
    {"scenes", QT_TRANSLATE_NOOP("tarjetas", "Escenas"), "▦"},
    {"mixer", QT_TRANSLATE_NOOP("tarjetas", "Mezclador"), "🎚"},
    {"guests", QT_TRANSLATE_NOOP("tarjetas", "Invitados"), "☺"},
    {"health", QT_TRANSLATE_NOOP("tarjetas", "Sistema"), "♥"},
    {"monitor", QT_TRANSLATE_NOOP("tarjetas", "Monitor"), "▭"},
    {"sources", QT_TRANSLATE_NOOP("tarjetas", "Fuentes"), "◉"},
    {"lobby", QT_TRANSLATE_NOOP("tarjetas", "Conferencia"), "☎"},
    {"speakers", QT_TRANSLATE_NOOP("tarjetas", "Oradores"), "✋"},
    {"carrusel", QT_TRANSLATE_NOOP("tarjetas", "Carrusel"), "⟳"},
    {"ptz", QT_TRANSLATE_NOOP("tarjetas", "Control PTZ"), "⌖"},
    {"pdf", "PDF", "▤"},
    {"linea", QT_TRANSLATE_NOOP("tarjetas", "Línea de tiempo"), "⇥"},
};

} // namespace ba::app::tarjetas
