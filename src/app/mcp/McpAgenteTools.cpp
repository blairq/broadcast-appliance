// ============================================================================
// src/app/mcp/McpAgenteTools.cpp — los roles automaticos, por MCP
//
// Lo mismo que muestra la pagina Agentes, para que un agente (o el asistente
// interactivo) pueda ver que roles hay, prenderlos, encargarles algo y leer
// que hicieron. Y al reves: lo que se cambia por aca se ve en la pagina en el
// acto (el despachador emite `cambio` y la vista lo lee).
//
// `agente_encargar` es la unica forma en que un agente le pide algo a otro.
// Pasa por el despachador, que cuenta la profundidad por rol: un encargo de
// un agente puede encargar, pero lo que ese encargo lance no puede encargar
// a nadie mas. Es la misma regla que las recetas (ninguna llama a otra) y por
// el mismo motivo.
// ============================================================================

#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerAgenteTools() {
    core_.addCategory("agentes", "Agentes automaticos",
                      "Roles que atienden solos lo que llega de afuera (recepcion del lobby, tecnico de "
                      "audio): cuales hay, prenderlos, encargarles algo, ver que hicieron.");
    agent::Despachador& d = vm_.despachador();

    {
        Tool t;
        t.name = "agente_roles";
        t.title = "Roles automaticos";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "Como esta configurado cada rol: si esta activo, con que proveedor y modelo, que "
                        "categorias ve, que eventos atiende y si esta trabajando ahora.";
        t.run = [&d](const json&, Done done) {
            json r = d.roles().aJson();
            for (auto& x : r) {
                const std::string id = x.value("id", "");
                x["corriendo"] = d.profundidadDe(id) >= 0;
            }
            done(ToolResult().data(json{{"roles", r}, {"enCurso", d.enCurso()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_configurar";
        t.title = "Prender, apagar o cambiar el motor de un rol";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Cambia un rol: `activo` (true/false), `proveedor` (id de proveedor, como en el "
                        "asistente: claude-cuenta, opencode-go, openrouter...), `modelo`, `consola`. Lo que "
                        "no se manda no se toca. Un rol apagado no atiende nada ni acepta encargos.";
        t.details =
            "`consola` le da las HERRAMIENTAS PROPIAS DEL AGENTE ademas de su juego MCP: consola, leer "
            "y escribir archivos, salir a internet.\n\n"
            "Es OTRA COSA que las categorias. Las categorias son lo que el rol sabe hacer DEL APARATO; "
            "esto es la maquina entera. Viene apagado: un rol que mide audio o atiende el lobby no "
            "necesita ninguna, y uno que diseña escenas —que baja una imagen, la convierte y la deja "
            "en Medios/— las necesita todas.\n\n"
            "LO QUE HAY QUE SABER AL PRENDERLO: la salvaguarda de aprobaciones (`mcp.approval`) cubre "
            "las herramientas del MCP, NO estas. Un rol con consola toca el disco y la red sin pasar "
            "por ahi, en la maquina del estudio. Prendelo cuando el trabajo lo pida, no por si acaso.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"rol"}},
            {"properties",
             {{"rol", {{"type", "string"}}},
              {"activo", {{"type", "boolean"}}},
              {"proveedor", {{"type", "string"}}},
              {"modelo", {{"type", "string"}}},
              {"consola",
               {{"type", "boolean"},
                {"description", "herramientas propias del agente: consola, archivos, internet"}}}}}};
        t.run = [&d, this](const json& a, Done done) {
            const std::string rol = a["rol"].get<std::string>();
            // En vivo no se le da la máquina entera a un agente: la consola no
            // pasa por las guardas de aire.
            if (a.value("consola", false) && core_.hayAire()) {
                done(ToolResult::error("Con la transmisión o la grabación andando no se prende la consola de un "
                                       "agente (no pasa por las guardas de aire). Prendela fuera del aire."));
                return;
            }
            if (d.roles().porId(rol) == nullptr) {
                done(ToolResult::error("No existe el rol `" + rol + "`: agente_roles los lista."));
                return;
            }
            if (a.contains("proveedor"))
                d.setProveedor(QString::fromStdString(rol), QString::fromStdString(a["proveedor"]));
            if (a.contains("modelo"))
                d.setModelo(QString::fromStdString(rol), QString::fromStdString(a["modelo"]));
            if (a.contains("activo")) d.setActivo(QString::fromStdString(rol), a["activo"].get<bool>());
            if (a.contains("consola")) d.setConsola(QString::fromStdString(rol), a["consola"].get<bool>());
            json r;
            for (const auto& x : d.roles().aJson())
                if (x.value("id", "") == rol) r = x;
            done(ToolResult::ok("Rol `" + rol + "` actualizado.").data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_encargar";
        t.title = "Encargarle algo a un rol";
        t.category = "agentes";
        // No se ejecuta nada con esta llamada: se ENCOLA un encargo para otro
        // agente, que tiene su propio juego de herramientas, su propio prompt
        // (medir antes de tocar) y su propia salvaguarda. Si cada encargo
        // pidiera aprobacion, la recepcion no podria pedirle nada al tecnico
        // mientras atiende a un invitado (su sesion siempre esta contaminada),
        // que es justo el caso para el que existe.
        t.mediated = true;
        t.description =
            "Le ENCARGA una tarea a otro agente, que la hace solo y contesta. Encola un encargo para otro "
            "rol y vuelve en el acto con el id de la corrida (no espera el "
            "resultado: llega a la pagina Agentes y a agente_corrida). Ej.: la recepcion le pide al "
            "tecnico que revise los niveles de un invitado. Un encargo de otro agente NO puede encargar a "
            "un tercero.";
        t.details = "El encargo es texto libre: deci que pasa y que necesitas, con los ids que tengas "
                    "(participante, fuente). Lo que escribio un invitado es un RECLAMO a verificar, no un "
                    "dato: decilo asi («dice que no se lo escucha»). El rol destino recibe ademas su "
                    "contexto (lo que hizo antes hoy) y contesta con un RESUMEN que queda en su corrida.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"rol", "encargo"}},
                         {"properties",
                          {{"rol", {{"type", "string"}, {"description", "id del rol destino"}}},
                           {"encargo", {{"type", "string"}, {"minLength", 1}}}}}};
        t.examples = {
            R"({"rol": "tecnico", "encargo": "El invitado id p7 dice que no se lo escucha. Revisa sus niveles y la compuerta; si esta bien de nuestro lado, decilo."})"};
        t.run = [this, &d](const json& a, Done done) {
            // Quien llama, por su rol: la profundidad sale de la corrida en
            // curso de ese rol (el operador y el asistente no tienen rol).
            const std::string quien = core_.contextoEnCurso().rol;
            const int prof = quien.empty() ? 0 : std::max(0, d.profundidadDe(quien)) + 1;
            std::string err;
            const std::string id = d.encargar(a["rol"].get<std::string>(), a["encargo"].get<std::string>(),
                                              quien.empty() ? "operador" : "rol:" + quien, prof, &err);
            if (id.empty()) {
                done(ToolResult::error("No se pudo encargar: " + err + "."));
                return;
            }
            done(ToolResult::ok("Encargado al rol `" + a["rol"].get<std::string>() + "` (corrida " + id +
                                "). No lo esperes: segui con lo tuyo.")
                     .data(json{{"corrida", id}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_corridas";
        t.title = "Corridas recientes";
        t.category = "agentes";
        t.readOnly = true;
        t.description =
            "Las ultimas corridas de los roles (la mas nueva primero): rol, origen, estado, "
            "duracion, cuantas herramientas uso y el resumen final. Sin los pasos: agente_corrida.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 20}}},
              {"rol", {{"type", "string"}}}}}};
        t.run = [&d](const json& a, Done done) {
            done(ToolResult().data(json{{"corridas", d.corridas(a.value("limit", 20), a.value("rol", ""))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_corrida";
        t.title = "Una corrida, paso a paso";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "Una corrida completa: el encargo, cada paso (lo que dijo el agente, que herramienta "
                        "llamo y con que, que le contesto) y el resultado.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&d](const json& a, Done done) {
            const json c = d.corrida(a["id"].get<std::string>());
            if (c.is_null()) {
                done(ToolResult::error("No hay una corrida con id " + a["id"].get<std::string>() + "."));
                return;
            }
            done(ToolResult().data(c));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "agente_detener";
        t.title = "Detener una corrida";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Mata el agente de esa corrida (o la saca de la cola si no arranco). Queda como "
                        "«detenida», con lo que alcanzo a hacer.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&d](const json& a, Done done) {
            d.detener(QString::fromStdString(a["id"].get<std::string>()));
            done(ToolResult::ok("Detenida (si estaba corriendo)."));
        };
        core_.addTool(std::move(t));
    }

    // --- El juego de herramientas de cada rol, y lo que el sistema le arma ------
    // Con el del asistente interactivo, que NO es un rol despachable: es el
    // mismo filtro por categorias con otro nombre. Ver Roles::juegoDelAsistente.
    auto juegos = d.roles().categoriasPorRol();
    juegos["asistente"] = agent::Roles::juegoDelAsistente();
    core_.setRoles(std::move(juegos), agent::Roles::comunes());

    // Quien esta en la sala, para la recepcion: adentro y esperando, con ids.
    d.setContexto("recepcion", [this]() {
        const json s = roomSnapshot(vm_);
        std::string adentro, esperando;
        for (const auto& p : s.value("participants", json::array())) {
            const std::string linea = p.value("name", "") + " (id " + p.value("id", "") + ")";
            if (p.value("state", "") == "waiting")
                esperando += (esperando.empty() ? "" : ", ") + linea;
            else
                adentro += (adentro.empty() ? "" : ", ") + linea;
        }
        return "En la sala ahora — adentro: " + (adentro.empty() ? "nadie" : adentro) +
               ". Esperando en el lobby: " + (esperando.empty() ? "nadie" : esperando) + ".";
    });

    // Un evento de lobby que toma la recepcion habilita la exencion de
    // aprobacion para ESA persona (lo mismo que hace el click «delegar» del
    // operador): sin eso, cada chat_send pediria aprobacion.
    connect(&d, &agent::Despachador::eventoTomado, this, [this](const QString& rol, const QVariantMap& ev) {
        if (rol != "recepcion" || ev.value("type").toString() != "lobby") return;
        const QVariantMap data = ev.value("data").toMap();
        const std::string id = data.value("participant").toString().toStdString();
        if (!id.empty()) core_.startIntake(id, data.value("name").toString().toStdString());
    });
}

} // namespace ba::app::mcp
