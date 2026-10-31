// ============================================================================
// src/app/mcp/McpCore.cpp — ver McpCore.hpp
// ============================================================================

#include "mcp/McpCore.hpp"

#include "mcp/McpEtiquetas.hpp"
#include "mcp/McpIndice.hpp"
#include "mcp/McpTraza.hpp"
#include "i18n/I18n.hpp"

#include <chrono>

#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"

#include <chrono>

#include <algorithm>
#include <map>
#include <cctype>
#include <memory>
#include <set>
#include <sstream>

namespace ba::app::mcp {

namespace {

const char* kSupportedProtocols[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

json rpcResult(const json& id, json result) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}
json rpcError(const json& id, int code, const std::string& message) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}


std::string typeName(const json& v) {
    if (v.is_string()) return "string";
    if (v.is_boolean()) return "boolean";
    if (v.is_number_integer()) return "integer";
    if (v.is_number()) return "number";
    if (v.is_array()) return "array";
    if (v.is_object()) return "object";
    return "null";
}

bool typeMatches(const std::string& want, const json& v) {
    if (want == "string") return v.is_string();
    if (want == "boolean") return v.is_boolean();
    if (want == "integer")
        return v.is_number_integer() ||
               (v.is_number() && v.get<double>() == static_cast<long long>(v.get<double>()));
    if (want == "number") return v.is_number();
    if (want == "array") return v.is_array();
    if (want == "object") return v.is_object();
    if (want == "null") return v.is_null();
    return true;
}

} // namespace

// --- ToolResult ---------------------------------------------------------------

ToolResult& ToolResult::text(const std::string& t) {
    content.push_back(json{{"type", "text"}, {"text", t}});
    return *this;
}
ToolResult& ToolResult::image(const std::string& base64, const std::string& mime) {
    content.push_back(json{{"type", "image"}, {"data", base64}, {"mimeType", mime}});
    return *this;
}
ToolResult& ToolResult::data(const json& d) {
    structured = d;
    content.push_back(json{{"type", "text"}, {"text", d.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)}});
    return *this;
}
ToolResult ToolResult::error(const std::string& message) {
    ToolResult r;
    r.isError = true;
    r.text(message);
    return r;
}
ToolResult ToolResult::ok(const std::string& message) {
    ToolResult r;
    r.text(message);
    return r;
}

// --- McpCore ------------------------------------------------------------------

McpCore::McpCore() {
    addCategory("ayuda", ba::i18n::traducir("mcp", "Ayuda"), ba::i18n::traducir("mcp", "Guía, temas y fichas de cada herramienta."));
    // La ayuda es parte del protocolo de este servidor: siempre esta.
    Tool help;
    help.name = "help";
    help.title = ba::i18n::traducir("mcp", "Ayuda del appliance");
    help.category = "ayuda";
    help.readOnly = true;
    help.idempotent = true;
    help.description =
        ba::i18n::traducir("mcp", "Sistema de ayuda. Sin argumentos: guia general, flujos de trabajo y catalogo completo de "
        "herramientas "
        "por categoria. Con `topic`: un tema (ej. 'escenas', 'imagenes', 'oradores'). Con `tool`: ficha "
        "completa "
        "de una herramienta (que hace, argumentos, ejemplos). Empeza por aca si no sabes como hacer algo.");
    help.inputSchema =
        json{{"type", "object"},
             {"properties",
              {{"topic", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Id del tema (ver la guia general).")}}},
               {"tool", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Nombre de una herramienta.")}}}}}};
    help.examples = {R"({})", R"({"topic": "imagenes"})", R"({"tool": "scene_define"})"};
    help.run = [this](const json& a, Done done) {
        if (a.contains("tool") && a["tool"].is_string()) {
            const std::string t = helpTool(a["tool"].get<std::string>());
            done(t.empty() ? ToolResult::error(ba::i18n::llenar(
                                 ba::i18n::traducir("mcp", "No existe la herramienta '%1'. Llama `help` sin argumentos "
                                                           "para ver el catalogo, o `help_search`."),
                                 {a["tool"].get<std::string>()}))
                           : ToolResult::ok(t));
            return;
        }
        if (a.contains("topic") && a["topic"].is_string()) {
            const std::string t = helpTopic(a["topic"].get<std::string>());
            done(t.empty() ? ToolResult::error(ba::i18n::llenar(
                                 ba::i18n::traducir("mcp", "No existe el tema '%1'. Los temas estan listados en `help`."),
                                 {a["topic"].get<std::string>()}))
                           : ToolResult::ok(t));
            return;
        }
        done(ToolResult::ok(helpOverview()));
    };
    addTool(std::move(help));

    // Estado de la salvaguarda de la sesion que llama (lo resuelve callTool).
    Tool sec;
    sec.name = "security_status";
    sec.title = ba::i18n::traducir("mcp", "Salvaguarda de esta sesion");
    sec.category = "ayuda";
    sec.readOnly = true;
    sec.description =
        ba::i18n::traducir("mcp", "Dice si esta sesion leyo contenido de los invitados de la conferencia (su chat, nombres, archivos o "
        "video). Si lo leyo, toda accion que modifica algo espera que el operador la apruebe con un "
        "click: es "
        "una regla del servidor, no depende de vos. Para volver a actuar directo hace falta una sesion nueva "
        "(reiniciar el asistente).");
    sec.run = [](const json&, Done done) { done(ToolResult::error(ba::i18n::traducir("mcp", "(la resuelve el servidor)"))); };
    addTool(std::move(sec));

    Tool search;
    search.name = "help_search";
    search.title = ba::i18n::traducir("mcp", "Buscar en la ayuda");
    search.category = "ayuda";
    search.readOnly = true;
    search.idempotent = true;
    search.description =
        ba::i18n::traducir("mcp", "Busca por palabras en todas las herramientas y temas de ayuda (tolera tildes). Devuelve "
        "las mejores coincidencias con su descripcion.");
    search.inputSchema =
        json{{"type", "object"},
             {"properties", {{"query", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Que queres hacer o saber.")}}}}},
             {"required", {"query"}}};
    search.examples = {R"({"query": "poner un texto en pantalla"})",
                       R"({"query": "ver la camara de un invitado"})"};
    search.run = [this](const json& a, Done done) {
        const json hits = this->search(a.value("query", ""), enCurso_.rol);
        if (hits.empty()) {
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Sin coincidencias. Proba con otras palabras o llama `help` para ver todo.")));
            return;
        }
        std::ostringstream out;
        out << ba::i18n::llenar(ba::i18n::traducir("mcp", "Coincidencias para \"%1\":"), {a.value("query", "")}) << "\n";
        for (const auto& h : hits) {
            out << "- [" << h["kind"].get<std::string>() << "] " << h["id"].get<std::string>() << " — "
                << h["title"].get<std::string>();
            // Por que salio. Una busqueda que no dice con que palabra enganchó
            // no se puede corregir: el que pregunta vuelve a probar a ciegas.
            if (h.contains("porque") && h["porque"].is_array() && !h["porque"].empty()) {
                std::string por;
                for (const auto& t : h["porque"]) por += (por.empty() ? "" : ", ") + t.get<std::string>();
                out << "  " << ba::i18n::llenar(ba::i18n::traducir("mcp", "(por: %1)"), {por});
            }
            out << "\n";
        }
        out << "\n" << ba::i18n::traducir("mcp", "Para el detalle: help {\"tool\": ...} o help {\"topic\": ...}.");
        ToolResult r;
        r.text(out.str());
        r.structured = json{{"results", hits}};
        done(r);
    };
    addTool(std::move(search));
}

void McpCore::addInstructions(const std::string& mas) {
    if (mas.empty()) return;
    if (!instructions_.empty()) instructions_ += "\n";
    instructions_ += mas;
}

std::string McpCore::mapaDeCategorias(const std::string& rol) const {
    std::map<std::string, int> cuantas;
    for (const Tool& t : tools_)
        if (permitida(t, rol)) ++cuantas[t.category];
    std::string out =
        rol.empty() ? ba::i18n::traducir("mcp", "## Mapa de herramientas\n\nNo te quedes con lo que ves: si algo no "
                                                "aparece, fijate en que categoria caeria y pedi su ayuda con "
                                                "`help {\"topic\": \"<categoria>\"}`.\n\n")
                    : ba::i18n::llenar(
                          ba::i18n::traducir("mcp", "## Mapa de herramientas\n\nSos el rol `%1`: este es TODO tu juego. "
                                                    "Lo que no esta aca no lo podes hacer vos; si el "
                                                    "encargo lo necesita, pediselo a otro rol con `agente_encargar` o "
                                                    "contalo en tu resumen final para que lo vea el operador.\n\n"),
                          {rol});
    for (const Category& c : categories_) {
        const auto it = cuantas.find(c.id);
        const int n = it == cuantas.end() ? 0 : it->second;
        if (n == 0) continue;
        out += "- **" + c.id + "** (" + std::to_string(n) + "): " + c.title;
        if (!c.summary.empty()) out += " — " + c.summary;
        out += "\n";
    }
    return out;
}

void McpCore::setServerInfo(std::string name, std::string version, std::string instructions) {
    name_ = std::move(name);
    version_ = std::move(version);
    instructions_ = std::move(instructions);
}

json McpCore::catalogo() const {
    json out = json::array();
    std::map<std::string, std::size_t> donde;
    for (const auto& c : categories_) {
        donde[c.id] = out.size();
        out.push_back({{"id", c.id}, {"titulo", c.title}, {"resumen", c.summary}, {"herramientas", json::array()}});
    }
    for (const auto& t : tools_) {
        auto it = donde.find(t.category);
        if (it == donde.end()) {
            it = donde.find("");
            if (it == donde.end()) {
                it = donde.emplace("", out.size()).first;
                out.push_back({{"id", ""},
                               {"titulo", ba::i18n::traducir("mcp", "Otras")},
                               {"resumen", ""},
                               {"herramientas", json::array()}});
            }
        }
        std::string etiquetas;
        for (const auto& e : etiquetasDe(t.name, t.antes)) etiquetas += (etiquetas.empty() ? "" : " ") + e;
        // El primer ejemplo, si es JSON: precarga los argumentos.
        std::string ejemplo;
        for (const auto& e : t.examples)
            if (json::parse(e, nullptr, false).is_object()) {
                ejemplo = e;
                break;
            }
        out[it->second]["herramientas"].push_back({{"nombre", t.name},
                                                   {"titulo", t.title.empty() ? t.name : t.title},
                                                   {"descripcion", t.description},
                                                   {"etiquetas", etiquetas},
                                                   {"ejemplo", ejemplo},
                                                   {"solo_lectura", t.readOnly},
                                                   {"destructiva", t.destructive}});
    }
    json sin = json::array();
    for (auto& c : out)
        if (!c["herramientas"].empty()) sin.push_back(std::move(c));
    return sin;
}

void McpCore::addCategory(const std::string& id, const std::string& title, const std::string& summary) {
    categories_.push_back(Category{id, title, summary});
}

void McpCore::verificarJuegos() const {
    // AVISA SI UN JUEGO NOMBRA UNA CATEGORIA QUE NO EXISTE. Este error es mudo:
    // la categoria inventada no rompe nada, simplemente esas herramientas
    // dejan de existir para ese rol. Paso de verdad —«media» por «medios»,
    // «sistema» por «equipo»— y el asistente se quedo sin poder poner musica ni
    // mirar el disco sin que nada fallara.
    //
    // SE LLAMA AL FINAL, con todas registradas. Puesto dentro de `setRoles`
    // avisaba de `qr`, `rostros` y `ventanas` —que existen— solo porque sus
    // herramientas se registran despues.
    std::set<std::string> reales;
    for (const Tool& t : tools_)
        reales.insert(t.category);
    // Una categoria que usan herramientas pero nadie registro: caen en «Otras»,
    // fuera del mapa del asistente y de las secciones de la Botonera.
    std::set<std::string> registradas;
    for (const Category& c : categories_) registradas.insert(c.id);
    for (const std::string& c : reales)
        if (registradas.count(c) == 0)
            ba::diag::registro().linea(
                ba::diag::Nivel::Aviso, "mcp", 0,
                ba::i18n::llenar(ba::i18n::traducir("mcp", "la categoria '%1' la usan herramientas pero no esta "
                                                           "registrada (falta addCategory)"),
                                 {c}));
    // El asistente interactivo ve todo lo que se opera: una categoria nueva que no
    // esta en su juego no existe para el (ni en help_search). Ver Roles::juegoDelAsistente.
    if (const auto as = roles_.find("asistente"); as != roles_.end())
        for (const std::string& c : reales)
            if (as->second.count(c) == 0 && comunes_.count(c) == 0) {
                int n = 0;
                for (const Tool& t : tools_) n += t.category == c ? 1 : 0;
                ba::diag::registro().linea(
                    ba::diag::Nivel::Aviso, "mcp", 0,
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "el asistente no ve la categoria '%1' (%2 herramientas): "
                                                               "sumarla a Roles::juegoDelAsistente si se opera"),
                                     {c, std::to_string(n)}));
            }
    for (const auto& [rol, cats] : roles_)
        for (const std::string& c : cats)
            if (reales.count(c) == 0)
                ba::diag::registro().linea(
                    ba::diag::Nivel::Aviso, "mcp", 0,
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "el juego de '%1' nombra la categoria '%2', que no la "
                                                               "usa ninguna herramienta (ver addCategory)"),
                                     {rol, c}));
}

void McpCore::setRoles(std::map<std::string, std::set<std::string>> categoriasPorRol,
                       std::set<std::string> comunes) {
    roles_ = std::move(categoriasPorRol);
    comunes_ = std::move(comunes);
}

bool McpCore::permitida(const Tool& t, const std::string& rol) const {
    if (rol.empty()) return true;
    if (comunes_.count(t.category) != 0) return true;
    const auto it = roles_.find(rol);
    return it != roles_.end() && it->second.count(t.category) != 0;
}

void McpCore::addTool(Tool tool) {
    // Una herramienta de salida acepta `salida` sin que cada una lo escriba en su
    // esquema: así dicen todas lo mismo y ninguna se olvida.
    if (tool.porSalida) {
        if (!tool.inputSchema.contains("properties") || !tool.inputSchema["properties"].is_object())
            tool.inputSchema["properties"] = json::object();
        if (!tool.inputSchema["properties"].contains("salida"))
            tool.inputSchema["properties"]["salida"] = {
                {"type", "string"},
                {"description", ba::i18n::traducir("mcp", "Salida (id o nombre; `salidas_listar`). Sin ella, la por "
                                                          "omisión (o la de quien llama).")}};
    }
    tools_.push_back(std::move(tool));
}

std::string McpCore::salidaPedida(const json& args, const CallContext& ctx) {
    if (args.is_object())
        if (const auto it = args.find("salida"); it != args.end() && it->is_string() && !it->get<std::string>().empty())
            return it->get<std::string>();
    return ctx.salida;
}

std::string McpCore::resolverSalida(const std::string& pedida, std::string& err) const {
    err.clear();
    if (pedida.empty() || !salidasLista_) return pedida;
    const auto bajar = [](std::string x) {
        for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x;
    };
    const json lista = salidasLista_();
    std::string hay;
    for (const auto& s : lista) {
        const std::string id = s.value("id", std::string()), nombre = s.value("nombre", std::string());
        if (id == pedida || (!nombre.empty() && bajar(nombre) == bajar(pedida)))
            return s.value("por_omision", false) ? std::string() : id;
        hay += (hay.empty() ? "" : ", ") + id + (nombre.empty() || nombre == id ? "" : " (" + nombre + ")");
    }
    err = ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la salida '%1'. Salidas: %2. `salidas_listar` las muestra."),
                           {pedida, hay});
    return {};
}

std::string McpCore::salidaDe(const json& args) const {
    std::string err;
    return resolverSalida(salidaPedida(args, enCurso_), err);
}
void McpCore::addTopic(HelpTopic topic) { topics_.push_back(std::move(topic)); }
void McpCore::addResource(Resource resource) { resources_.push_back(std::move(resource)); }
void McpCore::addPrompt(Prompt prompt) { prompts_.push_back(std::move(prompt)); }

const Tool* McpCore::findTool(const std::string& name) const {
    for (const Tool& t : tools_)
        if (t.name == name) return &t;
    for (const Tool& t : tools_)
        if (std::find(t.antes.begin(), t.antes.end(), name) != t.antes.end()) return &t;
    return nullptr;
}

std::string McpCore::nombreActual(const std::string& name) const {
    const Tool* t = findTool(name);
    return t != nullptr ? t->name : name;
}

bool McpCore::agregarNombreAnterior(const std::string& actual, const std::string& viejo) {
    for (Tool& t : tools_) {
        if (t.name != actual) continue;
        if (std::find(t.antes.begin(), t.antes.end(), viejo) == t.antes.end()) t.antes.push_back(viejo);
        return true;
    }
    return false;
}

// --- Validacion ---------------------------------------------------------------

namespace {
std::string validarObjeto(const json& schema, const json& obj, const std::string& ruta);

/// Un valor contra su esquema (tipo, enum, mínimo y máximo) y, si es un objeto o
/// una lista, lo de adentro: un enum anidado (`gente[0].papel`) se respeta igual.
std::string validarValor(const json& p, const json& v, const std::string& ruta) {
    const auto dump = [](const json& j) { return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); };
    if (p.contains("type")) {
        bool ok = false;
        std::string want;
        if (p["type"].is_array()) {
            for (const auto& t : p["type"]) {
                want += (want.empty() ? "" : "|") + t.get<std::string>();
                ok = ok || typeMatches(t.get<std::string>(), v);
            }
        } else {
            want = p["type"].get<std::string>();
            ok = typeMatches(want, v);
        }
        if (!ok)
            return ba::i18n::llenar(ba::i18n::traducir("mcp", "`%1` debe ser %2 (llego %3)"),
                                    {ruta, want, typeName(v)});
    }
    if (p.contains("enum") && std::find(p["enum"].begin(), p["enum"].end(), v) == p["enum"].end())
        return ba::i18n::llenar(ba::i18n::traducir("mcp", "`%1` debe ser uno de %2"), {ruta, dump(p["enum"])});
    if (v.is_number()) {
        if (p.contains("minimum") && v.get<double>() < p["minimum"].get<double>())
            return ba::i18n::llenar(ba::i18n::traducir("mcp", "`%1` minimo %2"), {ruta, dump(p["minimum"])});
        if (p.contains("maximum") && v.get<double>() > p["maximum"].get<double>())
            return ba::i18n::llenar(ba::i18n::traducir("mcp", "`%1` maximo %2"), {ruta, dump(p["maximum"])});
    }
    if (v.is_object() && p.contains("properties")) return validarObjeto(p, v, ruta + ".");
    if (v.is_array() && p.contains("items") && p["items"].is_object())
        for (std::size_t i = 0; i < v.size(); ++i)
            if (std::string e = validarValor(p["items"], v[i], ruta + "[" + std::to_string(i) + "]"); !e.empty()) return e;
    return {};
}

/// `ruta`: el prefijo de los nombres en los mensajes ("" arriba, "gente[0]." adentro).
std::string validarObjeto(const json& schema, const json& obj, const std::string& ruta) {
    const json props = schema.value("properties", json::object());
    for (const auto& req : schema.value("required", json::array())) {
        const std::string k = req.get<std::string>();
        if (!obj.contains(k) || obj[k].is_null())
            return ba::i18n::llenar(ba::i18n::traducir("mcp", "falta el argumento obligatorio `%1`"), {ruta + k});
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!props.contains(it.key())) {
            if (schema.value("additionalProperties", true) == false)
                return ba::i18n::llenar(ba::i18n::traducir("mcp", "argumento desconocido `%1`"), {ruta + it.key()});
            continue;
        }
        if (std::string e = validarValor(props[it.key()], it.value(), ruta + it.key()); !e.empty()) return e;
    }
    return {};
}
} // namespace

std::string McpCore::validate(const json& schema, const json& args) {
    if (!args.is_object()) return ba::i18n::traducir("mcp", "los argumentos deben ser un objeto JSON");
    return validarObjeto(schema, args, "");
}

// --- JSON-RPC -----------------------------------------------------------------

void McpCore::handle(const json& message, std::function<void(json)> reply, const CallContext& ctx) {
    if (message.is_array()) {
        if (message.empty()) {
            reply(rpcError(nullptr, -32600, ba::i18n::traducir("mcp", "lote vacio")));
            return;
        }
        // Lote: se junta cada respuesta (pueden llegar en otro orden/momento).
        struct Batch {
            json out = json::array();
            size_t pending = 0;
            std::function<void(json)> reply;
        };
        auto batch = std::make_shared<Batch>();
        batch->pending = message.size();
        batch->reply = std::move(reply);
        for (const auto& m : message) {
            handleOne(
                m,
                [batch](json r) {
                    if (!r.is_null()) batch->out.push_back(std::move(r));
                    if (--batch->pending == 0) batch->reply(batch->out.empty() ? json() : batch->out);
                },
                ctx);
        }
        return;
    }
    handleOne(message, std::move(reply), ctx);
}

void McpCore::startIntake(const std::string& id, const std::string& name) { intake_[id] = name; }

void McpCore::endIntake(const std::string& id) { intake_.erase(id); }

std::vector<std::string> McpCore::intakes() const {
    std::vector<std::string> out;
    for (const auto& [id, name] : intake_)
        out.push_back(id);
    return out;
}

bool McpCore::intakeAllows(const Tool& t, const nlohmann::json& args) const {
    if (intake_.empty()) return false;
    // Solo lo que hace falta para atender a alguien del lobby.
    static const std::set<std::string> kIntakeTools{"chat_send", "participant_profile", "participant_admit",
                                                    "participant_talk"};
    if (kIntakeTools.count(t.name) == 0) return false;
    // Y solo si la accion apunta a esa persona. Lo resuelve quien conoce la
    // sala (el nombre puede haber cambiado al completar la ficha).
    if (!args.is_object()) return false;
    if (intakeMatch_ && intakeMatch_(t.name, args)) return true;
    for (const auto& [key, v] : args.items()) {
        if (!v.is_string()) continue;
        const std::string val = v.get<std::string>();
        for (const auto& [id, name] : intake_)
            if (val == id || val == name) return true;
    }
    return false;
}

void McpCore::taint(const std::string& session, const std::string& source) {
    if (taint_.count(session)) return; // se recuerda lo primero que la contamino
    taint_[session] =
        Taint{source, static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch())
                                                    .count())};
}

bool McpCore::tainted(const std::string& session) const { return taint_.count(session) != 0; }

json McpCore::taintInfo(const std::string& session) const {
    const auto it = taint_.find(session);
    if (it == taint_.end()) return {{"session", session}, {"tainted", false}};
    return {
        {"session", session}, {"tainted", true}, {"source", it->second.source}, {"atMs", it->second.atMs}};
}

void McpCore::handleOne(const json& msg, std::function<void(json)> reply, const CallContext& ctx) {
    if (!msg.is_object() || msg.value("jsonrpc", "") != "2.0") {
        reply(rpcError(msg.is_object() && msg.contains("id") ? msg["id"] : json(), -32600,
                       ba::i18n::traducir("mcp", "no es JSON-RPC 2.0")));
        return;
    }
    const bool isRequest = msg.contains("id") && !msg["id"].is_null();
    if (!msg.contains("method")) {
        reply(json()); // respuesta del cliente a algo nuestro: no hay nada que contestar
        return;
    }
    const std::string method = msg.value("method", "");
    const json id = isRequest ? msg["id"] : json();
    const json params = msg.value("params", json::object());
    if (!isRequest) {
        // Notificaciones (initialized, cancelled, roots/list_changed...): sin respuesta.
        Traza::la().anotar("   · " + method + " (aviso) " + Traza::resumir(params));
        reply(json());
        return;
    }
    // TODO lo que entra y sale queda escrito, con sus argumentos y su
    // respuesta. Ver McpTraza.hpp: sin esto, «lo acepte y no lo metio a la
    // sala» no se puede contestar mirando ningun archivo.
    const std::string quien = ctx.rol.empty() ? std::string() : " [" + ctx.rol + "]";
    const std::string etiqueta =
        method +
        (method == "tools/call" && params.contains("name") && params["name"].is_string()
             ? ":" + params["name"].get<std::string>()
             : std::string()) +
        quien;
    Traza::la().anotar("--> " + etiqueta + " id=" + id.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + " " + Traza::resumir(params));
    const auto arranque = std::chrono::steady_clock::now();
    reply = [reply, etiqueta, id, arranque](json r) {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - arranque)
                .count();
        // `isError` de una herramienta viaja DENTRO del resultado: un fallo de
        // herramienta es una respuesta valida de JSON-RPC. Si solo se mirara
        // `error`, la traza diria «OK» de cada herramienta que fallo.
        const bool fallo =
            r.contains("error") || (r.contains("result") && r["result"].value("isError", false));
        Traza::la().anotar((fallo ? "<-- FALLO " : "<-- ok    ") + etiqueta + " id=" + id.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + " (" +
                           std::to_string(ms) + " ms) " + Traza::resumir(r));
        reply(std::move(r));
    };
    try {
        if (method == "initialize") {
            const json ci = params.value("clientInfo", json::object());
            if (ci.is_object() && ci.contains("name")) {
                std::string quien = ci.value("name", std::string("?"));
                if (ci.contains("version") && ci["version"].is_string()) quien += " " + ci["version"].get<std::string>();
                clientes_[ctx.session] = quien;
                // Con rol y sesión, con uno solo o con ninguno: cada frase entera.
                const std::string ses = ctx.session.empty() ? std::string() : ctx.session.substr(0, 8);
                const std::string texto =
                    !ctx.rol.empty() && !ses.empty()
                        ? ba::i18n::llenar(ba::i18n::traducir("mcp", "se conectó «%1» con el rol %2 (sesión %3)"),
                                           {quien, ctx.rol, ses})
                    : !ctx.rol.empty()
                        ? ba::i18n::llenar(ba::i18n::traducir("mcp", "se conectó «%1» con el rol %2"), {quien, ctx.rol})
                    : !ses.empty()
                        ? ba::i18n::llenar(ba::i18n::traducir("mcp", "se conectó «%1» (sesión %2)"), {quien, ses})
                        : ba::i18n::llenar(ba::i18n::traducir("mcp", "se conectó «%1»"), {quien});
                ba::diag::registro().linea(ba::diag::Nivel::Info, "mcp", 0, texto);
            }
            reply(rpcResult(id, initializeResult(params, ctx)));
        } else if (method == "ping") {
            reply(rpcResult(id, json::object()));
        } else if (method == "tools/list") {
            json list = json::array();
            for (const Tool& t : tools_)
                if (permitida(t, ctx.rol)) list.push_back(toolListEntry(t));
            reply(rpcResult(id, json{{"tools", list}}));
        } else if (method == "tools/call") {
            if (!params.contains("name") || !params["name"].is_string()) {
                reply(rpcError(id, -32602, ba::i18n::traducir("mcp", "falta `name`")));
                return;
            }
            callTool(params, [id, reply](json result) { reply(rpcResult(id, std::move(result))); }, ctx);
        } else if (method == "resources/list") {
            reply(rpcResult(id, json{{"resources", resourceList()}}));
        } else if (method == "resources/templates/list") {
            reply(rpcResult(id, json{{"resourceTemplates",
                                      json::array({json{{"uriTemplate", "ba://ayuda/{tema}"},
                                                        {"name", "Tema de ayuda"},
                                                        {"mimeType", "text/markdown"}},
                                                   json{{"uriTemplate", "ba://ayuda/herramienta/{nombre}"},
                                                        {"name", "Ficha de una herramienta"},
                                                        {"mimeType", "text/markdown"}}})}}));
        } else if (method == "resources/read") {
            std::string err;
            const json r = readResource(params.value("uri", ""), err);
            // El estado de la sala trae nombres/datos escritos por invitados.
            if (params.value("uri", "") == "ba://sala") taint(ctx.session, "ba://sala");
            if (!err.empty()) {
                reply(rpcError(id, -32002, err));
                return;
            }
            reply(rpcResult(id, r));
        } else if (method == "prompts/list") {
            json list = json::array();
            for (const Prompt& p : prompts_) {
                list.push_back(json{{"name", p.name},
                                    {"title", p.title},
                                    {"description", p.description},
                                    {"arguments", p.arguments}});
            }
            reply(rpcResult(id, json{{"prompts", list}}));
        } else if (method == "prompts/get") {
            const std::string name = params.value("name", "");
            const auto it = std::find_if(prompts_.begin(), prompts_.end(),
                                         [&](const Prompt& p) { return p.name == name; });
            if (it == prompts_.end()) {
                reply(rpcError(id, -32602, ba::i18n::llenar(ba::i18n::traducir("mcp", "no existe el prompt '%1'"), {name})));
                return;
            }
            const std::string text = it->render(params.value("arguments", json::object()));
            reply(rpcResult(
                id,
                json{{"description", it->description},
                     {"messages", json::array({json{{"role", "user"},
                                                    {"content", {{"type", "text"}, {"text", text}}}}})}}));
        } else if (method == "logging/setLevel") {
            reply(rpcResult(id, json::object()));
        } else if (method == "completion/complete") {
            reply(rpcResult(id, json{{"completion", {{"values", json::array()}, {"hasMore", false}}}}));
        } else {
            reply(rpcError(id, -32601, ba::i18n::llenar(ba::i18n::traducir("mcp", "metodo desconocido: %1"), {method})));
        }
    } catch (const std::exception& e) {
        reply(rpcError(id, -32603, ba::i18n::llenar(ba::i18n::traducir("mcp", "error interno: %1"), {e.what()})));
    }
}

json McpCore::initializeResult(const json& params, const CallContext& ctx) const {
    // Negociacion: la version del cliente si la soportamos; si no, la ultima.
    std::string version = kLatestProtocol;
    const std::string asked = params.value("protocolVersion", "");
    for (const char* v : kSupportedProtocols)
        if (asked == v) version = asked;
    return json{{"protocolVersion", version},
                {"capabilities",
                 {{"tools", {{"listChanged", false}}},
                  {"resources", {{"subscribe", false}, {"listChanged", false}}},
                  {"prompts", {{"listChanged", false}}},
                  {"logging", json::object()}}},
                {"serverInfo", {{"name", name_}, {"title", "Broadcast Appliance"}, {"version", version_}}},
                // El mapa se arma ACA y no de antemano: es distinto para cada
                // rol, y para un cliente sin rol es el completo.
                {"instructions", instructions_ + "\n" + mapaDeCategorias(ctx.rol)}};
}

json McpCore::toolListEntry(const Tool& t) const {
    // MEDIDO Y DESCARTADO: marcar aca «[SOLO MIRA]» / «[HACE UN CAMBIO]» segun
    // `readOnly` no mejora que el modelo elija bien —dio 43/67 contra 45/67— y
    // cuesta tokens en cada pedido. Con el pensamiento apagado no razona «me
    // piden hacer algo, elijo una que hace»: empareja las palabras del pedido
    // con las de la descripcion. Lo que funciona es que las descripciones NO SE
    // PISEN, herramienta por herramienta (ver tools/entrenar/bateria-modelo.py).
    json e{
        {"name", t.name}, {"title", t.title}, {"description", t.description}, {"inputSchema", t.inputSchema}};
    // Las cuatro marcas van SIEMPRE, aunque repitan el valor por defecto: en
    // el protocolo `destructiveHint` y `openWorldHint` valen true si no estan,
    // asi que omitirlas no ahorra bytes, les cambia el sentido.
    //
    // Lo que si se saca es el titulo repetido adentro de las anotaciones: ya
    // viaja arriba, y el listado entero se manda en CADA pedido al modelo.
    e["annotations"] = json{{"readOnlyHint", t.readOnly},
                            {"destructiveHint", t.destructive},
                            {"idempotentHint", t.idempotent},
                            {"openWorldHint", t.openWorld}};
    return e;
}

namespace {
/// El primer texto de una respuesta, para la linea del registro. Cuando una
/// herramienta falla, el motivo esta ahi: sin el, el registro dice «FALLO» y
/// hay que abrir la traza para saber por que.
std::string primerTexto(const ToolResult& r) {
    for (const auto& c : r.content)
        if (c.value("type", "") == "text") {
            std::string t = c.value("text", std::string());
            if (t.size() > 200) t = t.substr(0, 200) + "…";
            return t;
        }
    return {};
}
} // namespace

void McpCore::callTool(const json& params, std::function<void(json)> done, const CallContext& ctx) {
    // Con un nombre viejo, todo lo que sigue (aire, aprobaciones, registro) con el actual.
    const std::string name = nombreActual(params["name"].get<std::string>());
    const json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                                      : json::object();
    // Un aviso que va al final de la respuesta (p. ej. argumentos que no existen).
    auto nota = std::make_shared<std::string>();
    const auto finish = [done, nota](const ToolResult& r) {
        json out{{"content", r.content}, {"isError", r.isError}};
        if (!r.structured.is_null()) out["structuredContent"] = r.structured;
        if (!nota->empty() && out["content"].is_array())
            out["content"].push_back({{"type", "text"}, {"text", *nota}});
        done(out);
    };
    const Tool* tool = findTool(name);
    if (tool == nullptr) {
        const json hits = search(name, ctx.rol);
        std::string hint;
        for (const auto& h : hits)
            if (h["kind"] == "herramienta") hint += " " + h["id"].get<std::string>();
        // Al registro general TAMBIEN, y como aviso: que alguien pida una
        // herramienta que no existe casi siempre quiere decir que falta
        // registrarla, no que el que llamo se equivoco. Eso hay que verlo sin
        // tener que abrir la traza.
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "mcp", 0,
            ba::i18n::llenar(ba::i18n::traducir("mcp", "NO EXISTE la herramienta '%1'%2%3"),
                             {name, ctx.rol.empty() ? std::string() : " [" + ctx.rol + "]",
                              hint.empty() ? std::string()
                                           : ba::i18n::llenar(ba::i18n::traducir("mcp", " (parecidas:%1)"), {hint})}));
        finish(ToolResult::error(
            hint.empty()
                ? ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la herramienta '%1'. `help` lista todas."), {name})
                : ba::i18n::llenar(ba::i18n::traducir(
                                       "mcp", "No existe la herramienta '%1'. ¿Quisiste decir:%2? `help` lista todas."),
                                   {name, hint})));
        return;
    }
    // El rol de los disparadores: todo menos lo que corta o desarma (salvo lo
    // que la regla permita con `permitir_aire`; y en vivo igual pasa por la
    // guarda de aire).
    if (ctx.rol == "disparador") {
        static const std::set<std::string> kNo{"stream_stop",      "record_stop",  "app_quit",
                                               "gateway_restart",  "participant_kick", "settings_set",
                                               "agente_configurar", "mcp_aire_ajustar"};
        const bool negada = kNo.count(name) != 0 || name.rfind("plugin_", 0) == 0;
        if (negada && ctx.permitidas.count(name) == 0) {
            ToolResult r = ToolResult::error(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "El rol `disparador` no puede usar '%1'. Si la regla lo necesita, agregalo "
                                          "en `permitir_aire` (fuera del aire)."),
                {name}));
            r.structured = json{{"bloqueada_por_rol", true}};
            finish(r);
            return;
        }
    } else if (!permitida(*tool, ctx.rol)) {
        finish(
            ToolResult::error(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "La herramienta '%1' (categoria `%2`) no esta en el juego del rol `%3`. "
                                          "`agente_roles` dice que roles tienen esa categoria: si alguno la tiene, "
                                          "pediselo con `agente_encargar`; si ninguno, no se puede desde aca: decile al "
                                          "operador que hace falta y no improvises otro camino."),
                {name, tool->category, ctx.rol})));
        return;
    }
    const std::string problem = validate(tool->inputSchema, args);
    if (!problem.empty()) {
        finish(ToolResult::error(ba::i18n::llenar(
            ba::i18n::traducir("mcp", "Argumentos invalidos para %1: %2. Ficha completa: help {\"tool\": \"%1\"}."),
            {name, problem})));
        return;
    }
    // La salida de una herramienta de salida: que exista ANTES de aprobar nada ni
    // correr (con una sola salida y sin pedir ninguna, no cambia nada).
    std::string salida;
    if (tool->porSalida) {
        std::string err;
        salida = resolverSalida(salidaPedida(args, ctx), err);
        if (!err.empty()) {
            finish(ToolResult::error(err));
            return;
        }
    }
    // Un argumento que la herramienta no conoce se ignora… y se avisa: si no,
    // un nombre equivocado (`name` en vez de `fuente`) pasa callado y la
    // herramienta hace otra cosa (mira el programa en vez de la escena pedida).
    if (const json props = tool->inputSchema.value("properties", json::object());
        props.is_object() && !props.empty()) {
        std::string ignorados, acepta;
        for (auto it = args.begin(); it != args.end(); ++it)
            if (!props.contains(it.key())) ignorados += (ignorados.empty() ? "`" : ", `") + it.key() + "`";
        if (!ignorados.empty()) {
            for (auto it = props.begin(); it != props.end(); ++it)
                acepta += (acepta.empty() ? "`" : ", `") + it.key() + "`";
            *nota = ba::i18n::llenar(ba::i18n::traducir("mcp", "OJO: %1 no conoce %2 (se ignoró). Acepta: %3."),
                                     {name, ignorados, acepta});
        }
    }
    // Estado de la salvaguarda de ESTA sesion (lo sabe el servidor, no el modelo).
    if (name == "security_status") {
        const bool t = tainted(ctx.session);
        const char* pol = policy_ == ApprovalPolicy::Always  ? "siempre"
                          : policy_ == ApprovalPolicy::Never ? "nunca"
                                                             : "invitados";
        finish(
            ToolResult()
                .text(t ? ba::i18n::traducir("mcp", "Esta sesion leyo contenido de invitados de la conferencia: toda "
                                                    "accion que modifica algo necesita que el operador la apruebe con "
                                                    "un click.")
                        : ba::i18n::llenar(ba::i18n::traducir("mcp", "Esta sesion no leyo contenido de invitados: las "
                                                                     "acciones se ejecutan directo (politica: %1)."),
                                           {pol}))
                .data(json{{"policy", pol}, {"session", taintInfo(ctx.session)}}));
        return;
    }
    // Una herramienta que falla por un JSON inesperado no tumba nada: se
    // informa como error de la herramienta.
    const std::string session = ctx.session;
    const bool untrustedOut = tool->untrustedOutput;
    const bool modifica = mutating(*tool);
    const auto execute = [this, tool, args, finish, session, untrustedOut, name, modifica, ctx]() {
        // QUIEN ESTA LLAMANDO, mientras dura la llamada. Se limpiaba al final
        // pero no se ponia nunca, asi que `contextoEnCurso()` siempre devolvia
        // vacio: el buscador no filtraba por rol y el cerrojo de imagenes no
        // dejaba pasar ninguna, ni siquiera al rol que mira. Un campo que solo
        // se limpia es un campo que no existe.
        enCurso_ = ctx;
        // Para el vigía del bucle de la interfaz: si esta herramienta lo demora, se sabe cuál.
        ba::diag::VigiaBucle::interfaz().anotar("mcp " + name);
        auto called = std::make_shared<bool>(false);
        // TODO lo que entra por MCP queda escrito. Es el plano de control del
        // aparato —por acá cambian escenas, salidas y pantallas, y a veces lo
        // maneja un asistente—, asi que «quien hizo que» tiene que poder
        // contestarse despues. Es el unico lugar por donde pasan todas: las
        // recetas y los agentes tambien entran por aca.
        //
        // Lo que MODIFICA va como info; lo que solo consulta, como depuracion:
        // un `status` cada dos segundos ahogaria el registro a nivel normal.
        const auto arranque = std::chrono::steady_clock::now();
        // Quién: el rol, de dónde salió (asistente, receta, disparador, botonera…) y, si vino de
        // afuera, el cliente que se presentó al conectarse.
        std::string quien;
        if (!ctx.rol.empty()) quien += " [" + ctx.rol + (ctx.asistente.empty() ? "" : ":" + ctx.asistente) + "]";
        if (!ctx.origen.empty()) quien += " <" + ctx.origen + ">";
        if (const auto c = clientes_.find(ctx.session); c != clientes_.end()) quien += " «" + c->second + "»";
        else if (ctx.origen.empty() && !ctx.session.empty())
            quien += ba::i18n::llenar(ba::i18n::traducir("mcp", " (sesión %1)"), {ctx.session.substr(0, 8)});
        // Los argumentos van en la linea: «audio_set FALLO» sin decir de que
        // canal ni a que valor no sirve para entender nada despues.
        const std::string resumenArgs = Traza::resumir(args, 220);
        try {
            tool->run(args, [this, finish, called, session, untrustedOut, name, modifica, arranque, quien,
                             resumenArgs](ToolResult r) {
                if (*called) return; // una sola respuesta por pedido
                *called = true;
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - arranque)
                                    .count();
                ba::diag::registro().linea(
                    r.isError  ? ba::diag::Nivel::Aviso
                    : modifica ? ba::diag::Nivel::Info
                               : ba::diag::Nivel::Depuracion,
                    "mcp", 0,
                    r.isError ? ba::i18n::llenar(ba::i18n::traducir("mcp", "%1%2 FALLO (%3 ms) %4 -> %5"),
                                                 {name, quien, std::to_string(ms), resumenArgs, primerTexto(r)})
                              : name + quien + " (" + std::to_string(ms) + " ms) " + resumenArgs);
                if (untrustedOut || r.untrusted) taint(session, name);
                // Si modifico algo y salio bien, las vistas se enteran ANTES de
                // que se conteste: asi no hay un instante en el que el
                // asistente ya sabe algo que la pantalla todavia no muestra.
                // Ver setSincronizador.
                if (modifica && !r.isError && sincronizar_) sincronizar_();
                finish(r);
            });
        } catch (const std::exception& e) {
            if (!*called) {
                *called = true;
                ba::diag::registro().linea(
                    ba::diag::Nivel::Error, "mcp", 0,
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "%1%2 reviento: %3"), {name, quien, e.what()}));
                finish(ToolResult::error(
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "La herramienta fallo: %1"), {e.what()})));
            }
        }
        enCurso_ = CallContext{};
    };
    // Salvaguarda: en una sesion contaminada (o con politica "siempre"), una
    // accion que modifica algo espera la aprobacion de una persona.
    // Guardas de aire: con la transmisión o la grabación andando, lo que cambia
    // el aire pedido por algo automático espera SIEMPRE a una persona, sin
    // importar política, rol ni contaminación. La voz y los disparadores, en
    // vivo, solo leen o hacen lo reversible sin preguntar.
    // Una herramienta de salida es de aire si sale SU salida (otra transmitiendo no
    // la vuelve de aire); las demás, si sale cualquiera.
    const bool enAire = !ctx.humano && (tool->porSalida && salidaSale_ ? salidaSale_(salida) : hayAire());
    const bool deAire = enAire && aire_.esDeAire(name, args);
    const bool automaticoEnVivo = enAire && !deAire && mutating(*tool) &&
                                  (ctx.origen == "voz" || ctx.origen == "disparador") && !reversibleEnVivo(name);
    const bool needApproval =
        deAire || automaticoEnVivo ||
        (mutating(*tool) && !ctx.humano && // una persona apretando no es el asistente
         (policy_ == ApprovalPolicy::Always || (policy_ == ApprovalPolicy::Untrusted && tainted(session))) &&
         !intakeAllows(*tool, args));
    if (!needApproval) {
        execute();
        return;
    }
    if (sinAprobaciones_) {
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "mcp", 0,
            deAire || automaticoEnVivo
                ? ba::i18n::llenar(ba::i18n::traducir("mcp", "MODO PRUEBA: %1 se ejecuta sin aprobación (acción de aire)"),
                                   {name})
                : ba::i18n::llenar(ba::i18n::traducir("mcp", "MODO PRUEBA: %1 se ejecuta sin aprobación"), {name}));
        execute();
        return;
    }
    const std::string quienPide = ctx.origen.empty() ? ba::i18n::traducir("mcp", "el asistente") : ctx.origen;
    if (!approver_) {
        ToolResult r = ToolResult::error(
            deAire || automaticoEnVivo
                ? ba::i18n::llenar(
                      ba::i18n::traducir("mcp", "Accion de AIRE bloqueada (%1, pedida por %2): con la transmision o la "
                                                "grabacion andando necesita que una persona la confirme, y no "
                                                "hay a quien preguntarle. Pedile al operador que la haga el."),
                      {name, quienPide})
                : ba::i18n::traducir("mcp", "Accion bloqueada: esta sesion leyo contenido de invitados y no hay a quien "
                                            "pedirle aprobacion. Pedile al operador que la haga el."));
        if (deAire || automaticoEnVivo) r.structured = json{{"necesita_confirmacion", true}, {"motivo", "de aire"}};
        finish(r);
        return;
    }
    // El prefijo «ACCIÓN DE AIRE: » NO se traduce: es la marca con la que el que
    // arma la tarjeta (McpService::setApprover) sabe que es de aire. Lo de después sí.
    const std::string kDeAire = "ACCIÓN DE AIRE: ";
    const std::string reason =
        deAire ? kDeAire + ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» cambia lo que sale y la pidió %2. Con "
                                                                      "la transmisión o la grabación andando, siempre "
                                                                      "la confirma una persona."),
                                            {name, quienPide})
        : automaticoEnVivo
            ? kDeAire + ba::i18n::llenar(ba::i18n::traducir("mcp", "en vivo, %1 solo hace lo reversible sin "
                                                                   "preguntar; «%2» necesita tu confirmación."),
                                         {quienPide, name})
        : policy_ == ApprovalPolicy::Always
            ? ba::i18n::traducir("mcp", "La politica pide aprobar toda accion del asistente.")
            : ba::i18n::llenar(ba::i18n::traducir("mcp", "El asistente leyo contenido de invitados de la conferencia "
                                                         "(%1): por seguridad, lo que modifica algo necesita tu "
                                                         "aprobacion."),
                               {taint_[session].source});
    approver_(*tool, args, reason, [execute, finish, name](bool ok) {
        if (ok) {
            execute();
        } else {
            finish(ToolResult::error(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "El operador NO aprobo %1. No reintentes la misma accion: preguntale que "
                                          "queres hacer (operator_ask) o segui con otra cosa."),
                {name})));
        }
    });
}

void McpCore::pedirAprobacion(const std::string& herramienta, const json& args, const std::string& motivo,
                              std::function<void(bool)> decide) {
    if (sinAprobaciones_) {
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "mcp", 0,
            ba::i18n::llenar(ba::i18n::traducir("mcp", "MODO PRUEBA: %1 sigue sin la aprobación de una persona (%2)"),
                             {herramienta, motivo}));
        decide(true);
        return;
    }
    const Tool* t = findTool(herramienta);
    if (!approver_ || t == nullptr) {
        decide(false);
        return;
    }
    approver_(*t, args, motivo, std::move(decide));
}

json McpCore::resourceList() const {
    json list = json::array();
    for (const HelpTopic& t : topics_) {
        list.push_back(json{{"uri", "ba://ayuda/" + t.id},
                            {"name", "ayuda-" + t.id},
                            {"title", ba::i18n::llenar(ba::i18n::traducir("mcp", "Ayuda: %1"), {t.title})},
                            {"description", t.summary},
                            {"mimeType", "text/markdown"}});
    }
    for (const Resource& r : resources_) {
        list.push_back(
            json{{"uri", r.uri}, {"name", r.name}, {"description", r.description}, {"mimeType", r.mimeType}});
    }
    return list;
}

json McpCore::readResource(const std::string& uri, std::string& error) const {
    const auto contents = [&](const std::string& text, const std::string& mime) {
        return json{{"contents", json::array({json{{"uri", uri}, {"mimeType", mime}, {"text", text}}})}};
    };
    static const std::string kHelpTool = "ba://ayuda/herramienta/";
    static const std::string kHelp = "ba://ayuda/";
    if (uri.rfind(kHelpTool, 0) == 0) {
        const std::string t = helpTool(uri.substr(kHelpTool.size()));
        if (t.empty()) error = ba::i18n::traducir("mcp", "no existe esa herramienta");
        return contents(t, "text/markdown");
    }
    if (uri.rfind(kHelp, 0) == 0) {
        const std::string id = uri.substr(kHelp.size());
        const std::string t = id == "indice" ? helpOverview() : helpTopic(id);
        if (t.empty()) error = ba::i18n::traducir("mcp", "no existe ese tema");
        return contents(t, "text/markdown");
    }
    for (const Resource& r : resources_) {
        if (r.uri == uri) return contents(r.read ? r.read() : std::string(), r.mimeType);
    }
    error = ba::i18n::llenar(ba::i18n::traducir("mcp", "recurso desconocido: %1"), {uri});
    return json();
}

// --- Ayuda ----------------------------------------------------------------------

std::string McpCore::helpOverview() const {
    std::ostringstream o;
    o << ba::i18n::traducir("mcp", "# Broadcast Appliance — guia para operar con herramientas") << "\n\n";
    if (!instructions_.empty()) o << instructions_ << "\n\n";
    o << ba::i18n::traducir("mcp", "## Temas de ayuda (help {\"topic\": \"<id>\"})") << "\n";
    for (const HelpTopic& t : topics_)
        o << "- `" << t.id << "` — " << t.title << ": " << t.summary << "\n";
    o << "\n" << ba::i18n::traducir("mcp", "## Herramientas por categoria (help {\"tool\": \"<nombre>\"} para la ficha)")
      << "\n";
    for (const Category& c : categories_) {
        std::vector<const Tool*> in;
        for (const Tool& t : tools_)
            if (t.category == c.id) in.push_back(&t);
        if (in.empty()) continue;
        o << "\n### " << c.title << "\n" << c.summary << "\n";
        for (const Tool* t : in) {
            std::string first = t->description.substr(0, t->description.find(". ") + 1);
            if (first.size() <= 1) first = t->description;
            o << "- `" << t->name << "`" << (t->readOnly ? "" : " ✎") << " — " << first << "\n";
        }
    }
    o << "\n" << ba::i18n::traducir("mcp", "(✎ = cambia algo en la app; el resto solo consulta.)") << "\n";
    if (!prompts_.empty()) {
        o << "\n" << ba::i18n::traducir("mcp", "## Guias paso a paso (prompts MCP)") << "\n";
        for (const Prompt& p : prompts_)
            o << "- `" << p.name << "` — " << p.description << "\n";
    }
    return o.str();
}

std::string McpCore::helpTopic(const std::string& id) const {
    for (const HelpTopic& t : topics_) {
        if (t.id == id) return "# " + t.title + "\n\n" + t.body;
    }
    return {};
}

std::string McpCore::helpTool(const std::string& name) const {
    const Tool* t = findTool(name);
    if (t == nullptr) return {};
    std::ostringstream o;
    o << "# " << t->name << " — " << t->title << "\n\n" << t->description << "\n";
    if (!t->details.empty()) o << "\n" << t->details << "\n";
    const std::string tipo = t->readOnly ? ba::i18n::traducir("mcp", "solo consulta") : ba::i18n::traducir("mcp", "cambia la app");
    o << "\n"
      << (t->destructive
              ? ba::i18n::llenar(ba::i18n::traducir("mcp", "**Tipo:** %1 (destructiva: borra, corta o saca algo)"), {tipo})
              : ba::i18n::llenar(ba::i18n::traducir("mcp", "**Tipo:** %1"), {tipo}))
      << "\n";
    const json props = t->inputSchema.value("properties", json::object());
    if (!props.empty()) {
        o << "\n" << ba::i18n::traducir("mcp", "## Argumentos") << "\n";
        const json req = t->inputSchema.value("required", json::array());
        for (auto it = props.begin(); it != props.end(); ++it) {
            const json& p = it.value();
            const bool r = std::find(req.begin(), req.end(), it.key()) != req.end();
            const std::string tipoArg = p.contains("type")
                                            ? p["type"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
                                            : ba::i18n::traducir("mcp", "cualquiera");
            o << "- `" << it.key() << "` ("
              << (r ? ba::i18n::llenar(ba::i18n::traducir("mcp", "%1, obligatorio"), {tipoArg})
                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "%1, opcional"), {tipoArg}))
              << ")";
            if (p.contains("description")) o << ": " << p["description"].get<std::string>();
            if (p.contains("enum"))
                o << " "
                  << ba::i18n::llenar(ba::i18n::traducir("mcp", "Valores: %1."),
                                      {p["enum"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
            if (p.contains("default"))
                o << " "
                  << ba::i18n::llenar(ba::i18n::traducir("mcp", "Por defecto: %1."),
                                      {p["default"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
            o << "\n";
        }
    } else {
        o << "\n" << ba::i18n::traducir("mcp", "Sin argumentos.") << "\n";
    }
    if (!t->examples.empty()) {
        o << "\n" << ba::i18n::traducir("mcp", "## Ejemplos") << "\n";
        for (const std::string& e : t->examples)
            o << "```json\n" << e << "\n```\n";
    }
    o << "\n" << ba::i18n::traducir("mcp", "## Esquema JSON") << "\n```json\n"
      << t->inputSchema.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n```\n";
    return o.str();
}

void McpCore::armarIndice() const {
    indice_ = std::make_unique<Indice>();
    // Una herramienta sin etiquetas solo se encuentra si alguien adivina las
    // palabras de su documentacion, que son las de quien ya sabia como se
    // llamaba. No falla nada: simplemente no aparece nunca en una busqueda.
    // Por eso se avisa acá, donde se arma el indice, y no en una prueba con
    // una lista aparte que hay que acordarse de actualizar.
    std::vector<std::string> sinEtiquetas;
    for (const Tool& t : tools_) {
        Documento d;
        d.id = t.name;
        d.clase = "herramienta";
        d.nombre = t.name;
        d.titulo = t.title;
        d.etiquetas = etiquetasDe(t.name, t.antes);
        if (d.etiquetas.empty()) sinEtiquetas.push_back(t.name);
        d.cuerpo = t.description + " " + t.details;
        indice_->agregar(std::move(d));
    }
    if (!sinEtiquetas.empty()) {
        std::string lista;
        for (const std::string& n : sinEtiquetas)
            lista += (lista.empty() ? "" : ", ") + n;
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "mcp", 0,
            ba::i18n::llenar(ba::i18n::traducir("mcp", "sin etiquetas (no se van a encontrar buscando; ver "
                                                       "McpEtiquetas.cpp): %1"),
                             {lista}));
    }
    for (const HelpTopic& t : topics_) {
        Documento d;
        d.id = t.id;
        d.clase = "tema";
        d.nombre = t.id;
        d.titulo = t.title;
        d.cuerpo = t.summary + " " + t.body;
        indice_->agregar(std::move(d));
    }
    indice_->armar();
    indiceDe_ = tools_.size() + topics_.size();
}

json McpCore::search(const std::string& query, const std::string& rol) const {
    // Se arma una sola vez, y se rearma si aparecieron herramientas nuevas
    // (las extensiones se registran despues del arranque).
    if (!indice_ || indiceDe_ != tools_.size() + topics_.size()) armarIndice();
    json out = json::array();
    // PRIMERO EL GRUPO, DESPUES LA BUSQUEDA. Buscar sobre todo y despues
    // contestarle a un rol cosas que no puede llamar es peor que inutil: el rol
    // `vision` —que solo mira imagenes— recibia `audio_eq` y `audio_module_add`
    // como respuesta, y si las intentaba rebotaban. Ademas le gasta el lugar a
    // las que si puede usar, que es justo lo que se esta tratando de cuidar.
    // Se pide de mas al indice porque el filtro saca de la lista.
    for (const Resultado& r : indice_->buscar(query, rol.empty() ? 12 : 40)) {
        const Documento& d = indice_->doc(r.doc);
        if (!rol.empty() && d.clase == "herramienta") {
            const Tool* t = findTool(d.id);
            if (t == nullptr || !permitida(*t, rol)) continue;
        }
        if (out.size() >= 12) break;
        out.push_back(json{{"kind", d.clase},
                           {"id", d.id},
                           {"title", d.titulo},
                           {"score", static_cast<int>(r.puntaje * 10)},
                           {"porque", r.porque}});
    }
    return out;
}

json McpCore::buscarHerramientas(const std::string& query, std::size_t tope) const {
    if (!indice_ || indiceDe_ != tools_.size() + topics_.size()) armarIndice();
    json out = json::array();
    for (const Resultado& r : indice_->buscar(query, tope + topics_.size())) {
        const Documento& d = indice_->doc(r.doc);
        if (d.clase != "herramienta") continue;
        out.push_back({{"nombre", d.id}, {"puntaje", r.puntaje}, {"porque", r.porque}});
        if (out.size() >= tope) break;
    }
    return out;
}

} // namespace ba::app::mcp
