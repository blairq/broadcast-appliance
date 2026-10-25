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

namespace ba::app::tarjetas {

struct Tipo {
    const char* id;
    const char* titulo;
    const char* icono; ///< el de la barra de tarjetas (un signo, como los del riel)
};

inline constexpr Tipo kTipos[] = {
    // Primera a proposito: es la que se mira todo el tiempo.
    {"direccion", "Dirección", "◎"}, {"aire", "Aire", "●"},         {"outputs", "Salidas", "⇪"},
    {"scenes", "Escenas", "▦"},      {"mixer", "Mezclador", "🎚"},  {"guests", "Invitados", "☺"},
    {"health", "Sistema", "♥"},      {"monitor", "Monitor", "▭"},   {"sources", "Fuentes", "◉"},
    {"lobby", "Sala", "☎"},          {"speakers", "Oradores", "✋"}, {"carrusel", "Carrusel", "⟳"},
    {"ptz", "Control PTZ", "⌖"},     {"pdf", "PDF", "▤"},
};

} // namespace ba::app::tarjetas
