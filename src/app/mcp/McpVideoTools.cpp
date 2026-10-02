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

#include <QPointer>

namespace ba::app::mcp {

void McpService::registerVideoTools() {
    video::VideoGrafos* vg = vm_.videoGrafos();
    if (vg == nullptr) return;
    core_.addCategory(
        "video", "Grafo de video",
        "Tarjetas de video por entrada, unidas con cables como el rack de audio: color, croma, recorte y espejo, "
        "y salidas con nombre que se ponen en cualquier escena. Lo corre la GPU.");
    const QPointer<video::VideoGrafos> v = vg;
    const json entrada = {{"type", "string"}, {"description", "La entrada de video (como en video_grafo)."}};
    const auto hecho = [v](const std::string& entradaNombre, const std::string& error, Done done) {
        if (!error.empty()) return done(ToolResult::error(error));
        done(ToolResult().data(v->describir(entradaNombre)));
    };

    {
        Tool t;
        t.name = "video_grafo";
        t.title = "El grafo de video de una entrada";
        t.category = "video";
        t.readOnly = true;
        t.description =
            "Sin `entrada`: las entradas de video y las tarjetas que se pueden agregar. Con `entrada`: su grafo "
            "(nodos con id, tipo, parámetros; cables desde -> hacia). `in` es la cámara (ya encuadrada por el "
            "PTZ) y `out` lo que ve la escena.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"entrada", entrada}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            v->sincronizar();
            const std::string e = a.value("entrada", std::string());
            if (e.empty()) {
                json ents = json::array();
                for (const QString& n : v->entradas()) ents.push_back(n.toStdString());
                return done(ToolResult().data({{"entradas", ents}, {"tarjetas", video::GrafoVideo::catalogo()}}));
            }
            const json d = v->describir(e);
            if (d.contains("error")) return done(ToolResult::error(d.value("error", std::string())));
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_tarjeta_agregar";
        t.title = "Agregar una tarjeta de video";
        t.category = "video";
        t.description =
            "Agrega la tarjeta `tipo` (color, croma, recorte, salida) al grafo de `entrada`, después de `despues` "
            "(vacío = justo antes de la escena). Color, croma y recorte se intercalan en la cadena; una salida se "
            "agrega como RAMA (publica ese punto como una fuente con nombre, sin cortar la cadena).";
        t.details =
            "`params` con los de fábrica que quieras cambiar (video_grafo los lista). Croma: `clave` = el color "
            "de fondo como entero 0xRRGGBB (de fábrica 0x00B140, verde), `similitud`, `suavidad`, `derrame` "
            "(0..1). Color: `brillo` (-1..1), `contraste`, `saturacion` (0..2), `gamma`. Recorte: `izquierda`, "
            "`arriba`, `derecha`, `abajo` (0..1), `espejoH`, `espejoV`. Salida: `nombre` (vacío = «<entrada> · "
            "salida»). Para que una salida se vea: video_salida_a_escena.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "tipo"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"tipo", {{"type", "string"}, {"enum", {"color", "croma", "recorte", "salida"}}}},
                           {"despues", {{"type", "string"}, {"description", "id de la tarjeta de la que cuelga"}}},
                           {"params", {{"type", "object"}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "tipo": "croma"})",
                      R"({"entrada": "Cámara 1", "tipo": "salida", "despues": "in", "params": {"nombre": "Cámara 1 limpia"}})"};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            std::string nuevo;
            const std::string err = v->editar(e, [&](video::GrafoVideo& g) {
                std::string error;
                nuevo = g.agregar(a.value("tipo", std::string()), a.value("despues", std::string()),
                                  a.value("params", json::object()), &error);
                return error;
            });
            if (!err.empty()) return done(ToolResult::error(err));
            json d = v->describir(e);
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
        t.description = "Quita la tarjeta `id` del grafo de `entrada`: lo de antes queda unido con lo de después.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "id"}},
                         {"properties", {{"entrada", entrada}, {"id", {{"type", "string"}}}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar(e, [&](video::GrafoVideo& g) {
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
        t.description = "Cambia `params` de la tarjeta `id` (solo los que tiene, del mismo tipo) y/o su `etiqueta`.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "id", "params"}},
                         {"properties", {{"entrada", entrada}, {"id", {{"type", "string"}}}, {"params", {{"type", "object"}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "id": "croma", "params": {"similitud": 0.5}})"};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar(e, [&](video::GrafoVideo& g) {
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
        t.title = "Conectar o desconectar tarjetas de video";
        t.category = "video";
        t.description =
            "Un cable de `desde` a `hacia` en el grafo de `entrada` (a cada tarjeta le llega UNO: el nuevo "
            "reemplaza al que había; de una salida pueden salir varios). Con `quitar`, lo saca. Sin ciclos.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "desde", "hacia"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"desde", {{"type", "string"}}},
                           {"hacia", {{"type", "string"}}},
                           {"quitar", {{"type", "boolean"}}}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar(e, [&](video::GrafoVideo& g) {
                std::string error;
                const std::string d = a.value("desde", std::string()), h = a.value("hacia", std::string());
                if (a.value("quitar", false)) {
                    if (!g.desconectar(d, h)) error = "no hay un cable de «" + d + "» a «" + h + "»";
                } else {
                    g.conectar(d, h, &error);
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
            "Pone la tarjeta salida `id` del grafo de `entrada` como fuente en `escena` (vacía = la que está al "
            "aire), con su nombre. Queda guardada como cualquier fuente de la escena.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "id"}},
                         {"properties", {{"entrada", entrada}, {"id", {{"type", "string"}}}, {"escena", {{"type", "string"}}}}}};
        t.run = [v](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string err = v->ponerEnEscena(a.value("entrada", std::string()), a.value("id", std::string()),
                                                     a.value("escena", std::string()));
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
        t.description = "Deja el grafo de `entrada` en «entrada -> escena» (saca todas las tarjetas y el filtro).";
        t.inputSchema = {{"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
        t.run = [v, hecho](const json& a, Done done) {
            if (!v) return done(ToolResult::error("el video no está disponible"));
            const std::string e = a.value("entrada", std::string());
            hecho(e, v->editar(e, [](video::GrafoVideo& g) {
                g = video::GrafoVideo();
                return std::string();
            }), done);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
