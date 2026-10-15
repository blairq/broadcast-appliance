// ============================================================================
// src/app/mcp/McpTiemposTools.cpp — los tiempos del pipeline por MCP (tiempos_*)
//
// Lo mismo que muestra la página «Tiempos» (TiemposModel): de cada entrada a cada
// salida, nodo por nodo, cuánto agrega cada uno y cuánto lleva desde la captura.
// ============================================================================
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "tiempos/TiemposModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerTiemposTools() {
    tiempos::TiemposModel* tm = vm_.tiemposModel();
    if (tm == nullptr) return;
    core_.addCategory("tiempos", "Tiempos del pipeline",
                      "Cuánto tarda cada cosa, de cada entrada a cada salida, en video y en audio: la captura y "
                      "la decodificación, cada tarjeta y cada módulo de audio, la escena y la mezcla, y cada salida. "
                      "Cada número dice si es medido o declarado (la latencia que se le fijó).");
    {
        Tool t;
        t.name = "tiempos_mapa";
        t.title = "Los tiempos del pipeline";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "El grafo de demoras de ahora: `nodos` (entrada, tarjeta, modulo, escena, mezcla, transmision, "
                        "grabacion) con `ms` (lo que agrega ese nodo), `acumulado_ms` (desde la captura), `fuente` "
                        "(medido | declarado | sin_medir) y `estado` (bien | justo | tarde: contra la latencia del "
                        "estudio); `tramos` (de un nodo a otro, video o audio); `salidas` con su `total_ms` y el "
                        "`camino` más lento que lo explica.";
        t.run = [tm](const json&, Done done) { done(ToolResult().data(tm->ahora())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_nodo";
        t.title = "Un nodo de los tiempos, con su historia";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "Un nodo de tiempos_mapa por su `id` (p. ej. «entrada:Cámara 1», «tarjeta:Cámara 1:superres», "
                        "«salida:transmision») con su detalle y la historia de los últimos 60 s (solo mientras la "
                        "página Tiempos está abierta).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [tm](const json& a, Done done) {
            tm->ahora();
            const json n = tm->nodo(a.value("id", std::string()));
            if (n.is_null()) return done(ToolResult::error("no hay un nodo «" + a.value("id", std::string()) + "» (tiempos_mapa da los que hay)"));
            done(ToolResult().data(n));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
