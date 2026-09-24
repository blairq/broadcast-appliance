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

void registerEventoTools(McpCore& core, EventoViewModel& ev, StudioViewModel& vm,
                         FichaDe fichaDe);

} // namespace mcp
} // namespace ba::app
