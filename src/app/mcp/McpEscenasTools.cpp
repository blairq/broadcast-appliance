// ============================================================================
// src/app/mcp/McpEscenasTools.cpp — Herramientas MCP del motor: escenas: listar, crear, copiar, ordenar, la transición, el bus de preview (armar, mirar, TAKE).
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "mcp/McpFamilia.hpp"
#include "escenas/Conmutador.hpp"
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
#include "mcp/McpToolsCommon.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerEscenasTools() {
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;
    // Por el nombre (lo de siempre) o por el id estable que da scene_list.
    const auto sceneExists = [&rt](const std::string& s) { return !rt.engine().sceneId(s).empty(); };
    const auto noScene = [&rt](const std::string& s) {
        std::string names;
        for (const auto& n : rt.engine().sceneNames())
            names += (names.empty() ? "" : ", ") + n;
        return ToolResult::error(
            ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'. Escenas: %2."), {s, names}));
    };
    const json layoutProp = {
        {"description", QT_TRANSLATE_NOOP("mcp", "Ubicacion: preset (full, left, right, top, bottom, center, top-left, top-right, "
                        "bottom-left, bottom-right, pip-tl, pip-tr, pip-bl, pip-br, lower-third, third-1..3, "
                        "grid-<n>-<i>) u objeto {x,y,w,h} en fracciones del lienzo (0..1).")}};

    // --- Escenas -------------------------------------------------------------------
    // Las escenas, las fuentes, la transición y el volumen son del motor y de cfg_, que son
    // del plano de control (F3-03): esas herramientas corren en su hilo, con el turno, y no
    // esperan a la interfaz (Hilo::Control). Las que leen objetos de la interfaz (las
    // propiedades de una fuente, los niveles del mezclador) siguen en ella.
    {
        Tool t;
        t.name = "scene_list";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Listar escenas");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "El listado de escenas que existen: cual esta al aire, en que carpeta esta cada una. "
            "Para el contenido de UNA escena, capa por capa, no es esta. "
            "Todas las escenas con sus capas (de abajo hacia arriba), cual esta al aire y en que "
            "carpeta esta cada una (`folder`, \"\" = suelta). Las carpetas solo ordenan la lista de la "
            "pestaña Escenas: no cambian nada de la emision.\n\n"
            "`live` es la que esta AL AIRE; `armada` es la del bus de preview, la que sale con el "
            "proximo `preview_al_aire`. Son los dos buses de una mesa de switcher. Con `salida`, los de esa salida "
            "(cada una tiene su aire y su armada; las escenas son de todas).\n\n"
            "Cada escena trae su `id`, que no cambia al renombrarla: donde va el nombre de una "
            "escena también se puede pasar el id.");
        t.run = [this, &rt, &vm](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            const std::string armada = conmutadorDe(core_, vm, a).armada();
            const std::string alAire = rt.engine().currentScene(salida);
            json scenes = json::array();
            for (const std::string& n : rt.orderedSceneNames()) {
                scenes.push_back(json{{"name", n},
                                      {"id", rt.engine().sceneId(n)},
                                      {"live", n == alAire},
                                      {"armada", n == armada},
                                      {"folder", rt.sceneFolder(n)},
                                      {"sources", rt.engine().sceneSources(n)}});
            }
            done(ToolResult().data(json{{"current", alAire},
                                        {"armada", armada},
                                        {"folders", rt.sceneFolders()},
                                        {"scenes", scenes}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_folder";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Mover una escena a una carpeta");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "QUE ES: las carpetas agrupan escenas en la lista de la pestaña Escenas (arbol que se "
            "pliega). PARA QUE: ordenar cuando hay muchas (bloques del programa, placas, invitados). "
            "COMO SE USA: `folder` con el nombre de la carpeta (se crea sola si no existe) o vacio para "
            "sacar la escena de toda carpeta. No cambia nada de la emision ni del contenido de la "
            "escena; `scene_list` informa la carpeta de cada una.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", {{"type", "string"}}}, {"folder", {{"type", "string"}}}}},
                         {"required", {"scene"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.setSceneFolder(a.value("scene", ""), a.value("folder", "")))
                return done(ToolResult::error(rt.lastError()));
            done(ToolResult().data(json{{"scene", a.value("scene", "")},
                                        {"folder", a.value("folder", "")},
                                        {"folders", rt.sceneFolders()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_move";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Reordenar una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "QUE ES: cambia el ORDEN de la lista de escenas (y de paso la carpeta). PARA QUE: dejar las "
            "escenas en el orden del programa. COMO SE USA: `before` es la escena delante de la cual "
            "queda (vacio = al final); ademas hereda su carpeta. No cambia nada de la emision. El orden "
            "que informa `scene_list` es el que ve el operador.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", {{"type", "string"}}}, {"before", {{"type", "string"}}}}},
                         {"required", {"scene"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.moveScene(a.value("scene", ""), a.value("before", "")))
                return done(ToolResult::error(rt.lastError()));
            done(ToolResult().data(json{{"orden", rt.orderedSceneNames()}}));
        };
        core_.addTool(std::move(t));
    }
    // --- El bus de preview: armar, mirar, TAKE --------------------------------
    {
        Tool t;
        t.name = "escena_transicion";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Transición de entrada de una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "La transicion con la que ENTRA la `escena` al aire (`tipo`: cut, fade, swipe, slide, "
                        "fade_to_color; vacio = la global) y su duracion `ms`. Un take con transicion explicita manda sobre esta.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"escena"}},
                         {"properties",
                          {{"escena", {{"type", "string"}}},
                           {"tipo", {{"type", "string"}, {"enum", {"", "cut", "fade", "swipe", "slide", "fade_to_color"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.conmutador().setTransicionEntrada(a.value("escena", std::string()),
                                                                       a.value("tipo", std::string()), a.value("ms", -1));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_afv";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Audio que sigue al video");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Con `sigue_video`, la `fuente` sube (a su volumen) cuando entra al aire una escena que la tiene y "
                        "baja a cero cuando sale, con la duracion de la transicion. Sin `sigue_video`, lista las que lo tienen. "
                        "Es de cada salida: mueve el fader de la fuente en la mezcla de esa `salida`.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"fuente", {{"type", "string"}}}, {"sigue_video", {{"type", "boolean"}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            escenas::Conmutador& c = conmutadorDe(core_, vm, a);
            if (a.contains("fuente") && a.contains("sigue_video"))
                c.setAfv(a.value("fuente", std::string()), a.value("sigue_video", false));
            json l = json::array();
            for (const std::string& f : c.afvFuentes()) l.push_back(f);
            done(ToolResult().data({{"sigue_video", l}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_create";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Crear escena vacia");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea una escena vacia. Para crearla con contenido de una vez, usa scene_define.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.addScene(a["name"].get<std::string>())) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena creada: %1"), {a["name"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_copy";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Copiar una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea una escena nueva (`to`) con todas las fuentes de otra (`from`), recreadas: las dos quedan "
                                                 "independientes.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Es como se arman las escenas parecidas sin repetir el trabajo: se hace UNA bien —la de "
                    "entrevista, con su camara, su zocalo y su fondo— y las demas salen de copiarla y "
                    "cambiarle lo que difiere.\n\n"
                    "Las fuentes se RECREAN, no se comparten: quedan dos escenas de verdad independientes, y "
                    "mover algo en la copia no mueve el original.\n\n"
                    "La copia nunca queda protegida, aunque el original lo este: para eso se copia. Si "
                    "alguna fuente no se pudo recrear, la escena igual queda creada y se dice cual fallo: "
                    "una escena a medias se arregla a mano, una que desaparecio sin explicacion no.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"from", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "La escena que se copia.")}}},
                           {"to", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Nombre de la copia.")}}}}},
                         {"required", {"from", "to"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string from = a["from"].get<std::string>();
            const std::string to = a["to"].get<std::string>();
            if (!rt.copyScene(from, to)) return done(ToolResult::error(rt.lastError()));
            // `lastError` con la copia hecha = alguna fuente no se pudo
            // recrear. Se dice, pero no es un error: la escena existe.
            const std::string aviso = rt.lastError();
            done(ToolResult::ok(
                aviso.empty() ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena «%1» copiada de «%2»."), {to, from})
                              : ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena «%1» copiada de «%2». Ojo: %3"),
                                                 {to, from, aviso})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_protect";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Proteger una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Protege una escena (o la desprotege, con `protected`: false): protegida no se puede borrar ni renombrar.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Para la escena BASE de la que se copian las demas —la que si alguien borra se pierde el "
                    "trabajo de todos— y para las que no se tocan en vivo.\n\n"
                    "Copiarla si se puede, y editar sus fuentes tambien: lo que se protege es que "
                    "desaparezca o cambie de nombre, que es lo que rompe todo lo que la nombra (el guion, "
                    "los vinculos de rotulos, las recetas).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", {{"type", "string"}}}, {"protected", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"scene"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string s = a["scene"].get<std::string>();
            const bool p = a.value("protected", true);
            if (!rt.setSceneProtected(s, p)) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(p ? ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» queda protegida: no se borra ni se renombra."), {s})
                                  : ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» ya no esta protegida."), {s})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_delete";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Borrar escena");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una escena");
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra una escena (las fuentes compartidas con otras escenas siguen existiendo).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            if (!rt.removeScene(name)) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena borrada: %1"), {name})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_rename";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Renombrar escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"from", {{"type", "string"}}}, {"to", {{"type", "string"}}}}},
                         {"required", {"from", "to"}}};
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia el nombre de una escena (de `from` a `to`). Una protegida (scene_protect) no se puede renombrar. "
                                                 "Al aire no corta: la escena es la misma (su `id` no cambia) y lo que la nombra (la línea, "
                                                 "el DSK, los atajos) la sigue.");
        t.run = [&rt, sceneExists, noScene](const json& a, Done done) {
            if (!sceneExists(a["from"])) return done(noScene(a["from"]));
            if (!rt.renameScene(a["from"], a["to"])) return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Renombrada a %1"), {a["to"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_export";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Exportar definicion de escenas");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Definicion completa (tipo, settings, ubicacion, visibilidad, volumen) de una escena o de "
            "todas, en el mismo formato que acepta scene_define: sirve para copiar, versionar o "
            "modificar escenas.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Una escena (vacio = todas).")}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string only = a.value("name", std::string());
            json out = json::array();
            for (const SceneConfig& sc : rt.config().scenes) {
                if (!only.empty() && sc.name != only) continue;
                json sources = json::array();
                for (const SourceSpec& s : sc.sources) {
                    const std::string live = rt.engine().sourceSettingsJson(s.name);
                    sources.push_back(
                        json{{"type", s.id},
                             {"name", s.name},
                             {"settings", json::parse(live.empty() ? s.settingsJson : live, nullptr, false)},
                             {"layout", {{"x", s.rect.x}, {"y", s.rect.y}, {"w", s.rect.w}, {"h", s.rect.h}}},
                             {"visible", s.visible},
                             {"volume", s.volume},
                             {"muted", s.muted}});
                }
                out.push_back(json{{"name", sc.name}, {"sources", sources}});
            }
            if (!only.empty() && out.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'."), {only})));
            done(ToolResult().data(json{{"scenes", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_define";
        t.hilo = Hilo::Control;
        t.porSalida = true; // `switch`: al aire en esa salida
        t.title = ba::i18n::traducir("mcp", "Definir una escena completa");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Crea (o rehace con replace=true) una escena con todas sus fuentes de una vez, en orden de "
            "abajo hacia arriba. Una fuente con el nombre de otra que ya existe se COMPARTE (misma "
            "camara en dos escenas); si trae settings, se actualizan. Al rehacer, las que vuelven a venir "
            "en la lista se CONSERVAN (con su camara, grafo y filtros): solo se quitan las que no estan. "
            "`layout` en fracciones del lienzo (0..1); si llega en pixeles se convierte y se avisa.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Cada fuente: {type, name, settings?, layout?, visible?, volume?, muted?}. `type` es el id del "
            "tipo (ver source_types): p. ej. image_source {file}, color_source_v3 {color, width, "
            "height}, text_ft2_source_v2 {text, font:{face,size}, color1}, ffmpeg_source {local_file, "
            "looping}, v4l2_input (camara), ba_guest_video {slot, channel: guest|share}, ba_subtitulos "
            "{flujo: \"Orador · en\", "
            "lineas, caracteres, ancho} (subtitulos de una salida de texto). `layout`: preset "
            "o {x,y,w,h}. Consejo: exporta una escena con scene_export, modificala y pasala aca.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}}},
              {"sources",
               {{"type", "array"},
                {"description", QT_TRANSLATE_NOOP("mcp", "Fuentes de abajo hacia arriba.")},
                {"items",
                 {{"type", "object"},
                  {"properties",
                   {{"type", {{"type", "string"}}},
                    {"name", {{"type", "string"}}},
                    {"settings", {{"type", "object"}}},
                    {"layout", layoutProp},
                    {"visible", {{"type", "boolean"}}},
                    {"volume", {{"type", "number"}, {"description", QT_TRANSLATE_NOOP("mcp", "Ganancia lineal (1 = 0 dB).")}}},
                    {"muted", {{"type", "boolean"}}}}},
                  {"required", {"type", "name"}}}}}},
              {"replace",
               {{"type", "boolean"},
                {"description", QT_TRANSLATE_NOOP("mcp", "Si existe, rehacerla (las fuentes que se repiten se conservan).")},
                {"default", false}}},
              {"switch",
               {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "Ponerla al aire al terminar.")}, {"default", false}}}}},
            {"required", {"name", "sources"}}};
        // La tipografia del ejemplo sale de kDefaultFontFace: si aca quedara
        // fija en "Sans", el asistente copiaria el ejemplo y en Windows crearia
        // texto invisible (FT2 no resuelve ese alias de fontconfig).
        t.examples = {std::string(R"({"name": "Titulo", "sources": [
  {"type": "color_source_v3", "name": "Fondo azul", "settings": {"color": 4282400832, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "text_ft2_source_v2", "name": "Titulo principal", "settings": {"text": "Bienvenidos", "font": {"face": ")") +
                      kDefaultFontFace +
                      R"(", "size": 96}}, "layout": {"x": 0.1, "y": 0.4, "w": 0.8, "h": 0.2}}
], "switch": true})"};
        t.run = [this, &rt, &vm, sceneExists](const json& a, Done done) {
            const std::string name = a["name"];
            std::vector<std::string> log;
            // Las que vienen en la lista nueva: si ya están en la escena, se
            // CONSERVAN (vaciar y volver a agregar borraba una entrada que vivía
            // solo acá y la recreaba vacía, sin su cámara ni su grafo).
            std::set<std::string> listadas;
            for (const json& s : a["sources"])
                listadas.insert(s.value("name", std::string()));
            std::vector<std::string> antes;
            if (sceneExists(name)) {
                if (!a.value("replace", false))
                    return done(
                        ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La escena '%1' ya existe: usa replace=true para "
                                                                               "rehacerla, u otro nombre."),
                                                           {name})));
                antes = rt.engine().sceneSources(name);
                int quitadas = 0;
                for (const std::string& s : antes)
                    if (!listadas.count(s)) {
                        rt.removeSource(name, s);
                        ++quitadas;
                    }
                log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "rehecha (%1 quitada(s); las que siguen, conservadas)"), {std::to_string(quitadas)}));
            } else if (!rt.addScene(name)) {
                return done(ToolResult::error(rt.lastError()));
            }
            int added = 0;
            for (const json& s : a["sources"]) {
                SourceSpec spec;
                spec.id = s.value("type", std::string());
                spec.name = s.value("name", std::string());
                spec.settingsJson = s.contains("settings") ? s["settings"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : "{}";
                spec.visible = s.value("visible", true);
                spec.volume = s.value("volume", 1.0f);
                spec.muted = s.value("muted", false);
                std::string err, nota;
                if (s.contains("layout") &&
                    !parseLayout(s["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err, &nota)) {
                    log.push_back(spec.name + ": " + err);
                    continue;
                }
                if (!nota.empty()) log.push_back(spec.name + ": " + nota);
                if (std::find(antes.begin(), antes.end(), spec.name) != antes.end()) {
                    // Ya estaba en la escena: se le aplica lo que trae el pedido.
                    if (s.contains("layout")) rt.setSourceRect(name, spec.name, spec.rect);
                    rt.setSourceVisible(name, spec.name, spec.visible);
                    if (s.contains("settings")) {
                        json cur = json::parse(rt.engine().sourceSettingsJson(spec.name), nullptr, false);
                        if (!cur.is_object()) cur = json::object();
                        cur.merge_patch(s["settings"]);
                        rt.updateSourceSettings(spec.name, cur.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                    }
                    if (s.contains("volume")) rt.setVolume(spec.name, spec.volume);
                    if (s.contains("muted")) rt.setMuted(spec.name, spec.muted);
                    ++added;
                    log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: conservada (ya estaba)"), {spec.name}));
                    continue;
                }
                const bool existed = !rt.engine().sourceTypeId(spec.name).empty();
                if (!rt.addSource(name, spec)) {
                    log.push_back(spec.name + ": " + rt.lastError());
                    continue;
                }
                if (existed && s.contains("settings")) rt.updateSourceSettings(spec.name, spec.settingsJson);
                if (existed) {
                    rt.setVolume(spec.name, spec.volume);
                    rt.setMuted(spec.name, spec.muted);
                }
                ++added;
                if (existed) log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: compartida (ya existia)"), {spec.name}));
            }
            // El orden, el de la lista (de abajo hacia arriba): cada una, arriba de todo.
            {
                const auto ahora = rt.engine().sceneSources(name);
                for (const json& s : a["sources"]) {
                    const std::string n = s.value("name", std::string());
                    if (std::find(ahora.begin(), ahora.end(), n) == ahora.end()) continue;
                    for (std::size_t i = 0; i < ahora.size(); ++i)
                        rt.moveSource(name, n, 1);
                }
            }
            // Por el conmutador (modelo), como scene_switch: transición de entrada y AFV.
            if (a.value("switch", false))
                if (const std::string e = conmutadorDe(core_, vm, a).poner(name); !e.empty()) // `switch`: en su salida
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena armada, pero no salió al aire: %1"), {e})));
            std::string msg = ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena '%1': %2 fuente(s)."), {name, std::to_string(added)});
            for (const auto& l : log)
                msg += "\n- " + l;
            msg += "\n" + ba::i18n::llenar(ba::i18n::traducir("mcp", "Verifica como quedo con foto_fuente {\"name\": \"%1\"}."), {name});
            done(ToolResult::ok(msg));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transition_set";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Transición entre escenas");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia la transición");
        t.description = QT_TRANSLATE_NOOP("mcp", "Tipo (corte, fundido, barrido, deslizamiento o fundido a un color) y duración en ms de la transición al "
                                                 "cambiar de escena.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"type", {{"type", "string"}, {"enum", {"cut", "fade", "swipe", "slide", "fade_to_color"}}}},
              {"durationMs", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}}}}},
            {"required", {"type"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.setTransition(a["type"], a.value("durationMs", rt.config().transition.durationMs)))
                return done(ToolResult::error(rt.lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Transicion: %1"), {a["type"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
