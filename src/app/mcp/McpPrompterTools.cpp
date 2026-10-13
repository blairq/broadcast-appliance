// ============================================================================
// src/app/mcp/McpPrompterTools.cpp — el teleprompter, por MCP
//
// Lo mismo que hace la pagina Teleprompter: los discursos (que viven en la
// carpeta del evento, no en una biblioteca aparte), quien esta conectado, y
// el control. Todo pasa por el MISMO ViewModel que la pantalla, asi que lo
// que cambia el asistente se ve en el acto y no hay dos verdades.
//
// Para que sirve de verdad: escribir y acortar discursos. "Este discurso dura
// nueve minutos y tengo seis" es exactamente el trabajo que conviene delegar,
// y para hacerlo hace falta leer el texto, medirlo y volver a escribirlo.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/Discursos.hpp"
#include "ui/PrompterViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// El estado como lo ve un cliente MCP. Aparte de la herramienta porque se
/// arma despues de una consulta asincrona.
json estadoDe(PrompterViewModel& p) {
    json conectados = json::array();
    for (const QVariant& v : p.conectados()) {
        const QVariantMap m = v.toMap();
        conectados.push_back(json{{"vista", m.value("vista").toString().toStdString()},
                                  {"nombre", m.value("nombre").toString().toStdString()},
                                  {"ip", m.value("ip").toString().toStdString()}});
    }
    const QVariantMap e = p.estado();
    return json{{"enLinea", p.enLinea()},
                {"cargado", p.cargado().toStdString()},
                {"corriendo", e.value("corriendo", false).toBool()},
                {"palabras", e.value("palabras", 0).toInt()},
                {"ppm", e.value("ppm", Discursos::kPpm).toInt()},
                {"avancePct", static_cast<int>(p.avance() * 100)},
                {"faltaSeg", p.faltaSeg()},
                {"duracionSeg", e.value("duracionSeg", 0).toInt()},
                {"aviso", e.value("aviso").toString().toStdString()},
                {"conectados", conectados},
                {"oradores", p.oradores()},
                {"controles", p.controles()}};
}

} // namespace

void McpService::registerPrompterTools() {
    core_.addCategory("prompter", "Teleprompter",
                      "Los discursos que lee el orador (carpeta del evento), el control del texto "
                      "(arrancar, velocidad, avisos) y qué pantallas hay conectadas.");
    auto* p = static_cast<PrompterViewModel*>(vm_.prompter());
    if (p == nullptr) return;

    {
        Tool t;
        t.name = "prompter_estado";
        t.title = "Como esta el teleprompter";
        t.category = "prompter";
        t.readOnly = true;
        t.description = "Que discurso esta cargado, si esta corriendo, por donde va, a que velocidad, "
                        "cuanto falta, y que pantallas hay conectadas (atril y control).";
        t.details = "Antes de que alguien suba al atril, lo que importa es `oradores`: si es 0, la tablet "
                    "esta apagada o nadie abrio el enlace. Eso conviene decirlo AHORA, no con el orador "
                    "parado ahi.";
        // Se le pregunta al gateway ANTES de contestar: la pagina solo consulta
        // mientras esta a la vista, y por MCP el estado tiene que estar fresco
        // aunque el operador este mirando otra pantalla.
        t.run = [p](const json&, Done done) {
            p->consultar([p, done](bool) { done(ToolResult().data(estadoDe(*p))); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prompter_documentos";
        t.title = "Los discursos que hay";
        t.category = "prompter";
        t.readOnly = true;
        t.description = "Los discursos de una carpeta de Discursos/ (del evento abierto), con cuantas "
                        "palabras tienen y cuanto dura leerlos. Sin `carpeta`, los de la raiz.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"carpeta", {{"type", "string"}, {"default", ""}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p->sinEvento().isEmpty()) {
                done(ToolResult::error(p->sinEvento().toStdString()));
                return;
            }
            const QString antes = p->carpeta();
            p->setCarpeta(QString::fromStdString(a.value("carpeta", "")));
            json docs = json::array();
            for (const QVariant& v : p->documentos()) {
                const QVariantMap m = v.toMap();
                docs.push_back(json{{"rel", m.value("rel").toString().toStdString()},
                                    {"palabras", m.value("palabras").toInt()},
                                    {"duracionSeg", m.value("duracionSeg").toInt()},
                                    {"cargado", m.value("cargado").toBool()}});
            }
            json carpetas = json::array();
            for (const QString& c : p->carpetas())
                carpetas.push_back(c.toStdString());
            p->setCarpeta(antes); // la pantalla queda donde estaba el operador
            done(ToolResult().data(
                json{{"carpeta", a.value("carpeta", "")}, {"documentos", docs}, {"carpetas", carpetas}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prompter_leer";
        t.title = "Leer un discurso";
        t.category = "prompter";
        t.readOnly = true;
        t.description = "El texto completo de un discurso, por su ruta relativa "
                        "(\"intendente.txt\" o \"Apertura/intendente.txt\").";
        t.inputSchema = {
            {"type", "object"}, {"required", {"rel"}}, {"properties", {{"rel", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString antes = p->abierto();
            p->abrir(QString::fromStdString(a["rel"].get<std::string>()));
            if (p->abierto() != QString::fromStdString(a["rel"].get<std::string>())) {
                done(ToolResult::error("No pude abrir '" + a["rel"].get<std::string>() +
                                       "'. prompter_documentos lista los que hay."));
                return;
            }
            ToolResult r;
            r.data(json{{"rel", p->abierto().toStdString()},
                        {"texto", p->texto().toStdString()},
                        {"palabras", p->palabras()},
                        {"duracionSeg", p->duracionSeg()}});
            (void)antes; // queda abierto: es lo que el operador va a querer ver
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prompter_escribir";
        t.title = "Escribir un discurso";
        t.category = "prompter";
        t.description = "Escribe (o crea) un discurso y lo guarda en la carpeta del evento. PISA lo que "
                        "hubiera. No lo manda al teleprompter: eso es prompter_cargar.";
        t.details = "Para escribir o acortar un discurso: prompter_leer, contar cuanto dura "
                    "(140 palabras por minuto), reescribir y guardar. Un renglon vacio separa parrafos, "
                    "y el control del operador puede saltar de parrafo en parrafo: conviene que cada uno "
                    "sea una idea. Nada de markdown ni asteriscos: esto se lee en voz alta y en la tablet "
                    "se ve tal cual.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"rel", "texto"}},
                         {"properties", {{"rel", {{"type", "string"}}}, {"texto", {{"type", "string"}}}}}};
        t.examples = {
            R"({"rel": "Apertura/intendente.txt", "texto": "Buenas noches a todos.\n\nGracias por venir."})"};
        t.run = [p](const json& a, Done done) {
            const QString rel = QString::fromStdString(a["rel"].get<std::string>());
            const QString texto = QString::fromStdString(a["texto"].get<std::string>());
            // Por RUTA, no por nombre: "Apertura/intendente.txt" crea la
            // carpeta si falta. (`crear` le sacaria la barra: toma un nombre
            // suelto para la carpeta que se esta mirando.)
            const QString err = p->escribirDoc(rel, texto);
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            const int palabras = Discursos::contarPalabras(texto);
            done(ToolResult::ok("Guardado.")
                     .data(json{{"rel", rel.toStdString()},
                                {"palabras", palabras},
                                {"duracionSeg", static_cast<int>(palabras * 60.0 / Discursos::kPpm)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prompter_cargar";
        t.title = "Mandar un discurso al teleprompter";
        t.category = "prompter";
        t.description = "Manda un discurso a la pantalla del orador. Queda LISTO pero PARADO: arrancarlo "
                        "es otra cosa (prompter_control).";
        t.inputSchema = {
            {"type", "object"}, {"required", {"rel"}}, {"properties", {{"rel", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString err = p->cargar(QString::fromStdString(a["rel"].get<std::string>()));
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            done(ToolResult::ok("Cargado y parado. El orador ya lo ve en el atril."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prompter_control";
        t.title = "Manejar el texto que esta al aire";
        t.category = "prompter";
        t.description =
            "Arranca o para el texto, cambia la velocidad, salta a una parte o le escribe un aviso al "
            "orador. Lo que no se manda, no se toca. `panico` para en seco sin mover la posicion.";
        t.details = "Cuidado con `corriendo`: del otro lado hay una persona leyendo en voz alta. Arrancar "
                    "o parar el texto sin que lo pida es de las pocas cosas que se notan al aire al "
                    "instante. La velocidad de fabrica (140 ppm) es la de una lectura normal en "
                    "castellano; si el orador va mas lento, bajala de a 10.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"corriendo", {{"type", "boolean"}}},
                           {"ppm", {{"type", "integer"}, {"minimum", 40}, {"maximum", 400}}},
                           {"avancePct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                           {"parrafo", {{"type", "integer"}, {"minimum", 0}}},
                           {"aviso", {{"type", "string"}, {"description", "vacio lo saca"}}},
                           {"panico", {{"type", "boolean"}}},
                           {"espejo", {{"type", "boolean"}, {"description", "texto espejado (para el vidrio del prompter)"}}}}}};
        t.examples = {R"({"aviso": "Redondeá"})", R"({"ppm": 130})"};
        t.run = [p](const json& a, Done done) {
            // El espejo es de la pantalla del prompter: vale aunque la sala no esté corriendo.
            if (a.contains("espejo")) {
                p->setEspejo(a.value("espejo", false));
                if (a.size() == 1) return done(ToolResult::ok(a.value("espejo", false) ? "Espejado." : "Sin espejo."));
            }
            if (!p->enLinea()) {
                done(ToolResult::error("El teleprompter no contesta: la sala no esta corriendo."));
                return;
            }
            std::string hecho;
            if (a.value("panico", false)) {
                p->panico();
                hecho += "parado en seco; ";
            }
            if (a.contains("ppm")) {
                p->setPpm(a["ppm"].get<int>());
                hecho += "velocidad " + std::to_string(a["ppm"].get<int>()) + " ppm; ";
            }
            if (a.contains("avancePct")) {
                p->irA(a["avancePct"].get<int>() / 100.0);
                hecho += "saltado al " + std::to_string(a["avancePct"].get<int>()) + "%; ";
            }
            if (a.contains("parrafo")) {
                p->irAParrafo(a["parrafo"].get<int>());
                hecho += "al parrafo " + std::to_string(a["parrafo"].get<int>()) + "; ";
            }
            if (a.contains("aviso")) {
                p->avisar(QString::fromStdString(a["aviso"].get<std::string>()));
                hecho += a["aviso"].get<std::string>().empty()
                             ? "aviso sacado; "
                             : "aviso \"" + a["aviso"].get<std::string>() + "\"; ";
            }
            if (a.contains("corriendo")) {
                p->arrancar(a["corriendo"].get<bool>());
                hecho += a["corriendo"].get<bool>() ? "arrancado; " : "parado; ";
            }
            if (hecho.empty()) {
                done(ToolResult::error(
                    "No mandaste nada que hacer. Ver help {\"tool\": \"prompter_control\"}."));
                return;
            }
            done(ToolResult::ok(hecho));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
