// ============================================================================
// src/app/mcp/McpCore.cpp — ver McpCore.hpp
// ============================================================================

#include "mcp/McpCore.hpp"

#include <chrono>

#include <algorithm>
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

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Quita tildes comunes (busqueda tolerante: "transmision" == "transmisión").
std::string fold(const std::string& s) {
    static const std::pair<const char*, char> kMap[] = {{"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'},
                                                        {"ú", 'u'}, {"ñ", 'n'}, {"Á", 'a'}, {"É", 'e'},
                                                        {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ñ", 'n'}};
    std::string out = lower(s);
    for (const auto& [from, to] : kMap) {
        const std::string f = from;
        size_t p = 0;
        while ((p = out.find(f, p)) != std::string::npos) {
            out.replace(p, f.size(), 1, to);
            ++p;
        }
    }
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : fold(s)) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int score(const std::vector<std::string>& q, const std::string& title, const std::string& body) {
    static const std::set<std::string> kStop{"un",   "una", "el",   "la",  "los", "las", "de", "del", "en",
                                             "con",  "por", "para", "que", "y",   "o",   "a",  "al",  "mi",
                                             "como", "se",  "lo",   "le",  "es",  "the", "to", "of",  "and"};
    const std::string t = fold(title);
    const std::string b = fold(body);
    int s = 0;
    for (const std::string& w : q) {
        if (w.size() < 2 || kStop.count(w)) continue;
        if (t.find(w) != std::string::npos) s += 5;
        if (b.find(w) != std::string::npos) s += 1;
    }
    return s;
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
    content.push_back(json{{"type", "text"}, {"text", d.dump(2)}});
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
    addCategory("ayuda", "Ayuda", "Guia, temas y fichas de cada herramienta.");
    // La ayuda es parte del protocolo de este servidor: siempre esta.
    Tool help;
    help.name = "help";
    help.title = "Ayuda del appliance";
    help.category = "ayuda";
    help.readOnly = true;
    help.idempotent = true;
    help.description =
        "Sistema de ayuda. Sin argumentos: guia general, flujos de trabajo y catalogo completo de "
        "herramientas "
        "por categoria. Con `topic`: un tema (ej. 'escenas', 'imagenes', 'oradores'). Con `tool`: ficha "
        "completa "
        "de una herramienta (que hace, argumentos, ejemplos). Empeza por aca si no sabes como hacer algo.";
    help.inputSchema =
        json{{"type", "object"},
             {"properties",
              {{"topic", {{"type", "string"}, {"description", "Id del tema (ver la guia general)."}}},
               {"tool", {{"type", "string"}, {"description", "Nombre de una herramienta."}}}}}};
    help.examples = {R"({})", R"({"topic": "imagenes"})", R"({"tool": "scene_define"})"};
    help.run = [this](const json& a, Done done) {
        if (a.contains("tool") && a["tool"].is_string()) {
            const std::string t = helpTool(a["tool"].get<std::string>());
            done(t.empty() ? ToolResult::error("No existe la herramienta '" + a["tool"].get<std::string>() +
                                               "'. Llama `help` sin argumentos para ver el catalogo, o "
                                               "`help_search`.")
                           : ToolResult::ok(t));
            return;
        }
        if (a.contains("topic") && a["topic"].is_string()) {
            const std::string t = helpTopic(a["topic"].get<std::string>());
            done(t.empty() ? ToolResult::error("No existe el tema '" + a["topic"].get<std::string>() +
                                               "'. Los temas estan listados en `help`.")
                           : ToolResult::ok(t));
            return;
        }
        done(ToolResult::ok(helpOverview()));
    };
    addTool(std::move(help));

    // Estado de la salvaguarda de la sesion que llama (lo resuelve callTool).
    Tool sec;
    sec.name = "security_status";
    sec.title = "Salvaguarda de esta sesion";
    sec.category = "ayuda";
    sec.readOnly = true;
    sec.description =
        "Dice si esta sesion leyo contenido de los invitados de la sala (su chat, nombres, archivos o "
        "video). Si lo leyo, toda accion que modifica algo espera que el operador la apruebe con un "
        "click: es "
        "una regla del servidor, no depende de vos. Para volver a actuar directo hace falta una sesion nueva "
        "(reiniciar el asistente).";
    sec.run = [](const json&, Done done) { done(ToolResult::error("(la resuelve el servidor)")); };
    addTool(std::move(sec));

    Tool search;
    search.name = "help_search";
    search.title = "Buscar en la ayuda";
    search.category = "ayuda";
    search.readOnly = true;
    search.idempotent = true;
    search.description =
        "Busca por palabras en todas las herramientas y temas de ayuda (tolera tildes). Devuelve "
        "las mejores coincidencias con su descripcion.";
    search.inputSchema =
        json{{"type", "object"},
             {"properties", {{"query", {{"type", "string"}, {"description", "Que queres hacer o saber."}}}}},
             {"required", {"query"}}};
    search.examples = {R"({"query": "poner un texto en pantalla"})",
                       R"({"query": "ver la camara de un invitado"})"};
    search.run = [this](const json& a, Done done) {
        const json hits = this->search(a.value("query", ""));
        if (hits.empty()) {
            done(ToolResult::ok("Sin coincidencias. Proba con otras palabras o llama `help` para ver todo."));
            return;
        }
        std::ostringstream out;
        out << "Coincidencias para \"" << a.value("query", "") << "\":\n";
        for (const auto& h : hits) {
            out << "- [" << h["kind"].get<std::string>() << "] " << h["id"].get<std::string>() << " — "
                << h["title"].get<std::string>() << "\n";
        }
        out << "\nPara el detalle: help {\"tool\": ...} o help {\"topic\": ...}.";
        ToolResult r;
        r.text(out.str());
        r.structured = json{{"results", hits}};
        done(r);
    };
    addTool(std::move(search));
}

void McpCore::setServerInfo(std::string name, std::string version, std::string instructions) {
    name_ = std::move(name);
    version_ = std::move(version);
    instructions_ = std::move(instructions);
}

void McpCore::addCategory(const std::string& id, const std::string& title, const std::string& summary) {
    categories_.push_back(Category{id, title, summary});
}

void McpCore::addTool(Tool tool) { tools_.push_back(std::move(tool)); }
void McpCore::addTopic(HelpTopic topic) { topics_.push_back(std::move(topic)); }
void McpCore::addResource(Resource resource) { resources_.push_back(std::move(resource)); }
void McpCore::addPrompt(Prompt prompt) { prompts_.push_back(std::move(prompt)); }

const Tool* McpCore::findTool(const std::string& name) const {
    for (const Tool& t : tools_)
        if (t.name == name) return &t;
    return nullptr;
}

// --- Validacion ---------------------------------------------------------------

std::string McpCore::validate(const json& schema, const json& args) {
    if (!args.is_object()) return "los argumentos deben ser un objeto JSON";
    const json props = schema.value("properties", json::object());
    for (const auto& req : schema.value("required", json::array())) {
        const std::string k = req.get<std::string>();
        if (!args.contains(k) || args[k].is_null()) return "falta el argumento obligatorio `" + k + "`";
    }
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!props.contains(it.key())) {
            if (schema.value("additionalProperties", true) == false)
                return "argumento desconocido `" + it.key() + "`";
            continue;
        }
        const json& p = props[it.key()];
        const json& v = it.value();
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
            if (!ok) return "`" + it.key() + "` debe ser " + want + " (llego " + typeName(v) + ")";
        }
        if (p.contains("enum") && std::find(p["enum"].begin(), p["enum"].end(), v) == p["enum"].end())
            return "`" + it.key() + "` debe ser uno de " + p["enum"].dump();
        if (v.is_number()) {
            if (p.contains("minimum") && v.get<double>() < p["minimum"].get<double>())
                return "`" + it.key() + "` minimo " + p["minimum"].dump();
            if (p.contains("maximum") && v.get<double>() > p["maximum"].get<double>())
                return "`" + it.key() + "` maximo " + p["maximum"].dump();
        }
    }
    return {};
}

// --- JSON-RPC -----------------------------------------------------------------

void McpCore::handle(const json& message, std::function<void(json)> reply, const CallContext& ctx) {
    if (message.is_array()) {
        if (message.empty()) {
            reply(rpcError(nullptr, -32600, "lote vacio"));
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
                       "no es JSON-RPC 2.0"));
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
        reply(json());
        return;
    }
    try {
        if (method == "initialize") {
            reply(rpcResult(id, initializeResult(params)));
        } else if (method == "ping") {
            reply(rpcResult(id, json::object()));
        } else if (method == "tools/list") {
            json list = json::array();
            for (const Tool& t : tools_)
                list.push_back(toolListEntry(t));
            reply(rpcResult(id, json{{"tools", list}}));
        } else if (method == "tools/call") {
            if (!params.contains("name") || !params["name"].is_string()) {
                reply(rpcError(id, -32602, "falta `name`"));
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
                reply(rpcError(id, -32602, "no existe el prompt '" + name + "'"));
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
            reply(rpcError(id, -32601, "metodo desconocido: " + method));
        }
    } catch (const std::exception& e) {
        reply(rpcError(id, -32603, std::string("error interno: ") + e.what()));
    }
}

json McpCore::initializeResult(const json& params) const {
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
                {"instructions", instructions_}};
}

json McpCore::toolListEntry(const Tool& t) const {
    json e{
        {"name", t.name}, {"title", t.title}, {"description", t.description}, {"inputSchema", t.inputSchema}};
    e["annotations"] = json{{"title", t.title},
                            {"readOnlyHint", t.readOnly},
                            {"destructiveHint", t.destructive},
                            {"idempotentHint", t.idempotent},
                            {"openWorldHint", t.openWorld}};
    return e;
}

void McpCore::callTool(const json& params, std::function<void(json)> done, const CallContext& ctx) {
    const std::string name = params["name"].get<std::string>();
    const json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                                      : json::object();
    const auto finish = [done](const ToolResult& r) {
        json out{{"content", r.content}, {"isError", r.isError}};
        if (!r.structured.is_null()) out["structuredContent"] = r.structured;
        done(out);
    };
    const Tool* tool = findTool(name);
    if (tool == nullptr) {
        const json hits = search(name);
        std::string hint;
        for (const auto& h : hits)
            if (h["kind"] == "herramienta") hint += " " + h["id"].get<std::string>();
        finish(ToolResult::error("No existe la herramienta '" + name + "'." +
                                 (hint.empty() ? "" : " ¿Quisiste decir:" + hint + "?") +
                                 " `help` lista todas."));
        return;
    }
    const std::string problem = validate(tool->inputSchema, args);
    if (!problem.empty()) {
        finish(ToolResult::error("Argumentos invalidos para " + name + ": " + problem +
                                 ". Ficha completa: help {\"tool\": \"" + name + "\"}."));
        return;
    }
    // Estado de la salvaguarda de ESTA sesion (lo sabe el servidor, no el modelo).
    if (name == "security_status") {
        const bool t = tainted(ctx.session);
        const char* pol = policy_ == ApprovalPolicy::Always  ? "siempre"
                          : policy_ == ApprovalPolicy::Never ? "nunca"
                                                             : "invitados";
        finish(
            ToolResult()
                .text(t ? "Esta sesion leyo contenido de invitados de la sala: toda accion que modifica algo "
                          "necesita "
                          "que el operador la apruebe con un click."
                        : "Esta sesion no leyo contenido de invitados: las acciones se ejecutan directo "
                          "(politica: " +
                              std::string(pol) + ").")
                .data(json{{"policy", pol}, {"session", taintInfo(ctx.session)}}));
        return;
    }
    // Una herramienta que falla por un JSON inesperado no tumba nada: se
    // informa como error de la herramienta.
    const std::string session = ctx.session;
    const bool untrustedOut = tool->untrustedOutput;
    const auto execute = [this, tool, args, finish, session, untrustedOut, name]() {
        auto called = std::make_shared<bool>(false);
        try {
            tool->run(args, [this, finish, called, session, untrustedOut, name](ToolResult r) {
                if (*called) return; // una sola respuesta por pedido
                *called = true;
                if (untrustedOut || r.untrusted) taint(session, name);
                finish(r);
            });
        } catch (const std::exception& e) {
            if (!*called) {
                *called = true;
                finish(ToolResult::error(std::string("La herramienta fallo: ") + e.what()));
            }
        }
    };
    // Salvaguarda: en una sesion contaminada (o con politica "siempre"), una
    // accion que modifica algo espera la aprobacion de una persona.
    const bool needApproval =
        mutating(*tool) &&
        (policy_ == ApprovalPolicy::Always || (policy_ == ApprovalPolicy::Untrusted && tainted(session))) &&
        !intakeAllows(*tool, args);
    if (!needApproval) {
        execute();
        return;
    }
    if (!approver_) {
        finish(ToolResult::error(
            "Accion bloqueada: esta sesion leyo contenido de invitados y no hay a quien pedirle "
            "aprobacion. Pedile al operador que la haga el."));
        return;
    }
    const std::string reason = policy_ == ApprovalPolicy::Always
                                   ? "La politica pide aprobar toda accion del asistente."
                                   : "El asistente leyo contenido de invitados de la sala (" +
                                         taint_[session].source +
                                         "): por seguridad, lo que modifica algo necesita tu aprobacion.";
    approver_(*tool, args, reason, [execute, finish, name](bool ok) {
        if (ok) {
            execute();
        } else {
            finish(ToolResult::error(
                "El operador NO aprobo " + name +
                ". No reintentes la misma accion: preguntale que queres hacer (operator_ask) o "
                "segui con otra cosa."));
        }
    });
}

json McpCore::resourceList() const {
    json list = json::array();
    for (const HelpTopic& t : topics_) {
        list.push_back(json{{"uri", "ba://ayuda/" + t.id},
                            {"name", "ayuda-" + t.id},
                            {"title", "Ayuda: " + t.title},
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
        if (t.empty()) error = "no existe esa herramienta";
        return contents(t, "text/markdown");
    }
    if (uri.rfind(kHelp, 0) == 0) {
        const std::string id = uri.substr(kHelp.size());
        const std::string t = id == "indice" ? helpOverview() : helpTopic(id);
        if (t.empty()) error = "no existe ese tema";
        return contents(t, "text/markdown");
    }
    for (const Resource& r : resources_) {
        if (r.uri == uri) return contents(r.read ? r.read() : std::string(), r.mimeType);
    }
    error = "recurso desconocido: " + uri;
    return json();
}

// --- Ayuda ----------------------------------------------------------------------

std::string McpCore::helpOverview() const {
    std::ostringstream o;
    o << "# Broadcast Appliance — guia para operar con herramientas\n\n";
    if (!instructions_.empty()) o << instructions_ << "\n\n";
    o << "## Temas de ayuda (help {\"topic\": \"<id>\"})\n";
    for (const HelpTopic& t : topics_)
        o << "- `" << t.id << "` — " << t.title << ": " << t.summary << "\n";
    o << "\n## Herramientas por categoria (help {\"tool\": \"<nombre>\"} para la ficha)\n";
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
    o << "\n(✎ = cambia algo en la app; el resto solo consulta.)\n";
    if (!prompts_.empty()) {
        o << "\n## Guias paso a paso (prompts MCP)\n";
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
    o << "\n**Tipo:** " << (t->readOnly ? "solo consulta" : "cambia la app")
      << (t->destructive ? " (destructiva: borra, corta o saca algo)" : "") << "\n";
    const json props = t->inputSchema.value("properties", json::object());
    if (!props.empty()) {
        o << "\n## Argumentos\n";
        const json req = t->inputSchema.value("required", json::array());
        for (auto it = props.begin(); it != props.end(); ++it) {
            const json& p = it.value();
            const bool r = std::find(req.begin(), req.end(), it.key()) != req.end();
            o << "- `" << it.key() << "` ("
              << (p.contains("type") ? p["type"].dump() : std::string("cualquiera"))
              << (r ? ", obligatorio" : ", opcional") << ")";
            if (p.contains("description")) o << ": " << p["description"].get<std::string>();
            if (p.contains("enum")) o << " Valores: " << p["enum"].dump() << ".";
            if (p.contains("default")) o << " Por defecto: " << p["default"].dump() << ".";
            o << "\n";
        }
    } else {
        o << "\nSin argumentos.\n";
    }
    if (!t->examples.empty()) {
        o << "\n## Ejemplos\n";
        for (const std::string& e : t->examples)
            o << "```json\n" << e << "\n```\n";
    }
    o << "\n## Esquema JSON\n```json\n" << t->inputSchema.dump(2) << "\n```\n";
    return o.str();
}

json McpCore::search(const std::string& query) const {
    const std::vector<std::string> q = words(query);
    std::vector<std::pair<int, json>> hits;
    for (const Tool& t : tools_) {
        const int s = score(q, t.name + " " + t.title, t.description + " " + t.details);
        if (s > 0)
            hits.emplace_back(
                s, json{{"kind", "herramienta"}, {"id", t.name}, {"title", t.title}, {"score", s}});
    }
    for (const HelpTopic& t : topics_) {
        const int s = score(q, t.id + " " + t.title, t.summary + " " + t.body);
        if (s > 0)
            hits.emplace_back(s, json{{"kind", "tema"}, {"id", t.id}, {"title", t.title}, {"score", s}});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    json out = json::array();
    for (size_t i = 0; i < hits.size() && i < 12; ++i)
        out.push_back(hits[i].second);
    return out;
}

} // namespace ba::app::mcp
