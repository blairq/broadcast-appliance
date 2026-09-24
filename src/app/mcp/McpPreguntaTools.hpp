// ============================================================================
// src/app/mcp/McpPreguntaTools.hpp — ver McpPreguntaTools.cpp
// ============================================================================
#pragma once

namespace ba::app {
class StudioViewModel;
}

namespace ba::app::mcp {
class McpCore;

/// La cola de preguntas del publico y la pagina desde donde se mandan.
void registerPreguntaTools(McpCore& core, StudioViewModel& vm);

} // namespace ba::app::mcp
