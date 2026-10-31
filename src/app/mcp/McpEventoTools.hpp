// ============================================================================
// src/app/mcp/McpEventoTools.hpp — ver McpEventoTools.cpp
//
// Las herramientas del guion del evento viven aparte de McpService.cpp, que ya
// es largo: es un bloque cerrado que solo necesita el viewmodel del guion.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {
class EventoViewModel;
class StudioViewModel;
namespace mcp {

class McpCore;

/// Registra la categoria "evento" y sus herramientas en `core`.
/// :param fichaDe: dado un id de la libreta de rostros, si existe y como se
///        llama. Se pasa de afuera para que este archivo no dependa del
///        reconocimiento: sin modelos vendoreados, la lista de auto-aceptar
///        sigue existiendo (vacia) y nada se rompe.
using FichaDe = std::function<std::pair<bool, std::string>(const std::string& personaId)>;

void registerEventoTools(McpCore& core, EventoViewModel& ev, StudioViewModel& vm, FichaDe fichaDe);
/// Lo que la línea necesita del resto de la app, pasado de afuera (así McpLineaTools
/// no depende del motor, del orden del día ni de las plantillas). Vacías = no hay.
struct LineaFuentes {
    /// Lo que se ve en una escena (sus fuentes visibles): para la pantalla de cámara.
    std::function<std::vector<std::string>(const std::string& escena)> camarasDeEscena;
    /// Los puntos del orden del día (asamblea/OrdenDelDia::puntos).
    std::function<nlohmann::json()> ordenDelDia;
    /// El evento.json de la plantilla de un escenario (eventoDeEscenario), o null.
    std::function<nlohmann::json(const std::string& escenario)> eventoDeEscenario;
};

/// La línea de tiempo del evento (linea_*, ver McpLineaTools.cpp). La llama registerEventoTools.
void registerLineaTools(McpCore& core, EventoViewModel& ev, LineaFuentes fuentes);

} // namespace mcp
} // namespace ba::app
