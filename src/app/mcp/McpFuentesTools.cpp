// ============================================================================
// src/app/mcp/McpFuentesTools.cpp — Herramientas MCP del motor: fuentes: agregar, quitar, mover, ubicar, efectos, recorte, ajustes de cada fuente.
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <fstream>
#include "arranque/RuntimeMotor.hpp"
#include "emision/InputSpecs.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PropertiesModel.hpp"
#include "video/FuentesCaptura.hpp"
#include "Capturas.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerFuentesTools() {
    AppRuntime& rt = rt_;
    const auto sceneOr = [&rt](const json& a) {
        const std::string s = a.value("scene", std::string());
        return s.empty() ? rt.engine().currentScene() : s;
    };
    // Por el nombre (lo de siempre) o por el id estable que da scene_list.
    const auto sceneExists = [&rt](const std::string& s) { return !rt.engine().sceneId(s).empty(); };
    const auto noScene = [&rt](const std::string& s) {
        std::string names;
        for (const auto& n : rt.engine().sceneNames())
            names += (names.empty() ? "" : ", ") + n;
        return ToolResult::error(
            ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'. Escenas: %2."), {s, names}));
    };
    const json sceneProp = {{"type", "string"},
                            {"description", QT_TRANSLATE_NOOP("mcp", "Escena (por defecto, la que esta al aire).")}};
    const json layoutProp = {
        {"description", QT_TRANSLATE_NOOP("mcp", "Ubicacion: preset (full, left, right, top, bottom, center, top-left, top-right, "
                        "bottom-left, bottom-right, pip-tl, pip-tr, pip-bl, pip-br, lower-third, third-1..3, "
                        "grid-<n>-<i>) u objeto {x,y,w,h} en fracciones del lienzo (0..1).")}};

    // --- Fuentes ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "source_types";
        t.hilo = Hilo::Foto; // son fijos (BroadcastEngine::tiposDeFuente): no espera a nadie
        t.title = ba::i18n::traducir("mcp", "Tipos de fuente disponibles");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Tipos de fuente que esta instalacion puede crear (id, nombre, si tiene video "
            "y/o audio). El id es el `type` de crear_fuente / scene_define.");
        t.run = [](const json&, Done done) {
            json out = json::array();
            for (const SourceType& s : ApiMotor::tiposDeFuente())
                out.push_back(
                    json{{"type", s.id}, {"name", s.displayName}, {"video", s.video}, {"audio", s.audio}});
            done(ToolResult().data(json{{"types", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escena_fuentes_ver";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Fuentes de una escena");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las fuentes que hay ADENTRO de una escena, una por una; no son las capas del DSK. Fuentes de una "
            "escena (de abajo hacia arriba) con tipo, visibilidad, ubicacion en el lienzo, volumen y silencio.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"scene", sceneProp}}}};
        t.run = [&rt, sceneOr, sceneExists, noScene](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            if (!sceneExists(scene)) return done(noScene(scene));
            json out = json::array();
            for (const std::string& n : rt.engine().sceneSources(scene)) {
                json s{{"name", n},
                       {"type", rt.engine().sourceTypeId(n)},
                       {"visible", rt.engine().sourceVisible(scene, n)},
                       {"volume", rt.engine().sourceVolume(n)},
                       {"muted", rt.engine().sourceMuted(n)}};
                if (const std::optional<SourceSpec> spec = rt.specDe(scene, n))
                    s["layout"] = {
                        {"x", spec->rect.x}, {"y", spec->rect.y}, {"w", spec->rect.w}, {"h", spec->rect.h}};
                if (const EfectosCapa ef = rt.engine().sourceEfectos(scene, n); !ef.vacio()) s["efectos"] = ef.aJson();
                out.push_back(s);
            }
            done(ToolResult().data(json{{"scene", scene}, {"sources", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "crear_fuente";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Agregar fuente");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "CREA una fuente nueva en una escena: texto, título, zócalo, cámara, color, invitado; un archivo de "
            "medios va con medio_poner. Si ya existe una fuente con ese nombre (en otra escena) se agrega LA "
            "MISMA (compartida).");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Settings mas comunes: image_source {file: ruta}, ffmpeg_source {local_file, looping: true}, "
            "color_source_v3 {color: ABGR entero, width, height}, text_ft2_source_v2 {text, font: {face, "
            "size}, color1}, v4l2_input {device_id}. Para ver todas las opciones de una fuente ya creada: "
            "source_properties.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"scene", sceneProp},
                           {"type", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id del tipo (source_types).")}}},
                           {"name", {{"type", "string"}}},
                           {"settings", {{"type", "object"}}},
                           {"layout", layoutProp},
                           {"visible", {{"type", "boolean"}, {"default", true}}},
                           {"efectos",
                            {{"type", "object"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "transparencia, borde difuso, esquinas y sombra de entrada (lo mismo que "
                                             "source_update {efectos}).")}}}}},
                         {"required", {"type", "name"}}};
        t.examples = {
            R"({"type": "image_source", "name": "Logo", "settings": {"file": "/home/yo/logo.png"}, "layout": "pip-tr"})"};
        t.run = [&rt, sceneOr, sceneExists, noScene](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            if (!sceneExists(scene)) return done(noScene(scene));
            SourceSpec spec;
            spec.id = a["type"];
            spec.name = a["name"];
            spec.settingsJson = a.contains("settings") ? a["settings"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : "{}";
            spec.visible = a.value("visible", true);
            spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            if (a.contains("efectos")) {
                std::string e;
                if (!spec.efectos.aplicar(a["efectos"], &e)) return done(ToolResult::error(e));
            }
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Agregada '%1' a '%2'."), {spec.name, scene})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "captura_opciones";
        t.title = ba::i18n::traducir("mcp", "Qué pantallas y qué aplicaciones se pueden capturar");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Las PANTALLAS conectadas (para screen_capture: su `monitor`) y las VENTANAS de aplicaciones abiertas "
            "(para window_capture: su `ventana`, que es «aplicación\\ntítulo»), y si este sistema puede capturar "
            "una ventana sola. Para elegir antes de crear_fuente.");
        t.examples = {R"({})"};
        t.run = [](const json&, Done done) {
            json ps = json::array(), vs = json::array();
            for (const auto& p : video::pantallasDelEquipo())
                ps.push_back({{"monitor", p.nombre}, {"etiqueta", p.etiqueta}, {"principal", p.principal},
                              {"ancho", p.pxAncho}, {"alto", p.pxAlto}});
            for (const auto& w : video::ventanasParaElegir()) vs.push_back(w);
            std::string porque;
            const bool hay = ba::motor::hayCapturaDeVentana(&porque);
            json d{{"pantallas", ps}, {"ventanas", vs}, {"captura_de_ventana", hay}};
            if (!hay) d["por_que_no"] = porque;
            d["como"] = "crear_fuente {type: screen_capture, settings: {monitor}} o {type: window_capture, settings: {ventana}}";
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_remove";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Quitar fuente de una escena");
        t.category = "fuentes";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una fuente (puede estar al aire)");
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca la fuente de la escena (si esta en otras escenas, ahi sigue).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", sceneProp}, {"name", {{"type", "string"}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, sceneOr](const json& a, Done done) {
            if (!rt.removeSource(sceneOr(a), a["name"])) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Quitada: %1"), {a["name"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_update";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Modificar una fuente en una escena");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Cambia una fuente: mostrar/ocultar, mover o cambiar tamaño (layout), orden (order), cambiar "
            "el texto de un titulo o zocalo, la imagen, transparencia, borde difuso o sombra (efectos), etc. "
            "De una vez lo que se indique: visibilidad, ubicacion (layout), orden (order), "
            "settings (se MEZCLAN con los actuales), efectos (se MEZCLAN), volumen, silencio o nombre.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", sceneProp},
              {"name", {{"type", "string"}}},
              {"visible", {{"type", "boolean"}}},
              {"layout", layoutProp},
              {"order",
               {{"type", json::array({"string", "integer"})},
                {"description", QT_TRANSLATE_NOOP("mcp", "top | bottom | up | down | pasos (+ sube).")}}},
              {"settings", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "Solo las claves a cambiar.")}}},
              {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
              {"muted", {{"type", "boolean"}}},
              {"rename", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Nuevo nombre (en todas las escenas).")}}},
              {"recorte",
               {{"type", "object"},
                {"description",
                 QT_TRANSLATE_NOOP("mcp", "Lo que se ve de la fuente en ESTA escena: {izquierda, arriba, derecha, abajo} en fracciones de "
                 "su tamaño (0..1). Con el layout, amplía una zona (p. ej. comparar el mismo detalle). Todo en 0 "
                 "lo saca.")},
                {"properties",
                 {{"izquierda", {{"type", "number"}}},
                  {"arriba", {{"type", "number"}}},
                  {"derecha", {{"type", "number"}}},
                  {"abajo", {{"type", "number"}}}}}}},
              {"efectos",
               {{"type", "object"},
                {"description",
                 QT_TRANSLATE_NOOP("mcp", "Efectos de la fuente en ESTA escena (solo lo que se indique; lo demas queda). Medidas en pixeles "
                 "del lienzo. opacidad 0..1 (transparencia de todo el elemento); borde: borde difuso (pixeles que se "
                 "funden a transparente); esquinas: radio de las esquinas redondeadas; sombra: {activa, color \"#RRGGBB\", alfa 0..1 (su opacidad), difuminado "
                 "(radio), distancia, angulo (hacia donde cae: 0 derecha, 90 abajo, 45 abajo a la derecha), tamano "
                 "(mas grande o mas chica que el elemento)}. Pasar numeros de la sombra la prende; "
                 "{\"sombra\": false} la apaga. Resplandor: distancia 0, color claro, difuminado grande.")},
                {"properties",
                 {{"opacidad", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                  {"borde", {{"type", "number"}, {"minimum", 0}}},
                  {"esquinas", {{"type", "number"}, {"minimum", 0}}},
                  {"sombra",
                   {{"type", json::array({"object", "boolean"})},
                    {"properties",
                     {{"activa", {{"type", "boolean"}}},
                      {"color", {{"type", "string"}}},
                      {"alfa", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                      {"difuminado", {{"type", "number"}, {"minimum", 0}}},
                      {"distancia", {{"type", "number"}, {"minimum", 0}}},
                      {"angulo", {{"type", "number"}}},
                      {"tamano", {{"type", "number"}}}}}}}}}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Logo", "layout": "pip-br", "visible": true})",
                      R"({"name": "Camara", "efectos": {"sombra": {"alfa": 0.7, "difuminado": 25, "distancia": 12, "angulo": 45}}})",
                      R"({"name": "Zocalo", "efectos": {"opacidad": 0.85, "borde": 20}})",
                      R"({"name": "Titulo principal", "settings": {"text": "Ultimo momento"}})",
                      R"({"name": "Camara", "order": "top"})"};
        t.run = [&rt, sceneOr](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            std::string name = a["name"];
            const auto items = rt.engine().sceneSources(scene);
            if (std::find(items.begin(), items.end(), name) == items.end())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' no esta en la escena '%2'. escena_fuentes_ver la muestra."),
                                                               {name, scene})));
            std::vector<std::string> did;
            std::vector<std::string> failed;
            if (a.contains("visible")) {
                (rt.setSourceVisible(scene, name, a["visible"]) ? did : failed).push_back("visible");
            }
            if (a.contains("layout")) {
                CanvasRect r;
                std::string err;
                std::string nota;
                if (!parseLayout(a["layout"], r.x, r.y, r.w, r.h, err, &nota)) return done(ToolResult::error(err));
                (rt.setSourceRect(scene, name, r) ? did : failed).push_back(nota.empty() ? "layout" : "layout (" + nota + ")");
            }
            if (a.contains("recorte") && a["recorte"].is_object()) {
                const json& c = a["recorte"];
                (rt.setSourceCrop(scene, name, c.value("izquierda", 0.0f), c.value("arriba", 0.0f),
                                  c.value("derecha", 0.0f), c.value("abajo", 0.0f))
                     ? did
                     : failed)
                    .push_back("recorte");
            }
            if (a.contains("efectos")) {
                EfectosCapa ef = rt.engine().sourceEfectos(scene, name);
                std::string err;
                if (!ef.aplicar(a["efectos"], &err))
                    failed.push_back("efectos (" + err + ")");
                else
                    (rt.setSourceEfectos(scene, name, ef) ? did : failed).push_back("efectos");
            }
            if (a.contains("order")) {
                const json& o = a["order"];
                int steps = 0;
                if (o.is_number())
                    steps = o.get<int>();
                else if (o == "up")
                    steps = 1;
                else if (o == "down")
                    steps = -1;
                else if (o == "top")
                    steps = static_cast<int>(items.size());
                else if (o == "bottom")
                    steps = -static_cast<int>(items.size());
                for (int i = 0; i < std::abs(steps); ++i)
                    rt.moveSource(scene, name, steps > 0 ? 1 : -1);
                did.push_back("order");
            }
            if (a.contains("settings")) {
                json cur = json::parse(rt.engine().sourceSettingsJson(name), nullptr, false);
                if (!cur.is_object()) cur = json::object();
                cur.merge_patch(a["settings"]);
                (rt.updateSourceSettings(name, cur.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) ? did : failed).push_back("settings");
            }
            if (a.contains("volumeDb")) {
                const float mul = static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0));
                (rt.setVolume(name, mul) ? did : failed).push_back("volumen");
            }
            if (a.contains("muted")) (rt.setMuted(name, a["muted"]) ? did : failed).push_back("silencio");
            if (a.contains("rename")) {
                const std::string to = a["rename"];
                if (rt.renameSource(name, to)) {
                    name = to;
                    did.push_back("nombre");
                } else {
                    failed.push_back("nombre (" + rt.lastError() + ")");
                }
            }
            std::string msg = "'" + name + "': ";
            for (const auto& d : did)
                msg += d + " ✓ ";
            for (const auto& f : failed)
                msg += f + " ✗ ";
            done(failed.empty() ? ToolResult::ok(msg) : ToolResult::error(msg));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_properties";
        t.title = ba::i18n::traducir("mcp", "Propiedades de una fuente");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Todas las opciones que la fuente expone (las mismas del panel Propiedades): nombre, "
            "etiqueta, tipo, valor actual, opciones posibles, rango. Sirve para cualquier plugin.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        // Las mismas filas que el panel (leídas en el dueño del motor), sin tocar el panel.
        t.run = [this, &rt](const json& a, Done done) {
            const std::string n = a["name"];
            PropertiesModel::leerFilas(vm_.ventanilla(), rt, this, n,
                                       [n, done](const PropertiesModel::Lectura& l, std::vector<PropertiesModel::Row> filas) {
                if (!l.existe)
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente '%1'."), {n})));
                done(ToolResult().data(
                    json{{"source", n}, {"type", l.tipo}, {"properties", PropertiesModel::filasJson(filas)}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_set_property";
        t.title = ba::i18n::traducir("mcp", "Cambiar una propiedad (o apretar un botón)");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Cambia una propiedad de source_properties por su `name` (se aplica en vivo), o aprieta una "
            "propiedad tipo boton (press=true), p. ej. 'refrescar dispositivos'.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", {{"type", "string"}}},
                           {"property", {{"type", "string"}}},
                           {"value", {{"description", QT_TRANSLATE_NOOP("mcp", "Valor (tipo segun la propiedad).")}}},
                           {"press", {{"type", "boolean"}}}}},
                         {"required", {"source", "property"}}};
        // Como el panel (las reglas de PropertiesModel; el cambio, en el dueño del motor).
        t.run = [this, &rt](const json& a, Done done) {
            const bool apretar = a.value("press", false);
            if (!apretar && !a.contains("value"))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta `value` (o press=true).")));
            PropertiesModel::cambiar(vm_.ventanilla(), rt, this, a["source"], a["property"],
                                     apretar ? QVariant() : jsonToQv(a["value"]), apretar,
                                     [done](const std::string& e, const std::string& ajustes) {
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho. Settings ahora: %1"), {ajustes})));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guest_source_add";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Poner a un invitado en una escena");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Agrega la camara (o la pantalla compartida) del invitado del lugar `slot` a una escena. Es "
            "la misma fuente en todas las escenas; se ve quien este en ese lugar de la conferencia.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", sceneProp},
              {"slot", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}},
              {"kind", {{"type", "string"}, {"enum", {"camera", "screen"}}, {"default", "camera"}}},
              {"layout", layoutProp}}},
            {"required", {"slot"}}};
        t.run = [&rt, sceneOr](const json& a, Done done) {
            const int slot = a["slot"];
            if (slot > rt.config().guests)
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Hay %1 lugares de invitado (ajuste guests.count)."),
                                                               {std::to_string(rt.config().guests)})));
            const bool share = a.value("kind", std::string("camera")) == "screen";
            SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                        "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
            if (!share) spec.rect = CanvasRect{0.5f, 0.5f, 0.5f, 0.5f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            const std::string scene = sceneOr(a);
            const auto items = rt.engine().sceneSources(scene);
            if (std::find(items.begin(), items.end(), spec.name) != items.end()) {
                rt.setSourceRect(scene, spec.name, spec.rect);
            } else if (!rt.addSource(scene, spec)) {
                return done(ToolResult::error(rt.lastError()));
            }
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' en '%2'."), {spec.name, scene})));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
