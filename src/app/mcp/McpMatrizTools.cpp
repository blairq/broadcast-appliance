// ============================================================================
// src/app/mcp/McpMatrizTools.cpp — la matriz de mezcla, por MCP
//
// Lo mismo que Audio -> Matriz: fuentes (a la salida de sus filtros) × pistas
// de OBS y buses virtuales. Operan sobre el mismo objeto (audio/Matriz): lo
// que se cambia por acá se ve en la matriz en el acto, y al revés.
//
// En «audio_rack» y no en «audio»: el modelo reactivo se entrena con las
// fichas de su perfil (que incluye «audio») y una herramienta nueva ahí le
// cambiaría el prompt.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/Matriz.hpp"
#endif

namespace ba::app::mcp {

void McpService::registerMatrizTools() {
#if defined(BA_HAVE_CLAP)
    auto* m = qobject_cast<audio::Matriz*>(vm_.matriz());
    if (m == nullptr) return;
    const auto err = [](const QString& e) { return ToolResult::error(e.toStdString()); };

    {
        Tool t;
        t.name = "audio_matriz";
        t.title = "La matriz de mezcla";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "Fuentes con audio (a la salida de sus filtros) y a qué buses van: las 6 pistas de OBS "
            "(la 1 es el programa; nativas, sin retardo) y los buses virtuales (salidas a un "
            "dispositivo, entradas que vuelven como fuente). Con los envíos y sus ajustes.";
        t.run = [m](const json&, Done done) {
            m->refrescar();
            done(ToolResult().data(m->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_matriz_cruce";
        t.title = "Prender, apagar o ajustar un cruce";
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            "`fuente` → `bus` (pista1..pista6 o el id de un bus virtual): `activo` lo prende o apaga. "
            "En buses virtuales, además, `nivel_db` (-60 a +12), `pan` (-1 izq a 1 der) y `mute`; "
            "`ajustes: false` lo vuelve a 0 dB, centro.";
        t.details =
            "Las pistas de OBS son la asignacion nativa de la fuente: solo prender o apagar, sin retardo. "
            "La pista 1 es lo que sale al AIRE: apagarla saca esa fuente del programa. Los buses virtuales "
            "pasan por una mezcla propia (~25 ms). Una entrada virtual no puede mandarse a su propio bus.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente", "bus"}},
                         {"properties",
                          {{"fuente", {{"type", "string"}}},
                           {"bus", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}},
                           {"nivel_db", {{"type", "number"}, {"minimum", -60}, {"maximum", 12}}},
                           {"pan", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"mute", {{"type", "boolean"}}},
                           {"ajustes", {{"type", "boolean"}}}}}};
        t.examples = {R"({"fuente": "Mic 1", "bus": "pista1", "activo": true})",
                      R"({"fuente": "Mic 1", "bus": "salida1", "activo": true, "nivel_db": -6})"};
        t.run = [m, err](const json& a, Done done) {
            const QString f = QString::fromStdString(a.value("fuente", std::string()));
            const QString b = QString::fromStdString(a.value("bus", std::string()));
            if (a.contains("activo")) {
                const QString e = m->setCruce(f, b, a.value("activo", true));
                if (!e.isEmpty()) return done(err(e));
            }
            if (a.contains("ajustes") && !a.value("ajustes", true)) m->setAjustes(f, b, false);
            if (a.contains("nivel_db") || a.contains("pan") || a.contains("mute")) {
                const QVariantMap c = m->cruces().value(f + "|" + b).toMap();
                if (!c.value("activo").toBool())
                    return done(
                        ToolResult::error("Ese cruce no está activo (o es una pista nativa, sin ajustes)."));
                m->ajustar(f, b, a.value("nivel_db", c.value("nivelDb").toDouble()),
                           a.value("pan", c.value("pan").toDouble()),
                           a.value("mute", c.value("mute").toBool()));
            }
            done(ToolResult::ok("Listo.").data(m->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_agregar";
        t.title = "Agregar una entrada o salida virtual";
        t.category = "audio_rack";
        t.description = "`tipo` \"salida\": un bus que sale por un dispositivo (`dispositivo`: id de "
                        "audio_dispositivos). `tipo` \"entrada\": un bus cuya mezcla vuelve como una fuente "
                        "nueva (aparece en Audio con su menú de filtros). `nombre` opcional.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"tipo", {{"type", "string"}, {"enum", {"entrada", "salida"}}}},
                           {"nombre", {{"type", "string"}}},
                           {"dispositivo", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            const QString e =
                a.value("tipo", std::string()) == "entrada"
                    ? m->agregarEntradaVirtual(nombre)
                    : m->agregarSalidaVirtual(nombre,
                                              QString::fromStdString(a.value("dispositivo", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok("Agregado.").data(m->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_quitar";
        t.title = "Quitar un bus virtual";
        t.category = "audio_rack";
        t.destructive = true;
        t.description = "Quita una entrada o salida virtual (y sus envíos). Las pistas de OBS no se quitan.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"bus"}}, {"properties", {{"bus", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e = m->quitarBus(QString::fromStdString(a.value("bus", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok("Quitado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_renombrar";
        t.title = "Renombrar un bus o una pista";
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            "Cambia el nombre de una pista (pista1..pista6) o de un bus virtual. Una entrada virtual "
            "cambia también el nombre de su fuente.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"bus", "nombre"}},
                         {"properties", {{"bus", {{"type", "string"}}}, {"nombre", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e = m->renombrar(QString::fromStdString(a.value("bus", std::string())),
                                           QString::fromStdString(a.value("nombre", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok("Renombrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_dispositivo";
        t.title = "A qué dispositivo sale una salida virtual";
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            "Cambia el dispositivo de una salida virtual (`dispositivo`: id de audio_dispositivos).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"bus", "dispositivo"}},
            {"properties", {{"bus", {{"type", "string"}}}, {"dispositivo", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e =
                m->setDispositivo(QString::fromStdString(a.value("bus", std::string())),
                                  QString::fromStdString(a.value("dispositivo", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok("Listo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_dispositivos";
        t.title = "Dispositivos de salida del equipo";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "Las salidas de audio del equipo (placas, auriculares, cables virtuales): id y nombre, "
            "para las salidas virtuales de la matriz.";
        t.run = [m](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : m->dispositivos())
                out.push_back({{"id", v.toMap().value("id").toString().toStdString()},
                               {"nombre", v.toMap().value("nombre").toString().toStdString()}});
            done(ToolResult().data(json{{"dispositivos", out}}));
        };
        core_.addTool(std::move(t));
    }
#endif
}

} // namespace ba::app::mcp
