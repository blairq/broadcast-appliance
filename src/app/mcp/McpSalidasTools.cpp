// ============================================================================
// src/app/mcp/McpSalidasTools.cpp — las salidas del aparato, por MCP
//
// docs/planes/PLAN-SALIDAS.md, etapa 3. Cada salida es un operador completo (su
// aire y su armada, su mezcla, sus destinos, su grabación…). Acá están:
//
//   - salidas_listar / salida_crear / salida_editar / salida_borrar, por el
//     mismo modelo que la GUI (AppRuntime::crearSalida…): lo que hace el
//     asistente lo ve el operador en el acto;
//   - el resumen por salida que usan `status` y `salidas_listar` (uno solo);
//   - lo que el núcleo necesita para las herramientas de salida (Tool::porSalida):
//     qué salidas hay y si una sale (McpCore::setSalidas);
//   - conmutadorDe: el conmutador de la salida de la llamada en curso, para las
//     herramientas de escenas (cada una lo pide acá, ninguna lo resuelve sola).
// ============================================================================

#include "arranque/AppRuntime.hpp"
#include "diag/Recursos.hpp"
#include "escenas/Conmutador.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

escenas::Conmutador& conmutadorDe(const McpCore& core, StudioViewModel& vm, const json& args) {
    // callTool ya validó la salida; si justo la borraron, la por omisión (no se cae).
    escenas::Conmutador* c = vm.conmutadores().de(core.salidaDe(args));
    return c != nullptr ? *c : vm.conmutadores().porOmision();
}

json resumenSalidas(AppRuntime& rt, StudioViewModel& vm) {
    const EngineStatus st = rt.engine().status();
    const json guardadas = rt.salidas();
    json out = json::array();
    for (const auto& s : st.salidas) {
        json g = json::object();
        for (const auto& x : guardadas.value("salidas", json::array()))
            if (x.value("id", std::string()) == s.id) g = x;
        escenas::Conmutador* c = vm.conmutadores().de(s.porOmision ? std::string() : s.id);
        out.push_back({{"id", s.id},
                       {"nombre", s.nombre},
                       {"color", g.value("color", std::string())},
                       {"por_omision", s.porOmision},
                       {"escena", s.escena},
                       {"armada", c != nullptr ? c->armada() : std::string()},
                       {"streaming", s.streaming},
                       {"reconectando", s.reconnecting},
                       {"recording", s.recording},
                       {"destinos", s.destinos},
                       {"limpia", s.limpia},
                       {"repeticion", s.repeticion},
                       {"placa", rt.engine().estadoPlaca(s.porOmision ? std::string() : s.id)}});
    }
    return out;
}

void McpService::registerSalidasTools() {
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    // Lo que el núcleo necesita para las herramientas de salida: qué salidas hay
    // (para validar `salida`, por id o nombre) y si una sale (la guarda de aire).
    core_.setSalidas(
        [&rt]() {
            json l = json::array();
            for (const auto& s : rt.engine().status().salidas)
                l.push_back({{"id", s.id}, {"nombre", s.nombre}, {"por_omision", s.porOmision}});
            return l;
        },
        [&rt](const std::string& id) { return rt.engine().haySalidaEn(id); });

    const json idProp = {{"type", "string"},
                         {"description", ba::i18n::traducir("mcp", "Id de la salida (letras, números, - y _).")}};
    {
        Tool t;
        t.name = "salidas_listar";
        t.title = ba::i18n::traducir("mcp", "Las salidas del aparato");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description = ba::i18n::traducir(
            "mcp",
            "Las salidas (cada una un operador completo: su escena al aire y su armada, su mezcla, sus destinos, su "
            "grabación, su repetición, su negro y su placa), la por omisión primera, con lo que hace cada una ahora; y "
            "el `tope` de salidas. Las herramientas que tocan el programa aceptan `salida` (id o nombre); sin ella, la "
            "por omisión.");
        t.run = [&rt, &vm](const json&, Done done) {
            done(ToolResult().data(
                json{{"tope", rt.salidas().value("tope", 0)}, {"salidas", resumenSalidas(rt, vm)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_crear";
        t.title = ba::i18n::traducir("mcp", "Crear una salida");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir(
            "mcp",
            "Una salida nueva, andando en el acto (con su programa, su preview y su mezcla), hasta el tope. Usa las "
            "mismas entradas y escenas que las demás. Cada salida es otra composición y, al transmitir o grabar, otros "
            "codificadores: la respuesta trae `recursos` (lo de sistema_recursos) y un `aviso` si la máquina ya está "
            "cerca del límite.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", idProp},
                           {"nombre", {{"type", "string"}}},
                           {"color", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "#RRGGBB (tally, multiview).")}}}}}};
        t.examples = {R"({"id": "youtube2", "nombre": "YouTube 2", "color": "#2E86DE"})"};
        RecursosViewModel* rec = vm_.recursos();
        t.run = [&rt, &vm, rec](const json& a, Done done) {
            const std::string id = a.value("id", std::string());
            if (const std::string e = rt.crearSalida(id, a.value("nombre", id), a.value("color", std::string()));
                !e.empty())
                return done(ToolResult::error(e));
            json out{{"salidas", resumenSalidas(rt, vm)}};
            // El margen de la máquina: el tope lo pone el operador; si aguanta, lo dice esto.
            if (rec != nullptr) {
                const json inf = rec->muestreador().informe(0, 1);
                const json mg = inf.value("margen", json::object());
                out["recursos"] = {{"estado", inf.value("estado", std::string("desconocido"))}, {"margen", mg}};
                const std::string estado = inf.value("estado", std::string());
                const double pct = mg.value("pct", json()).is_number() ? mg["pct"].get<double>() : -1.0;
                if (estado == "aviso" || estado == "critico" || (pct >= 0 && pct < 20))
                    out["aviso"] = ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "La máquina ya está cerca del límite (%1, margen %2 %): transmitir o "
                                                  "grabar con esta salida puede no dar abasto. Mirá sistema_recursos."),
                        {mg.value("limita", std::string("?")), pct >= 0 ? std::to_string(static_cast<int>(pct)) : "?"});
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_editar";
        t.title = ba::i18n::traducir("mcp", "Cambiar el nombre o el color de una salida (o el tope)");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir(
            "mcp",
            "`id` con `nombre` y/o `color` (#RRGGBB). `tope` (1 a 16) cambia cuántas salidas puede tener el aparato "
            "(no baja de las que ya hay: borrá alguna antes).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", idProp},
                           {"nombre", {{"type", "string"}}},
                           {"color", {{"type", "string"}}},
                           {"tope", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.examples = {R"({"id": "youtube2", "nombre": "YouTube en inglés"})", R"({"tope": 6})"};
        t.run = [&rt, &vm](const json& a, Done done) {
            const bool cambia = a.contains("nombre") || a.contains("color");
            if (!cambia && !a.contains("tope"))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta qué cambiar: `nombre`, `color` o `tope`.")));
            if (a.contains("tope"))
                if (const std::string e = rt.setTopeSalidas(a.value("tope", 0)); !e.empty())
                    return done(ToolResult::error(e));
            if (cambia) {
                json cambios = json::object();
                for (const char* k : {"nombre", "color"})
                    if (a.contains(k)) cambios[k] = a[k];
                if (const std::string e = rt.editarSalida(a.value("id", std::string()), cambios); !e.empty())
                    return done(ToolResult::error(e));
            }
            done(ToolResult().data(
                json{{"tope", rt.salidas().value("tope", 0)}, {"salidas", resumenSalidas(rt, vm)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar una salida");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una salida (corta lo que sale de ella)");
        t.destructive = true;
        t.description = ba::i18n::traducir(
            "mcp",
            "Para todo lo que sale de ella (transmisiones, grabación, repetición, señal limpia) y la saca. La por "
            "omisión no se borra. Sus destinos guardados quedan en stream_destinos: pasalos a otra salida con "
            "stream_destino_editar.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", idProp}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (const std::string e = rt.borrarSalida(a.value("id", std::string())); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data(json{{"salidas", resumenSalidas(rt, vm)}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
