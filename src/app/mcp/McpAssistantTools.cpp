// ============================================================================
// src/app/mcp/McpAssistantTools.cpp — La pestaña Asistente por MCP
//
// Estado, proveedor/modelo, instalacion del agente, arrancar/detener y mandar
// un pedido al agente de la pestaña (p. ej. desde otro cliente MCP).
// ============================================================================

#include <QTimer>

#include <memory>

#include "agent/AgentViewModel.hpp"
#include "agent/EstadoIA.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {
/// Los ids de proveedor que existen de verdad, para el esquema.
json idsDeProveedores(agent::AgentViewModel* ag) {
    json out = json::array();
    if (ag == nullptr) return out;
    for (const QVariant& v : ag->providers())
        out.push_back(v.toMap().value("id").toString().toStdString());
    return out;
}
} // namespace

void McpService::registerAssistantTools() {
    core_.addCategory(
        "asistente", ba::i18n::traducir("mcp", "Pestaña Asistente"),
        ba::i18n::traducir("mcp", "El agente de IA que corre en la pestaña Asistente (Claude Code, Antigravity para Gemini u opencode): "
        "proveedor, modelo, instalación, arrancar/detener y mandarle pedidos."));
    auto* ag = static_cast<agent::AgentViewModel*>(vm_.agent());
    {
        Tool t;
        t.name = "ia_estado";
        t.title = ba::i18n::traducir("mcp", "Qué parte de la IA no anda o falta configurar");
        t.category = "asistente";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "La lista de lo que falta para que la asistencia con IA ande entera: el asistente (instalado, "
            "clave, corriendo, MCP), el modelo local, el dictado y la voz del operador (reconocedor, "
            "micrófono), la voz que contesta, los auriculares, la voz del teleprompter y las traducciones. "
            "Cada problema trae `pagina` y `seccion` (ui_show_page) para llevar al operador a arreglarlo. "
            "Es lo mismo que muestra la pastilla «IA» del encabezado.");
        t.details = ba::i18n::traducir("mcp", "Usalo primero cuando alguien no sabe por dónde empezar, o algo de voz/IA no responde: "
                    "seguí la guía de puesta en marcha (help {\"topic\": \"puesta-en-marcha\"}) en el orden "
                    "que dice, y volvé a mirar hasta que `todo_bien` sea true.");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        StudioViewModel* vm = &vm_;
        t.run = [vm](const json&, Done done) {
            agent::EstadoIA* e = vm->estadoIAModelo();
            if (e == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "El estado de la IA no está disponible.")));
            done(ToolResult().data(e->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_limpieza";
        t.destructive = true; // borra (la lista sola no toca nada)
        t.title = ba::i18n::traducir("mcp", "Limpiar los asistentes");
        t.category = "asistente";
        t.description =
            ba::i18n::traducir("mcp", "Lo que ocupan los asistentes (Claude Code, Antigravity, opencode) y su limpieza: los chats guardados "
            "que se acumulan, las versiones viejas del programa, o reinstalar si algo falla.");
        t.details =
            ba::i18n::traducir("mcp", "Sin `accion` (o `ver`): por motor, las versiones instaladas (cuál se usa), lo que ocupa el "
            "programa, los chats y el perfil entero, y los espacios de trabajo.\n"
            "- `chats`: borra los chats guardados (`motor` vacío = de todos, y los espacios de trabajo). "
            "El login y los ajustes quedan.\n"
            "- `versiones`: borra las versiones del programa que no se usan.\n"
            "- `reinstalar`: borra el programa del `motor` y lo vuelve a bajar (el login y los chats quedan).\n"
            "- `de_cero`: además borra el perfil entero: chats, ajustes y el LOGIN (hay que volver a entrar). "
            "Las claves de API pegadas quedan.\n"
            "Nada de eso se puede con el asistente o un encargo de agentes corriendo.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"accion", {{"type", "string"}, {"enum", {"ver", "chats", "versiones", "reinstalar", "de_cero"}}}},
              {"motor", {{"type", "string"}, {"enum", {"", "claude", "antigravity", "opencode", "pi"}}}}}}};
        t.run = [ag](const json& a, Done done) {
            const std::string accion = a.value("accion", std::string("ver"));
            const QString motor = QString::fromStdString(a.value("motor", std::string()));
            const auto mb = [](double b) { return std::to_string(static_cast<long long>(b / 1048576.0)) + " MB"; };
            if (accion == "chats" || accion == "versiones") {
                const QVariantMap r = accion == "chats" ? ag->borrarChats(motor) : ag->borrarVersionesViejas(motor);
                if (!r.value("ok").toBool()) return done(ToolResult::error(r.value("error").toString().toStdString()));
                return done(ToolResult::ok(ba::i18n::llenar(
                    accion == "chats" ? ba::i18n::traducir("mcp", "Chats borrados: se liberaron %1.")
                                      : ba::i18n::traducir("mcp", "Versiones viejas borradas: se liberaron %1."),
                    {mb(r.value("bytes").toDouble())})));
            }
            if (accion == "reinstalar" || accion == "de_cero") {
                if (motor.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Decí qué `motor` reinstalar.")));
                const QString e = ag->reinstalar(motor, accion == "de_cero");
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok(ba::i18n::llenar(
                    accion == "de_cero"
                        ? ba::i18n::traducir("mcp", "Reinstalando %1 desde cero (hay que volver a entrar): el avance, en "
                                                    "assistant_status.")
                        : ba::i18n::traducir("mcp", "Reinstalando %1: el avance, en assistant_status."),
                    {motor.toStdString()})));
            }
            ag->revisarLimpieza();
            done(ToolResult().data(json{{"limpieza", qvToJson(ag->limpieza())}}));
        };
        core_.addTool(std::move(t));
    }
    // Hay DOS asistentes (cada uno con su motor, su terminal y su charla): «evento» (de
    // fábrica) y «automatizacion» (el que atiende las alertas). `asistente` elige cuál.
    auto* agAuto = static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion());
    const auto elegir = [ag, agAuto](const json& a) {
        return a.value("asistente", std::string()) == "automatizacion" && agAuto != nullptr ? agAuto : ag;
    };
    const json propAsistente = {{"type", "string"},
                                {"enum", {"evento", "automatizacion"}},
                                {"description", ba::i18n::traducir("mcp", "Cuál asistente: el del evento (de fábrica) o el de automatización (las alertas).")}};
    {
        Tool t;
        t.name = "assistant_status";
        t.title = ba::i18n::traducir("mcp", "Estado del asistente");
        t.category = "asistente";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Proveedor y modelo elegidos, proveedores disponibles, si el agente esta instalado y corriendo.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"asistente", propAsistente}}}};
        t.run = [elegir](const json& a, Done done) {
            auto* ag = elegir(a);
            done(ToolResult().data(json{{"provider", ag->provider().toStdString()},
                                        {"current", qvToJson(ag->current())},
                                        {"model", ag->model().toStdString()},
                                        {"hasKey", ag->hasKey()},
                                        {"installed", ag->installed()},
                                        {"installing", ag->installing()},
                                        {"installText", ag->installText().toStdString()},
                                        {"running", ag->running()},
                                        {"autoStart", ag->autoStart()},
                                        {"alertsToChat", ag->alertsToChat()},
                                        {"providers", qvToJson(ag->providers())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_config";
        t.title = ba::i18n::traducir("mcp", "Configurar el asistente");
        t.category = "asistente";
        t.description =
            ba::i18n::traducir("mcp", "Cambia proveedor, modelo, arranque automatico y si los avisos de disparadores van al "
            "chat. `assistant_status` lista los proveedores que hay y los modelos de cada uno. La "
            "clave de API se carga en la pestaña (no por aca).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"provider",
                            // La lista SALE DE LA TABLA, no escrita a mano.
                            // Escrita a mano se desactualizo en cuanto se
                            // agrego un proveedor: el nuevo existia en la
                            // pestaña, aparecia en `assistant_status` y esta
                            // herramienta lo rechazaba como invalido.
                            {{"type", "string"}, {"enum", idsDeProveedores(ag)}}},
                           {"model", {{"type", "string"}}},
                           {"autoStart", {{"type", "boolean"}}},
                           {"alertsToChat", {{"type", "boolean"}}},
                           {"asistente", propAsistente}}}};
        t.run = [elegir](const json& a, Done done) {
            auto* ag = elegir(a);
            if (a.contains("provider")) ag->setProvider(QString::fromStdString(a["provider"]));
            if (a.contains("model")) ag->setModel(QString::fromStdString(a["model"]));
            if (a.contains("autoStart")) ag->setAutoStart(a["autoStart"].get<bool>());
            if (a.contains("alertsToChat")) ag->setAlertsToChat(a["alertsToChat"].get<bool>());
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: %1"),
                                                 {ag->provider().toStdString() +
                                                  (ag->model().isEmpty() ? "" : " / " + ag->model().toStdString())})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_install";
        t.title = ba::i18n::traducir("mcp", "Instalar el agente");
        t.category = "asistente";
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Descarga el agente del proveedor elegido (version fijada, SHA-256) a la carpeta portable. "
            "Con wait=true espera a que termine (max. 15 min).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"wait", {{"type", "boolean"}, {"default", false}}}, {"asistente", propAsistente}}}};
        t.run = [elegir](const json& a, Done done) {
            auto* ag = elegir(a);
            if (ag->installed()) return done(ToolResult::ok(ba::i18n::traducir("mcp", "Ya esta instalado.")));
            ag->install();
            if (!a.value("wait", false))
                return done(ToolResult::ok(
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "Instalando: %1"), {ag->installText().toStdString()})));
            auto* timer = new QTimer(ag);
            auto elapsed = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, ag, [ag, timer, elapsed, done]() {
                *elapsed += 500;
                if (ag->installing() && *elapsed < 15 * 60 * 1000) return;
                timer->deleteLater();
                done(ag->installed() ? ToolResult::ok(ba::i18n::traducir("mcp", "Instalado."))
                                     : ToolResult::error(ag->installText().toStdString()));
            });
            timer->start(500);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_run";
        t.title = ba::i18n::traducir("mcp", "Arrancar / detener el asistente");
        t.category = "asistente";
        t.description = ba::i18n::traducir("mcp", "action: start | stop | restart.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"action", {{"type", "string"}, {"enum", {"start", "stop", "restart"}}}}, {"asistente", propAsistente}}},
            {"required", {"action"}}};
        t.run = [elegir](const json& a, Done done) {
            auto* ag = elegir(a);
            const std::string act = a["action"];
            if (act == "start") ag->start();
            if (act == "stop") ag->stop();
            if (act == "restart") ag->restart();
            // Arrancar puede tardar (el servidor compartido de OpenCode, cargar el modelo
            // local): se espera a que corra de verdad o falle, hasta 30 s.
            const bool arranca = act != "stop";
            auto* timer = new QTimer(ag);
            auto pasado = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, ag, [ag, timer, pasado, done, arranca]() {
                *pasado += 300;
                const bool fallo = !ag->lastError().isEmpty() && !ag->running();
                if (arranca && !ag->running() && !fallo && *pasado < 30000) return;
                timer->deleteLater();
                if (fallo) return done(ToolResult::error(ag->lastError().toStdString()));
                done(ToolResult::ok(ag->running() ? ba::i18n::traducir("mcp", "Corriendo.") : ba::i18n::traducir("mcp", "Detenido.")));
            });
            timer->start(300);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "assistant_send";
        t.title = ba::i18n::traducir("mcp", "Mandar un pedido al asistente");
        t.category = "asistente";
        t.description = ba::i18n::traducir("mcp", "Escribe el texto en el prompt del agente de la pestaña y lo manda (como si lo "
                        "tipeara la persona).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"text", {{"type", "string"}}}, {"asistente", propAsistente}}}, {"required", {"text"}}};
        t.run = [elegir](const json& a, Done done) {
            auto* ag = elegir(a);
            if (!ag->running())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "El asistente no esta corriendo (assistant_run).")));
            ag->send(QString::fromStdString(a["text"]));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Enviado.")));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
