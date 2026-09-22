// ============================================================================
// src/app/mcp/McpPantallaTools.cpp — las pantallas externas, por MCP
//
// Lo mismo que la pagina Pantallas y por el mismo ViewModel: lo que cambia el
// asistente se ve en la pantalla en el acto.
//
// Para que sirve: "poné el QR de la sala en el televisor del hall", "mostrá
// «Volvemos en 10 minutos» mientras cortamos", "el televisor del fondo que
// muestre el teleprompter". Son cosas que el operador pediria hablando
// mientras tiene las manos en otra cosa.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerPantallaTools() {
    core_.addCategory("pantallas", "Pantallas externas",
                      "Televisores y proyectores de la sala (Chromecast): cuales hay y que mostrar en "
                      "cada uno (un mensaje, un QR, la hora, una pagina, el teleprompter).");
    auto* p = static_cast<PantallasViewModel*>(vm_.pantallas());
    if (p == nullptr) return;

    {
        Tool t;
        t.name = "pantalla_listar";
        t.title = "Que pantallas hay";
        t.category = "pantallas";
        t.readOnly = true;
        t.description = "Las pantallas conocidas con su estado (libre, mostrando, ocupada con otra cosa, "
                        "no contesta) y que tienen puesto. Tambien que se puede mostrar en ellas.";
        t.details = "`estado` dice mucho: `ocupada` es que el televisor esta con otra aplicacion (Netflix, "
                    "YouTube) y mandarle algo se la va a pisar — avisale al operador antes. `apagada` es "
                    "que no contesta: puede estar apagado de verdad o en otra red.";
        t.run = [p](const json&, Done done) {
            json ps = json::array();
            for (const QVariant& v : p->pantallas()) {
                const QVariantMap m = v.toMap();
                ps.push_back(json{{"id", m.value("id").toString().toStdString()},
                                  {"nombre", m.value("nombre").toString().toStdString()},
                                  {"modelo", m.value("modelo").toString().toStdString()},
                                  {"host", m.value("host").toString().toStdString()},
                                  {"estado", m.value("estado").toString().toStdString()},
                                  {"muestra", m.value("que").toString().toStdString()},
                                  {"error", m.value("error").toString().toStdString()}});
            }
            json cs = json::array();
            for (const QVariant& v : p->contenidos()) {
                const QVariantMap m = v.toMap();
                json campos = json::array();
                for (const QVariant& f : m.value("campos").toList())
                    campos.push_back(f.toMap().value("clave").toString().toStdString());
                json c{{"tipo", m.value("id").toString().toStdString()},
                       {"que_es", m.value("ayuda").toString().toStdString()},
                       {"campos", campos}};
                if (!m.value("disponible").toBool())
                    c["todavia_no"] = m.value("porque").toString().toStdString();
                cs.push_back(c);
            }
            done(ToolResult().data(json{{"pantallas", ps}, {"sePuedeMostrar", cs}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_buscar";
        t.title = "Buscar televisores en la red";
        t.category = "pantallas";
        t.description = "Busca Chromecast en la red local (tarda unos segundos). Las que aparezcan quedan "
                        "en la lista; volve a mirar con pantalla_listar.";
        t.run = [p](const json&, Done done) {
            p->buscar();
            done(ToolResult::ok("Buscando. En unos segundos preguntale a pantalla_listar que aparecio."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_mostrar";
        t.title = "Mostrar algo en una pantalla";
        t.category = "pantallas";
        t.description =
            "Le manda un contenido a una pantalla. `tipo`: mensaje (texto grande), qr (un QR a pantalla "
            "completa), reloj, url (una direccion cualquiera) o prompter (la vista del orador). Los campos "
            "de cada tipo salen de pantalla_listar.";
        t.details = "Ojo con la que este `ocupada`: mostrarle algo le pisa lo que estuviera pasando. Si no "
                    "te lo pidieron explicitamente, preguntale al operador con `operator_ask`.\n\n"
                    "El texto se lee DE LEJOS: frases cortas y pocas palabras. «Volvemos en 10 minutos» "
                    "sirve; un parrafo, no.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla", "tipo"}},
            {"properties",
             {{"pantalla", {{"type", "string"}, {"description", "id o nombre, como lo da pantalla_listar"}}},
              {"tipo", {{"type", "string"}, {"enum", {"mensaje", "qr", "reloj", "url", "prompter"}}}},
              {"texto", {{"type", "string"}, {"description", "mensaje: lo que dice; qr: lo que codifica"}}},
              {"pie", {{"type", "string"}, {"description", "la linea de abajo, mas chica"}}},
              {"url", {{"type", "string"}, {"description", "solo con tipo=url"}}}}}};
        t.examples = {R"({"pantalla": "TV del hall", "tipo": "mensaje", "texto": "Volvemos en 10 minutos"})",
                      R"({"pantalla": "TV del hall", "tipo": "qr", "texto": "https://...", "pie": "Escaneá para entrar"})"};
        t.run = [p](const json& a, Done done) {
            // Por id o por nombre: el operador (y el asistente) piensan en
            // "el televisor del hall", no en un id.
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            QString id;
            for (const QVariant& v : p->pantallas()) {
                const QVariantMap m = v.toMap();
                if (m.value("id").toString() == quien ||
                    m.value("nombre").toString().compare(quien, Qt::CaseInsensitive) == 0) {
                    id = m.value("id").toString();
                    break;
                }
            }
            if (id.isEmpty()) {
                done(ToolResult::error("No conozco la pantalla '" + quien.toStdString() +
                                       "'. pantalla_listar dice cuales hay; pantalla_buscar busca de nuevo."));
                return;
            }
            QVariantMap datos;
            for (const char* k : {"texto", "pie", "url"})
                if (a.contains(k) && a[k].is_string())
                    datos[k] = QString::fromStdString(a[k].get<std::string>());
            const QString err = p->mostrar(id, QString::fromStdString(a["tipo"].get<std::string>()), datos);
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            done(ToolResult::ok("Mandado a «" + id.toStdString() +
                                "». Tarda un par de segundos: pantalla_listar dice como salio."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_parar";
        t.title = "Dejar de mostrar";
        t.category = "pantallas";
        t.idempotent = true;
        t.description = "La pantalla vuelve a lo que tenia antes (su fondo de pantalla).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"pantalla"}},
                         {"properties", {{"pantalla", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            for (const QVariant& v : p->pantallas()) {
                const QVariantMap m = v.toMap();
                if (m.value("id").toString() == quien ||
                    m.value("nombre").toString().compare(quien, Qt::CaseInsensitive) == 0) {
                    p->parar(m.value("id").toString());
                    done(ToolResult::ok("Parando «" + m.value("nombre").toString().toStdString() + "»."));
                    return;
                }
            }
            done(ToolResult::error("No conozco la pantalla '" + quien.toStdString() + "'."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
