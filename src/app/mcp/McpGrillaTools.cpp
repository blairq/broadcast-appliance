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
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#include <functional>

namespace ba::app::mcp {

void McpService::registerGrillaTools() {
    StudioViewModel* vmp = &vm_;
    const std::string ajustesAyuda = ba::i18n::traducir("mcp", 
        "origen (sala|asamblea|ambas), orden (llegada|habla|prioridad), tope (celdas con el «+N», 0 = sin tope), "
        "solo_con_camara, sin_al_aire (sin los que ya están al aire en otra fuente o en otra grilla que se ve, contando "
        "las escenas de las capas DSK), resaltar (a quien habla), grupo "
        "(las grillas con el mismo grupo se reparten la gente sin repetir; \"\" = sola), orden_en_grupo (la primera "
        "manda el orden y el modo), reparto (en_orden: llena la primera hasta su tope; parejo: la misma cantidad en "
        "cada una), ancho, alto, proporcion (16:9|4:3|1:1|3:4|9:16|llenar), separacion, margen, transicion_ms, fondo "
        "y color_resaltar (\"#AARRGGBB\").");
    {
        Tool t;
        t.name = "grilla_personas_estado";
        t.title = ba::i18n::traducir("mcp", "Qué muestra cada grilla de personas");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true; // los nombres los escribieron los invitados
        t.description = QT_TRANSLATE_NOOP("mcp", "Las fuentes «Grilla de personas» (ba_grilla): a quién muestra cada una ahora, cuántos quedaron en el "
                        "«+N», su canal y sus ajustes principales; y la gente que puede aparecer (conferencia y asamblea) con "
                        "quién habla, quién ya está al aire (en otra fuente o en una grilla que se ve), quién tiene la "
                        "cámara prendida, en qué grillas está cada uno y lo decidido a mano (fijo, fuera).");
        t.details = ba::i18n::llenar(QT_TRANSLATE_NOOP("mcp", "Una grilla crece y se acomoda sola con la gente conectada: los admitidos de la Conferencia (al aire o en la "
                    "fila de oradores) y los integrantes habilitados de la Asamblea. La misma fuente en dos escenas es la "
                    "misma imagen: para repartir gente entre escenas se crean varias con el mismo `grupo`. Crear una: "
                    "crear_fuente {type: \"ba_grilla\", name, settings: {…}}. Ajustes: %1"), {ajustesAyuda});
        t.examples = {R"({})"};
        t.run = [vmp](const json&, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
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
        t.title = ba::i18n::traducir("mcp", "Cambiar los ajustes de una grilla de personas");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = ba::i18n::llenar(QT_TRANSLATE_NOOP("mcp", "Cambia los ajustes de una fuente «Grilla de personas» (los mismos de la ventana Propiedades). Solo "
                        "los que vienen; se validan todos antes de tocar nada. %1"), {ajustesAyuda});
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente", "ajustes"}},
                         {"properties",
                          {{"fuente", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el nombre de la fuente ba_grilla")}}},
                           {"ajustes", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "los ajustes a cambiar")}}}}}};
        t.examples = {R"({"fuente": "Grilla", "ajustes": {"origen": "asamblea", "orden": "prioridad", "tope": 12}})",
                      R"({"fuente": "Grilla 2", "ajustes": {"grupo": "recinto", "orden_en_grupo": 2, "reparto": "parejo"}})"};
        t.run = [vmp](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            const std::string e = g->ajustar(a.value("fuente", std::string()), a.value("ajustes", json::object()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» quedó con los ajustes nuevos."), {a.value("fuente", std::string())})));
        };
        core_.addTool(std::move(t));
    }

    // --- A mano: lo mismo que el menú de cada cara en la vista de las grillas ------------
    // (Propiedades de la fuente, Sala, Asamblea). Las claves salen de grilla_personas_estado.
    const json esquemaClave = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la persona: sala:<id> o asamblea:<id> (ver grilla_personas_estado)")}};
    const json esquemaFuente = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el nombre de la fuente ba_grilla")}};
    const auto aMano = [this, vmp](const char* nombre, const std::string& titulo, const std::string& descripcion, json props,
                                   std::vector<std::string> requeridos, std::vector<std::string> ejemplos,
                                   std::function<std::string(escenas::ControlGrillas&, const json&)> hacer,
                                   std::function<std::string(const json&)> listo) {
        Tool t;
        t.name = nombre;
        t.title = titulo;
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = descripcion;
        t.details = QT_TRANSLATE_NOOP("mcp", "Lo decidido a mano vive mientras la app está abierta (las claves son de la conexión de cada uno) y "
                    "se ve en la vista de las grillas: en las Propiedades de la fuente, en la Conferencia y en la Asamblea.");
        t.inputSchema = {{"type", "object"}, {"required", requeridos}, {"properties", props}};
        t.examples = std::move(ejemplos);
        t.run = [vmp, hacer, listo](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            if (const std::string e = hacer(*g, a); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(listo(a)));
        };
        core_.addTool(std::move(t));
    };
    const auto txt = [](const json& a, const char* k) { return a.value(k, std::string()); };
    aMano("grilla_personas_fijar", ba::i18n::traducir("mcp", "Fijar a alguien en una grilla de personas"),
          ba::i18n::traducir("mcp", "Fija a una persona en una grilla: va siempre en esa (antes que nadie), aunque los filtros digan otra cosa (al "
          "aire en otra fuente, sin cámara, otro origen). En su grupo no aparece en ninguna otra. Fijarla en otra grilla "
          "suelta la anterior."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"}, {R"({"fuente": "Grilla", "clave": "sala:3f2a"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.fijar(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» quedó fijo en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
    aMano("grilla_personas_sacar", ba::i18n::traducir("mcp", "Sacar a alguien de una grilla de personas"),
          ba::i18n::traducir("mcp", "Saca a una persona de una grilla (y de las de su grupo): no aparece ahí aunque los filtros la acepten. Si "
          "estaba fija ahí, deja de estarlo. Para que vuelva: grilla_personas_devolver."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"},
          {R"({"fuente": "Grilla", "clave": "asamblea:12"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.sacar(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» ya no aparece en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
    aMano("grilla_personas_mover", ba::i18n::traducir("mcp", "Mover a alguien de una grilla de personas a otra"),
          ba::i18n::traducir("mcp", "Pasa a una persona de una grilla a otra: la fija en `hacia` y, si las dos son de grupos distintos, la saca de "
          "`desde`."),
          {{"clave", esquemaClave},
           {"desde", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la grilla donde está")}}},
           {"hacia", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la grilla adonde va")}}}},
          {"clave", "desde", "hacia"}, {R"({"clave": "sala:3f2a", "desde": "Grilla", "hacia": "Grilla 2"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.mover(txt(a, "clave"), txt(a, "desde"), txt(a, "hacia")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» pasó de «%2» a «%3»."), {txt(a, "clave"), txt(a, "desde"), txt(a, "hacia")}); });
    aMano("grilla_personas_devolver", ba::i18n::traducir("mcp", "Devolver a alguien a lo automático en una grilla de personas"),
          ba::i18n::traducir("mcp", "Deshace lo hecho a mano con una persona en una grilla (fijarla o sacarla): vuelve a entrar o salir según los "
          "ajustes de la grilla."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"},
          {R"({"fuente": "Grilla", "clave": "asamblea:12"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.devolver(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» vuelve a lo automático en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
}

} // namespace ba::app::mcp
