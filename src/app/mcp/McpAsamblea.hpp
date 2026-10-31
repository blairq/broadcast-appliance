// ============================================================================
// src/app/mcp/McpAsamblea.hpp — lo que necesita una sesión de asamblea, revisado
//
// `revisarAsamblea` mira lo que hace falta para sesionar (Cuerpo, recinto,
// cámaras con su preset por banca, ESCENAS que muestren esas cámaras y a los
// remotos, el debate, la central, la banca virtual, el orden del día) y dice
// qué falta y cómo se arregla. Lo usan asamblea_revisar y evento_armar (que
// avisa solo, así el asistente se entera aunque no haya leído la ayuda).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

namespace ba::app {
class AppRuntime;
class StudioViewModel;
} // namespace ba::app

namespace ba::app::mcp {

/// {listo, faltas: [{que, detalle, como}], avisos: [...], resumen: {...}}. `listo` = sin faltas.
nlohmann::json revisarAsamblea(StudioViewModel& vm);

} // namespace ba::app::mcp
