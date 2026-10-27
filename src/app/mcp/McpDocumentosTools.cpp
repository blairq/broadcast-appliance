// ============================================================================
// src/app/mcp/McpDocumentosTools.cpp — la documentación del evento, por MCP
//
// Los mismos documentos que la pestaña «Documentos» de la página del evento
// (ui/DocumentosViewModel sobre <evento>/docs/): agregar un PDF (su carátula queda
// hecha), describirlo, relacionarlo con puntos del orden del día y secciones del
// guion, publicarlo o no en la web del evento, leer su texto. Ver
// docs/planes/PLAN-DOCUMENTOS-EVENTO.md.
// ============================================================================

#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "ui/DocumentosViewModel.hpp"
#include "ui/PdfEscena.hpp"
#include "ui/StudioViewModel.hpp"

#include <algorithm>

namespace ba::app::mcp {

void McpService::registerDocumentosTools() {
    DocumentosViewModel* dv = vm_.documentos();
    if (dv == nullptr) return;
    core_.addCategory("documentos", ba::i18n::traducir("mcp", "Documentación del evento"),
                      ba::i18n::traducir("mcp", "Los documentos de cada evento (PDF con su carátula), relacionados con puntos del orden del día "
                      "y secciones del guion, que la página web del evento muestra y deja bajar."));
    const json relaciones = {
        {"puntos", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", ba::i18n::traducir("mcp", "números de punto del orden del día")}}},
        {"secciones", {{"type", "array"}, {"items", {{"type", "string"}}},
                       {"description", ba::i18n::traducir("mcp", "secciones del guion: el id del bloque (guion_ver) o su número")}}},
        {"publico", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "si la web del evento lo muestra y deja bajar (por defecto sí)")}}},
        {"pagina_tapa", {{"type", "integer"}, {"description", ba::i18n::traducir("mcp", "la página del PDF que hace de carátula (1 = la primera); se rehace la miniatura")}}}};
    {
        Tool t;
        t.name = "documentos_listar";
        t.title = ba::i18n::traducir("mcp", "Los documentos del evento");
        t.category = "documentos";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Los documentos del evento abierto: id, título, descripción, páginas, tamaño, si es público, a qué "
                        "puntos del orden del día y secciones del guion está relacionado y si tiene carátula. Sin relaciones "
                        "es documentación del evento en general. Con `punto` o `seccion`, solo los de ese punto del orden "
                        "del día o esa sección del guion (lo mismo que muestra la web y la fila del punto).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"punto", {{"type", "integer"}, {"description", ba::i18n::traducir("mcp", "número de punto del orden del día")}}},
                           {"seccion", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "id del bloque del guion o su número")}}}}}};
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
        t.title = ba::i18n::traducir("mcp", "Agregar un documento al evento");
        t.category = "documentos";
        t.description = ba::i18n::traducir("mcp", "Agrega un documento a la documentación del evento: un PDF o una imagen tal cual, y un Word, "
                        "planilla, presentación, HTML, Markdown o texto pasado a PDF antes (WordCraft, DeckCraft, GridCraft o Qt, del bundle). "
                        "Lo copia a la carpeta docs/ del evento y "
                        "guarda su carátula (la primera página) para la web. `archivo`: la ruta. `titulo` y `descripcion` "
                        "(si no se dan: el título del PDF; la respuesta trae el comienzo del texto para escribir una "
                        "descripción con documento_editar). Relaciones opcionales.");
        json props = relaciones;
        props["archivo"] = {{"type", "string"}};
        props["titulo"] = {{"type", "string"}};
        props["descripcion"] = {{"type", "string"}};
        t.inputSchema = {{"type", "object"}, {"required", {"archivo"}}, {"properties", props}};
        t.run = [dv](const json& a, Done done) {
            json datos = a;
            datos.erase("archivo");
            const std::string ruta = a.value("archivo", std::string());
            dv->agregarConvirtiendo(std::filesystem::path(std::u8string(ruta.begin(), ruta.end())), datos, [done](const json& r) {
                if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pdf_convertir";
        t.title = ba::i18n::traducir("mcp", "Convertir a PDF");
        t.category = "documentos";
        t.description =
            ba::i18n::traducir("mcp", "Convierte a PDF con lo que viaja en el sistema (sin Chrome ni nada instalado): `archivo` (Word, Excel, "
            "PowerPoint, OpenDocument, RTF, CSV, HTML, Markdown, texto o imagen) o `texto` que escribas vos, con "
            "`formato` markdown (de fábrica), html o texto; cualquier idioma. `destino`: la ruta del .pdf (si no, al lado "
            "del archivo, o en <datos>/Runtime/pdf/ para un texto). Con `agregar_al_evento`, en vez de eso lo suma a la "
            "documentación del evento (con `titulo`, `descripcion`, `puntos`, `secciones`, `publico`) y la web lo "
            "publica. Para un PDF desde HTML usá esto y no Chrome sin pantalla (en macOS pide el llavero).");
        json props = relaciones;
        props["archivo"] = {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "lo que se convierte (o `texto`)")}};
        props["texto"] = {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "el contenido, si no hay archivo")}};
        props["formato"] = {{"type", "string"}, {"enum", {"markdown", "html", "texto"}}};
        props["titulo"] = {{"type", "string"}};
        props["descripcion"] = {{"type", "string"}};
        props["destino"] = {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "la ruta del .pdf")}};
        props["agregar_al_evento"] = {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "sumarlo a la documentación del evento")}};
        t.inputSchema = {{"type", "object"}, {"properties", props}};
        t.run = [dv](const json& a, Done done) {
            namespace fs = std::filesystem;
            const auto deU8 = [](const std::string& x) { return fs::path(std::u8string(x.begin(), x.end())); };
            const auto aQ = [](const fs::path& p) { return QString::fromStdU16String(p.u16string()); };
            const std::string archivo = a.value("archivo", std::string()), texto = a.value("texto", std::string());
            const std::string titulo = a.value("titulo", std::string());
            if (archivo.empty() == texto.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "pasá `archivo` o `texto` (uno de los dos)")));
            const bool alEvento = a.value("agregar_al_evento", false);
            json datos = a;
            for (const char* k : {"archivo", "texto", "formato", "destino", "agregar_al_evento"}) datos.erase(k);
            // Un archivo al evento: el camino de documento_agregar (convierte y agrega).
            if (alEvento && !archivo.empty()) {
                dv->agregarConvirtiendo(deU8(archivo), datos, [done](const json& r) {
                    if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                    done(ToolResult().data(r));
                });
                return;
            }
            const std::string nombre = !titulo.empty() ? titulo : !archivo.empty() ? deU8(archivo).stem().string() : "documento";
            fs::path destino = deU8(a.value("destino", std::string()));
            if (alEvento || destino.empty())
                destino = (!alEvento && !archivo.empty()) ? deU8(archivo).replace_extension(".pdf")
                                                          : ba::platform::dataRoot() / "Runtime" / "pdf" / deU8(nombre + ".pdf");
            if (destino.extension() != ".pdf") destino += ".pdf";
            // Lo que queda hecho: al evento, o la ruta.
            const auto terminar = [dv, alEvento, datos, destino, done](const QString& err) {
                if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                if (!alEvento) return done(ToolResult().data({{"pdf", destino.string()}}));
                dv->agregarConvirtiendo(destino, datos, [destino, done](const json& r) {
                    std::error_code ec;
                    std::filesystem::remove(destino, ec); // ya está copiado en docs/
                    if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                    done(ToolResult().data(r));
                });
            };
            if (!texto.empty()) {
                terminar(APdf::texto(QString::fromStdString(texto), QString::fromStdString(a.value("formato", std::string("markdown"))),
                                     QString::fromStdString(nombre), aQ(destino)));
                return;
            }
            dv->aPdf().archivo(aQ(deU8(archivo)), aQ(destino), terminar);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_editar";
        t.title = ba::i18n::traducir("mcp", "Describir o relacionar un documento");
        t.category = "documentos";
        t.description = ba::i18n::traducir("mcp", "Cambia lo que se pase: `titulo`, `descripcion`, `publico`, `puntos` (del orden del día) y `secciones` "
                        "(del guion; las listas reemplazan a las de antes), y `pagina_tapa` (otra página del PDF como "
                        "carátula: rehace la miniatura). La web del evento se actualiza sola.");
        json props = relaciones;
        props["id"] = {{"type", "string"}};
        props["titulo"] = {{"type", "string"}};
        props["descripcion"] = {{"type", "string"}};
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", props}};
        t.run = [dv](const json& a, Done done) {
            json c = a;
            c.erase("id");
            if (const std::string e = dv->editar(a.value("id", std::string()), c); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_quitar";
        t.title = ba::i18n::traducir("mcp", "Quitar un documento del evento");
        t.category = "documentos";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Lo saca de la documentación del evento (el archivo va a la papelera del evento, se puede recuperar).");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [dv](const json& a, Done done) {
            if (const std::string e = dv->quitar(a.value("id", std::string())); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado (está en la papelera del evento).")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_abrir";
        t.title = ba::i18n::traducir("mcp", "Abrir un documento en pantalla");
        t.category = "documentos";
        t.description = ba::i18n::traducir("mcp", "Abre el documento con el visor del sistema (el visor de PDF), en esta máquina: lo mismo que el "
                        "doble clic en la pestaña Documentos o el botón 📄 de un punto del orden del día.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [dv](const json& a, Done done) {
            const std::string id = a.value("id", std::string());
            if (dv->documento(id).is_null()) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay un documento «%1»"), {id})));
            dv->abrirUi(QString::fromStdString(id));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Abierto.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_texto";
        t.title = ba::i18n::traducir("mcp", "Leer el texto de un documento");
        t.category = "documentos";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "El texto de un PDF del evento, de la página `desde` a `hasta` (por defecto todo): para resumirlo, "
                        "describirlo o saber a qué punto del orden del día corresponde.");
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
    // --- Un PDF en una escena (fuente ba_pdf): elegirlo y pasarle páginas ---------------
    PdfEscena* pe = vm_.pdfEscena();
    if (pe == nullptr) return;
    {
        Tool t;
        t.name = "pdf_escena_ver";
        t.title = ba::i18n::traducir("mcp", "Los PDF en las escenas");
        t.category = "documentos";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Las fuentes PDF de las escenas (ba_pdf: un PDF a pantalla, una página por vez): qué documento y qué "
                        "página muestra cada una (de cuántas), y los PDF de la documentación del evento que se pueden elegir. "
                        "Para mostrar un PDF en vivo sin desarmarlo en láminas.");
        t.run = [pe](const json&, Done done) { done(ToolResult().data(pe->estado())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pdf_escena_poner";
        t.title = ba::i18n::traducir("mcp", "Elegir el PDF o la página de una fuente PDF");
        t.category = "documentos";
        t.description = ba::i18n::traducir("mcp", "En la fuente PDF `fuente` (sin ella, la primera): `documento` (el id de uno de la documentación del "
                        "evento: pdf_escena_ver / documentos_listar) o `archivo` (la ruta de otro PDF), y/o `pagina` (1 = la "
                        "primera) o `paso` (+1 la siguiente, -1 la anterior); y el encuadre: `zoom` (1 = la página entera, "
                        "hasta 8) y `x`, `y` (el centro de lo que se ve, 0 a 1 sobre la página: 0.5, 0.2 = arriba al medio). "
                        "Elegir un documento lo deja en la página 1 y entero. SE VE AL AIRE si esa escena está en el programa.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"fuente", {{"type", "string"}}},
                           {"documento", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}},
                           {"pagina", {{"type", "integer"}, {"minimum", 1}}},
                           {"paso", {{"type", "integer"}}},
                           {"zoom", {{"type", "number"}, {"minimum", 1}, {"maximum", 8}}},
                           {"x", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"y", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}}}};
        t.run = [pe](const json& a, Done done) {
            const QString f = QString::fromStdString(a.value("fuente", std::string()));
            QString e;
            if (a.contains("documento")) e = pe->elegir(f, QString::fromStdString(a.value("documento", std::string())));
            else if (a.contains("archivo")) e = pe->elegirArchivo(f, QString::fromStdString(a.value("archivo", std::string())));
            if (e.isEmpty() && a.contains("pagina")) e = pe->irA(f, a.value("pagina", 1));
            if (e.isEmpty() && a.contains("paso")) e = pe->paso(f, a.value("paso", 1));
            if (e.isEmpty() && (a.contains("zoom") || a.contains("x") || a.contains("y")))
                e = pe->encuadre(f, a.value("zoom", -1.0), a.value("x", -1.0), a.value("y", -1.0));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(pe->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pdf_escena_crear";
        t.title = ba::i18n::traducir("mcp", "Armar una escena para mostrar PDF");
        t.category = "documentos";
        t.description = ba::i18n::traducir("mcp", "Crea la escena `nombre` (de fábrica «PDF») con una fuente PDF a pantalla completa (si ya existe, le "
                        "agrega la fuente si no la tiene). Después: pdf_escena_poner para elegir el documento y pasar páginas, "
                        "y la tarjeta «PDF» del tablero hace lo mismo a mano.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"nombre", {{"type", "string"}}}}}};
        t.run = [pe](const json& a, Done done) {
            const QString e = pe->crearEscena(QString::fromStdString(a.value("nombre", std::string("PDF"))));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(pe->estado()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
