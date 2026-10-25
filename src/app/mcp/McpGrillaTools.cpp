// ============================================================================
// src/app/mcp/McpGrillaTools.cpp — la grilla de personas, por MCP
//
// Lo mismo que la ventana Propiedades de una fuente «Grilla de personas»
// (ba_grilla) y por el mismo modelo (escenas::ControlGrillas): lo que ajusta el
// asistente lo ve el operador en el acto, y al revés. Crear una es crear_fuente
// {type: "ba_grilla"} como cualquier fuente.
// ============================================================================

#include "escenas/ControlGrillas.hpp"
#include "escenas/GrillaPersonas.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerGrillaTools() {
    StudioViewModel* vmp = &vm_;
    const std::string ajustesAyuda =
        "origen (sala|asamblea|ambas), orden (llegada|habla|prioridad), tope (celdas con el «+N», 0 = sin tope), "
        "solo_con_camara, sin_al_aire (sin los que ya están al aire en otra fuente), resaltar (a quien habla), grupo "
        "(las grillas con el mismo grupo se reparten la gente sin repetir; \"\" = sola), orden_en_grupo (la primera "
        "manda el orden y el modo), reparto (en_orden: llena la primera hasta su tope; parejo: la misma cantidad en "
        "cada una), ancho, alto, proporcion (16:9|4:3|1:1|3:4|9:16|llenar), separacion, margen, transicion_ms, fondo "
        "y color_resaltar (\"#AARRGGBB\").";
    {
        Tool t;
        t.name = "grilla_personas_estado";
        t.title = "Qué muestra cada grilla de personas";
        t.category = "fuentes";
        t.readOnly = true;
        t.untrustedOutput = true; // los nombres los escribieron los invitados
        t.description = "Las fuentes «Grilla de personas» (ba_grilla): a quién muestra cada una ahora, cuántos quedaron en el "
                        "«+N», su canal y sus ajustes principales; y la gente que puede aparecer (sala y asamblea) con "
                        "quién habla, quién ya está al aire y quién tiene la cámara prendida.";
        t.details = "Una grilla crece y se acomoda sola con la gente conectada: los admitidos de la Sala (al aire o en la "
                    "fila de oradores) y los integrantes habilitados de la Asamblea. La misma fuente en dos escenas es la "
                    "misma imagen: para repartir gente entre escenas se crean varias con el mismo `grupo`. Crear una: "
                    "crear_fuente {type: \"ba_grilla\", name, settings: {…}}. Ajustes: " + ajustesAyuda;
        t.examples = {R"({})"};
        t.run = [vmp](const json&, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error("la grilla de personas todavía no arrancó"));
            g->recalcular();
            ToolResult r;
            r.data(g->estado());
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grilla_personas_ajustar";
        t.title = "Cambiar los ajustes de una grilla de personas";
        t.category = "fuentes";
        t.idempotent = true;
        t.description = "Cambia los ajustes de una fuente «Grilla de personas» (los mismos de la ventana Propiedades). Solo "
                        "los que vienen; se validan todos antes de tocar nada. " + ajustesAyuda;
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente", "ajustes"}},
                         {"properties",
                          {{"fuente", {{"type", "string"}, {"description", "el nombre de la fuente ba_grilla"}}},
                           {"ajustes", {{"type", "object"}, {"description", "los ajustes a cambiar"}}}}}};
        t.examples = {R"({"fuente": "Grilla", "ajustes": {"origen": "asamblea", "orden": "prioridad", "tope": 12}})",
                      R"({"fuente": "Grilla 2", "ajustes": {"grupo": "recinto", "orden_en_grupo": 2, "reparto": "parejo"}})"};
        t.run = [vmp](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error("la grilla de personas todavía no arrancó"));
            const std::string e = g->ajustar(a.value("fuente", std::string()), a.value("ajustes", json::object()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Listo: «" + a.value("fuente", std::string()) + "» quedó con los ajustes nuevos."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
