// ============================================================================
// src/app/mcp/McpPreguntaTools.hpp — ver McpPreguntaTools.cpp
// ============================================================================
#pragma once

namespace ba::app {
class AppRuntime;
class StudioViewModel;
} // namespace ba::app

namespace ba::app::mcp {
class McpCore;

/// La cola de preguntas del publico y la pagina desde donde se mandan.
void registerPreguntaTools(McpCore& core, StudioViewModel& vm, AppRuntime& rt);

} // namespace ba::app::mcp
