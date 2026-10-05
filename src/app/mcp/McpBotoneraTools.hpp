// ============================================================================
// src/app/mcp/McpBotoneraTools.hpp — ver McpBotoneraTools.cpp
// ============================================================================
#pragma once

namespace ba::app {
class StudioViewModel;
namespace mcp {

class McpCore;

/// Registra la categoria "botonera": bus de eventos (eventos_*) y botonera_*.
void registerBotoneraTools(McpCore& core, StudioViewModel& vm);

} // namespace mcp
} // namespace ba::app
