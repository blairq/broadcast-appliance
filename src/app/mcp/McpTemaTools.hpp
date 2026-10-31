// ============================================================================
// src/app/mcp/McpTemaTools.hpp — ver McpTemaTools.cpp
// ============================================================================
#pragma once

namespace ba::app {
class TemaEventoViewModel;
namespace mcp {

class McpCore;

/// Registra evento_tema (en la categoría "evento", que registra McpEventoTools).
void registerTemaTools(McpCore& core, TemaEventoViewModel& tema);

} // namespace mcp
} // namespace ba::app
