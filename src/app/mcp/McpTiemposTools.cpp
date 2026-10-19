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
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <QPointer>

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
    {
        Tool t;
        t.name = "tiempos_pipeline";
        t.title = "La cadena de una entrada por dentro (elemento por elemento)";
        t.category = "tiempos";
        t.description =
            "Analiza el pipeline de UNA `entrada` (el nombre de tiempos_mapa / video_estado) durante `segundos` (de "
            "fábrica 5): pone una sonda en cada elemento y devuelve:\n"
            "- `hallazgos`, ya razonados y ordenados por gravedad, cada uno con la tarjeta culpable: ida_y_vuelta (el "
            "video baja de la GPU y vuelve a subir: lo del medio trabaja en RAM, y dice si es porque el plugin solo "
            "acepta RAM o porque negoció RAM pudiendo GPU), termina_en_ram, retiene (agrega más de cuadro y medio: tarda "
            "o guarda cuadros), declara_menos (tarda más de lo que le declara al pipeline: el estudio no lo compensa y "
            "puede llegar tarde a la mezcla), declara_de_mas, conversion_cpu (cambia formato o tamaño en la CPU);\n"
            "- `tarjetas`: lo medido de cada tarjeta (sus elementos y los conversores que pide), su memoria de "
            "entrada/salida y sus cruces;\n"
            "- `elementos`, de la fuente a la salida: agrega_ms (mediana; cuenta lo que retiene), agrega_p95_ms, "
            "declara_ms, memoria_entrada/memoria_salida (lo que negoció: gl, d3d12, d3d11, cuda, va, dmabuf, sistema), "
            "puede_entrada/puede_salida (lo que SABE usar: sus pad templates), caps, cruce (baja/sube), nivel_cola_ms y "
            "a qué tarjeta pertenece.\n"
            "`medir: false` devuelve el último análisis sin volver a medir. No cambia nada del aire; fuera del análisis "
            "no cuesta nada. Para entender un plugin en particular: tiempos_plugin.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada"}},
                         {"properties",
                          {{"entrada", {{"type", "string"}}},
                           {"medir", {{"type", "boolean"}, {"description", "de fábrica, sí; false: el último análisis"}}},
                           {"segundos", {{"type", "integer"}, {"minimum", 2}, {"maximum", 30}}}}}};
        t.examples = {R"({"entrada": "Cámara 1"})", R"({"entrada": "Cámara 1", "medir": false})"};
        t.run = [tm](const json& a, Done done) {
            const std::string e = a.value("entrada", std::string());
            if (!tm->existeEntrada(e)) return done(ToolResult::error("no existe la entrada «" + e + "» (tiempos_mapa o video_estado dan las que hay)"));
            if (!a.value("medir", true)) {
                const json r = tm->recorrido(e);
                if (r.is_null()) return done(ToolResult::error("«" + e + "» todavía no se analizó: tiempos_pipeline con medir true"));
                return done(ToolResult().data(r));
            }
            QPointer<tiempos::TiemposModel> vivo = tm;
            const QString err = tm->analizar(std::clamp(a.value("segundos", 5), 2, 30), [vivo, e, done](const json& an) {
                if (!vivo) return done(ToolResult::error("se cerró el análisis"));
                const json r = vivo->recorrido(e);
                if (r.is_null())
                    return done(ToolResult::error(an.value("error", std::string("no se pudo medir «" + e + "»: ¿la entrada está andando?"))));
                done(ToolResult().data(r));
            }, {e});
            if (!err.isEmpty()) done(ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        const QPointer<video::VideoGrafos> v = vm_.videoGrafos();
        Tool t;
        t.name = "tiempos_plugin";
        t.title = "Cómo trabaja un plugin (y cómo trabaja en esta cadena)";
        t.category = "tiempos";
        t.readOnly = true;
        t.description =
            "Cómo trabaja un plugin de video por dentro. Con `elemento` (la fábrica de GStreamer, p. ej. videobalance, "
            "bavtsuperres, glcolorbalance): su documentación, clase, plugin, las memorias que aceptan sus pads "
            "(`memorias.entrada` / `memorias.salida`: gl, d3d12, cuda… = sabe trabajar en la GPU; solo «sistema» = "
            "trabaja en RAM y obliga a bajar el video de la GPU) y sus propiedades. Con `entrada` + `tarjeta` (el id o "
            "el título de la tarjeta en esa entrada) suma `en_uso`: lo que hace AHÍ según el último tiempos_pipeline "
            "(ms medidos contra los que informa, la memoria que negoció, los conversores que pide, la latencia que "
            "declara contra la que mide) y los `hallazgos` que la nombran. Para comparar alternativas: tiempos_plugin de "
            "cada una y mirar `gpu`.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"elemento", {{"type", "string"}}}, {"entrada", {{"type", "string"}}}, {"tarjeta", {{"type", "string"}}}}}};
        t.examples = {R"({"elemento": "videobalance"})", R"({"entrada": "Cámara 1", "tarjeta": "Superresolución"})"};
        t.run = [tm, v](const json& a, Done done) {
            std::string el = a.value("elemento", std::string());
            const std::string e = a.value("entrada", std::string()), tj = a.value("tarjeta", std::string());
            json out = json::object();
            if (!e.empty() && !tj.empty()) {
                const auto minus = [](std::string x) {
                    for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    return x;
                };
                const json mapa = tm->ahora();
                const std::string pre = "tarjeta:" + e + ":";
                const json nodos = mapa.value("nodos", json::array());
                const json* n = nullptr; // apunta adentro de `nodos` (se usa después del for)
                for (const auto& x : nodos) {
                    const std::string id = x.value("id", std::string());
                    if (id.rfind(pre, 0) != 0) continue;
                    if (id == pre + tj || minus(x.value("titulo", std::string())) == minus(tj)) n = &x;
                }
                if (n == nullptr) return done(ToolResult::error("no hay una tarjeta «" + tj + "» en «" + e + "» (tiempos_mapa da las que hay)"));
                const json& d = (*n)["detalle"];
                if (el.empty()) el = d.value("elemento", std::string());
                json uso = {{"id", (*n)["id"]}, {"titulo", (*n)["titulo"]}, {"ms", (*n)["ms"]}, {"fuente", (*n)["fuente"]},
                            {"medido_por_elemento", d.value("medido_sondas", false)}};
                for (const char* k : {"ms_por_cuadro", "memoria_entrada", "memoria_salida", "cruces", "elementos", "caps",
                                      "motor-usado", "proveedor-usado", "error", "andando"})
                    if (d.contains(k)) uso[k] = d[k];
                if (!d.value("medido_sondas", false))
                    uso["nota"] = "sin análisis por elemento: tiempos_pipeline {\"entrada\": \"" + e + "\"} para medirla por dentro";
                json hall = json::array();
                const json r = tm->recorrido(e);
                if (!r.is_null())
                    for (const auto& h : r.value("hallazgos", json::array()))
                        for (const auto& t2 : h.value("tarjetas", json::array()))
                            if (t2.value("id", std::string()) == (*n)["id"].get<std::string>()) hall.push_back(h);
                uso["hallazgos"] = hall;
                out["en_uso"] = uso;
            }
            if (el.empty()) return done(ToolResult::error("hace falta `elemento`, o `entrada` y `tarjeta`"));
            out["elemento"] = el;
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const video::CatalogoGst& c = v->gst();
            if (!c.disponible()) return done(ToolResult::error("GStreamer no está disponible: " + c.error()));
            const json in = c.interior(el);
            if (!in.value("existe", false) && !out.contains("en_uso"))
                return done(ToolResult::error("no existe el elemento «" + el + "» (video_modulos con buscar)"));
            out["interior"] = in;
            const json doc = c.modulo(el);
            if (!doc.empty()) out["documentacion"] = doc;
            json props = json::array();
            for (const auto& p : c.propiedades(el, json::object()))
                props.push_back({{"nombre", p.value("nombre", std::string())}, {"descripcion", p.value("descripcion", std::string())},
                                 {"en_vivo", p.value("enVivo", false)}, {"defecto", p.value("defecto", json())}});
            out["propiedades"] = props;
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
