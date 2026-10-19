// ============================================================================
// src/app/mcp/McpTiemposTools.cpp — los tiempos del pipeline por MCP (tiempos_*)
//
// Lo mismo que muestra la página «Latencias del pipeline» (TiemposModel): de cada entrada a cada
// salida, nodo por nodo, cuánto agrega cada uno y cuánto lleva desde la captura.
// ============================================================================
#include "mcp/McpService.hpp"

#include <QDateTime>
#include "mcp/McpToolsCommon.hpp"
#include "tiempos/TiemposModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerTiemposTools() {
    tiempos::TiemposModel* tm = vm_.tiemposModel();
    if (tm == nullptr) return;
    core_.addCategory("tiempos", "Tiempos del pipeline",
                      "Cuánto tarda cada cosa, de cada entrada a cada salida, en video y en audio: la captura y "
                      "la decodificación, cada tarjeta y cada módulo de audio, la escena y la mezcla, y cada salida. "
                      "Cada número dice si es medido o declarado (la latencia que se le fijó).");
    {
        Tool t;
        t.name = "tiempos_mapa";
        t.title = "Los tiempos del pipeline";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "El grafo de demoras de ahora: `nodos` (entrada, tarjeta, modulo, escena, mezcla, transmision, "
                        "grabacion) con `ms` (lo que agrega ese nodo), `acumulado_ms` (desde la captura), `fuente` "
                        "(medido | declarado | sin_medir) y `estado` (bien | justo | tarde: contra la latencia del "
                        "estudio); `tramos` (de un nodo a otro, video o audio); `salidas` con su `total_ms` y el "
                        "`camino` más lento que lo explica; `eventos` de los últimos 10 min (conexiones y "
                        "desconexiones, cambios de escena, salidas que arrancan o paran, cambios de latencia).";
        t.run = [tm](const json&, Done done) {
            json m = tm->ahora();
            m["eventos"] = tm->eventosJson(600);
            done(ToolResult().data(m));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_medir";
        t.title = "Correr un análisis de tiempos";
        t.category = "tiempos";
        t.description = "Mide `segundos` (de fábrica 10, de 2 a 120) y devuelve la mediana y el p95 de lo que agrega "
                        "cada nodo y del total de cada salida. Mientras mide, cada elemento del pipeline de las entradas "
                        "(todas, o las de `entradas`) lleva una sonda: `pipelines` trae por entrada cada elemento con "
                        "agrega_ms (lo que tarda Y lo que retiene), memoria_entrada/memoria_salida (gl, d3d12, cuda… o "
                        "sistema) y `cruces` (dónde el video baja de la GPU a la memoria o vuelve a subir: cada cruce copia "
                        "el cuadro entero). Lo de cada tarjeta pasa a ser lo medido. Queda en el historial del evento (la "
                        "página Latencias del pipeline). No cambia nada del aire. Uno por vez.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"segundos", {{"type", "integer"}, {"minimum", 2}, {"maximum", 120}}},
                           {"entradas", {{"type", "array"}, {"items", {{"type", "string"}}},
                                         {"description", "las entradas a analizar por elemento (vacío: todas)"}}}}}};
        t.run = [tm](const json& a, Done done) {
            std::vector<std::string> entradas;
            for (const auto& x : a.value("entradas", json::array()))
                if (x.is_string()) entradas.push_back(x.get<std::string>());
            const QString e = tm->analizar(a.value("segundos", 10), [done](const json& r) {
                if (r.value("estado", std::string()) != "listo") return done(ToolResult::error(r.value("error", std::string("falló"))));
                done(ToolResult().data(r));
            }, entradas);
            if (!e.isEmpty()) done(ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_eventos";
        t.title = "Los eventos de la línea de tiempo";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "Lo que pasó en los últimos `segundos` (de fábrica 600; hasta 1800): conexion / desconexion "
                        "de entradas, escena (lo que salió al aire), salida (arrancó o paró), latencia (del estudio), "
                        "tarde (una entrada que llega tarde a la mezcla), rearme, y nota (las que puso el operador o el "
                        "asistente). Cada uno con `t` (ms de época). Para cruzar con las latencias: tiempos_evento_impacto.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1800}}}}}};
        t.run = [tm](const json& a, Done done) { done(ToolResult().data({{"eventos", tm->eventosJson(a.value("segundos", 600))}})); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_evento_agregar";
        t.title = "Marcar un momento en la línea de tiempo";
        t.category = "tiempos";
        t.description = "Agrega una nota con `texto` en la línea de tiempo de Latencias del pipeline: ahora, o hace "
                        "`hace_s` segundos, o en `t_ms` (ms de época). Sirve para marcar algo que el sistema no ve solo "
                        "(«se movió el cable», «entró el presidente») y después ver con tiempos_evento_impacto qué "
                        "cambió.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"texto"}},
                         {"properties",
                          {{"texto", {{"type", "string"}}}, {"hace_s", {{"type", "number"}, {"minimum", 0}}}, {"t_ms", {{"type", "integer"}}}}}};
        t.run = [tm](const json& a, Done done) {
            qint64 t = a.value("t_ms", qint64{0});
            if (a.contains("hace_s")) t = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(a.value("hace_s", 0.0) * 1000);
            done(ToolResult().data(tm->agregarEvento(t, a.value("texto", std::string()), "asistente")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_evento_impacto";
        t.title = "Qué cambió alrededor de un momento";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "Para el momento `t_ms` (el `t` de un evento de tiempos_eventos) o hace `hace_s` segundos: la "
                        "mediana de lo que agrega cada nodo (y de lo acumulado desde la captura) en los `segundos` de "
                        "antes (de fábrica 10) contra los de después, ordenado por cuánto cambió. Así se ve si un cambio "
                        "de escena, una conexión o una nota coincide con un salto de latencia y dónde.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"t_ms", {{"type", "integer"}}}, {"hace_s", {{"type", "number"}, {"minimum", 0}}},
                           {"segundos", {{"type", "integer"}, {"minimum", 2}, {"maximum", 300}}}}}};
        t.run = [tm](const json& a, Done done) {
            qint64 t = a.value("t_ms", qint64{0});
            if (a.contains("hace_s")) t = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(a.value("hace_s", 0.0) * 1000);
            if (t <= 0) return done(ToolResult::error("falta `t_ms` (el de un evento) o `hace_s`"));
            done(ToolResult().data({{"t_ms", t}, {"segundos", a.value("segundos", 10)}, {"nodos", tm->impacto(t, a.value("segundos", 10))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_linea";
        t.title = "Qué tarjetas se ven en la línea de tiempo";
        t.category = "tiempos";
        t.description = "Elige los `nodos` (ids de tiempos_mapa) que la página Latencias del pipeline muestra en su "
                        "línea de tiempo, cada uno con su color (en ese orden). Sin `nodos`, dice cuáles están. "
                        "`agregar` / `sacar` cambian uno sin tocar los demás.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nodos", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"agregar", {{"type", "string"}}},
                           {"sacar", {{"type", "string"}}}}}};
        t.run = [tm](const json& a, Done done) {
            QStringList l = tm->linea();
            if (a.contains("nodos") && a["nodos"].is_array()) {
                l.clear();
                for (const auto& n : a["nodos"])
                    if (n.is_string()) l << QString::fromStdString(n.get<std::string>());
            }
            if (a.contains("agregar") && !l.contains(QString::fromStdString(a.value("agregar", std::string()))))
                l << QString::fromStdString(a.value("agregar", std::string()));
            if (a.contains("sacar")) l.removeAll(QString::fromStdString(a.value("sacar", std::string())));
            tm->setLinea(l);
            json out = json::array();
            for (const QString& s : l) out.push_back(s.toStdString());
            done(ToolResult().data({{"linea", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "tiempos_nodo";
        t.title = "Un nodo de los tiempos, con su historia";
        t.category = "tiempos";
        t.readOnly = true;
        t.description = "Un nodo de tiempos_mapa por su `id` (p. ej. «entrada:Cámara 1», «tarjeta:Cámara 1:superres», "
                        "«salida:transmision») con su detalle y su historia de los últimos `segundos` (de fábrica "
                        "60; hasta 1800: se junta siempre, una muestra por segundo).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", {{"type", "string"}}}, {"segundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1800}}}}}};
        t.run = [tm](const json& a, Done done) {
            tm->ahora();
            const json n = tm->nodo(a.value("id", std::string()), a.value("segundos", 60));
            if (n.is_null()) return done(ToolResult::error("no hay un nodo «" + a.value("id", std::string()) + "» (tiempos_mapa da los que hay)"));
            done(ToolResult().data(n));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
