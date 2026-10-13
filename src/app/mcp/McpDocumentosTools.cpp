// ============================================================================
// src/app/mcp/McpDocumentosTools.cpp — la documentación del evento, por MCP
//
// Los mismos documentos que la pestaña «Documentos» de la página del evento
// (ui/DocumentosViewModel sobre <evento>/docs/): agregar un PDF (su carátula queda
// hecha), describirlo, relacionarlo con puntos del orden del día y secciones del
// guion, publicarlo o no en la web del evento, leer su texto. Ver
// docs/planes/PLAN-DOCUMENTOS-EVENTO.md.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/DocumentosViewModel.hpp"
#include "ui/StudioViewModel.hpp"

#include <algorithm>

namespace ba::app::mcp {

void McpService::registerDocumentosTools() {
    DocumentosViewModel* dv = vm_.documentos();
    if (dv == nullptr) return;
    core_.addCategory("documentos", "Documentación del evento",
                      "Los documentos de cada evento (PDF con su carátula), relacionados con puntos del orden del día "
                      "y secciones del guion, que la página web del evento muestra y deja bajar.");
    const json relaciones = {
        {"puntos", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "números de punto del orden del día"}}},
        {"secciones", {{"type", "array"}, {"items", {{"type", "string"}}},
                       {"description", "secciones del guion: el id del bloque (evento_ver) o su número"}}},
        {"publico", {{"type", "boolean"}, {"description", "si la web del evento lo muestra y deja bajar (por defecto sí)"}}}};
    {
        Tool t;
        t.name = "documentos_listar";
        t.title = "Los documentos del evento";
        t.category = "documentos";
        t.readOnly = true;
        t.description = "Los documentos del evento abierto: id, título, descripción, páginas, tamaño, si es público, a qué "
                        "puntos del orden del día y secciones del guion está relacionado y si tiene carátula. Sin relaciones "
                        "es documentación del evento en general. Con `punto` o `seccion`, solo los de ese punto del orden "
                        "del día o esa sección del guion (lo mismo que muestra la web y la fila del punto).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"punto", {{"type", "integer"}, {"description", "número de punto del orden del día"}}},
                           {"seccion", {{"type", "string"}, {"description", "id del bloque del guion o su número"}}}}}};
        t.run = [dv](const json& a, Done done) {
            json l = dv->lista();
            const auto tiene = [](const json& arr, const json& v) { return arr.is_array() && std::find(arr.begin(), arr.end(), v) != arr.end(); };
            if (a.contains("punto") && a["punto"].is_number_integer()) {
                json f = json::array();
                for (const auto& d : l)
                    if (tiene(d.value("puntos", json::array()), a["punto"])) f.push_back(d);
                l = f;
            }
            if (a.contains("seccion") && (a["seccion"].is_string() || a["seccion"].is_number_integer())) {
                const json k = a["seccion"].is_string() ? a["seccion"] : json(std::to_string(a["seccion"].get<int>()));
                json f = json::array();
                for (const auto& d : l)
                    if (tiene(d.value("secciones", json::array()), k)) f.push_back(d);
                l = f;
            }
            done(ToolResult().data({{"documentos", l}, {"carpeta", dv->carpeta().string()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_agregar";
        t.title = "Agregar un documento al evento";
        t.category = "documentos";
        t.description = "Agrega un PDF (o una imagen) a la documentación del evento: lo copia a la carpeta docs/ del evento y "
                        "guarda su carátula (la primera página) para la web. `archivo`: la ruta. `titulo` y `descripcion` "
                        "(si no se dan: el título del PDF; la respuesta trae el comienzo del texto para escribir una "
                        "descripción con documento_editar). Relaciones opcionales.";
        json props = relaciones;
        props["archivo"] = {{"type", "string"}};
        props["titulo"] = {{"type", "string"}};
        props["descripcion"] = {{"type", "string"}};
        t.inputSchema = {{"type", "object"}, {"required", {"archivo"}}, {"properties", props}};
        t.run = [dv](const json& a, Done done) {
            json datos = a;
            datos.erase("archivo");
            const std::string ruta = a.value("archivo", std::string());
            const json r = dv->agregar(std::filesystem::path(std::u8string(ruta.begin(), ruta.end())), datos);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_editar";
        t.title = "Describir o relacionar un documento";
        t.category = "documentos";
        t.description = "Cambia lo que se pase: `titulo`, `descripcion`, `publico`, `puntos` (del orden del día) y `secciones` "
                        "(del guion; las listas reemplazan a las de antes). La web del evento se actualiza sola.";
        json props = relaciones;
        props["id"] = {{"type", "string"}};
        props["titulo"] = {{"type", "string"}};
        props["descripcion"] = {{"type", "string"}};
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", props}};
        t.run = [dv](const json& a, Done done) {
            json c = a;
            c.erase("id");
            if (const std::string e = dv->editar(a.value("id", std::string()), c); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Listo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_quitar";
        t.title = "Quitar un documento del evento";
        t.category = "documentos";
        t.destructive = true;
        t.description = "Lo saca de la documentación del evento (el archivo va a la papelera del evento, se puede recuperar).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [dv](const json& a, Done done) {
            if (const std::string e = dv->quitar(a.value("id", std::string())); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Quitado (está en la papelera del evento)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_abrir";
        t.title = "Abrir un documento en pantalla";
        t.category = "documentos";
        t.description = "Abre el documento con el visor del sistema (el visor de PDF), en esta máquina: lo mismo que el "
                        "doble clic en la pestaña Documentos o el botón 📄 de un punto del orden del día.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [dv](const json& a, Done done) {
            const std::string id = a.value("id", std::string());
            if (dv->documento(id).is_null()) return done(ToolResult::error("no hay un documento «" + id + "»"));
            dv->abrirUi(QString::fromStdString(id));
            done(ToolResult::ok("Abierto."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_texto";
        t.title = "Leer el texto de un documento";
        t.category = "documentos";
        t.readOnly = true;
        t.description = "El texto de un PDF del evento, de la página `desde` a `hasta` (por defecto todo): para resumirlo, "
                        "describirlo o saber a qué punto del orden del día corresponde.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", {{"type", "string"}}}, {"desde", {{"type", "integer"}}}, {"hasta", {{"type", "integer"}}}}}};
        t.run = [dv](const json& a, Done done) {
            const json r = dv->texto(a.value("id", std::string()), a.value("desde", 1), a.value("hasta", 0));
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
