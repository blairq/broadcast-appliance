// ============================================================================
// src/app/mcp/McpCore.hpp — Nucleo del servidor MCP (Model Context Protocol)
//
// Proposito:
//   Que un asistente de IA (un LLM con vision) pueda operar TODO el appliance:
//   escenas, fuentes, audio, salidas, configuracion, sala, oradores, ventanas,
//   y VER lo que pasa (programa, cada fuente, cada persona, la propia app).
//
//   Este archivo es el protocolo, sin Qt ni transporte: JSON-RPC 2.0 segun la
//   especificacion MCP (2025-06-18, compatible con 2025-03-26 y 2024-11-05):
//     initialize / ping / tools/list / tools/call / resources/list /
//     resources/templates/list / resources/read / prompts/list / prompts/get /
//     logging/setLevel / completion/complete (vacio).
//   El transporte (HTTP en 127.0.0.1 con token) esta en McpHttp; el puente
//   stdio para clientes que lanzan procesos, en el binario ba-mcp.
//
// Sistema de ayuda propio:
//   - Herramienta `help` (sin argumentos: guia general y catalogo por
//     categoria; `topic`: un tema; `tool`: la ficha completa de una
//     herramienta con su esquema y ejemplos) y `help_search` (busqueda por
//     palabras en herramientas y temas).
//   - Cada tema es tambien un recurso `ba://ayuda/<tema>` y cada herramienta
//     `ba://ayuda/herramienta/<nombre>`.
//   - Las `instructions` del initialize le dicen al cliente por donde empezar.
//
// Errores: un argumento invalido o una falla de la app vuelven como resultado
// con isError=true y un texto que explica que paso y como seguir (el modelo
// puede corregirse solo). Los errores de protocolo, como error JSON-RPC.
//
// Threading: no es thread-safe; todo se llama desde el thread de la app (el
// transporte y las herramientas viven en el mismo event loop). Las
// herramientas pueden terminar DESPUES (asincronas): llaman `done` cuando
// tienen el resultado.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

using json = nlohmann::json;

/// Resultado de una herramienta (contenido MCP: texto, imagenes, JSON).
struct ToolResult {
    json content = json::array();
    json structured; ///< structuredContent (opcional)
    bool isError = false;
    /// La respuesta trae contenido escrito por invitados o publico (chat,
    /// nombres, archivos, video de invitados): contamina la sesion (ver McpCore).
    bool untrusted = false;

    ToolResult& text(const std::string& t);
    /// Imagen en base64 (`mime`: image/png | image/jpeg).
    ToolResult& image(const std::string& base64, const std::string& mime);
    /// Agrega `data` como JSON estructurado y como texto legible.
    ToolResult& data(const json& data);
    static ToolResult error(const std::string& message);
    static ToolResult ok(const std::string& message);
};

using Done = std::function<void(ToolResult)>;

struct Tool {
    std::string name;
    std::string title;
    std::string category; ///< id de categoria (ver addCategory)
    std::string description;
    std::string details; ///< explicacion larga para `help tool=...`
    json inputSchema = json{{"type", "object"}, {"properties", json::object()}};
    std::vector<std::string> examples; ///< argumentos de ejemplo (JSON) con comentario
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
    bool openWorld = false;
    /// Siempre devuelve contenido de invitados/publico (contamina la sesion).
    bool untrustedOutput = false;
    /// Su efecto lo decide una persona con un click (preguntar al operador,
    /// sugerir respuestas): no necesita aprobacion aunque la sesion este contaminada.
    bool mediated = false;
    std::function<void(const json& args, Done done)> run;
};

/// Quien llama (para la contaminacion por sesion). La sesion la pone el
/// cliente MCP (cabecera Mcp-Session-Id), no el modelo.
struct CallContext {
    std::string session;
};

/// Politica de aprobacion de acciones que modifican algo.
enum class ApprovalPolicy {
    Untrusted, ///< (defecto) aprobacion humana solo si la sesion recibio contenido de invitados/publico
    Always,    ///< toda accion que modifica necesita aprobacion
    Never      ///< nunca (la persona decidio confiar)
};

struct HelpTopic {
    std::string id;
    std::string title;
    std::string summary;
    std::string body; ///< markdown
};

struct Resource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mimeType = "text/markdown";
    std::function<std::string()> read;
};

struct Prompt {
    std::string name;
    std::string title;
    std::string description;
    json arguments = json::array(); ///< [{name, description, required}]
    /// Devuelve el texto del mensaje de usuario con los argumentos aplicados.
    std::function<std::string(const json& args)> render;
};

class McpCore {
   public:
    McpCore();

    void setServerInfo(std::string name, std::string version, std::string instructions);
    void addCategory(const std::string& id, const std::string& title, const std::string& summary);
    void addTool(Tool tool);
    void addTopic(HelpTopic topic);
    void addResource(Resource resource);
    void addPrompt(Prompt prompt);

    /// Un mensaje JSON-RPC (objeto o arreglo). `reply` recibe la respuesta
    /// (objeto, arreglo, o null si eran solo notificaciones). Puede llamarse
    /// despues (herramientas asincronas), una sola vez.
    void handle(const json& message, std::function<void(json)> reply, const CallContext& ctx = {});

    // --- Salvaguarda determinista contra instrucciones inyectadas ----------------
    // Una sesion que recibio contenido de invitados o del publico queda
    // "contaminada" hasta que termina (cliente nuevo = sesion nueva). En una
    // sesion contaminada, NINGUNA herramienta que modifica algo se ejecuta sin
    // que una persona la apruebe con un click (la tarjeta la arma el servidor:
    // herramienta y argumentos exactos). No depende de lo que diga el modelo.
    using Approver = std::function<void(const Tool& tool, const json& args, const std::string& reason,
                                        std::function<void(bool approved)> decide)>;
    void setApprover(Approver fn) { approver_ = std::move(fn); }
    void setApprovalPolicy(ApprovalPolicy p) { policy_ = p; }
    ApprovalPolicy approvalPolicy() const { return policy_; }
    /// Marca la sesion como contaminada (p. ej. al leer un recurso de la sala).
    void taint(const std::string& session, const std::string& source);

    /// Atencion delegada de alguien del lobby. El operador la habilita con un
    /// click en su tarjeta: mientras dura, el asistente puede hablarle,
    /// completar su ficha y hacerla pasar SIN pedir aprobacion paso a paso
    /// (seguiria pidiendola por cada mensaje, que es lo que la hace inusable).
    /// La salvaguarda general no se toca: la exencion vale SOLO para esa
    /// persona y para esas herramientas.
    void startIntake(const std::string& id, const std::string& name);
    void endIntake(const std::string& id);
    std::vector<std::string> intakes() const;
    bool tainted(const std::string& session) const;
    /// {session, tainted, source, atMs}
    json taintInfo(const std::string& session) const;
    /// true si la herramienta modifica algo (ni solo lectura ni mediada por una persona).
    static bool mutating(const Tool& t) { return !t.readOnly && !t.mediated; }

    // --- Ayuda (tambien las usa la herramienta `help`) ------------------------
    std::string helpOverview() const;
    std::string helpTopic(const std::string& id) const;
    std::string helpTool(const std::string& name) const;
    /// Coincidencias por palabras: [{kind, id, title, score}].
    json search(const std::string& query) const;

    const std::vector<Tool>& tools() const { return tools_; }
    /// (id, titulo) de cada categoria, en orden.
    std::vector<std::pair<std::string, std::string>> categoryTitles() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& c : categories_)
            out.emplace_back(c.id, c.title);
        return out;
    }
    const Tool* findTool(const std::string& name) const;
    /// Para ajustar herramientas ya registradas (marcas de seguridad).
    void forEachTool(const std::function<void(Tool&)>& fn) {
        for (Tool& t : tools_)
            fn(t);
    }
    /// Valida `args` contra el esquema (requeridos, tipos, enums, rangos).
    /// :returns: vacio si esta bien, o el problema explicado.
    static std::string validate(const json& schema, const json& args);

    static constexpr const char* kLatestProtocol = "2025-06-18";

   private:
    void handleOne(const json& msg, std::function<void(json)> reply, const CallContext& ctx);
    json initializeResult(const json& params) const;
    json toolListEntry(const Tool& t) const;
    void callTool(const json& params, std::function<void(json result)> done, const CallContext& ctx);
    json resourceList() const;
    json readResource(const std::string& uri, std::string& error) const;

    struct Category {
        std::string id, title, summary;
    };
    std::string name_ = "broadcast-appliance";
    std::string version_ = "1.0";
    std::string instructions_;
    std::vector<Category> categories_;
    std::vector<Tool> tools_;
    std::vector<HelpTopic> topics_;
    std::vector<Resource> resources_;
    std::vector<Prompt> prompts_;
    Approver approver_;
    ApprovalPolicy policy_ = ApprovalPolicy::Untrusted;
    struct Taint {
        std::string source;
        std::int64_t atMs = 0;
    };
    std::map<std::string, Taint> taint_;
    /// id -> nombre de las personas cuya atencion delego el operador.
    std::map<std::string, std::string> intake_;
    bool intakeAllows(const Tool& t, const nlohmann::json& args) const;
};

} // namespace ba::app::mcp
