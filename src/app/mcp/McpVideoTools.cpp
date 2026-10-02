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

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/VideoGrafos.hpp"

#include <QJsonDocument>
#include <QPointer>

#include <cctype>

namespace ba::app::mcp {

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
            "la crea en `escena` (vacía = la del aire). Queda como una entrada más: con su sub-grafo de tarjetas.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"preset", {{"type", "string"}}},
                           {"pipeline", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}}}}};
        t.examples = {R"({"nombre": "Barras", "preset": "barras-hd"})",
                      R"({"nombre": "Cámara IP", "pipeline": "rtspsrc location=rtsp://10.0.0.5/stream latency=200 ! decodebin"})"};
        t.run = [v, deQt](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string nombre = a.value("nombre", std::string());
            if (nombre.empty()) return done(ToolResult().data(deQt(video::VideoGrafos::virtuales())));
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
            "Agrega una tarjeta. En la `entrada` (después de `despues`; vacío = justo antes de su escena): `color`, "
            "`croma`, `recorte`, `obs` (un filtro de OBS: `filtro` = su id, de video_filtros) y `gst` (un módulo de "
            "GStreamer: `elemento`, de video_modulos) se intercalan; "
            "`retardo` atrasa la imagen; `salida` se cuelga como RAMA (publica ese punto como fuente con nombre). "
            "`compositor` es del estudio (sin entrada): se le conectan entradas (o tarjetas) con video_cable, y se "
            "pone en una escena con video_salida_a_escena o se cablea a la escena de una entrada.";
        t.details =
            "`params` con los de fábrica que quieras cambiar (video_grafo los lista). Croma: `clave` 0xRRGGBB (verde "
            "0x00B140), `similitud`, `suavidad`, `derrame`. Color: `brillo`, `contraste`, `saturacion`, `gamma`. "
            "Recorte: `izquierda`, `arriba`, `derecha`, `abajo` (0..1), `espejoH`, `espejoV`. Retardo: `cuadros` "
            "(0..120, para sincronizar). Salida: `nombre`. Compositor: `ancho`, `alto`, `fondo` (0xAARRGGBB), "
            "`nombre`, y por puerto `capas.in_N` {x, y, ancho, alto (0..1 del lienzo), opacidad, orden, ajuste "
            "(encajar|llenar)}; sin capas, mosaico; null en una capa la vuelve al mosaico. "
            "Un filtro de OBS: sus ajustes van en `params.ajustes` (video_filtros con el id de la tarjeta los "
            "lista). Un módulo de GStreamer: sus propiedades en `params.propiedades`, con los nombres de "
            "GStreamer (video_modulos con `elemento` las lista; los enum, por su nick).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"tipo", {{"type", "string"}, {"enum", {"color", "croma", "recorte", "obs", "gst", "retardo", "salida", "compositor"}}}},
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
            const std::string e = a.value("entrada", std::string()), tipo = a.value("tipo", std::string());
            json params = a.value("params", json::object());
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
            "`{\"local\": {…}}`. Un compositor: `{\"capas\": {\"in_0\": {…}}}` por puerto.";
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
            "Pone la tarjeta salida `id` (o un compositor) como fuente en `escena` (vacía = la que está al aire), "
            "con su nombre (`params.nombre`). Queda guardada como cualquier fuente de la escena.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
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
}

} // namespace ba::app::mcp
