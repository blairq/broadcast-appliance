// ============================================================================
// src/app/mcp/McpConfiguracionesTools.cpp — configuraciones con nombre, por MCP
//
// Cada configuración es una carpeta con todo lo suyo (escenas, medios, plugins,
// Cuerpo, botonera, eventos…); cambiar de una a otra no copia nada (reinicia con
// la otra carpeta). Ver arranque/Configuraciones.hpp.
// ============================================================================

#include "arranque/Configuraciones.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerConfiguracionesTools() {
    Configuraciones* cf = vm_.configuraciones();
    if (cf == nullptr) return;
    core_.addCategory("configuraciones", "Configuraciones (cada una con lo suyo)",
                      "Configuraciones con nombre: cada una con sus escenas, medios, plugins, Cuerpo, botonera y "
                      "eventos, sin compartir nada; cambiar de una a otra no copia nada (reinicia la app).");
    const json nombre = {{"type", "string"}, {"description", "El nombre de la configuración."}};
    {
        Tool t;
        t.name = "configuraciones_listar";
        t.title = "Las configuraciones";
        t.category = "configuraciones";
        t.readOnly = true;
        t.description = "Las configuraciones que hay (cuál es la activa, su descripción, de dónde salió y cuánto ocupa) "
                        "y las demos que trae el programa.";
        t.run = [cf](const json&, Done done) {
            done(ToolResult().data({{"configuraciones", cf->listar()}, {"activa", cf->activa()}, {"demos", cf->demos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_crear";
        t.title = "Crear una configuración";
        t.category = "configuraciones";
        t.description = "Crea una configuración nueva (no la activa). `desde`: «vacia» (la de fábrica, de fábrica), "
                        "«actual» (una copia de la activa), el nombre de otra, «demo:<nombre>» (una demo del programa: "
                        "configuraciones_listar las dice) o «evento:<carpeta>/<inicio|cierre|ultima>» (la configuración "
                        "con la que se hizo un evento, con los medios de la actual). Las grabaciones no se copian salvo "
                        "`con_grabaciones`.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", nombre},
                           {"desde", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"con_grabaciones", {{"type", "boolean"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string e = cf->crear(a.value("nombre", std::string()), a.value("desde", std::string("vacia")),
                                            a.value("descripcion", std::string()), a.value("con_grabaciones", false));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Creada «" + a.value("nombre", std::string()) +
                                "». Para usarla: configuracion_cambiar (reinicia la app)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_cambiar";
        t.title = "Cambiar de configuración";
        t.category = "configuraciones";
        t.description = "Pasa a la configuración `nombre`: queda anotada y la app se REINICIA con ella (no copia nada). "
                        "Se corta todo lo que esté andando (transmisión, grabación, sala): pedile confirmación al "
                        "operador antes.";
        t.inputSchema = {{"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", nombre}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            if (n == cf->activa()) return done(ToolResult::error("ya es la activa"));
            if (const std::string e = cf->activar(n); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Cambiando a «" + n + "»: la app se reinicia."));
            cf->reiniciar();
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_editar";
        t.title = "Renombrar, describir o borrar una configuración";
        t.category = "configuraciones";
        t.description = "`renombrar_a`: le cambia el nombre; `descripcion`: la anota; `borrar: true`: la manda a la "
                        "papelera (Configuraciones/.papelera). La activa no se renombra ni se borra.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", nombre},
                           {"renombrar_a", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"borrar", {{"type", "boolean"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            std::string e;
            if (a.contains("descripcion")) e = cf->describir(n, a.value("descripcion", std::string()));
            if (e.empty() && a.value("borrar", false)) e = cf->borrar(n);
            else if (e.empty() && a.contains("renombrar_a")) e = cf->renombrar(n, a.value("renombrar_a", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"configuraciones", cf->listar()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
