// ============================================================================
// src/app/mcp/McpEventoTools.hpp — ver McpEventoTools.cpp
//
// Las herramientas del guion del evento viven aparte de McpService.cpp, que ya
// es largo: es un bloque cerrado que solo necesita el viewmodel del guion.
// ============================================================================
#pragma once

namespace ba::app {
class EventoViewModel;
namespace mcp {

class McpCore;

/// Registra la categoria "evento" y sus herramientas en `core`.
void registerEventoTools(McpCore& core, EventoViewModel& ev);

} // namespace mcp
} // namespace ba::app
