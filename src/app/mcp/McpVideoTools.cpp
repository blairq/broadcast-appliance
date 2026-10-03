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
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Diagnostico.hpp"
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
    nlohmann::json props{{"plugin", tj.value("plugin", std::string())}, {"tarjeta", tj.value("tarjeta", std::string())}};
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
        "Tarjetas propias (color, croma, recorte, salida), cualquier filtro de video de OBS o cualquier módulo de "
        "video de GStreamer (video_modulos: el catálogo documentado). Entradas virtuales (barras, negro, un "
        "pipeline de GStreamer) con video_entrada_virtual. Lo corre la GPU.");
    const QPointer<video::VideoGrafos> v = vg;
    const auto deQt = [](const QVariant& x) {
        return json::parse(QJsonDocument::fromVariant(x).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    };
    const json entrada = {{"type", "string"}, {"description", "La entrada de video (como en video_grafo)."}};
    const json idTarjeta = {{"type", "string"}, {"description", "El id de la tarjeta (como en video_grafo), p. ej. «camara-1.croma»."}};
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
        t.description =
            "Sin `entrada`: todo el estudio (cada entrada con su sub-grafo, los nodos del estudio y los cables "
            "entre ellos) y las tarjetas propias. Con `entrada`: su sub-grafo, con los nodos de afuera que se "
            "conectan con él marcados `externo`. Cada nodo trae sus puertos (`entradas`, `salidas`; un compositor "
            "tiene puertos a pedido in_0, in_1… y siempre uno libre). Una tarjeta que no puede correr trae `error` "
            "con el motivo (p. ej. un módulo de GStreamer que no arrancó).";
        t.inputSchema = {{"type", "object"},
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
                for (const QString& n : v->entradas()) ents.push_back(n.toStdString());
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
        t.title = "Los filtros de video de OBS (y los controles de una tarjeta)";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "Sin `id`: los filtros de video de OBS que se pueden poner como tarjeta (corrección de color, aplicar "
            "LUT .cube, llaves de croma/color/luma, nitidez, recorte, escala, máscaras, Mejorar imagen, PTZ "
            "digital…). Con `id` de una tarjeta de filtro de OBS o de un módulo de GStreamer: sus controles (nombre, "
            "tipo, valor, rango, opciones).";
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
        t.description =
            "El catálogo de módulos de video de GStreamer que trae el programa, con su documentación en castellano "
            "(para qué sirven y cómo se usan en vivo). Sin argumentos: un resumen por categoría. `buscar`: por "
            "palabra (en castellano o en inglés, en el nombre, el título o la descripción). `categoria`: los de "
            "una. `elemento`: uno solo con todo (clase, licencia, paquete) y sus propiedades. `tarjeta` dice si va "
            "como tarjeta de filtro (video_tarjeta_agregar tipo gst); los demás son fuentes, salidas o mezcladores.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"buscar", {{"type", "string"}}},
                           {"categoria", {{"type", "string"}}},
                           {"elemento", {{"type", "string"}}},
                           {"soloTarjetas", {{"type", "boolean"}, {"description", "solo los que van como tarjeta"}}}}}};
        t.examples = {R"({"buscar": "croma"})", R"({"elemento": "deinterlace"})", R"({"categoria": "Color"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const video::CatalogoGst& c = v->gst();
            if (!c.disponible()) return done(ToolResult::error("GStreamer no está disponible: " + c.error()));
            const std::string el = a.value("elemento", std::string());
            if (!el.empty()) {
                json m = c.modulo(el);
                if (m.empty()) return done(ToolResult::error("no existe el módulo «" + el + "» (video_modulos con buscar)"));
                m["propiedades"] = c.propiedades(el, json::object());
                return done(ToolResult().data(m));
            }
            const auto minus = [](std::string x) {
                for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return x;
            };
            const std::string buscar = minus(a.value("buscar", std::string())), cat = a.value("categoria", std::string());
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
                return done(ToolResult().data({{"version", c.version()}, {"total", c.modulos().size()}, {"categorias", cats}}));
            }
            json out = json::array();
            for (const auto& m : c.modulos()) {
                if (!cat.empty() && m.value("categoria", std::string()) != cat) continue;
                if (solo && !m.value("tarjeta", false)) continue;
                if (!buscar.empty()) {
                    const std::string todo = minus(m.value("elemento", "") + " " + m.value("titulo", "") + " " +
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
            "Sin `nombre`: las entradas virtuales que hay (barras HD, barras 75 %, barras con la hora, negro, "
            "blanco, verde, placa de zonas, damero, pelota, ruido). Con `nombre` y `preset` (o `pipeline`, la parte "
            "de origen de un pipeline de GStreamer como en gst-launch, p. ej. «rtspsrc location=… ! decodebin»): "
            "la crea en `escena` (obligatoria: para no tocar la que está al aire sin querer; `al_aire: true` la pone en "
            "la del aire). Queda como una entrada más: con su sub-grafo de tarjetas. Para un stream de red "
            "(srt/rtmp/rtsp) usá video_entrada_red, que lo sincroniza.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"preset", {{"type", "string"}}},
                           {"pipeline", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}},
                           {"al_aire", {{"type", "boolean"}}}}}};
        t.examples = {R"({"nombre": "Barras", "preset": "barras-hd"})",
                      R"({"nombre": "Cámara IP", "pipeline": "rtspsrc location=rtsp://10.0.0.5/stream latency=200 ! decodebin"})"};
        t.run = [v, deQt](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string nombre = a.value("nombre", std::string());
            if (nombre.empty()) return done(ToolResult().data(deQt(video::VideoGrafos::virtuales())));
            if (a.value("escena", std::string()).empty() && !a.value("al_aire", false))
                return done(ToolResult::error("falta `escena` (o `al_aire: true` para ponerla en la que está al aire)"));
            const std::string pl = a.value("pipeline", std::string());
            const std::string err = v->crearEntradaVirtual(nombre, pl.empty() ? a.value("preset", std::string("barras-hd")) : "pipeline:" + pl,
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
            "Agrega una tarjeta (para video, PRIMERO GStreamer: `gst` con un módulo de video_modulos). En la `entrada` "
            "(después de `despues`; vacío = justo antes de su escena): `color`, "
            "`croma`, `recorte`, `obs` (un filtro de OBS: `filtro` = su id, de video_filtros) y `gst` (un módulo de "
            "GStreamer: `elemento`, de video_modulos) y `plugin` (la tarjeta de un plugin de Python: `plugin`, "
            "`tarjeta`, `parametros`) se intercalan; "
            "`retardo` atrasa la imagen; `salida` se cuelga como RAMA (publica ese punto como fuente con nombre). "
            "`compositor` es del estudio (sin entrada): se le conectan entradas (o tarjetas) con video_cable, y se "
            "pone en una escena con video_salida_a_escena o se cablea a la escena de una entrada.";
        t.details =
            "`params` con los de fábrica que quieras cambiar (video_grafo los lista). Croma: `clave` 0xRRGGBB (verde "
            "0x00B140), `similitud`, `suavidad`, `derrame`. Color: `brillo`, `contraste`, `saturacion`, `gamma`. "
            "Recorte: `izquierda`, `arriba`, `derecha`, `abajo` (0..1), `espejoH`, `espejoV`. Retardo: `cuadros` "
            "(0..120, para sincronizar). Rostros: `cada` (ms), `marcar` (dibuja las caras); la pone sola Personas "
            "al elegir una camara (`entrada:<camara>`), `uso` dice para que. Salida: `nombre`. Compositor: `ancho`, `alto`, `fondo` (0xAARRGGBB), "
            "`nombre`, y por puerto `capas.in_N` {x, y, ancho, alto (0..1 del lienzo), opacidad, orden, ajuste "
            "(encajar|llenar)}; sin capas, mosaico; null en una capa la vuelve al mosaico. "
            "Un filtro de OBS: sus ajustes van en `params.ajustes` (video_filtros con el id de la tarjeta los "
            "lista). Un módulo de GStreamer: sus propiedades en `params.propiedades`, con los nombres de "
            "GStreamer (video_modulos con `elemento` las lista; los enum, por su nick).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"tipo", {{"type", "string"}, {"enum", {"color", "croma", "recorte", "obs", "gst", "plugin", "retardo", "normalizar", "rostros", "salida", "compositor"}}}},
                           {"plugin", {{"type", "string"}, {"description", "con tipo plugin: el id del plugin de Python (plugin_listar)"}}},
                           {"tarjeta", {{"type", "string"}, {"description", "con tipo plugin: la tarjeta de video que declaró"}}},
                           {"parametros", {{"type", "object"}, {"description", "con tipo plugin: sus ajustes"}}},
                           {"filtro", {{"type", "string"}, {"description", "con tipo obs: el id del filtro de OBS"}}},
                           {"elemento", {{"type", "string"}, {"description", "con tipo gst: el módulo de GStreamer (p. ej. videobalance)"}}},
                           {"despues", idTarjeta},
                           {"params", {{"type", "object"}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "tipo": "croma"})",
                      R"({"entrada": "Cámara 1", "tipo": "obs", "filtro": "clut_filter", "params": {"ajustes": {"image_path": "/ruta/look.cube"}}})",
                      R"({"entrada": "Cámara 1", "tipo": "gst", "elemento": "gamma", "params": {"propiedades": {"gamma": 1.3}}})",
                      R"({"tipo": "compositor"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const json ap = tarjetaDePlugin(json{{"tipo", a.value("tipo", std::string())},
                                                 {"plugin", a.value("plugin", std::string())},
                                                 {"tarjeta", a.value("tarjeta", std::string())},
                                                 {"parametros", a.value("parametros", json::object())}});
            const std::string e = a.value("entrada", std::string()), tipo = ap.value("tipo", std::string());
            json params = a.value("params", json::object());
            if (ap.contains("elemento")) params = {{"elemento", ap["elemento"]}, {"propiedades", ap["propiedades"]}};
            std::string etiqueta;
            if (tipo == "obs") {
                const std::string f = a.value("filtro", params.value("filtro", std::string()));
                etiqueta = video::VideoGrafos::nombreFiltro(f);
                if (etiqueta.empty()) return done(ToolResult::error("no existe el filtro «" + f + "» (video_filtros los lista)"));
                params["filtro"] = f;
            }
            std::string nuevo;
            const std::string err = v->editar([&](video::GrafoVideo& g) {
                std::string error;
                if (tipo == "gst") {
                    nuevo = v->agregarModuloEn(g, a.value("elemento", params.value("elemento", std::string())), e,
                                               a.value("despues", std::string()), params.value("propiedades", json::object()),
                                               &error);
                    return error;
                }
                nuevo = g.agregar(tipo, e, a.value("despues", std::string()), params, &error);
                if (!nuevo.empty() && !etiqueta.empty()) g.ajustar(nuevo, json{{"etiqueta", etiqueta}}, nullptr);
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
        t.name = "video_tarjeta_quitar";
        t.title = "Quitar una tarjeta de video";
        t.category = "video";
        t.description = "Quita la tarjeta `id`: un filtro se puentea (lo de antes queda unido con lo de después).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", idTarjeta}, {"entrada", entrada}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(a.value("entrada", std::string()), v->editar([&](video::GrafoVideo& g) {
                std::string error;
                g.quitar(a.value("id", std::string()), &error);
                return error;
            }), done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_ajustar";
        t.title = "Ajustar una tarjeta de video";
        t.category = "video";
        t.idempotent = true;
        t.description =
            "Cambia `params` de la tarjeta `id` (solo los que tiene, del mismo tipo) y/o su `etiqueta`. Un filtro de "
            "OBS: `{\"ajustes\": {…}}`; un módulo de GStreamer: `{\"propiedades\": {…}}` (se mezclan). Una tarjeta "
            "manejada por un controlador: el ajuste va al controlador (cambia en todas); lo propio de esa entrada, en "
            "`{\"local\": {…}}`. Un compositor: `{\"capas\": {\"in_0\": {…}}}` por puerto. La tarjeta de un plugin de "
            "Python: `{\"parametros\": {\"fuerza\": 0.5}}` (o los parámetros sueltos), se mezclan con los que tiene.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "params"}},
                         {"properties", {{"id", idTarjeta}, {"params", {{"type", "object"}}}, {"entrada", entrada}}}};
        t.examples = {R"({"id": "camara-1.croma", "params": {"similitud": 0.5}})"};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(a.value("entrada", std::string()), v->editar([&](video::GrafoVideo& g) {
                return video::VideoGrafos::ajustarEn(g, a.value("id", std::string()), a.value("params", json::object()));
            }), done);
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
            "distintas). `entrada`: el puerto de `hacia` (vacío = el primero; en un compositor, uno nuevo). A cada "
            "puerto le llega UNO (el nuevo reemplaza al que había); de una salida pueden salir varios. Sin ciclos. "
            "Con `quitar`, lo saca.";
        t.inputSchema = {{"type", "object"},
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
                    if (!g.desconectar(d, a.value("salida", std::string()), h, a.value("entrada", std::string())))
                        error = "no hay un cable de «" + d + "» a «" + h + "»";
                } else {
                    g.conectar(d, a.value("salida", std::string()), h, a.value("entrada", std::string()), &error);
                }
                return error;
            }), done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_controlador";
        t.title = "Manejar tarjetas de varias entradas con un controlador";
        t.category = "video";
        t.description =
            "Un controlador es un nodo del estudio con el molde de una tarjeta: las tarjetas conectadas a su salida "
            "de control (`ctl`) corren con SUS parámetros (la misma corrección de color para las cámaras de tres "
            "marcas), y cada una puede tener lo suyo encima (`local`, con video_tarjeta_ajustar). Con `tarjeta`: "
            "hace un controlador con esa tarjeta (o devuelve el que ya la maneja). Con `controlador` y `entrada`: "
            "agrega a esa entrada una tarjeta manejada por él (antes de su escena, o después de `despues`). Con "
            "`soltar` (id de tarjeta): la desconecta y se queda con los valores que usaba. Para conectar una "
            "tarjeta que ya existe: video_cable desde el controlador (salida ctl) a la tarjeta (entrada ctl).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"tarjeta", idTarjeta},
                           {"controlador", idTarjeta},
                           {"entrada", entrada},
                           {"despues", idTarjeta},
                           {"soltar", idTarjeta}}}};
        t.examples = {R"({"tarjeta": "sony.gst"})", R"({"controlador": "estudio.control", "entrada": "Pelco"})",
                      R"({"soltar": "pelco.gst"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::string hecho;
            const std::string err = v->editar([&](video::GrafoVideo& g) {
                std::string e;
                if (a.contains("soltar")) {
                    g.soltar(a.value("soltar", std::string()), &e);
                } else if (a.contains("controlador")) {
                    hecho = g.agregarControlada(a.value("controlador", std::string()), a.value("entrada", std::string()),
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
            "Pone la tarjeta salida `id` (o un compositor) como fuente en `escena` (obligatoria: nunca la del aire sin "
            "pedirlo). La fuente se llama como el nombre de la tarjeta (se cambia con video_tarjeta_ajustar "
            "{\"params\": {\"nombre\": …}} antes de ponerla). Queda guardada como cualquier fuente de la escena. Para un "
            "mosaico entero en una llamada: video_mosaico.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "escena"}},
                         {"properties", {{"id", idTarjeta}, {"escena", {{"type", "string"}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string err = v->ponerEnEscena(a.value("id", std::string()), a.value("escena", std::string()));
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
        t.description = "Deja la `entrada` en «entrada -> escena»: saca sus tarjetas y lo que llegaba a su escena desde afuera.";
        t.inputSchema = {{"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar([&](video::GrafoVideo& g) {
                g.limpiar(e);
                return std::string();
            }), done);
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
            "Arma, en orden, las tarjetas de la `entrada` (de la cámara hacia la escena) en UNA llamada y valida "
            "cada una. `modo`: «reemplazar» (de fábrica: saca las que había) o «agregar» (al final). Cada tarjeta: "
            "{tipo, ...}. Tipos (usá `gst` primero: color, nitidez, efectos y casi todo lo de video se hace con GStreamer): "
            "`gst` (+`elemento` y `propiedades`, de video_modulos), `obs` (+`filtro` y "
            "`ajustes`), `color` {brillo, contraste, saturacion, gamma}, `croma`, `recorte`, `retardo` {ms}, "
            "`normalizar` {matriz_declarada, matriz_real, rango_declarado, rango_real, desentrelazar}, `rostros`, "
            "`salida` {nombre} (rama). COMPARTIR entre entradas: `compartida: \"nombre\"` en la tarjeta; la primera "
            "vez crea un controlador con ese nombre y las siguientes (en otras entradas, con el mismo nombre) "
            "quedan manejadas por él: cambiar el controlador las cambia a todas. Lo propio de una entrada sobre lo "
            "compartido: `local: {...}` (p. ej. {\"propiedades\": {\"hue\": -0.05}}). VARIAS ENTRADAS en una llamada: "
            "`entradas: [...]` con las mismas `tarjetas` y `local_por_entrada` para lo propio de cada una (el caso «la "
            "misma corrección en las tres cámaras»). El controlador toma el id del nombre compartido (estudio.<nombre>). "
            "Devuelve el sub-grafo y el diagnóstico de cada entrada.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"tarjetas"}},
            {"properties",
             {{"entrada", entrada},
              {"entradas",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "la MISMA cadena en varias entradas a la vez (las `compartida` quedan manejadas por un solo controlador)"}}},
              {"local_por_entrada",
               {{"type", "object"},
                {"description", "lo propio de cada entrada sobre lo compartido: {\"Pelco\": {\"propiedades\": {\"hue\": 0.04}}}"}}},
              {"modo", {{"type", "string"}, {"enum", {"reemplazar", "agregar"}}}},
              {"tarjetas",
               {{"type", "array"},
                {"items",
                 {{"type", "object"},
                  {"properties",
                   {{"tipo", {{"type", "string"}, {"description", "color, croma, recorte, obs, gst, plugin (+ `plugin`, `tarjeta`, `parametros`), retardo, normalizar…"}}},
                    {"plugin", {{"type", "string"}}},
                    {"tarjeta", {{"type", "string"}}},
                    {"parametros", {{"type", "object"}}},
                    {"elemento", {{"type", "string"}}},
                    {"filtro", {{"type", "string"}}},
                    {"propiedades", {{"type", "object"}}},
                    {"ajustes", {{"type", "object"}}},
                    {"params", {{"type", "object"}, {"description", "los de una tarjeta propia (color, croma, retardo…)"}}},
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
            for (const auto& x : a.value("entradas", json::array())) entradas.push_back(x.get<std::string>());
            if (a.contains("entrada")) entradas.insert(entradas.begin(), a.value("entrada", std::string()));
            if (entradas.empty()) return done(ToolResult::error("falta `entrada` (o `entradas`)"));
            for (const auto& e : entradas)
                if (!v->esEntrada(e)) return done(ToolResult::error("«" + e + "» no es una entrada de video (video_estado las lista)"));
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
                        if (!tj.value("compartida", std::string()).empty() && localPor.contains(e)) tj["local"] = localPor[e];
                        const std::string tipo = tj.value("tipo", std::string());
                        const std::string donde = "«" + e + "», tarjeta " + std::to_string(i) + " (" + tipo + "): ";
                        std::string error, id;
                        const std::string comp = tj.value("compartida", std::string());
                        std::string control;
                        if (!comp.empty())
                            for (const auto& n : g.nodos())
                                if (n.tipo == "control" && n.etiqueta == comp) control = n.id;
                        if (!control.empty()) {
                            id = g.agregarControlada(control, e, despues, &error);
                        } else if (tipo == "gst") {
                            id = v->agregarModuloEn(g, tj.value("elemento", std::string()), e, despues, tj.value("propiedades", json::object()), &error);
                        } else if (tipo == "obs") {
                            const std::string f = tj.value("filtro", std::string());
                            if (video::VideoGrafos::nombreFiltro(f).empty())
                                error = "no existe el filtro de OBS «" + f + "» (video_filtros)";
                            else
                                id = g.agregar("obs", e, despues, json{{"filtro", f}, {"ajustes", tj.value("ajustes", json::object())}}, &error);
                            if (!id.empty()) g.ajustar(id, json{{"etiqueta", video::VideoGrafos::nombreFiltro(f)}}, nullptr);
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
                        if (tj.contains("local") && !g.ajustar(id, json{{"local", tj["local"]}}, &error)) return donde + error;
                        if (tj.contains("etiqueta")) g.ajustar(id, json{{"etiqueta", tj["etiqueta"]}}, nullptr);
                        hechas.push_back({{"entrada", e}, {"id", id}, {"tipo", tipo}, {"compartida", comp}, {"controlador", control}});
                        if (tipo != "salida") despues = id;
                    }
                }
                return std::string();
            });
            if (!err.empty()) return done(ToolResult::error(err + " (no se cambió nada)"));
            json d{{"hechas", hechas}};
            json diag = json::object();
            for (const auto& e : entradas) diag[e] = video::diagnosticar(v->estadoEntrada(e));
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
            "Junta `entradas` en una `escena` (si no existe, la crea) en UNA llamada: crea el compositor (o rearma el que se llame `nombre`), "
            "conecta lo que llega a la escena de cada entrada (o sea, con sus tarjetas de color, etc.), lo pone en la "
            "escena y, con `ocultar_sueltas` (de fábrica, sí), oculta en esa escena las entradas sueltas (el mosaico "
            "las sigue recibiendo: no se apagan). `disposicion`: mosaico (grilla; con 3, una arriba al centro y dos abajo, sin hueco), pip (la primera de fondo y las "
            "demás chicas arriba a la derecha) o lado (una al lado de la otra). Para retocar después: "
            "video_tarjeta_ajustar sobre el compositor (`capas`).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"escena", "entradas"}},
                         {"properties",
                          {{"escena", {{"type", "string"}}},
                           {"entradas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"disposicion", {{"type", "string"}, {"enum", {"mosaico", "pip", "lado"}}}},
                           {"nombre", {{"type", "string"}, {"description", "el de la fuente del mosaico (de fábrica, «Estudio · compositor»)"}}},
                           {"ocultar_sueltas", {{"type", "boolean"}}}}}};
        t.examples = {R"({"escena": "Exterior", "entradas": ["Axis Terreno", "SRT 1", "SRT 2"], "nombre": "Mosaico Exterior"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            std::vector<std::string> es;
            for (const auto& x : a.value("entradas", json::array())) es.push_back(x.get<std::string>());
            json hecho;
            const std::string err = v->armarMosaico(a.value("escena", std::string()), es, a.value("disposicion", std::string("mosaico")),
                                                    a.value("nombre", std::string()), a.value("ocultar_sueltas", true), &hecho);
            if (!err.empty()) return done(ToolResult::error(err));
            hecho["verificar"] = "imagen_analizar sobre «" + hecho.value("fuente", std::string()) +
                                 "» con una pregunta concreta (¿algo estirado o en negro?; el primer cuadro puede tardar un instante)";
            done(ToolResult().data(hecho));
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
            "Sin `entrada`: LISTA todas las entradas de video (cámaras, red, virtuales) con su diagnóstico resumido. Con `entrada`: tamaño, cuadros por segundo "
            "(declarados y medidos), formato, colorimetría (BT.601/709, rango), si llega entrelazada y su "
            "desentrelazado, el reloj y si está sincronizado, el colchón de red (latencia, cuadros que llegaron "
            "tarde), la latencia que agrega su sub-grafo, y los errores y avisos de sus tarjetas. `diagnostico`: "
            "cada problema con su gravedad y el ARREGLO (herramienta y argumentos). Usala antes y después de armar.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"entrada", entrada}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            v->sincronizar();
            const std::string e = a.value("entrada", std::string());
            if (!e.empty()) {
                json st = v->estadoEntrada(e);
                if (st.contains("error") && !st.contains("tipo")) return done(ToolResult::error(st.value("error", std::string())));
                st["diagnostico"] = video::diagnosticar(st);
                return done(ToolResult().data(st));
            }
            json out = json::array();
            for (const QString& n : v->entradas()) {
                const json st = v->estadoEntrada(n.toStdString());
                json r{{"entrada", n.toStdString()},
                       {"tipo", st.value("tipo", std::string())},
                       {"tamaño", std::to_string(st.value("ancho", 0)) + "x" + std::to_string(st.value("alto", 0))},
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
                        r["llega"] = std::to_string(o.value("ancho", 0)) + "x" + std::to_string(o.value("alto", 0)) + " " +
                                     o.value("formato", std::string()) + ", " + o.value("matriz", std::string()) + " rango " +
                                     o.value("rango", std::string()) + ", " +
                                     (o.value("fps_variable", false) ? std::string("fps variables")
                                                                      : std::to_string(std::lround(o.value("fps", 0.0))) + " fps") +
                                     (o.value("entrelazado", false) ? ", entrelazada" : "") +
                                     (o.value("pixel", std::string("cuadrado")) != "cuadrado" ? ", píxel " + o.value("pixel", std::string()) : "");
                    r["entra"] = g.value("formato", std::string()) + ", " + g.value("colorimetria", std::string());
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
        t.description =
            "Crea una entrada de video (y audio) desde `uri`: srt://host:puerto (o srt://:puerto?mode=listener para "
            "recibir), rtmp://servidor/app/clave, rtsp://camara/stream, udp://@:5000, http(s)://…/x.m3u8 o un "
            "archivo. Usa el reloj del estudio (video_reloj) y un COLCHÓN de `latencia_ms` (de fábrica, el del "
            "estudio; la `escena` se crea si no existe): cada cuadro sale a su hora de captura + latencia, así dos streams con la misma latencia "
            "salen juntos. RTSP trae la hora de captura del que manda (RTCP/NTP): se alinea solo. SRT y RTMP no la "
            "traen: el desfase entre ellos se mide con video_sincronia. Sobre una entrada de red que ya existe, la "
            "actualiza (otra uri o latencia).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"uri", {{"type", "string"}}},
                           {"escena", {{"type", "string"}, {"description", "dónde ponerla (obligatoria al crear)"}}},
                           {"latencia_ms", {{"type", "integer"}, {"description", "el colchón (de fábrica, el del estudio)"}}},
                           {"audio", {{"type", "boolean"}, {"description", "con su audio (de fábrica, sí)"}}},
                           {"normalizar",
                            {{"type", "boolean"},
                             {"description", "de fábrica, sí: GStreamer la desentrelaza si hace falta, la lleva a los cuadros "
                                             "del estudio, a píxel cuadrado y a color BT.709 antes de entrar"}}},
                           {"ntp_sync",
                            {{"type", "boolean"},
                             {"description", "RTSP: usar la hora de captura que manda la cámara (de fábrica, sí). Si su reloj "
                                             "está corrido (video_estado lo avisa), false"}}},
                           {"esperar_seg",
                            {{"type", "integer"},
                             {"description", "cuánto esperar a que conecte antes de devolver el estado con su diagnóstico (4 de "
                                             "fábrica; 0: no esperar)"}}}}}};
        t.examples = {R"({"nombre": "Móvil SRT", "uri": "srt://:9000?mode=listener", "escena": "Móvil", "latencia_ms": 300})",
                      R"({"nombre": "Corresponsal", "uri": "rtmp://10.0.0.20/live/corresponsal", "escena": "Móvil"})"};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string n = a.value("nombre", std::string());
            const std::string err = v->crearEntradaRed(n, a.value("uri", std::string()), a.value("latencia_ms", -1),
                                                       a.value("escena", std::string()),
                                                       a.contains("audio") ? (a.value("audio", true) ? 1 : 0) : -1,
                                                       a.contains("normalizar") ? (a.value("normalizar", true) ? 1 : 0) : -1,
                                                       a.contains("ntp_sync") ? (a.value("ntp_sync", true) ? 1 : 0) : -1);
            if (!err.empty()) return done(ToolResult::error(err));
            esperarEntrada(v, n, a.value("esperar_seg", 4) * 1000, [done](json st) { done(ToolResult().data(st)); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_reloj";
        t.title = "El reloj del estudio (sincronía de las entradas de red)";
        t.category = "video";
        t.description =
            "Sin argumentos: el reloj del estudio y en qué estado está en cada entrada de red. Cambiarlo: `tipo` "
            "sistema (el de esta máquina; de fábrica) | ntp (`servidor`, `puerto` 123: todos los equipos del lugar "
            "contra el mismo NTP) | ptp (`dominio`; IEEE 1588, el de los equipos de estudio; necesita permisos) | "
            "red (`servidor`, `puerto`: el reloj que publica OTRO Broadcast Appliance). `latencia_ms`: el colchón "
            "de las entradas de red (más = más tolerancia a la red, más retraso). `publicar_puerto`: publica el "
            "reloj de ESTE aparato en ese puerto para que otros usen `red` contra él (0 = dejar de publicar). Se "
            "aplica a todas las entradas de GStreamer.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"tipo", {{"type", "string"}, {"enum", {"sistema", "ntp", "ptp", "red"}}}},
                           {"servidor", {{"type", "string"}}},
                           {"puerto", {{"type", "integer"}}},
                           {"dominio", {{"type", "integer"}}},
                           {"latencia_ms", {{"type", "integer"}}},
                           {"publicar_puerto", {{"type", "integer"}}}}}};
        t.examples = {R"({})", R"({"tipo": "ntp", "servidor": "pool.ntp.org"})", R"({"publicar_puerto": 8554})",
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
            done(ToolResult().data({{"reloj", v->reloj()},
                                    {"entradas_gstreamer", fuentes},
                                    {"nota", "las cámaras locales (USB, captura) van con el reloj de libobs; se alinean "
                                             "con video_sincronia (retardo)"}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_sincronia";
        t.title = "Medir (y corregir) cuánto atrasa una entrada respecto de otra";
        t.category = "video";
        t.description =
            "Mide durante `segundos` (de fábrica 8) cuánto atrasa `b` respecto de `a`, comparando cuadro a cuadro "
            "lo que se ve. Funciona si las dos ven algo EN COMÚN que cambie: la misma imagen por dos caminos (el "
            "mismo stream por SRT y por RTMP: muy preciso) o dos cámaras del mismo lugar con movimiento, un "
            "aplauso o un flash. Con imagen quieta no puede (lo dice). Con `aplicar`, lo corrige: atrasa la que "
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
        t.description =
            "Atrasa (o adelanta) una entrada `ms` milisegundos: una entrada de red, con el desfase de su colchón "
            "(para los dos lados, en vivo); una cámara local, solo para atrás (una tarjeta retardo). `relativo` "
            "(de fábrica, sí) suma a lo que tenía; sin él, lo deja en `ms`. Para medirlo solo: video_sincronia.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "ms"}},
                         {"properties", {{"entrada", entrada}, {"ms", {{"type", "number"}}}, {"relativo", {{"type", "boolean"}}}}}};
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
