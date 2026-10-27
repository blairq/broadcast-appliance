// ============================================================================
// src/app/mcp/McpControlWebTools.cpp — el control web por MCP (control/ControlWeb)
//
// control_web_*: los aparatos del dashboard /control/ (emparejar, aprobar,
// perfiles, quitar, el acceso desde afuera). Ningún aparato las ve: administrar
// el control web no se hace desde el control web.
//
// Lo que el asistente NO hace solo: aprobar un aparato, ampliar un perfil ni
// habilitar el acceso desde afuera. Para eso aparece la tarjeta de aprobación y
// decide una persona (en modo prueba, --test, pasa y queda en el registro).
// Achicar, rechazar, quitar y apagar el túnel, sí.
// ============================================================================

#include "mcp/McpControlWebTools.hpp"

#include <QCoreApplication>
#include <QPointer>

#include "audio/Mezclador.hpp"
#include "control/ControlWeb.hpp"
#include "control/TemasWeb.hpp"
#include "control/ControlWebGateway.hpp"
#include "mcp/McpCore.hpp"
#include "i18n/I18n.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void registerControlWebTools(McpCore& core, StudioViewModel& vm) {
    core.addCategory("control_web", ba::i18n::traducir("mcp", "Control web"),
                     ba::i18n::traducir("mcp", "Los aparatos del dashboard web /control/ (tablet, notebook, PC de la red local): emparejar, "
                     "aprobar, el perfil de cada uno (qué categorías de herramientas puede usar) y quitarlos."));
    control::ControlWeb& cw = vm.controlWeb();
    // El control web corre las herramientas de los aparatos por este servidor.
    cw.setCore(&core);
    // Mientras alguien mira el audio, el Mezclador mide solo (a 30 Hz): así
    // mezclador_estado no rehace sus listas en cada consulta.
    if (auto* mz = qobject_cast<audio::Mezclador*>(vm.mezcladorQml())) {
        QPointer<audio::Mezclador> m(mz);
        cw.ganchos().mirando = [m, &cw](const std::string& tema, bool mirando) {
            if (tema == "audio" && m) m->usar(&cw, mirando);
        };
    }

    {
        Tool t;
        t.name = "control_web_aparatos";
        t.title = ba::i18n::traducir("mcp", "Aparatos del control web");
        t.category = "control_web";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Los aparatos emparejados del dashboard /control/ (id, nombre, perfil, IP, último uso, si están "
                        "conectados), las solicitudes que esperan a una persona, si el acceso desde afuera está "
                        "habilitado y lo que dice el gateway (conexiones vivas).");
        t.run = [&vm, &cw](const json&, Done done) {
            json base{{"aparatos", cw.aparatos()},
                      {"solicitudes", cw.solicitudes()},
                      {"tunel", cw.tunel()},
                      {"conectados", cw.conectados()}};
            vm.controlWebGateway().estado([done, base](const json& gw) mutable {
                base["gateway"] = gw;
                done(ToolResult().data(base));
            });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_web_emparejar";
        t.title = ba::i18n::traducir("mcp", "Emparejar un aparato al control web");
        t.category = "control_web";
        t.description = ba::i18n::traducir("mcp", "Genera un código de un uso (vence en unos minutos) y la URL /control/#p=… para abrir en el "
                        "navegador del aparato (o el QR de Servicios → Control web). `perfil`: las categorías que se le "
                        "proponen a quien aprueba (\"*\" = todas, de fábrica). Cuando el aparato la abre, una PERSONA "
                        "lo aprueba en la app.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"perfil", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"nombre", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            vm.controlWebGateway().emparejar(a.contains("perfil") ? a["perfil"] : json(), a.value("nombre", std::string()),
                                             [done](const json& r) {
                                                 if (!r.contains("url"))
                                                     return done(ToolResult::error(r.value("error", ba::i18n::traducir("mcp", "no se pudo"))));
                                                 done(ToolResult()
                                                          .text(ba::i18n::traducir("mcp", "Abrí la URL en el navegador del aparato. Después una "
                                                                "persona lo aprueba en Servicios → Control web."))
                                                          .data({{"url", r.value("url", std::string())},
                                                                 {"vence_ms", r.value("vence_ms", std::int64_t{0})},
                                                                 {"perfil", r.value("perfil", json::array())},
                                                                 {"huella_tls", r.value("huella", std::string())},
                                                                 {"aviso", r.value("aviso", std::string())}}));
                                             });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_web_resolver";
        t.title = ba::i18n::traducir("mcp", "Aprobar o rechazar un aparato del control web");
        t.category = "control_web";
        t.description = ba::i18n::traducir("mcp", "Resuelve la `solicitud` (control_web_aparatos). `aprobar`: false la rechaza; true la aprueba con "
                        "el `perfil` (categorías; \"*\" = todas; sin perfil, el que propuso el código). Aprobar lo "
                        "confirma una persona: aparece la tarjeta de aprobación.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"solicitud", "aprobar"}},
                         {"properties",
                          {{"solicitud", {{"type", "string"}}},
                           {"aprobar", {{"type", "boolean"}}},
                           {"perfil", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}};
        t.run = [&core, &cw](const json& a, Done done) {
            const std::string sol = a.value("solicitud", std::string());
            const bool aprobar = a.value("aprobar", false);
            const json perfil = a.contains("perfil") ? a["perfil"] : json();
            const auto hacer = [&cw, sol, aprobar, perfil, done]() {
                const json r = cw.resolverSolicitud(sol, aprobar, perfil, true);
                if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                done(ToolResult::ok(aprobar ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Aparato emparejado (%1)."),
                                                               {r.value("aparato", std::string())})
                                            : ba::i18n::traducir("mcp", "Solicitud rechazada.")));
            };
            // Rechazar, o lo pide una persona (un atajo, la botonera): directo.
            if (!aprobar || core.contextoEnCurso().humano) return hacer();
            core.pedirAprobacion("control_web_resolver", a,
                                 ba::i18n::traducir("mcp", "Aprobar un aparato del control web le da acceso a herramientas de la app desde la red "
                                 "local: lo decide una persona."),
                                 [hacer, done](bool ok) {
                                     if (ok) return hacer();
                                     done(ToolResult::error(ba::i18n::traducir("mcp", "El operador no aprobó el aparato.")));
                                 });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_web_perfil";
        t.title = ba::i18n::traducir("mcp", "Perfil de un aparato del control web");
        t.category = "control_web";
        t.description = ba::i18n::traducir("mcp", "Cambia las `categorias` de herramientas que puede usar el `aparato` (\"*\" = todas; nunca "
                        "control_web). Achicar es directo; ampliar lo confirma una persona (tarjeta de aprobación).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"aparato", "categorias"}},
                         {"properties",
                          {{"aparato", {{"type", "string"}}},
                           {"categorias", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}};
        t.run = [&core, &cw](const json& a, Done done) {
            const std::string ap = a.value("aparato", std::string());
            const json cats = a.value("categorias", json::array());
            const bool humano = core.contextoEnCurso().humano;
            // Primero como asistente: si solo achica, listo.
            std::string e = cw.setPerfil(ap, cats, humano);
            if (e.empty()) return done(ToolResult::ok(ba::i18n::traducir("mcp", "Perfil guardado.")));
            if (humano || e.rfind("ampliar", 0) != 0) return done(ToolResult::error(e));
            core.pedirAprobacion("control_web_perfil", a,
                                 ba::i18n::traducir("mcp", "Ampliar el perfil de un aparato del control web (lo que puede tocar desde la red "
                                 "local): lo decide una persona."),
                                 [&cw, ap, cats, done](bool ok) {
                                     if (!ok) return done(ToolResult::error(ba::i18n::traducir("mcp", "El operador no aprobó ampliar el perfil.")));
                                     const std::string e2 = cw.setPerfil(ap, cats, true);
                                     if (!e2.empty()) return done(ToolResult::error(e2));
                                     done(ToolResult::ok(ba::i18n::traducir("mcp", "Perfil ampliado.")));
                                 });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_web_quitar";
        t.title = ba::i18n::traducir("mcp", "Quitar un aparato del control web");
        t.category = "control_web";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "El token del `aparato` deja de valer y se cortan sus conexiones. Para volver, se empareja de nuevo.");
        t.inputSchema = {{"type", "object"}, {"required", {"aparato"}}, {"properties", {{"aparato", {{"type", "string"}}}}}};
        t.run = [&cw](const json& a, Done done) {
            if (!cw.quitar(a.value("aparato", std::string()))) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese aparato")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "control_web_tunel";
        t.title = ba::i18n::traducir("mcp", "Control web desde afuera (túnel, con PIN)");
        t.category = "control_web";
        t.description = ba::i18n::traducir("mcp", "De fábrica el control web anda solo en la red local. `habilitar` true con un `pin` (4 a 12 "
                        "cifras) lo deja entrar también por el túnel, pidiendo el PIN además del token del aparato: lo "
                        "confirma una persona. false lo apaga (directo) y corta lo que esté conectado desde afuera.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"habilitar"}},
                         {"properties", {{"habilitar", {{"type", "boolean"}}}, {"pin", {{"type", "string"}}}}}};
        t.run = [&core, &cw](const json& a, Done done) {
            const bool on = a.value("habilitar", false);
            const std::string pin = a.value("pin", std::string());
            const auto hacer = [&cw, on, pin, done]() {
                const std::string e = cw.setTunel(on, pin, true);
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult::ok(on ? ba::i18n::traducir("mcp", "Habilitado desde afuera (con PIN).")
                                       : ba::i18n::traducir("mcp", "Solo red local.")));
            };
            if (!on || core.contextoEnCurso().humano) return hacer();
            core.pedirAprobacion("control_web_tunel", a,
                                 ba::i18n::traducir("mcp", "Habilitar el control web desde internet (por el túnel, con PIN): lo decide una persona."),
                                 [hacer, done](bool ok) {
                                     if (ok) return hacer();
                                     done(ToolResult::error(ba::i18n::traducir("mcp", "El operador no aprobó el acceso desde afuera.")));
                                 });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "web_tema";
        t.title = ba::i18n::traducir("mcp", "Tema de las interfaces web");
        t.category = "control_web";
        t.description = ba::i18n::traducir("mcp", "El tema de todas las páginas de control (dashboard, botonera, presidencia, rótulos, "
                        "corrección): sin `tema`, el actual y los que hay; con `tema` (un id), lo cambia en todos los "
                        "aparatos sin recargar (como Servicios → Templates o Configuración).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"tema", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("tema")) {
                const std::string id = a.value("tema", std::string());
                if (!control::temaWebValido(id)) return done(ToolResult::error(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "No hay un tema «%1» (web_tema sin nada: la lista)."), {id})));
                vm.setTemaWeb(QString::fromStdString(id));
            }
            json lista = json::array();
            for (const auto& x : control::temasWeb())
                lista.push_back({{"id", x.id},
                                 {"nombre", QCoreApplication::translate("control-web", x.nombre).toStdString()},
                                 {"descripcion", QCoreApplication::translate("control-web", x.descripcion).toStdString()}});
            done(ToolResult().data({{"actual", vm.temaWeb().toStdString()}, {"temas", lista}}));
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
