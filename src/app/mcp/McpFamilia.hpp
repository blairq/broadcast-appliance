// ============================================================================
// src/app/mcp/McpFamilia.hpp — Lo que usan TODAS las familias de McpService::registerTools()
//
// Las familias (McpEscenasTools.cpp, McpFuentesTools.cpp… el orden, en mcp/McpService.hpp)
// definen un McpService::registerXTools(), registran sus herramientas en el núcleo, leen la
// interfaz por InterfazMcp y traducen sus textos. Eso, y nada más: cada familia incluye
// aparte lo que usa ella (el motor por el runtime, los modelos de cada área, los
// ViewModels). Antes era mcp/McpRegistro.hpp, que traía todo a todas: tocar un ViewModel
// recompilaba las diez (Ola 4 del diseño físico, docs/local/PLAN-DISENO-FISICO.md).
// ============================================================================
#pragma once

#include "i18n/I18nTexto.hpp"
#include "mcp/InterfazMcp.hpp"
#include "mcp/McpService.hpp"
