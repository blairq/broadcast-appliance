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

namespace ba::app::mcp {

void McpService::registerVideoTools() {
    video::VideoGrafos* vg = vm_.videoGrafos();
    if (vg == nullptr) return;
    core_.addCategory(
        "video", "Grafo de video",
        "Un grafo de video para todo el estudio, como un pipeline de GStreamer: cada entrada es un sub-grafo "
        "(entrada -> tarjetas -> escena) y los cables pueden cruzar entre entradas (p. ej. a un compositor). "
        "Tarjetas propias (color, croma, recorte, salida) o cualquier filtro de video de OBS. Lo corre la GPU.");
    const QPointer<video::VideoGrafos> v = vg;
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
            "tiene puertos a pedido in_0, in_1… y siempre uno libre).";
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
            "digital…). Con `id` de una tarjeta de filtro: sus controles (nombre, tipo, valor, rango, opciones).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", idTarjeta}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            const QVariantList l = id.isEmpty() ? v->filtrosObs() : v->propiedades(id);
            done(ToolResult().data(json::parse(QJsonDocument::fromVariant(l).toJson(QJsonDocument::Compact).toStdString(),
                                               nullptr, false)));
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
            "`croma`, `recorte` y `obs` (un filtro de OBS: `filtro` = su id, de video_filtros) se intercalan; "
            "`salida` se cuelga como RAMA (publica ese punto como fuente con nombre). `compositor` es del estudio "
            "(sin entrada): se le conectan entradas con video_cable.";
        t.details =
            "`params` con los de fábrica que quieras cambiar (video_grafo los lista). Croma: `clave` 0xRRGGBB (verde "
            "0x00B140), `similitud`, `suavidad`, `derrame`. Color: `brillo`, `contraste`, `saturacion`, `gamma`. "
            "Recorte: `izquierda`, `arriba`, `derecha`, `abajo` (0..1), `espejoH`, `espejoV`. Salida: `nombre`. "
            "Un filtro de OBS: sus ajustes van en `params.ajustes` (video_filtros con el id de la tarjeta los "
            "lista).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"tipo", {{"type", "string"}, {"enum", {"color", "croma", "recorte", "obs", "salida", "compositor"}}}},
                           {"filtro", {{"type", "string"}, {"description", "con tipo obs: el id del filtro de OBS"}}},
                           {"despues", idTarjeta},
                           {"params", {{"type", "object"}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "tipo": "croma"})",
                      R"({"entrada": "Cámara 1", "tipo": "obs", "filtro": "clut_filter", "params": {"ajustes": {"image_path": "/ruta/look.cube"}}})",
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
            "OBS: `{\"ajustes\": {…}}` (se mezclan). Un compositor: `{\"capas\": {\"in_0\": {…}}}` por puerto.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "params"}},
                         {"properties", {{"id", idTarjeta}, {"params", {{"type", "object"}}}, {"entrada", entrada}}}};
        t.examples = {R"({"id": "camara-1.croma", "params": {"similitud": 0.5}})"};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            hecho(a.value("entrada", std::string()), v->editar([&](video::GrafoVideo& g) {
                std::string error;
                g.ajustar(a.value("id", std::string()), a.value("params", json::object()), &error);
                return error;
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
        t.name = "video_salida_a_escena";
        t.title = "Poner una salida del grafo en una escena";
        t.category = "video";
        t.description =
            "Pone la tarjeta salida `id` como fuente en `escena` (vacía = la que está al aire), con su nombre. "
            "Queda guardada como cualquier fuente de la escena.";
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
