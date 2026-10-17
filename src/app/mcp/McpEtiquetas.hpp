// ============================================================================
// src/app/mcp/McpEtiquetas.hpp — con qué palabras se pide cada herramienta
//
// POR QUE HACE FALTA. El nombre y la descripción de una herramienta los
// escribió alguien que ya sabía cómo se llamaba. `audio_set` no dice
// "silenciar" en ningún lado, `medio_poner` no dice "música", `participant_kick`
// no dice "echar". Ninguna búsqueda por esas palabras las iba a encontrar, y
// son exactamente las palabras con las que alguien las pide.
//
// Las etiquetas son eso: los términos con los que una persona (o un modelo)
// pediría esta herramienta, no los que usa su documentación.
//
// POR QUE EN UNA TABLA Y NO EN CADA HERRAMIENTA. Se pensó ponerlas al lado de
// cada `Tool`, repartidas en los veintipico de archivos que las registran.
// Juntas se leen de otra manera: se ve que dos herramientas se disputan la
// misma palabra, y se ve el agujero cuando una familia entera quedó sin un
// sinónimo que las otras tienen. Repartidas, eso no lo ve nadie.
//
// El precio de una tabla aparte es que se despegue del código. Contra eso hay
// una prueba (test_mcp_busqueda) que falla si una herramienta registrada no
// tiene etiquetas: agregar una nueva y no etiquetarla rompe la compilación de
// la suite, no queda para después.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::app::mcp {

/// Los términos con los que se pediría `nombre`. Vacío si no está etiquetada.
/// Incluye los de su familia (`youtube_*`, `audio_*`...) más los propios.
/// `antes`: los nombres que tuvo (Tool::antes); suman sus familias y etiquetas.
std::vector<std::string> etiquetasDe(const std::string& nombre, const std::vector<std::string>& antes = {});

} // namespace ba::app::mcp
