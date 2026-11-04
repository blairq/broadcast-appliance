// ============================================================================
// src/app/mcp/McpInformesTools.cpp — Herramientas MCP del motor: los informes del resultado de la sesión (asamblea/InformesResultado).
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include "asamblea/Presidencia.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <fstream>
#include "arranque/RuntimeMotor.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/InformesResultado.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerInformesTools() {
    InterfazMcp& vm = vm_;

    // --- Los informes del resultado de la sesión (asamblea/InformesResultado) ---------
    {
        Tool t;
        t.name = "informe_plantillas";
        t.title = ba::i18n::traducir("mcp", "Las plantillas del informe del resultado");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las plantillas del informe del resultado de la sesión ({id, nombre}) y los campos que "
                        "se pueden usar. Con `id`, también su texto y cómo queda rellenada con lo de ahora (`vista`).");
        t.details = QT_TRANSLATE_NOOP("mcp", "Una plantilla es Markdown con campos: {{cuerpo}}, {{evento}}, {{fecha}}, {{hora}}, "
                    "{{generado}}, {{quorum.estado}}, {{quorum.presentes}}, {{quorum.necesarios}}, {{quorum.miembros}}, "
                    "{{padron.presentes}}, {{padron.total}}. Las listas se repiten entre {{#lista}} y {{/lista}} y adentro se "
                    "usan los campos de cada uno: presentes (nombre, papel, por_donde, hora), puntos (n, expediente, titulo, "
                    "estado, resultado), votaciones (asunto, afirmativos, negativos, abstenciones, presentes, mayoria, "
                    "resultado), palabra (nombre, tiempo). {{^lista}}…{{/lista}} sale solo si está vacía y {{#lista.0}}…"
                    "{{/lista.0}} una vez si tiene alguno (para el encabezado de una tabla). Un tag solo en su renglón no deja "
                    "renglón vacío, así las filas de una tabla quedan juntas.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            json out{{"plantillas", inf->plantillas()}, {"campos", asamblea::InformesResultado::campos()}};
            const std::string id = a.value("id", std::string());
            if (!id.empty()) {
                const std::string texto = inf->texto(id);
                if (texto.empty())
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una plantilla «%1»."), {id})));
                out["texto"] = texto;
                out["vista"] = inf->vista(id);
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "informe_plantilla_guardar";
        t.title = ba::i18n::traducir("mcp", "Escribir una plantilla del informe del resultado");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Escribe una plantilla del informe del resultado (Markdown con campos: ver "
                        "informe_plantillas). Con `id` cambia esa (y su `nombre`, si viene); sin `id` crea una nueva con `nombre`. "
                        "`quitar`: true la borra. El operador la ve y la cambia en Asamblea → Informes → Plantillas.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}}, {"nombre", {{"type", "string"}}}, {"texto", {{"type", "string"}}},
                           {"quitar", {{"type", "boolean"}}}}}};
        t.examples = {R"({"nombre": "Acta para la prensa", "texto": "# {{cuerpo}} — {{fecha}}\n\n{{#puntos}}\n- {{n}}. {{titulo}}: **{{resultado}}**\n{{/puntos}}"})"};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            std::string id = a.value("id", std::string());
            if (a.value("quitar", false)) {
                const std::string e = inf->quitar(id);
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().data({{"plantillas", inf->plantillas()}}));
            }
            const std::string texto = a.value("texto", std::string());
            if (texto.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta el `texto` de la plantilla.")));
            if (id.empty()) {
                std::string err;
                id = inf->crear(a.value("nombre", std::string()), texto, &err);
                if (id.empty()) return done(ToolResult::error(err));
            } else if (const std::string e = inf->guardar(id, texto, a.value("nombre", std::string())); !e.empty()) {
                return done(ToolResult::error(e));
            }
            done(ToolResult().data({{"id", id}, {"vista", inf->vista(id)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "informe_generar";
        t.title = ba::i18n::traducir("mcp", "Generar el informe del resultado en PDF");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Rellena la plantilla `plantilla` (su id; sin ella, la primera) con lo de ahora y la "
                        "guarda en PDF en la carpeta del evento (informes/), con el Markdown al lado. Con `soloVer`: true "
                        "devuelve el texto sin escribir nada.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"plantilla", {{"type", "string"}}}, {"soloVer", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            std::string id = a.value("plantilla", std::string());
            if (id.empty() && !inf->plantillas().empty()) id = inf->plantillas()[0].value("id", std::string());
            if (inf->texto(id).empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una plantilla «%1»."), {id})));
            if (a.value("soloVer", false)) return done(ToolResult().text(inf->vista(id)));
            const json r = inf->generar(id);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Informe guardado: %1"), {r.value("archivo", std::string())})).data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_orador_terminar";
        t.title = ba::i18n::traducir("mcp", "Terminar al orador presencial");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Termina a UN orador presencial: cierra el micrófono de su banca y para su reloj; no calla a los "
            "demás.");
        t.run = [&vm](const json&, Done done) {
            vm.bancas().terminar();
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "presidencia_callar_todos";
        t.title = ba::i18n::traducir("mcp", "Silenciar a todos");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Calla a TODOS a la vez: cierra todos los micrófonos de la central menos el de la presidencia y "
            "termina al que está hablando. Lo mismo que el botón de prioridad del presidente en su unidad.");
        t.run = [&vm](const json&, Done done) {
            // La misma operación que el botón y las consolas; el error es el de la central.
            if (vm.presidencia() == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La presidencia no está disponible.")));
            vm.presidencia()->silenciarTodos("mcp", [done](const std::string& e) {
                done(e.empty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Todos callados.")) : ToolResult::error(e));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "presidencia_hablar";
        t.title = ba::i18n::traducir("mcp", "Hablo yo (la presidencia)");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`activo: true` calla a todos (como presidencia_callar_todos) y abre el micrófono de la unidad de "
                        "presidente de la central; false lo cierra. Hace falta la central conectada.");
        t.inputSchema = {{"type", "object"}, {"required", {"activo"}}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (vm.presidencia() == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La presidencia no está disponible.")));
            const bool activo = a.value("activo", true);
            // Contesta cuando contesta la central (abrió o no el micrófono).
            vm.presidencia()->hablaPresidencia(activo, "mcp", [done, activo](const std::string& e) {
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult::ok(activo ? ba::i18n::traducir("mcp", "Todos callados; habla la presidencia.")
                                           : ba::i18n::traducir("mcp", "La presidencia cerró su micrófono.")));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
