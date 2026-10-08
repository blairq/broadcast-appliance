// ============================================================================
// src/app/mcp/McpVideoTools.cpp — el grafo de tarjetas de video de cada entrada, por MCP
//
// Lo mismo que la página «Video»: operan sobre el mismo objeto
// (video/VideoGrafos), así que lo que se arma por acá se ve allá en el acto y
// lo corre la GPU (filtro ba_video_grafo).
//
// En su propia categoría («video»): no está en el perfil del modelo reactivo,
// así que agregarlas no le cambia el prompt.
// ============================================================================

#include "mcp/EsperarEntrada.hpp"
#include "Mapa.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Diagnostico.hpp"
#include "video/BancoSuperRes.hpp"
#include "video/SuperRes.hpp"
#include "video/VideoGrafos.hpp"

#include <QJsonDocument>
#include <QPointer>

#include <cctype>

namespace ba::app::mcp {

namespace {
/// `{"tipo": "plugin", "plugin", "tarjeta", "parametros"}` es una tarjeta `gst`
/// con el módulo `baplugin` (la tarjeta de video de un plugin de Python).
nlohmann::json tarjetaDePlugin(nlohmann::json tj) {
    if (tj.value("tipo", std::string()) != "plugin") return tj;
    nlohmann::json props{{"plugin", tj.value("plugin", std::string())},
                         {"tarjeta", tj.value("tarjeta", std::string())}};
    const nlohmann::json par = tj.value("parametros", nlohmann::json::object());
    if (par.is_object() && !par.empty()) props["parametros"] = par.dump();
    tj["tipo"] = "gst";
    tj["elemento"] = "baplugin";
    tj["propiedades"] = props;
    return tj;
}
} // namespace

void McpService::registerVideoTools() {
    video::VideoGrafos* vg = vm_.videoGrafos();
    if (vg == nullptr) return;
    core_.addCategory(
        "video", "Grafo de video",
        "Un grafo de video para todo el estudio, como un pipeline de GStreamer: cada entrada es un sub-grafo "
        "(entrada -> tarjetas -> escena) y los cables pueden cruzar entre entradas (p. ej. a un compositor). "
        "Tarjetas propias (color, croma, recorte, salida) o cualquier "
        "módulo de "
        "video de GStreamer (video_modulos: el catálogo documentado). Entradas virtuales (barras, negro, un "
        "pipeline de GStreamer) con video_entrada_virtual. Las tarjetas de imagen corren en la cadena de "
        "cada entrada.");
    const QPointer<video::VideoGrafos> v = vg;
    const auto deQt = [](const QVariant& x) {
        return json::parse(QJsonDocument::fromVariant(x).toJson(QJsonDocument::Compact).toStdString(),
                           nullptr, false);
    };
    const json entrada = {{"type", "string"}, {"description", "La entrada de video (como en video_grafo)."}};
    const json idTarjeta = {
        {"type", "string"},
        {"description", "El id de la tarjeta (como en video_grafo), p. ej. «camara-1.croma»."}};
    // Lo que cambió, visto desde la entrada que corresponde (o todo el estudio).
    const auto hecho = [v](const std::string& verEntrada, const std::string& error, Done done) {
        if (!error.empty()) return done(ToolResult::error(error));
        done(ToolResult().data(v->describir(verEntrada)));
    };

    {
        Tool t;
        t.name = "video_grafo";
        t.title = "El grafo de video";
        t.category = "video";
        t.readOnly = true;
        t.description = "Sin `entrada`: todo el estudio (cada entrada con su sub-grafo, los nodos del "
                        "estudio y los cables "
                        "entre ellos) y las tarjetas propias. Con `entrada`: su sub-grafo, con los nodos de "
                        "afuera que se "
                        "conectan con él marcados `externo`. Cada nodo trae sus puertos (`entradas`, "
                        "`salidas`; un compositor "
                        "tiene puertos a pedido in_0, in_1… y siempre uno libre). Una tarjeta que no puede "
                        "correr trae `error` "
                        "con el motivo (p. ej. un módulo de GStreamer que no arrancó).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"entrada", entrada},
              {"mostrar",
               {{"type", "string"},
                {"enum", {"racks", "estudio"}},
                {"description", "Además, que la página Video muestre esa vista (y esa entrada)."}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            v->sincronizar();
            const std::string e = a.value("entrada", std::string());
            if (a.contains("mostrar")) {
                v->setVista(QString::fromStdString(a.value("mostrar", std::string())));
                if (!e.empty()) v->seleccionar(QString::fromStdString(e));
            }
            json d = v->describir(e);
            if (d.contains("error")) return done(ToolResult::error(d.value("error", std::string())));
            if (e.empty()) {
                json ents = json::array();
                for (const QString& n : v->entradas())
                    ents.push_back(n.toStdString());
                d["entradas"] = ents;
                d["tarjetas"] = video::GrafoVideo::catalogo();
            }
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_filtros";
        t.title = "Los controles de una tarjeta";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "Con `id` de una tarjeta (un módulo de GStreamer): sus controles (nombre, tipo, valor, rango, "
            "opciones). Sin `id`, vacío: los filtros de OBS se fueron con libobs; el catálogo de módulos "
            "está en video_modulos.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", idTarjeta}}}};
        t.run = [v, deQt](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            done(ToolResult().data(deQt(id.isEmpty() ? v->filtrosObs() : v->propiedades(id))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_modulos";
        t.title = "Los módulos de video de GStreamer (catálogo documentado)";
        t.category = "video";
        t.readOnly = true;
        t.description = "El catálogo de módulos de video de GStreamer que trae el programa, con su "
                        "documentación en castellano "
                        "(para qué sirven y cómo se usan en vivo). Sin argumentos: un resumen por categoría. "
                        "`buscar`: por "
                        "palabra (en castellano o en inglés, en el nombre, el título o la descripción). "
                        "`categoria`: los de "
                        "una. `elemento`: uno solo con todo (clase, licencia, paquete) y sus propiedades. "
                        "`tarjeta` dice si va "
                        "como tarjeta de filtro (video_tarjeta_agregar tipo gst); los demás son fuentes, "
                        "salidas o mezcladores.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"buscar", {{"type", "string"}}},
              {"categoria", {{"type", "string"}}},
              {"elemento", {{"type", "string"}}},
              {"soloTarjetas", {{"type", "boolean"}, {"description", "solo los que van como tarjeta"}}}}}};
        t.examples = {R"({"buscar": "croma"})", R"({"elemento": "deinterlace"})",
                      R"({"categoria": "Color"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const video::CatalogoGst& c = v->gst();
            if (!c.disponible()) return done(ToolResult::error("GStreamer no está disponible: " + c.error()));
            const std::string el = a.value("elemento", std::string());
            if (!el.empty()) {
                json m = c.modulo(el);
                if (m.empty())
                    return done(
                        ToolResult::error("no existe el módulo «" + el + "» (video_modulos con buscar)"));
                m["propiedades"] = c.propiedades(el, json::object());
                return done(ToolResult().data(m));
            }
            const auto minus = [](std::string x) {
                for (auto& ch : x)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return x;
            };
            const std::string buscar = minus(a.value("buscar", std::string())),
                              cat = a.value("categoria", std::string());
            const bool solo = a.value("soloTarjetas", false);
            if (buscar.empty() && cat.empty() && !solo) {
                json cats = json::object();
                for (const auto& m : c.modulos()) {
                    json& x = cats[m.value("categoria", std::string())];
                    if (!x.is_object()) x = {{"cantidad", 0}, {"ejemplos", json::array()}};
                    x["cantidad"] = x.value("cantidad", 0) + 1;
                    if (!x.contains("ejemplos")) x["ejemplos"] = json::array();
                    if (x["ejemplos"].size() < 6) x["ejemplos"].push_back(m.value("elemento", std::string()));
                }
                return done(ToolResult().data(
                    {{"version", c.version()}, {"total", c.modulos().size()}, {"categorias", cats}}));
            }
            json out = json::array();
            for (const auto& m : c.modulos()) {
                if (!cat.empty() && m.value("categoria", std::string()) != cat) continue;
                if (solo && !m.value("tarjeta", false)) continue;
                if (!buscar.empty()) {
                    const std::string todo =
                        minus(m.value("elemento", "") + " " + m.value("titulo", "") + " " +
                              m.value("nombre", "") + " " + m.value("para", "") + " " +
                              m.value("descripcion", "") + " " + m.value("uso", ""));
                    if (todo.find(buscar) == std::string::npos) continue;
                }
                out.push_back({{"elemento", m.value("elemento", "")},
                               {"titulo", m.value("titulo", m.value("nombre", ""))},
                               {"categoria", m.value("categoria", "")},
                               {"para", m.value("para", m.value("descripcion", ""))},
                               {"tarjeta", m.value("tarjeta", false)}});
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_entrada_virtual";
        t.title = "Crear una entrada virtual (barras, negro, un pipeline de GStreamer)";
        t.category = "video";
        t.description =
            "Sin `nombre`: las entradas virtuales que hay (barras HD, barras 75 %, barras con la hora, "
            "negro, "
            "blanco, verde, placa de zonas, damero, pelota, ruido). Con `nombre` y `preset` (o `pipeline`, "
            "la parte "
            "de origen de un pipeline de GStreamer como en gst-launch, p. ej. «rtspsrc location=… ! "
            "decodebin»): "
            "la crea en `escena` (obligatoria: para no tocar la que está al aire sin querer; `al_aire: true` "
            "la pone en "
            "la del aire). Queda como una entrada más: con su sub-grafo de tarjetas. Para un stream de red "
            "(srt/rtmp/rtsp) usá video_entrada_red, que lo sincroniza.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"preset", {{"type", "string"}}},
                           {"pipeline", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}},
                           {"al_aire", {{"type", "boolean"}}}}}};
        t.examples = {
            R"({"nombre": "Barras", "preset": "barras-hd"})",
            R"({"nombre": "Cámara IP", "pipeline": "rtspsrc location=rtsp://10.0.0.5/stream latency=200 ! decodebin"})"};
        t.run = [v, deQt](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string nombre = a.value("nombre", std::string());
            if (nombre.empty()) return done(ToolResult().data(deQt(video::VideoGrafos::virtuales())));
            if (a.value("escena", std::string()).empty() && !a.value("al_aire", false))
                return done(ToolResult::error(
                    "falta `escena` (o `al_aire: true` para ponerla en la que está al aire)"));
            const std::string pl = a.value("pipeline", std::string());
            const std::string err = v->crearEntradaVirtual(
                nombre, pl.empty() ? a.value("preset", std::string("barras-hd")) : "pipeline:" + pl,
                a.value("escena", std::string()));
            if (!err.empty()) return done(ToolResult::error(err));
            done(ToolResult().data(v->describir(nombre)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_agregar";
        t.title = "Agregar una tarjeta de video";
        t.category = "video";
        t.description =
            "Agrega una tarjeta (para video, PRIMERO GStreamer: `gst` con un módulo de video_modulos). En la "
            "`entrada` "
            "(después de `despues`; vacío = justo antes de su escena): `color`, "
            "`croma`, `recorte` y `gst` (un "
            "módulo de "
            "GStreamer: `elemento`, de video_modulos) y `plugin` (la tarjeta de un plugin de Python: "
            "`plugin`, "
            "`tarjeta`, `parametros`) se intercalan; "
            "`retardo` atrasa la imagen; `salida` se cuelga como RAMA (publica ese punto como fuente con "
            "nombre). "
            "`compositor` es del estudio (sin entrada): se le conectan entradas (o tarjetas) con "
            "video_cable, y se "
            "pone en una escena con video_salida_a_escena o se cablea a la escena de una entrada.";
        t.details =
            "`params` con los de fábrica que quieras cambiar (video_tarjeta_ayuda explica cada uno, con "
            "rangos y opciones; el texto del overlay de Rostros también). Croma: `clave` "
            "0xRRGGBB (verde "
            "0x00B140), `similitud`, `suavidad`, `derrame`. Color: `brillo`, `contraste`, `saturacion`, "
            "`gamma`. "
            "Recorte: `izquierda`, `arriba`, `derecha`, `abajo` (0..1), `espejoH`, `espejoV`. Retardo: "
            "`cuadros` "
            "(0..120, para sincronizar). Rostros: `fps`, `reconocer`, `detector`, `marcar`, `estilo`, `color`, "
            "`texto` (plantilla {nombre} {cargo} {id} {confianza} {n}); la pone sola Personas al elegir una "
            "cámara (`entrada:<camara>`), `uso` dice para qué. Seguir: `objetivo`, `zoom`, `aire`… Salida: `nombre`. Vista 360 (una cámara 360 vista como una común, en la placa): `yaw`, `pitch`, `fov` (grados), `proporcion`. Compositor: "
            "`ancho`, `alto`, `fondo` (0xAARRGGBB), "
            "`nombre`, y por puerto `capas.in_N` {x, y, ancho, alto (0..1 del lienzo), opacidad, orden, "
            "ajuste "
            "(encajar|llenar)}; sin capas, mosaico; null en una capa la vuelve al mosaico. "
            "Un módulo de GStreamer: sus propiedades en `params.propiedades`, con los nombres de "
            "GStreamer (video_modulos con `elemento` las lista; los enum, por su nick).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"tipo"}},
            {"properties",
             {{"entrada", entrada},
              {"tipo",
               {{"type", "string"},
                {"enum",
                 {"color", "croma", "recorte", "gst", "plugin", "retardo", "normalizar", "rostros",
                  "seguir", "salida", "compositor", "superres", "vista360"}}}},
              {"plugin",
               {{"type", "string"},
                {"description", "con tipo plugin: el id del plugin de Python (plugin_listar)"}}},
              {"tarjeta",
               {{"type", "string"}, {"description", "con tipo plugin: la tarjeta de video que declaró"}}},
              {"parametros", {{"type", "object"}, {"description", "con tipo plugin: sus ajustes"}}},
              {"elemento",
               {{"type", "string"},
                {"description", "con tipo gst: el módulo de GStreamer (p. ej. videobalance)"}}},
              {"despues", idTarjeta},
              {"params", {{"type", "object"}}}}}};
        t.examples = {
            R"({"entrada": "Cámara 1", "tipo": "croma"})",
            R"({"entrada": "Cámara 1", "tipo": "gst", "elemento": "gamma", "params": {"propiedades": {"gamma": 1.3}}})",
            R"({"tipo": "compositor"})",
            R"({"entrada": "Cámara 1", "tipo": "rostros", "params": {"marcar": true, "estilo": "esquinas", "texto": "{nombre} — {cargo}"}})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const json ap = tarjetaDePlugin(json{{"tipo", a.value("tipo", std::string())},
                                                 {"plugin", a.value("plugin", std::string())},
                                                 {"tarjeta", a.value("tarjeta", std::string())},
                                                 {"parametros", a.value("parametros", json::object())}});
            const std::string e = a.value("entrada", std::string()), tipo = ap.value("tipo", std::string());
            json params = a.value("params", json::object());
            if (ap.contains("elemento"))
                params = {{"elemento", ap["elemento"]}, {"propiedades", ap["propiedades"]}};
            std::string etiqueta;
            if (tipo == "obs") {
                const std::string f = a.value("filtro", params.value("filtro", std::string()));
                etiqueta = video::VideoGrafos::nombreFiltro(f);
                if (etiqueta.empty())
                    return done(
                        ToolResult::error("no existe el filtro «" + f + "» (video_filtros los lista)"));
                params["filtro"] = f;
            }
            std::string nuevo;
            const std::string err = v->editar([&](video::GrafoVideo& g) {
                std::string error;
                if (tipo == "gst") {
                    nuevo =
                        v->agregarModuloEn(g, a.value("elemento", params.value("elemento", std::string())), e,
                                           a.value("despues", std::string()),
                                           params.value("propiedades", json::object()), &error);
                    return error;
                }
                nuevo = g.agregar(tipo, e, a.value("despues", std::string()), params, &error);
                if (!nuevo.empty() && !etiqueta.empty())
                    g.ajustar(nuevo, json{{"etiqueta", etiqueta}}, nullptr);
                return error;
            });
            if (!err.empty()) return done(ToolResult::error(err));
            json d = v->describir(tipo == "compositor" ? std::string() : e);
            d["agregada"] = nuevo;
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_ayuda";
        t.title = "Ayuda de un tipo de tarjeta";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "Qué hace un tipo de tarjeta de video, cada parámetro (tipo, rango, opciones, de fábrica) y qué "
            "eventos de GStreamer manda y escucha. Con Rostros, además, cómo se arma el texto del overlay. Sin "
            "`tipo`: todos.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}, {"description", "rostros, seguir, ptz, color…"}}}}}};
        t.examples = {R"({"tipo": "rostros"})", R"({"tipo": "seguir"})"};
        t.run = [](const json& a, Done done) {
            const std::string tipo = a.value("tipo", std::string());
            const json cat = video::GrafoVideo::catalogo();
            if (tipo.empty()) return done(ToolResult().data({{"tarjetas", cat}}));
            for (const json& c : cat) {
                if (c.value("tipo", std::string()) != tipo) continue;
                return done(ToolResult().data(c));
            }
            done(ToolResult::error("no hay una tarjeta «" + tipo + "»; sin `tipo` se listan todas"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_mover";
        t.title = "Mover una tarjeta en la cadena";
        t.category = "video";
        t.description =
            "Lleva la tarjeta `id` a otro lugar de la cadena de su entrada: después de `despues` (el id de otra "
            "tarjeta, o de la entrada para ponerla primera; vacío = justo antes de la escena), o un lugar antes "
            "(`paso`: -1) o después (`paso`: 1).";
        t.details =
            "El orden importa: el PTZ antes de la superresolución le da detalle a lo que se amplía; Rostros antes "
            "del PTZ ve la toma entera (y el seguimiento puede llevar al PTZ fuera de lo que se ve); después, ve "
            "lo que sale del encuadre. Todas las tarjetas dejan pasar los eventos de las otras (ba/rostros, "
            "ba/ptz, ba/encuadre, ba/control), así que cualquier orden funciona.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", idTarjeta},
                           {"entrada", entrada},
                           {"despues", {{"type", "string"}, {"description", "después de esta tarjeta"}}},
                           {"paso", {{"type", "integer"}, {"enum", {-1, 1}}}}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            QString e;
            if (a.contains("paso"))
                e = v->correr(id, a.value("paso", 1));
            else
                e = v->reubicar(id, QString::fromStdString(a.value("despues", std::string())));
            hecho(a.value("entrada", std::string()), e.toStdString(), done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_quitar";
        t.title = "Quitar una tarjeta de video";
        t.category = "video";
        t.description =
            "Quita la tarjeta `id`: un filtro se puentea (lo de antes queda unido con lo de después).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", idTarjeta}, {"entrada", entrada}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(a.value("entrada", std::string()), v->editar([&](video::GrafoVideo& g) {
                std::string error;
                g.quitar(a.value("id", std::string()), &error);
                return error;
            }),
                  done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_ajustar";
        t.title = "Ajustar una tarjeta de video";
        t.category = "video";
        t.idempotent = true;
        t.description = "Cambia `params` de la tarjeta `id` (solo los que tiene, del mismo tipo) y/o su "
                        "`etiqueta`. Un módulo de GStreamer: `{\"propiedades\": {…}}` (se "
                        "mezclan). Una tarjeta "
                        "manejada por un controlador: el ajuste va al controlador (cambia en todas); lo "
                        "propio de esa entrada, en "
                        "`{\"local\": {…}}`. Un compositor: `{\"capas\": {\"in_0\": {…}}}` por puerto. La "
                        "tarjeta de un plugin de "
                        "Python: `{\"parametros\": {\"fuerza\": 0.5}}` (o los parámetros sueltos), se "
                        "mezclan con los que tiene.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"id", "params"}},
            {"properties", {{"id", idTarjeta}, {"params", {{"type", "object"}}}, {"entrada", entrada}}}};
        t.examples = {R"({"id": "camara-1.croma", "params": {"similitud": 0.5}})"};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(a.value("entrada", std::string()), v->editar([&](video::GrafoVideo& g) {
                return video::VideoGrafos::ajustarEn(g, a.value("id", std::string()),
                                                     a.value("params", json::object()));
            }),
                  done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_cable";
        t.title = "Conectar o desconectar en el grafo de video";
        t.category = "video";
        t.description =
            "Un cable de `desde` a `hacia` (cualquier par de tarjetas del estudio, aunque sean de entradas "
            "distintas). `entrada`: el puerto de `hacia` (vacío = el primero; en un compositor, uno nuevo). "
            "A cada "
            "puerto le llega UNO (el nuevo reemplaza al que había); de una salida pueden salir varios. Sin "
            "ciclos. "
            "Con `quitar`, lo saca.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"desde", "hacia"}},
            {"properties",
             {{"desde", idTarjeta},
              {"hacia", idTarjeta},
              {"salida", {{"type", "string"}}},
              {"entrada", {{"type", "string"}, {"description", "puerto de `hacia` (in, in_0…)"}}},
              {"quitar", {{"type", "boolean"}}}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(std::string(), v->editar([&](video::GrafoVideo& g) {
                std::string error;
                const std::string d = a.value("desde", std::string()), h = a.value("hacia", std::string());
                if (a.value("quitar", false)) {
                    if (!g.desconectar(d, a.value("salida", std::string()), h,
                                       a.value("entrada", std::string())))
                        error = "no hay un cable de «" + d + "» a «" + h + "»";
                } else {
                    g.conectar(d, a.value("salida", std::string()), h, a.value("entrada", std::string()),
                               &error);
                }
                return error;
            }),
                  done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_controlador";
        t.title = "Manejar tarjetas de varias entradas con un controlador";
        t.category = "video";
        t.description =
            "Un controlador es un nodo del estudio con el molde de una tarjeta: las tarjetas conectadas a su "
            "salida "
            "de control (`ctl`) corren con SUS parámetros (la misma corrección de color para las cámaras de "
            "tres "
            "marcas), y cada una puede tener lo suyo encima (`local`, con video_tarjeta_ajustar). Con "
            "`tarjeta`: "
            "hace un controlador con esa tarjeta (o devuelve el que ya la maneja). Con `controlador` y "
            "`entrada`: "
            "agrega a esa entrada una tarjeta manejada por él (antes de su escena, o después de `despues`). "
            "Con "
            "`soltar` (id de tarjeta): la desconecta y se queda con los valores que usaba. Para conectar una "
            "tarjeta que ya existe: video_cable desde el controlador (salida ctl) a la tarjeta (entrada "
            "ctl).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"tarjeta", idTarjeta},
                           {"controlador", idTarjeta},
                           {"entrada", entrada},
                           {"despues", idTarjeta},
                           {"soltar", idTarjeta}}}};
        t.examples = {R"({"tarjeta": "sony.gst"})",
                      R"({"controlador": "estudio.control", "entrada": "Pelco"})",
                      R"({"soltar": "pelco.gst"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::string hecho;
            const std::string err = v->editar([&](video::GrafoVideo& g) {
                std::string e;
                if (a.contains("soltar")) {
                    g.soltar(a.value("soltar", std::string()), &e);
                } else if (a.contains("controlador")) {
                    hecho = g.agregarControlada(a.value("controlador", std::string()),
                                                a.value("entrada", std::string()),
                                                a.value("despues", std::string()), &e);
                } else if (a.contains("tarjeta")) {
                    hecho = g.controlarDesde(a.value("tarjeta", std::string()), &e);
                } else {
                    e = "falta `tarjeta`, `controlador` + `entrada`, o `soltar`";
                }
                return e;
            });
            if (!err.empty()) return done(ToolResult::error(err));
            json d = v->describir(std::string());
            if (!hecho.empty()) d["id"] = hecho;
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_salida_a_escena";
        t.title = "Poner una salida o un compositor del grafo en una escena";
        t.category = "video";
        t.description =
            "Pone la tarjeta salida `id` (o un compositor) como fuente en `escena` (obligatoria: nunca la "
            "del aire sin "
            "pedirlo). La fuente se llama como el nombre de la tarjeta (se cambia con video_tarjeta_ajustar "
            "{\"params\": {\"nombre\": …}} antes de ponerla). Queda guardada como cualquier fuente de la "
            "escena. Para un "
            "mosaico entero en una llamada: video_mosaico.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "escena"}},
                         {"properties", {{"id", idTarjeta}, {"escena", {{"type", "string"}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string err =
                v->ponerEnEscena(a.value("id", std::string()), a.value("escena", std::string()));
            if (!err.empty()) return done(ToolResult::error(err));
            done(ToolResult::ok("Salida puesta en la escena."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_grafo_limpiar";
        t.title = "Volver el video de una entrada a como vino";
        t.category = "video";
        t.description = "Deja la `entrada` en «entrada -> escena»: saca sus tarjetas y lo que llegaba a su "
                        "escena desde afuera.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar([&](video::GrafoVideo& g) {
                g.limpiar(e);
                return std::string();
            }),
                  done);
        };
        core_.addTool(std::move(t));
    }
    // ============================================================================
    // Armar pipelines enteros, diagnosticar, relojes y sincronía.
    // ============================================================================
    {
        Tool t;
        t.name = "video_pipeline";
        t.title = "Armar la cadena de tarjetas de una entrada (en una llamada)";
        t.category = "video";
        t.description =
            "Arma, en orden, las tarjetas de la `entrada` (de la cámara hacia la escena) en UNA llamada y "
            "valida "
            "cada una. `modo`: «reemplazar» (de fábrica: saca las que había) o «agregar» (al final). Cada "
            "tarjeta: "
            "{tipo, ...}. Tipos (usá `gst` primero: color, nitidez, efectos y casi todo lo de video se hace "
            "con GStreamer): "
            "`gst` (+`elemento` y `propiedades`, de video_modulos), `obs` (+`filtro` y "
            "`ajustes`), `color` {brillo, contraste, saturacion, gamma}, `croma`, `recorte`, `retardo` {ms}, "
            "`normalizar` {matriz_declarada, matriz_real, rango_declarado, rango_real, desentrelazar}, "
            "`rostros`, "
            "`salida` {nombre} (rama). COMPARTIR entre entradas: `compartida: \"nombre\"` en la tarjeta; la "
            "primera "
            "vez crea un controlador con ese nombre y las siguientes (en otras entradas, con el mismo "
            "nombre) "
            "quedan manejadas por él: cambiar el controlador las cambia a todas. Lo propio de una entrada "
            "sobre lo "
            "compartido: `local: {...}` (p. ej. {\"propiedades\": {\"hue\": -0.05}}). VARIAS ENTRADAS en una "
            "llamada: "
            "`entradas: [...]` con las mismas `tarjetas` y `local_por_entrada` para lo propio de cada una "
            "(el caso «la "
            "misma corrección en las tres cámaras»). El controlador toma el id del nombre compartido "
            "(estudio.<nombre>). "
            "Devuelve el sub-grafo y el diagnóstico de cada entrada.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tarjetas"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"entradas",
                            {{"type", "array"},
                             {"items", {{"type", "string"}}},
                             {"description", "la MISMA cadena en varias entradas a la vez (las `compartida` "
                                             "quedan manejadas por un solo controlador)"}}},
                           {"local_por_entrada",
                            {{"type", "object"},
                             {"description", "lo propio de cada entrada sobre lo compartido: {\"Pelco\": "
                                             "{\"propiedades\": {\"hue\": 0.04}}}"}}},
                           {"modo", {{"type", "string"}, {"enum", {"reemplazar", "agregar"}}}},
                           {"tarjetas",
                            {{"type", "array"},
                             {"items",
                              {{"type", "object"},
                               {"properties",
                                {{"tipo",
                                  {{"type", "string"},
                                   {"description", "color, croma, recorte, obs, gst, plugin (+ `plugin`, "
                                                   "`tarjeta`, `parametros`), retardo, normalizar…"}}},
                                 {"plugin", {{"type", "string"}}},
                                 {"tarjeta", {{"type", "string"}}},
                                 {"parametros", {{"type", "object"}}},
                                 {"elemento", {{"type", "string"}}},
                                 {"filtro", {{"type", "string"}}},
                                 {"propiedades", {{"type", "object"}}},
                                 {"ajustes", {{"type", "object"}}},
                                 {"params",
                                  {{"type", "object"},
                                   {"description", "los de una tarjeta propia (color, croma, retardo…)"}}},
                                 {"compartida", {{"type", "string"}}},
                                 {"local", {{"type", "object"}}},
                                 {"etiqueta", {{"type", "string"}}}}},
                               {"required", {"tipo"}}}}}}}}};
        t.examples = {
            R"({"entrada": "Sony", "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.9}, "compartida": "Corrección cámaras"}]})",
            R"({"entrada": "Pelco", "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "compartida": "Corrección cámaras", "local": {"propiedades": {"hue": 0.04}}}]})",
            R"({"entrada": "Panasonic", "tarjetas": [{"tipo": "normalizar", "params": {"desentrelazar": "yadif"}}, {"tipo": "gst", "elemento": "videobalance", "compartida": "Corrección cámaras"}]})",
            R"({"entradas": ["Sony", "Pelco", "Panasonic"], "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.85}, "compartida": "Color cámaras"}], "local_por_entrada": {"Pelco": {"propiedades": {"hue": 0.04}}}})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::vector<std::string> entradas;
            for (const auto& x : a.value("entradas", json::array()))
                entradas.push_back(x.get<std::string>());
            if (a.contains("entrada")) entradas.insert(entradas.begin(), a.value("entrada", std::string()));
            if (entradas.empty()) return done(ToolResult::error("falta `entrada` (o `entradas`)"));
            for (const auto& e : entradas)
                if (!v->esEntrada(e))
                    return done(
                        ToolResult::error("«" + e + "» no es una entrada de video (video_estado las lista)"));
            const json tarjetas = a.value("tarjetas", json::array());
            if (!tarjetas.is_array() || tarjetas.empty()) return done(ToolResult::error("faltan `tarjetas`"));
            const json localPor = a.value("local_por_entrada", json::object());
            json hechas = json::array();
            // Todo en UNA edición: si algo falla en cualquier entrada, no cambia nada.
            const std::string err = v->editar([&](video::GrafoVideo& g) {
                for (const std::string& e : entradas) {
                    if (a.value("modo", std::string("reemplazar")) == "reemplazar") g.limpiar(e);
                    std::string despues;
                    int i = 0;
                    for (json tj : tarjetas) {
                        ++i;
                        tj = tarjetaDePlugin(tj);
                        // Lo propio de esta entrada, sobre la tarjeta compartida.
                        if (!tj.value("compartida", std::string()).empty() && localPor.contains(e))
                            tj["local"] = localPor[e];
                        const std::string tipo = tj.value("tipo", std::string());
                        const std::string donde =
                            "«" + e + "», tarjeta " + std::to_string(i) + " (" + tipo + "): ";
                        std::string error, id;
                        const std::string comp = tj.value("compartida", std::string());
                        std::string control;
                        if (!comp.empty())
                            for (const auto& n : g.nodos())
                                if (n.tipo == "control" && n.etiqueta == comp) control = n.id;
                        if (!control.empty()) {
                            id = g.agregarControlada(control, e, despues, &error);
                        } else if (tipo == "gst") {
                            id = v->agregarModuloEn(g, tj.value("elemento", std::string()), e, despues,
                                                    tj.value("propiedades", json::object()), &error);
                        } else if (tipo == "obs") {
                            // Los filtros de OBS se fueron con libobs.
                            error = "los filtros de OBS ya no existen (no hay libobs): usá una tarjeta `gst` "
                                    "(video_modulos), p. ej. videobalance o gamma";
                        } else {
                            id = g.agregar(tipo, e, despues, tj.value("params", json::object()), &error);
                        }
                        if (id.empty()) return donde + (error.empty() ? "no se pudo agregar" : error);
                        if (!comp.empty() && control.empty()) {
                            control = g.controlarDesde(id, &error);
                            if (control.empty()) return donde + error;
                            g.ajustar(control, json{{"etiqueta", comp}}, nullptr);
                            control = g.renombrar(control, comp); // id legible: estudio.<nombre>
                        }
                        if (tj.contains("local") && !g.ajustar(id, json{{"local", tj["local"]}}, &error))
                            return donde + error;
                        if (tj.contains("etiqueta"))
                            g.ajustar(id, json{{"etiqueta", tj["etiqueta"]}}, nullptr);
                        hechas.push_back({{"entrada", e},
                                          {"id", id},
                                          {"tipo", tipo},
                                          {"compartida", comp},
                                          {"controlador", control}});
                        if (tipo != "salida") despues = id;
                    }
                }
                return std::string();
            });
            if (!err.empty()) return done(ToolResult::error(err + " (no se cambió nada)"));
            json d{{"hechas", hechas}};
            json diag = json::object();
            for (const auto& e : entradas)
                diag[e] = video::diagnosticar(v->estadoEntrada(e));
            d["diagnostico"] = diag;
            if (entradas.size() == 1) d["grafo"] = v->describir(entradas[0]);
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_mosaico";
        t.title = "Mosaico, imagen en imagen o lado a lado de varias entradas (en una llamada)";
        t.category = "video";
        t.description =
            "Junta `entradas` en una `escena` (si no existe, la crea) en UNA llamada: crea el compositor (o "
            "rearma el que se llame `nombre`), "
            "conecta lo que llega a la escena de cada entrada (o sea, con sus tarjetas de color, etc.), lo "
            "pone en la "
            "escena y, con `ocultar_sueltas` (de fábrica, sí), oculta en esa escena las entradas sueltas (el "
            "mosaico "
            "las sigue recibiendo: no se apagan). `disposicion`: mosaico (grilla; con 3, una arriba al "
            "centro y dos abajo, sin hueco), pip (la primera de fondo y las "
            "demás chicas arriba a la derecha) o lado (una al lado de la otra). Para retocar después: "
            "video_tarjeta_ajustar sobre el compositor (`capas`).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"escena", "entradas"}},
            {"properties",
             {{"escena", {{"type", "string"}}},
              {"entradas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"disposicion", {{"type", "string"}, {"enum", {"mosaico", "pip", "lado"}}}},
              {"nombre",
               {{"type", "string"},
                {"description", "el de la fuente del mosaico (de fábrica, «Estudio · compositor»)"}}},
              {"ocultar_sueltas", {{"type", "boolean"}}}}}};
        t.examples = {
            R"({"escena": "Exterior", "entradas": ["Cámara 1", "SRT 1", "SRT 2"], "nombre": "Mosaico Exterior"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::vector<std::string> es;
            for (const auto& x : a.value("entradas", json::array()))
                es.push_back(x.get<std::string>());
            json hecho;
            const std::string err = v->armarMosaico(
                a.value("escena", std::string()), es, a.value("disposicion", std::string("mosaico")),
                a.value("nombre", std::string()), a.value("ocultar_sueltas", true), &hecho);
            if (!err.empty()) return done(ToolResult::error(err));
            hecho["verificar"] = "imagen_analizar sobre «" + hecho.value("fuente", std::string()) +
                                 "» con una pregunta concreta (¿algo estirado o en negro?; el primer cuadro "
                                 "puede tardar un instante)";
            done(ToolResult().data(hecho));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_ordenar";
        t.title = "Acomodar las tarjetas del grafo de video";
        t.category = "video";
        t.description =
            "Acomoda las tarjetas del grafo (una franja por entrada, en columnas según la cadena; el botón "
            "«Ordenar» de la página Video). No cambia ninguna conexión.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [vg](const json&, Done done) {
            const QString e = vg->ordenar();
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().text("Tarjetas acomodadas."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "motor_mapa";
        t.title = "Dónde pasa el video (y el audio) de la GPU a la CPU, pipeline por pipeline";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "El mapa vivo de TODOS los pipelines del motor (entradas, placas, composiciones, mezclas, vistas, "
            "salidas): cada enlace con la memoria negociada de verdad (GLMemory, D3D11Memory, VAMemory, "
            "CUDAMemory… o «sistema» = CPU), las TRANSICIONES (dónde se baja de la GPU a la CPU o se sube) y "
            "el TRABAJO EN CPU (convertir, escalar, componer, filtrar video en memoria de sistema; "
            "decodificar o codificar sin hardware). Sin `detalle`: por pipeline, qué lee y escribe, sus "
            "trabajos en CPU y sus transiciones. Con `detalle`: también cada elemento y cada enlace.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"detalle", {{"type", "boolean"}, {"description", "cada elemento y cada enlace"}}},
                           {"filtro", {{"type", "string"}, {"description", "solo los pipelines cuya etiqueta lo contiene"}}}}}};
        t.run = [](const json& a, Done done) {
            json m = motor::mapa();
            const bool detalle = a.value("detalle", false);
            const std::string filtro = a.value("filtro", std::string());
            json ps = json::array();
            for (auto& p : m["pipelines"]) {
                if (!filtro.empty() && p.value("etiqueta", std::string()).find(filtro) == std::string::npos) continue;
                if (!detalle) {
                    // La cadena en una línea: fábrica[memoria] de lo que sale de cada elemento.
                    std::string cadena;
                    for (const auto& en : p["enlaces"]) {
                        const std::string med = en.value("medio", std::string());
                        if (med.rfind("video/x-raw", 0) != 0) continue;
                        cadena += (cadena.empty() ? "" : " → ") + en.value("desde", std::string()) + "[" +
                                  en.value("memoria", std::string()) + " " + en.value("formato", std::string()) + "]";
                    }
                    p.erase("elementos");
                    p.erase("enlaces");
                    if (!cadena.empty()) p["video"] = cadena;
                }
                ps.push_back(p);
            }
            m["pipelines"] = ps;
            done(ToolResult().data(m));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_estado";
        t.title = "Las entradas de video y cómo está cada una (y qué le pasa)";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "Sin `entrada`: LISTA todas las entradas de video (cámaras, red, virtuales) con su diagnóstico "
            "resumido. Con `entrada`: tamaño, cuadros por segundo "
            "(declarados y medidos), formato, colorimetría (BT.601/709, rango), si llega entrelazada y su "
            "desentrelazado, el reloj y si está sincronizado, el colchón de red (latencia, cuadros que "
            "llegaron "
            "tarde), la latencia que agrega su sub-grafo, y los errores y avisos de sus tarjetas. "
            "`diagnostico`: "
            "cada problema con su gravedad y el ARREGLO (herramienta y argumentos). Usala antes y después de "
            "armar.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"entrada", entrada}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            v->sincronizar();
            const std::string e = a.value("entrada", std::string());
            if (!e.empty()) {
                json st = v->estadoEntrada(e);
                if (st.contains("error") && !st.contains("tipo"))
                    return done(ToolResult::error(st.value("error", std::string())));
                st["diagnostico"] = video::diagnosticar(st);
                return done(ToolResult().data(st));
            }
            json out = json::array();
            for (const QString& n : v->entradas()) {
                const json st = v->estadoEntrada(n.toStdString());
                json r{{"entrada", n.toStdString()},
                       {"tipo", st.value("tipo", std::string())},
                       {"tamaño",
                        std::to_string(st.value("ancho", 0)) + "x" + std::to_string(st.value("alto", 0))},
                       {"latencia_grafo_ms", st.value("latencia_grafo_ms", 0.0)},
                       {"diagnostico", video::diagnosticar(st)}};
                if (st.contains("gstreamer")) {
                    const json& g = st["gstreamer"];
                    r["red"] = {{"reloj", g.value("reloj", std::string())},
                                {"latencia_ms", g.value("latencia_ms", 0)},
                                {"desfase_ms", g.value("desfase_ms", 0)},
                                {"fps", std::round(g.value("fps_recibidos", 0.0))},
                                {"normalizada", g.value("normalizado", false)},
                                {"tarde_10s", g.value("tarde_10s", 0)}};
                    // Cómo llega (antes de normalizar), en una línea.
                    const json o = g.value("origen", json::object());
                    if (!o.empty())
                        r["llega"] = std::to_string(o.value("ancho", 0)) + "x" +
                                     std::to_string(o.value("alto", 0)) + " " +
                                     o.value("formato", std::string()) + ", " +
                                     o.value("matriz", std::string()) + " rango " +
                                     o.value("rango", std::string()) + ", " +
                                     (o.value("fps_variable", false)
                                          ? std::string("fps variables")
                                          : std::to_string(std::lround(o.value("fps", 0.0))) + " fps") +
                                     (o.value("entrelazado", false) ? ", entrelazada" : "") +
                                     (o.value("pixel", std::string("cuadrado")) != "cuadrado"
                                          ? ", píxel " + o.value("pixel", std::string())
                                          : "");
                    r["entra"] =
                        g.value("formato", std::string()) + ", " + g.value("colorimetria", std::string());
                    if (!g.value("superres_motor", std::string()).empty()) {
                        json sr{{"motor", g.value("superres_motor", std::string())},
                                {"ms_por_cuadro", std::round(g.value("superres_ms", 0.0) * 10) / 10}};
                        if (!g.value("superres_modelo", std::string()).empty())
                            sr["modelo"] = g.value("superres_modelo", std::string());
                        if (!g.value("superres_proveedor", std::string()).empty())
                            sr["acelera"] = g.value("superres_proveedor", std::string());
                        if (!g.value("superres_error", std::string()).empty())
                            sr["error"] = g.value("superres_error", std::string());
                        r["superres"] = sr;
                    }
                }
                out.push_back(r);
            }
            done(ToolResult().data({{"entradas", out}, {"reloj_estudio", v->reloj()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_entrada_red";
        t.title = "Recibir un stream de red (SRT, RTMP, RTSP, UDP, HTTP) sincronizado";
        t.category = "video";
        t.description = "Crea una entrada de video (y audio) desde `uri`: srt://host:puerto (o "
                        "srt://:puerto?mode=listener para "
                        "recibir), rtmp://servidor/app/clave, rtsp://camara/stream, udp://@:5000, "
                        "http(s)://…/x.m3u8 o un "
                        "archivo. Usa el reloj del estudio (video_reloj) y un COLCHÓN de `latencia_ms` (de "
                        "fábrica, el del "
                        "estudio; la `escena` se crea si no existe): cada cuadro sale a su hora de captura + "
                        "latencia, así dos streams con la misma latencia "
                        "salen juntos. RTSP trae la hora de captura del que manda (RTCP/NTP): se alinea "
                        "solo. SRT y RTMP no la "
                        "traen: el desfase entre ellos se mide con video_sincronia. Sobre una entrada de red "
                        "que ya existe, la "
                        "actualiza (otra uri o latencia).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"nombre"}},
            {"properties",
             {{"nombre", {{"type", "string"}}},
              {"uri", {{"type", "string"}}},
              {"escena", {{"type", "string"}, {"description", "dónde ponerla (obligatoria al crear)"}}},
              {"latencia_ms",
               {{"type", "integer"}, {"description", "el colchón (de fábrica, el del estudio)"}}},
              {"audio", {{"type", "boolean"}, {"description", "con su audio (de fábrica, sí)"}}},
              {"normalizar",
               {{"type", "boolean"},
                {"description",
                 "de fábrica, sí: GStreamer la desentrelaza si hace falta, la lleva a los cuadros "
                 "del estudio, a píxel cuadrado y a color BT.709 antes de entrar"}}},
              {"superres",
               {{"type", "boolean"},
                {"description",
                 "superresolución x2 (macOS 26, cámaras chicas: hasta el tamaño máximo que acepte el sistema, "
                 "hoy 960x960; sale al doble de lo que entra). Para cámaras viejas o de baja definición"}}},
              {"ntp_sync",
               {{"type", "boolean"},
                {"description",
                 "RTSP: usar la hora de captura que manda la cámara (de fábrica, sí). Si su reloj "
                 "está corrido (video_estado lo avisa), false"}}},
              {"sincronia",
               {{"type", "string"},
                {"enum", {"colchon", "gstreamer"}},
                {"description",
                 "cómo sale a horario (de fábrica, colchon): colchon = cola propia con la hora de "
                 "captura, el colchón y la compensación del estudio; gstreamer = appsink sincronizado "
                 "al reloj con la latencia del estudio (gst_pipeline_set_latency: lo que declaran los "
                 "elementos o lo medido al llegar, lo mayor). Audio y video siguen juntos en los dos"}}},
              {"esperar_seg",
               {{"type", "integer"},
                {"description",
                 "cuánto esperar a que conecte antes de devolver el estado con su diagnóstico (4 de "
                 "fábrica; 0: no esperar)"}}}}}};
        t.examples = {
            R"({"nombre": "Móvil SRT", "uri": "srt://:9000?mode=listener", "escena": "Móvil", "latencia_ms": 300})",
            R"({"nombre": "Corresponsal", "uri": "rtmp://10.0.0.20/live/corresponsal", "escena": "Móvil"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string n = a.value("nombre", std::string());
            const std::string err = v->crearEntradaRed(
                n, a.value("uri", std::string()), a.value("latencia_ms", -1),
                a.value("escena", std::string()), a.contains("audio") ? (a.value("audio", true) ? 1 : 0) : -1,
                a.contains("normalizar") ? (a.value("normalizar", true) ? 1 : 0) : -1,
                a.contains("ntp_sync") ? (a.value("ntp_sync", true) ? 1 : 0) : -1,
                a.contains("superres") ? (a.value("superres", false) ? 1 : 0) : -1,
                a.value("sincronia", std::string()));
            if (!err.empty()) return done(ToolResult::error(err));
            esperarEntrada(v, n, a.value("esperar_seg", 4) * 1000,
                           [done](json st) { done(ToolResult().data(st)); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "entrada_control";
        t.title = "Mandar un evento de control a la cadena de GStreamer de una entrada";
        t.category = "video";
        t.description =
            "El canal de control de las entradas de GStreamer: `control` (un objeto plano: texto, números, sí/no) "
            "viaja por la cadena de la entrada como evento estándar de GStreamer «ba/control» (personalizado, hacia "
            "abajo, fuera de banda): cada elemento que lo entiende lo usa y todos lo dejan pasar; la entrada lo "
            "recuerda y lo reenvía si la cadena se rearma. Hoy lo entiende la superresolución: {\"superres\": "
            "\"activo\"|\"reposo\"|\"barato\", \"transicion_ms\": 400} (lo usa el programador; a mano, con el "
            "programador apagado). Se suma a lo anterior: campos nuevos para efectos o eventos futuros.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "control"}},
                         {"properties", {{"entrada", {{"type", "string"}}}, {"control", {{"type", "object"}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "control": {"superres": "activo", "transicion_ms": 300}})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = v->controlEntrada(a.value("entrada", std::string()), a.value("control", json::object()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text("Mandado por la cadena de la entrada."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "superres_programador";
        t.title = "Programador de superresolución: qué cámaras usan la red según programa y preview";
        t.category = "video";
        t.description =
            "Con varias cámaras, las redes de todas quedan cargadas y calientes pero solo trabajan las que se ven "
            "grandes: el programador mira el programa y el preview (la escena armada) y pone cada cámara en activo "
            "(hasta `max_activas`, por prioridad: programa, preview, más grande), barato (se ve grande pero no tiene "
            "turno: agrandado afilado) o reposo (no se ve o se ve chica). Se prende al pasar a preview, con una mezcla "
            "gradual de `transicion_ms`: la mejora entra sin saltos ni cuadros negros. La CAPACIDAD se mide sola "
            "(`capacidad` auto): cada acelerador (Neural Engine, GPU, CPU, CUDA, DirectML…) corre de a una red por vez, "
            "así que entran las redes cuyo tiempo medido sume hasta `uso_pct` % de un cuadro en su acelerador "
            "(`max_activas` es el tope); con `capacidad` fija, exactamente `max_activas`. `en_preview` false: solo lo "
            "que está al aire. `sin_turno`: barato (afilado) o reposo. `umbral_aumento`: desde cuánto agrandada en "
            "pantalla conviene la red (1.1 = más grande que la cámara). `forzar`: {entrada: siempre|nunca|auto}. "
            "Sin argumentos devuelve la configuración, el estado de cada cámara (modo, ms, acelerador) y el "
            "presupuesto usado de cada acelerador; con `activo` false, todas las redes trabajan siempre. "
            "Si una red no llega (tarda más del `umbral_pct` % del cuadro, 80 de fábrica, durante 2 s) suena la "
            "alarma (disparador superres_tarde, evento superres/tarde) y, con `bypass_auto` (de fábrica, sí), pasa "
            "sola a barato y vuelve a probar a los 15 s, 30 s, 1 min, 2 min; cada entrada dice `tarde` y `bypass_s`.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"capacidad", {{"type", "string"}, {"enum", {"auto", "fija"}}}},
                           {"max_activas", {{"type", "integer"}}},
                           {"uso_pct", {{"type", "number"}}},
                           {"transicion_ms", {{"type", "integer"}}},
                           {"en_preview", {{"type", "boolean"}}},
                           {"sin_turno", {{"type", "string"}, {"enum", {"barato", "reposo"}}}},
                           {"umbral_aumento", {{"type", "number"}}},
                           {"forzar", {{"type", "object"}}},
                           {"bypass_auto", {{"type", "boolean"}}},
                           {"umbral_pct", {{"type", "number"}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            if (!a.empty()) {
                const std::string e = v->configurarProgramador(a);
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data(v->programador()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "entrada_filtros_audio";
        t.title = "Filtros de audio de una entrada (en su cadena de GStreamer)";
        t.category = "video";
        t.description =
            "Pone (o saca, con una lista vacía) los filtros de AUDIO de una entrada de GStreamer (red, pipeline, "
            "plugin): elementos de GStreamer en la cadena de su audio, en orden. Para un plugin de Python de "
            "audio: {\"elemento\": \"baplugin\", \"propiedades\": {\"plugin\": id, \"tarjeta\": nombre, "
            "\"parametros\": \"{...}\"}}; también cualquier elemento de audio (audiodynamic, audioecho, "
            "audiocheblimit, volume…; gst-inspect los lista con video_modulos). La demora que agreguen se "
            "compensa sola: el colchón de la entrada atrasa el video lo mismo. Sin `filtros`, devuelve los "
            "que tiene.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada"}},
                         {"properties",
                          {{"entrada", {{"type", "string"}}},
                           {"filtros",
                            {{"type", "array"},
                             {"items",
                              {{"type", "object"},
                               {"properties",
                                {{"elemento", {{"type", "string"}}}, {"propiedades", {{"type", "object"}}}}}}}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "filtros": [{"elemento": "audiodynamic", "propiedades": {"mode": "compressor", "ratio": 0.5, "threshold": 0.3}}]})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            if (!a.contains("filtros")) return done(ToolResult().data({{"entrada", e}, {"filtros", v->filtrosAudioDe(e)}}));
            const std::string err = v->filtrosAudio(e, a["filtros"]);
            if (!err.empty()) return done(ToolResult::error(err));
            done(ToolResult().data({{"entrada", e}, {"filtros", v->filtrosAudioDe(e)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "estudio_latencia";
        t.title = "Latencia del estudio (y su tope)";
        t.category = "video";
        t.description =
            "Cuánto atrasa el estudio para alinear las entradas entre sí (la mayor compensación que piden, hasta el "
            "tope), el tope y qué entradas LLEGAN TARDE (piden más que el tope, p. ej. una cámara con "
            "superresolución pesada: van con su propia demora en vez de atrasar todo). Con `tope_ms` lo cambia "
            "(-1 = automático: 250 ms, deja afuera solo lo pesado como la superresolución); se guarda.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"tope_ms", {{"type", "integer"}, {"minimum", -1}, {"maximum", 5000}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            done(ToolResult().data(v->estudioLatencia(a.contains("tope_ms") ? a.value("tope_ms", -1) : -2)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "automixer";
        t.title = "Automixer de mesa redonda (micrófonos de las bancas)";
        t.category = "audio";
        t.description =
            "Automixer estilo Dugan para una mesa de micrófonos: el que habla pasa al frente, los demás bajan y "
            "la suma queda acotada (no se suman ruido ni retorno al abrir muchos). Corre en GStreamer, en la "
            "cadena de cada micrófono (`baautomixer`). Sin argumentos: las mesas, sus ajustes y, por "
            "micrófono, la ganancia que aplica ahora y el nivel que entra. Con `microfono` y `grupo`: lo pone "
            "en esa mesa (grupo \"\" lo saca). Con `grupo` y `ajustes`: cambia los de la mesa (umbral_db: "
            "desde qué nivel cuenta como abierto; hold_ms y piso_hold_db: cuánto y a cuánto sostiene al que "
            "acaba de hablar; max_abiertos; ataque_ms; liberacion_ms).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"microfono", {{"type", "string"}, {"description", "nombre de la entrada de audio"}}},
                           {"microfonos", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "varios a la vez"}}},
                           {"grupo", {{"type", "string"}, {"description", "la mesa (p. ej. bancas)"}}},
                           {"ajustes", {{"type", "object"}}}}}};
        t.examples = {R"({"microfonos": ["Banca 1", "Banca 2", "Banca 3"], "grupo": "bancas"})",
                      R"({"grupo": "bancas", "ajustes": {"umbral_db": -45, "max_abiertos": 3}})", R"({})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::vector<std::string> mics;
            if (a.contains("microfono") && a["microfono"].is_string()) mics.push_back(a["microfono"]);
            for (const auto& m : a.value("microfonos", json::array()))
                if (m.is_string()) mics.push_back(m);
            const bool hayGrupo = a.contains("grupo") && a["grupo"].is_string();
            if (!mics.empty()) {
                if (!hayGrupo) return done(ToolResult::error("falta `grupo` (\"\" para sacarlo de la mesa)"));
                for (const auto& m : mics)
                    if (const std::string e = v->automixer(m, a["grupo"]); !e.empty()) return done(ToolResult::error(e));
            }
            if (a.contains("ajustes")) {
                if (!hayGrupo) return done(ToolResult::error("falta `grupo`"));
                if (const std::string e = v->automixerAjustes(a["grupo"], a["ajustes"]); !e.empty())
                    return done(ToolResult::error(e));
            }
            done(ToolResult().data({{"mesas", v->automixerEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    if (video::SuperRes* sr = vm_.superresVideo()) {
        const QPointer<video::SuperRes> srp = sr;
        {
            Tool t;
            t.name = "superres_estado";
            t.title = "Superresolución: motores, modelos y el trabajo en curso";
            t.category = "video";
            t.readOnly = true;
            t.description = "Qué motores de superresolución hay en este sistema (apple: VideoToolbox macOS "
                            "26, ~3 ms; fsr: FSR 1 de AMD, "
                            "cualquier GPU, sin red; onnx: una red ONNX), los modelos ONNX que hay (red "
                            "chica entrenada con una cámara, "
                            "SPAN, Real-ESRGAN: con su ganancia en dB si se midió), si el laboratorio está "
                            "listo y el trabajo en curso "
                            "(entrenando, convirtiendo…). Cada entrada de red dice su motor en video_estado. "
                            "Ayuda: help {\"topic\": \"superres\"}.";
            t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
            t.run = [srp](const json&, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                json mot = json::array();
                for (const QString& m : srp->motores())
                    mot.push_back(m.toStdString());
                const QVariantMap tr = srp->trabajo();
                json trabajo =
                    json::parse(QJsonDocument::fromVariant(tr).toJson(QJsonDocument::Compact).toStdString(),
                                nullptr, false);
                done(ToolResult().data({{"motores", mot},
                                        {"modelos", srp->modelos()},
                                        {"laboratorio_listo", srp->laboratorioListo()},
                                        {"trabajo", trabajo},
                                        {"carpeta", srp->carpetaModelos().string()}}));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_tarjeta";
            t.title = "Poner una tarjeta de superresolución (con un plugin) en una entrada";
            t.category = "video";
            t.description =
                "Una TARJETA del grafo de video de `entrada` (cámara, red o virtual) que agranda x2; lo que la "
                "sigue trabaja al doble y la escena la recibe grande. Lo de fábrica: `plugin`, el id de un plugin "
                "de superresolución instalado (superres_plugins: apple, fsr, realesr-general-x4v3, la red propia…): "
                "la app lo corre con lo mejor de ESTA máquina (CoreML en la Mac, ONNX con DirectML o TensorRT en "
                "Windows/Linux). Si la entrada ya tiene la tarjeta, le cambia el plugin. A mano (sin plugin): "
                "`motor` apple | fsr | onnx | coreml con `modelo` (ruta). `despues`: tras qué tarjeta (vacío: al final). "
                "`unidades` (redes CoreML en la Mac): auto (Neural Engine), ane, gpu o cpu: con dos redes a la vez, una en "
                "la GPU reparte la carga (la GPU también compone la imagen, así que suele ser más lenta). "
                "`proveedor` (redes ONNX en Windows/Linux): auto, dml (DirectML), vulkan (ncnn: AMD, Intel y NVIDIA; "
                "pasa la red a ncnn la primera vez), tensorrt, cuda, openvino, migraphx o cpu.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada"}},
                             {"properties",
                              {{"entrada", {{"type", "string"}}},
                               {"plugin", {{"type", "string"}, {"description", "id de superres_plugins"}}},
                               {"motor", {{"type", "string"}, {"enum", {"apple", "fsr", "onnx", "coreml"}}}},
                               {"modelo", {{"type", "string"}}},
                               {"unidades", {{"type", "string"}, {"enum", {"auto", "ane", "gpu", "cpu"}}}},
                               {"proveedor", {{"type", "string"},
                                              {"enum", {"auto", "dml", "vulkan", "tensorrt", "cuda", "openvino", "migraphx", "cpu"}}}},
                               {"despues", {{"type", "string"}}}}}};
            t.examples = {R"({"entrada": "Cámara 1", "plugin": "realesr-general-x4v3"})"};
            t.run = [srp, v = vm_.videoGrafos()](const json& a, Done done) {
                if (!srp || v == nullptr) return done(ToolResult::error("no disponible"));
                const std::string entrada = a.value("entrada", std::string());
                if (a.contains("plugin")) {
                    json extra = json::object();
                    if (a.contains("unidades")) extra["unidades"] = a.value("unidades", std::string("auto"));
                    if (a.contains("proveedor")) extra["proveedor"] = a.value("proveedor", std::string("auto"));
                    const std::string e = srp->tarjetaConPlugin(entrada, a.value("plugin", std::string()), extra);
                    if (!e.empty()) return done(ToolResult::error(e));
                    return done(ToolResult().text("Tarjeta con el plugin «" + a.value("plugin", std::string()) +
                                                  "». video_estado muestra el tamaño nuevo y su demora (el audio se "
                                                  "atrasa solo) en unos segundos; la entrada tiene que estar a la vista."));
                }
                if (!a.contains("motor")) return done(ToolResult::error("falta `plugin` (o `motor`)"));
                std::string modelo = a.value("modelo", std::string());
                if (!modelo.empty() && modelo.find('/') == std::string::npos &&
                    modelo.find('\\') == std::string::npos)
                    modelo = (srp->carpetaModelos() / modelo).string();
                const std::string e =
                    v->agregarSuperres(entrada, a.value("motor", std::string()), modelo, a.value("despues", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Tarjeta agregada. video_estado muestra el tamaño nuevo en unos segundos."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_plugins";
            t.title = "Plugins de superresolución: cuáles hay, cuáles están instalados y cuáles sirven acá";
            t.category = "video";
            t.readOnly = true;
            t.description =
                "Los plugins de superresolución: los del catálogo (redes como Real-ESRGAN, LSDIR, NomosUni, "
                "SPAN; shaders como FSRCNNX y Anime4K; los integrados apple y fsr) y los instalados que no están en "
                "él (la red propia entrenada con una cámara). Cada uno: qué hace, escala, plataformas, `instalado`, "
                "`aca` (si corre en esta máquina) y `ms_coreml` (lo que tarda medido). Se instalan con "
                "superres_instalar y se ponen con superres_tarjeta {plugin}.";
            t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
            t.run = [srp](const json&, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                done(ToolResult().data({{"plugins", srp->plugins()}}));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_plugin_elemento";
            t.title = "Crear un plugin de superresolución propio con cualquier elemento de GStreamer";
            t.category = "video";
            t.description =
                "Crea (o reemplaza) un plugin de tipo «elemento» (contrato abierto, docs/funciones/SUPERRESOLUCION.md): cualquier "
                "elemento de GStreamer —de GStreamer o propio, con su biblioteca en Datos/Modelos/superres/<id>/gst/— "
                "que después se elige como cualquier plugin (superres_tarjeta {plugin: id}). `memoria`: vacía "
                "(sistema), CUDAMemory, GLMemory, D3D11Memory o D3D12Memory: la cadena de la entrada sube a esa memoria "
                "antes y baja después, sin pasar por la CPU si el decodificador ya entrega ahí. `memoria_salida` \"\" "
                "si el elemento ya entrega en memoria de sistema. `propiedades`: las del elemento ({dir} = la carpeta "
                "del plugin). `escala`: lo que agranda (2).";
            t.inputSchema = {{"type", "object"},
                             {"required", {"id", "elemento"}},
                             {"properties",
                              {{"id", {{"type", "string"}}},
                               {"nombre", {{"type", "string"}}},
                               {"elemento", {{"type", "string"}}},
                               {"memoria", {{"type", "string"}, {"enum", {"", "CUDAMemory", "GLMemory", "D3D11Memory", "D3D12Memory"}}}},
                               {"memoria_salida", {{"type", "string"}}},
                               {"propiedades", {{"type", "object"}}},
                               {"escala", {{"type", "integer"}}},
                               {"descripcion", {{"type", "string"}}}}}};
            t.examples = {R"({"id": "fsrcnnx-gl", "nombre": "FSRCNNX en GL", "elemento": "baglsl", "memoria": "GLMemory", "propiedades": {"shader": "{dir}/shader.glsl"}})"};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->guardarPluginElemento(a);
                if (!e.empty() && e.rfind("guardado", 0) != 0) return done(ToolResult::error(e));
                done(ToolResult().text(e.empty() ? "Plugin guardado: se elige con superres_tarjeta {plugin}." : e));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_plugin_borrar";
            t.title = "Borrar un plugin de superresolución instalado";
            t.category = "video";
            t.description = "Borra la carpeta de un plugin instalado (`id`, de superres_plugins): una red instalada, un "
                            "shader o un plugin propio. Los del catálogo se pueden volver a instalar.";
            t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->borrarPlugin(a.value("id", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Borrado."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_variantes";
            t.title = "Ver las variantes de las redes (compiladas para un tamaño fijo)";
            t.category = "video";
            t.description =
                "Las variantes de una red de superresolución: la misma red compilada para un tamaño de entrada fijo "
                "(CoreML para el Neural Engine; ONNX fijo para TensorRT/DirectML). Cada una con su variante.json: "
                "qué es, `para` qué cámara, `entrada`/`red`/`salida` (tamaños), `archivos`, `medido` (ms y "
                "acelerador), cuándo y con qué se hizo. El motor usa sola la del tamaño que le llega; si no hay, la "
                "general. `faltan`: los tamaños de las cámaras del estudio sin variante de ese plugin.";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"plugin", {{"type", "string"}, {"description", "id del plugin (vacío: todos)"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string p = a.value("plugin", std::string());
                json r{{"variantes", srp->variantes(p)}};
                if (!p.empty()) r["faltan"] = srp->tamanosSinVariante(p);
                done(ToolResult().data(r));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_variante_crear";
            t.title = "Compilar una red de superresolución para un tamaño de entrada";
            t.category = "video";
            t.description =
                "Compila la red de `plugin` para un tamaño de entrada fijo (`ancho` x `alto`, el de la cámara tal "
                "como llega a la tarjeta), o para varios (`tamanos`: [\"1024x576\", …]), o para todas las cámaras "
                "del estudio que todavía no tienen la suya (`faltantes`: true). Sin PyTorch (coremltools y onnx "
                "vienen en el programa): en la Mac CoreML (el Neural Engine solo corre tamaños compilados) y en "
                "todos un ONNX con la forma fija. Es un trabajo: superres_estado muestra cómo va. Al terminar, las "
                "tarjetas que usan ese plugin toman la variante del tamaño que les llega. Un shader GLSL no lleva "
                "variantes: la placa lo compila para cualquier tamaño.";
            t.details =
                "`para` y `descripcion` quedan en variante.json (para qué cámara o uso es). `motores`: \"auto\" (todo "
                "lo que tenga el plugin), o \"coreml\", \"onnx\", \"onnx_nv12\" separados por coma. Un plugin "
                "instalado antes de esta versión no guardó el paquete CoreML: avisa, y hay que reinstalarlo "
                "(superres_instalar) para hacer variantes CoreML; las ONNX salen igual. `reemplazar`: rehacer una "
                "que ya existe. Un ancho o alto impar se redondea para abajo.";
            t.inputSchema = {
                {"type", "object"},
                {"required", {"plugin"}},
                {"properties",
                 {{"plugin", {{"type", "string"}}},
                  {"ancho", {{"type", "integer"}}},
                  {"alto", {{"type", "integer"}}},
                  {"tamanos", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                  {"faltantes", {{"type", "boolean"}, {"description", "las cámaras del estudio sin variante"}}},
                  {"entrada", {{"type", "string"}, {"description", "el tamaño de esta entrada (como llega)"}}},
                  {"id", {{"type", "string"}, {"description", "de fábrica, «<ancho>x<alto>»"}}},
                  {"motores", {{"type", "string"}}},
                  {"para", {{"type", "string"}}},
                  {"descripcion", {{"type", "string"}}},
                  {"reemplazar", {{"type", "boolean"}}}}}};
            t.examples = {R"({"plugin": "lsdir-compact-x4", "faltantes": true})",
                          R"({"plugin": "nomosuni-compact-x2", "ancho": 1024, "alto": 576, "para": "cámara del patio"})"};
            t.run = [srp, v](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string p = a.value("plugin", std::string());
                std::vector<std::string> tam;
                const auto par = [](int w, int h) { return std::to_string(w - w % 2) + "x" + std::to_string(h - h % 2); };
                if (a.value("faltantes", false)) tam = srp->tamanosSinVariante(p);
                for (const auto& x : a.value("tamanos", json::array()))
                    if (x.is_string()) tam.push_back(x.get<std::string>());
                if (a.contains("ancho") && a.contains("alto")) tam.push_back(par(a.value("ancho", 0), a.value("alto", 0)));
                if (const std::string e = a.value("entrada", std::string()); !e.empty() && v) {
                    const json o = v->estadoEntrada(e).value("gstreamer", json::object()).value("origen", json::object());
                    if (o.value("ancho", 0) <= 0) return done(ToolResult::error("«" + e + "» todavía no tiene imagen"));
                    tam.push_back(par(o.value("ancho", 0), o.value("alto", 0)));
                }
                if (tam.empty())
                    return done(ToolResult::error(a.value("faltantes", false)
                                                      ? "todas las cámaras del estudio ya tienen su variante"
                                                      : "falta el tamaño: ancho y alto, tamanos, entrada o faltantes"));
                const std::string e = srp->crearVariantes(p, tam, a.value("id", std::string()),
                                                          a.value("motores", std::string()), a.value("para", std::string()),
                                                          a.value("descripcion", std::string()), a.value("reemplazar", false));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().data(json{{"trabajo", "compilando"}, {"tamanos", tam},
                                            {"siguiente", "superres_estado para ver cómo va; después superres_variantes"}}));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_variante_editar";
            t.title = "Cambiar para qué es una variante";
            t.category = "video";
            t.description = "Cambia `para`, `descripcion` o `nombre` del variante.json de una variante (`plugin`, `id`). "
                            "Lo demás (tamaños, archivos, lo medido) lo escribe la herramienta.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"plugin", "id"}},
                             {"properties",
                              {{"plugin", {{"type", "string"}}},
                               {"id", {{"type", "string"}}},
                               {"para", {{"type", "string"}}},
                               {"descripcion", {{"type", "string"}}},
                               {"nombre", {{"type", "string"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->editarVariante(a.value("plugin", std::string()), a.value("id", std::string()), a);
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Guardado."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_variante_borrar";
            t.title = "Borrar una variante de una red";
            t.category = "video";
            t.description = "Borra una variante (`plugin`, `id`): las tarjetas vuelven a la red general para ese tamaño.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"plugin", "id"}},
                             {"properties", {{"plugin", {{"type", "string"}}}, {"id", {{"type", "string"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->borrarVariante(a.value("plugin", std::string()), a.value("id", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Borrada."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_destilar";
            t.title = "Destilar una red grande en una propia chica, a medida de una cámara";
            t.category = "video";
            t.description =
                "Entrena una red propia chica (luma x2: corre en vivo hasta en la CPU) que IMITA a una red grande del "
                "catálogo (`maestro`: realesr-general-x4v3 de fábrica, lsdir-compact-x4…) en lo que ve la entrada "
                "`entrada` (de red o cámara local): graba 60 s, el maestro procesa cada cuadro y la chica aprende a llegar a eso. Para "
                "máquinas donde el maestro no da en vivo. `minutos` de entrenamiento; `cpu` true para no usar la GPU "
                "(más lento, no calienta); `aplicar` la pone en la entrada al terminar. Queda como plugin "
                "«destilada-<entrada>» (superres_plugins). El avance, en superres_estado.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada"}},
                             {"properties",
                              {{"entrada", {{"type", "string"}}},
                               {"maestro", {{"type", "string"}}},
                               {"minutos", {{"type", "number"}}},
                               {"cpu", {{"type", "boolean"}}},
                               {"aplicar", {{"type", "boolean"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->destilar(a.value("entrada", std::string()), a.value("maestro", std::string()),
                                                    a.value("minutos", 10.0), a.value("aplicar", false),
                                                    a.value("cpu", false));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Destilando: superres_estado muestra el avance."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_runtime_gpu";
            t.title = "El ONNX Runtime de GPU de esta máquina (NVIDIA, AMD, Intel)";
            t.category = "video";
            t.readOnly = true;
            t.description =
                "Qué placa de video hay, si está bajado el ONNX Runtime de GPU (NVIDIA: CUDA + TensorRT, que "
                "compila la red para esa placa; AMD en Linux: ROCm/MIGraphX; Intel: OpenVINO) y qué conviene. "
                "En Windows con AMD (también las integradas Ryzen) y en la Mac no hace falta: DirectML y CoreML "
                "vienen con el programa. Se baja con superres_runtime_gpu_instalar.";
            t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
            t.run = [srp](const json&, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                done(ToolResult().data(srp->runtimeGpu()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_banco";
            t.title = "Medir qué superresolución puede usar esta máquina";
            t.category = "video";
            t.description =
                "El banco de pruebas: mide cada superresolución disponible (FSR, Apple, los shaders y cada red "
                "instalada con cada acelerador: Neural Engine y GPU en la Mac; DirectML, CUDA/TensorRT, ROCm, "
                "OpenVINO, Vulkan y el procesador en Windows/Linux) a 540p, 720p, 1080p y al tamaño de cada cámara, y "
                "dice cuál corre en vivo, cuál a menos cuadros y cuál solo sirve para lo grabado. `accion`: resultado "
                "(lo último medido, de fábrica), correr, estado o cancelar.";
            t.details =
                "Mide lo que tarda de verdad la red por cuadro (lo que mide el propio elemento; FSR y shaders, con "
                "sondas). Veredicto con los cuadros del lienzo: en_vivo (entra en el 85 % del cuadro, con cuántas a la "
                "vez), reducido (hasta 2,5 cuadros), diferido, no_corre. `recomendado`: lo mejor por tamaño (entre lo "
                "que va en vivo, una red le gana a FSR). Queda en Config/superres-banco.json y el programador lo usa "
                "para saber cuánto va a costar una red antes de prenderla. Tarda unos minutos y calienta la máquina; "
                "no arranca con algo al aire salvo `forzar`. Opciones: `resoluciones` [\"1280x720\"…], `metodos` "
                "[ids de plugin], `segundos` por prueba (4).";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"accion", {{"type", "string"}, {"enum", {"resultado", "correr", "estado", "cancelar"}}}},
                               {"resoluciones", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                               {"metodos", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                               {"segundos", {{"type", "number"}, {"minimum", 1}, {"maximum", 30}}},
                               {"forzar", {{"type", "boolean"}}}}}};
            t.examples = {R"({})", R"({"accion": "correr"})", R"({"accion": "correr", "resoluciones": ["1280x720"], "metodos": ["fsr"]})",
                          R"({"accion": "estado"})"};
            t.run = [this](const json& a, Done done) {
                auto* b = vm_.bancoSr();
                if (b == nullptr) return done(ToolResult::error("no disponible"));
                const std::string ac = a.value("accion", std::string("resultado"));
                if (ac == "correr") {
                    const std::string e = b->correr(a);
                    if (!e.empty()) return done(ToolResult::error(e));
                    json est = b->estado();
                    est.erase("parciales");
                    return done(ToolResult().text("Midiendo: superres_banco {accion: \"estado\"} muestra el avance.").data(est));
                }
                if (ac == "cancelar") {
                    b->cancelar();
                    return done(ToolResult::ok("Cancelado: lo medido hasta acá queda."));
                }
                if (ac == "estado") return done(ToolResult().data(b->estado()));
                const json r = b->resultado();
                if (r.empty()) return done(ToolResult::ok("Nunca se midió esta máquina: superres_banco {accion: \"correr\"}."));
                done(ToolResult().data(r));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_runtime_gpu_instalar";
            t.title = "Bajar el ONNX Runtime de GPU (NVIDIA / AMD Linux / Intel)";
            t.category = "video";
            t.description =
                "Baja a Datos (portable) el ONNX Runtime de GPU para `tipo` (nvidia, amd o intel; vacío = el que "
                "conviene para esta placa), con versiones fijas de PyPI verificadas por sha256. NVIDIA: varios GB "
                "(CUDA 13, cuDNN, TensorRT; `tensorrt` false para bajar solo CUDA). Al terminar hay que "
                "reiniciar el programa. El avance, en superres_estado (`trabajo`).";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"tipo", {{"type", "string"}, {"enum", {"", "nvidia", "amd", "intel"}}}},
                               {"tensorrt", {{"type", "boolean"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->instalarRuntimeGpu(a.value("tipo", std::string()), a.value("tensorrt", true));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Bajando: superres_estado muestra el avance; al terminar, reiniciar."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_instalar";
            t.title = "Instalar un plugin de superresolución del catálogo";
            t.category = "video";
            t.description =
                "Baja el plugin `id` (verificado por sha256) y lo prepara para esta máquina: una red se exporta a "
                "ONNX y, en la Mac, a CoreML para el Neural Engine (y se mide cuánto tarda); un shader se guarda. "
                "Corre en el laboratorio (la primera vez baja PyTorch): el avance, en superres_estado (`trabajo`).";
            t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->instalarPlugin(a.value("id", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Instalando: superres_estado muestra el avance."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_aplicar";
            t.title = "Superresolución EN LA FUENTE de una entrada de red (alternativa a la tarjeta)";
            t.category = "video";
            t.description = "(Forma vieja, en los ajustes de la fuente: para redes CoreML, shaders, plugins y la "
                            "elección de placa usá superres_tarjeta {plugin}.) "
                            "Duplica la resolución de una entrada de red (`entrada`) con `motor`: apple (la "
                            "de Apple, la mejor en una Mac con "
                            "macOS 26), fsr (FSR 1, cualquier GPU), onnx (con `modelo`: el archivo de "
                            "superres_estado, p. ej. "
                            "chica-Camara_1.onnx) o vacío para sacarla. Con un motor lento la entrada "
                            "pierde cuadros: mirá superres_ms "
                            "en video_estado (tiene que quedar bastante debajo de lo que dura un cuadro del lienzo: 1000/fps ms, 33 ms a 30 fps, 16 ms a 60).";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada", "motor"}},
                             {"properties",
                              {{"entrada", {{"type", "string"}}},
                               {"motor", {{"type", "string"}, {"enum", {"", "apple", "fsr", "onnx"}}}},
                               {"modelo", {{"type", "string"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e =
                    srp->aplicar(a.value("entrada", std::string()), a.value("motor", std::string()),
                                 a.value("modelo", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Hecho. En unos segundos video_estado muestra el tamaño nuevo y lo "
                                       "que tarda (superres_ms)."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_modelo";
            t.title = "Bajar y convertir un modelo de superresolución (SPAN o Real-ESRGAN)";
            t.category = "video";
            t.description =
                "Baja y convierte a ONNX `cual`: span (SPAN x2, Apache-2.0: liviano y fiel) o esrgan "
                "(Real-ESRGAN compacto x4, "
                "BSD-3: limpia ruido y compresión, pesado: en una Mac ~290 ms por cuadro, no da en vivo). La "
                "primera vez baja el "
                "laboratorio (PyTorch, ~800 MB). Corre en segundo plano: el avance en superres_estado.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"cual"}},
                             {"properties", {{"cual", {{"type", "string"}, {"enum", {"span", "esrgan"}}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                const std::string e = srp->convertir(a.value("cual", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Empezó (superres_estado muestra el avance)."));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "superres_entrenar";
            t.title = "Entrenar la red chica mirando una cámara (un botón)";
            t.category = "video";
            t.description = "Graba ~1 minuto de la entrada `entrada` sin tocar la imagen al aire: una de red, de su "
                            "dirección; una cámara local, cuadros de lo que sale de su grafo (4 por segundo). Arma pares «la "
                            "imagen achicada y comprimida -> la original» y entrena una red chica (solo "
                            "luma, x2) `minutos` minutos (5 de "
                            "fábrica) para deshacer lo que hace ESA cámara. Si ya había una red de esa "
                            "entrada, sigue desde ella. Con "
                            "`aplicar` (de fábrica, sí) la pone en la entrada al terminar. Dice cuánto "
                            "mejora contra el agrandado común "
                            "(dB). Corre en segundo plano: el avance en superres_estado.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada"}},
                             {"properties",
                              {{"entrada", {{"type", "string"}}},
                               {"minutos", {{"type", "number"}, {"minimum", 1}, {"maximum", 120}}},
                               {"aplicar", {{"type", "boolean"}}},
                               {"cancelar", {{"type", "boolean"}}}}}};
            t.run = [srp](const json& a, Done done) {
                if (!srp) return done(ToolResult::error("no disponible"));
                if (a.value("cancelar", false)) {
                    srp->cancelar();
                    return done(ToolResult().text("Cancelado."));
                }
                const std::string e = srp->entrenar(a.value("entrada", std::string()),
                                                    a.value("minutos", 5.0), 60, a.value("aplicar", true));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().text("Empezó: graba ~1 minuto y entrena. superres_estado muestra el avance "
                                       "y la ganancia en dB."));
            };
            core_.addTool(std::move(t));
        }
    }
    {
        Tool t;
        t.name = "video_reloj";
        t.title = "El reloj del estudio (sincronía de las entradas de red)";
        t.category = "video";
        t.description = "Sin argumentos: el reloj del estudio y en qué estado está en cada entrada de red. "
                        "Cambiarlo: `tipo` "
                        "sistema (el de esta máquina; de fábrica) | ntp (`servidor`, `puerto` 123: todos los "
                        "equipos del lugar "
                        "contra el mismo NTP) | ptp (`dominio`; IEEE 1588, el de los equipos de estudio; "
                        "necesita permisos) | "
                        "red (`servidor`, `puerto`: el reloj que publica OTRO Broadcast Appliance). "
                        "`latencia_ms`: el colchón "
                        "de las entradas de red (más = más tolerancia a la red, más retraso). "
                        "`publicar_puerto`: publica el "
                        "reloj de ESTE aparato en ese puerto para que otros usen `red` contra él (0 = dejar "
                        "de publicar). Se "
                        "aplica a todas las entradas de GStreamer.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"tipo", {{"type", "string"}, {"enum", {"sistema", "ntp", "ptp", "red"}}}},
                           {"servidor", {{"type", "string"}}},
                           {"puerto", {{"type", "integer"}}},
                           {"dominio", {{"type", "integer"}}},
                           {"latencia_ms", {{"type", "integer"}}},
                           {"publicar_puerto", {{"type", "integer"}}}}}};
        t.examples = {
            R"({})", R"({"tipo": "ntp", "servidor": "pool.ntp.org"})", R"({"publicar_puerto": 8554})",
            R"({"tipo": "red", "servidor": "10.0.0.5", "puerto": 8554})", R"({"latencia_ms": 400})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            if (!a.empty()) {
                const std::string err = v->configurarReloj(a);
                if (!err.empty()) return done(ToolResult::error(err));
            }
            json fuentes = json::array();
            for (const QString& n : v->entradas()) {
                const json st = v->estadoEntrada(n.toStdString());
                if (!st.contains("gstreamer")) continue;
                fuentes.push_back({{"entrada", n.toStdString()},
                                   {"reloj", st["gstreamer"].value("reloj", std::string())},
                                   {"sincronizado", st["gstreamer"].value("reloj_sincronizado", false)},
                                   {"latencia_ms", st["gstreamer"].value("latencia_ms", 0)},
                                   {"desfase_ms", st["gstreamer"].value("desfase_ms", 0)}});
            }
            done(ToolResult().data(
                {{"reloj", v->reloj()},
                 {"entradas_gstreamer", fuentes},
                 {"nota", "todas las entradas van con el reloj del estudio (el del motor); se alinean "
                          "con video_desfase"}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_sincronia";
        t.title = "Medir (y corregir) cuánto atrasa una entrada respecto de otra";
        t.category = "video";
        t.description =
            "Mide durante `segundos` (de fábrica 8) cuánto atrasa `b` respecto de `a`, comparando cuadro a "
            "cuadro "
            "lo que se ve. Funciona si las dos ven algo EN COMÚN que cambie: la misma imagen por dos caminos "
            "(el "
            "mismo stream por SRT y por RTMP: muy preciso) o dos cámaras del mismo lugar con movimiento, un "
            "aplauso o un flash. Con imagen quieta no puede (lo dice). Con `aplicar`, lo corrige: atrasa la "
            "que "
            "va adelantada (una entrada de red, con su desfase; una cámara, con una tarjeta retardo). La "
            "resolución es un cuadro del lienzo. Conviene volver a medir después de aplicar (debe dar ~0).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"a", "b"}},
                         {"properties",
                          {{"a", entrada},
                           {"b", entrada},
                           {"segundos", {{"type", "integer"}}},
                           {"aplicar", {{"type", "boolean"}}}}}};
        t.examples = {R"({"a": "Móvil SRT", "b": "Corresponsal RTMP", "aplicar": true})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            v->medirDesfase(a.value("a", std::string()), a.value("b", std::string()), a.value("segundos", 8),
                            a.value("aplicar", false), [done](json r) {
                                // Sin poder empezar (una entrada que no existe): error. Si midió
                                // y no encontró nada, eso es un dato (con el motivo).
                                if (r.contains("error") && !r.contains("medido"))
                                    return done(ToolResult::error(r.value("error", std::string())));
                                done(ToolResult().data(r));
                            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_desfase";
        t.title = "Correr una entrada en el tiempo a mano";
        t.category = "video";
        t.description = "Atrasa (o adelanta) una entrada `ms` milisegundos: una entrada de red, con el "
                        "desfase de su colchón "
                        "(para los dos lados, en vivo); una cámara local, solo para atrás (una tarjeta "
                        "retardo). `relativo` "
                        "(de fábrica, sí) suma a lo que tenía; sin él, lo deja en `ms`. Para medirlo solo: "
                        "video_sincronia.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"entrada", "ms"}},
            {"properties",
             {{"entrada", entrada}, {"ms", {{"type", "number"}}}, {"relativo", {{"type", "boolean"}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            json hecho;
            const std::string err = v->ajustarDesfase(a.value("entrada", std::string()), a.value("ms", 0.0),
                                                      a.value("relativo", true), &hecho);
            if (!err.empty()) return done(ToolResult::error(err));
            done(ToolResult().data(hecho));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
