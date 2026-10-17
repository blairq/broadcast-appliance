// ============================================================================
// src/app/mcp/McpControlWebTools.hpp — ver McpControlWebTools.cpp
// ============================================================================
#pragma once

namespace ba::app {
class StudioViewModel;
namespace mcp {

class McpCore;

/// Registra la categoría "control_web" (control_web_*) y le pasa el servidor al
/// control web (control/ControlWeb), que corre las herramientas de los aparatos.
void registerControlWebTools(McpCore& core, StudioViewModel& vm);

} // namespace mcp
} // namespace ba::app
