// ============================================================================
// src/app/mcp/McpPluginTools.cpp — los plugins de Python, para el asistente
//
// Cuando algo no se puede con lo que hay (una cámara con un PTZ propio, un
// equipo que manda datos raros, dos 360 que hay que juntar), el asistente
// ESCRIBE un plugin: plugin_escribir -> plugin_probar (sin la app: informe)
// -> plugin_activar -> lo usa (sus entradas ya son entradas, su PTZ ya es un
// PTZ, sus tarjetas van con plugin_tarjeta, sus controles con plugin_control).
// Ayuda: `help {"topic": "plugins"}`. Ver docs/PLUGINS.md.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "plugins/Plugins.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/StudioViewModel.hpp"

#include <QPointer>

namespace ba::app::mcp {

void McpService::registerPluginTools() {
    plugins::Plugins* pl = vm_.plugins();
    if (pl == nullptr) return;
    const QPointer<plugins::Plugins> p = pl;
    core_.addCategory("plugins", "Plugins de Python",
                      "Programas en Python que le suman al sistema lo que no trae: equipos raros (PTZ por USB/HID, "
                      "cámaras 360, datos de quién habla), tarjetas de video y audio, entradas generadas, y que pueden "
                      "manejar todo el sistema. Los escribe el asistente.");
    const json id = {{"type", "string"}, {"description", "El id del plugin (su carpeta: minúsculas, números y guiones)."}};
    {
        Tool t;
        t.name = "plugin_listar";
        t.title = "Plugins instalados y ejemplos";
        t.category = "plugins";
        t.readOnly = true;
        t.description =
            "Los plugins (estado: corriendo, apagado, caído…, lo que declararon: entradas, tarjetas, PTZ, controles) y "
            "los ejemplos para copiar. Para CREAR uno: help {\"topic\": \"plugins\"}.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [p](const json&, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            done(ToolResult().data({{"plugins", p->listar()},
                                    {"ejemplos", p->ejemplosDisponibles()},
                                    {"carpeta", p->carpeta().string()},
                                    {"python", p->python().string()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_ver";
        t.title = "Ver un plugin (o un ejemplo)";
        t.category = "plugins";
        t.readOnly = true;
        t.description =
            "El manifiesto (plugin.json), la lista de archivos y el contenido de `archivo` (de fábrica, el principal) "
            "de un plugin o de un EJEMPLO (con su nombre: «ejemplo-basico», «ejemplo-ptz-uvc»…). Leé un ejemplo antes "
            "de escribir uno nuevo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", id}, {"archivo", {{"type", "string"}, {"description", "ruta relativa (de fábrica, plugin.py)"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const json r = p->ver(a.value("id", std::string()), a.value("archivo", std::string()));
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_escribir";
        t.title = "Crear o cambiar un plugin";
        t.category = "plugins";
        t.description =
            "Escribe los `archivos` del plugin `id` ({\"plugin.py\": \"...código...\"}; pisa los que nombres, deja los "
            "demás) y mezcla `manifiesto` en su plugin.json (nombre, descripcion, version, dependencias: [paquetes de "
            "pip], config: {...}, escena: dónde van sus entradas). Vive en Datos/Plugins/<id> (sobrevive a las "
            "actualizaciones). Después: plugin_probar, y si anda, plugin_activar (si ya corría, plugin_reiniciar). "
            "Cómo se escribe: help {\"topic\": \"plugins\"} y los ejemplos (plugin_ver).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", id},
                           {"archivos", {{"type", "object"}, {"description", "ruta relativa -> contenido (texto)"}}},
                           {"manifiesto", {{"type", "object"}}}}}};
        t.examples = {R"({"id": "ptz-rally", "manifiesto": {"nombre": "PTZ Logitech Rally", "dependencias": ["pyusb", "libusb-package"]}, "archivos": {"plugin.py": "import ba_plugin as ba\nplugin = ba.Plugin()\n...\nplugin.correr()\n"}})"};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            json hecho;
            const std::string e = p->escribir(a.value("id", std::string()), a.value("archivos", json::object()), a.value("manifiesto", json::object()), &hecho);
            if (!e.empty()) return done(ToolResult::error(e));
            hecho["siguiente"] = "plugin_probar {\"id\": \"" + a.value("id", std::string()) + "\"}";
            done(ToolResult().data(hecho));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_desde_ejemplo";
        t.title = "Copiar un ejemplo como plugin nuevo";
        t.category = "plugins";
        t.description = "Copia el `ejemplo` (plugin_listar los lista) a un plugin nuevo `id`, para cambiarlo con plugin_escribir.";
        t.inputSchema = {{"type", "object"}, {"required", {"ejemplo", "id"}}, {"properties", {{"ejemplo", {{"type", "string"}}}, {"id", id}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string e = p->desdeEjemplo(a.value("ejemplo", std::string()), a.value("id", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(p->ver(a.value("id", std::string()), "")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_probar";
        t.title = "Probar un plugin sin tocar el sistema";
        t.category = "plugins";
        t.readOnly = true;
        t.description =
            "Baja sus dependencias y lo corre APARTE (no toca lo que está al aire): junta lo que declara, le pasa "
            "cuadros y audio de prueba a cada tarjeta (cuánto tarda, qué cambió), recibe `segundos` de cada entrada "
            "que manda cuadros (cuántos por segundo, tamaño, color), llama los `controles` que pidas, anota eventos y "
            "llamadas al sistema (no las ejecuta), y devuelve su registro (errores de Python incluidos). `ptz: true` "
            "mueve el PTZ un instante (el equipo de verdad). Si `ok` es false, arreglá con plugin_escribir y probá de "
            "nuevo. Las entradas `gst` (pipeline de GStreamer) se prueban al activarlo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", id},
                           {"segundos", {{"type", "number"}, {"description", "cuánto escuchar cada entrada (3 de fábrica)"}}},
                           {"controles",
                            {{"type", "array"},
                             {"items", {{"type", "object"}, {"properties", {{"nombre", {{"type", "string"}}}, {"args", {{"type", "object"}}}}}}}}},
                           {"ptz", {{"type", "boolean"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            p->probar(a.value("id", std::string()), a.value("segundos", 3.0), a.value("controles", json::array()), a.value("ptz", false),
                      [done](json r) { done(ToolResult().data(r)); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_activar";
        t.title = "Prender o apagar un plugin";
        t.category = "plugins";
        t.description =
            "Prende (`activo`, de fábrica true) o apaga el plugin, y queda así al reiniciar la app. Al prenderse: sus "
            "entradas aparecen como entradas de video/audio (en la `escena` que declaren o en «Plugins»; nunca la "
            "que está al aire), su PTZ queda como el de esa entrada, y sus tarjetas se pueden poner con "
            "plugin_tarjeta. Si se cae se reinicia solo (5 caídas en un minuto: queda apagado). Devuelve el estado "
            "cuando terminó de arrancar (o el error con el registro).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}, {"activo", {{"type", "boolean"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string i = a.value("id", std::string());
            p->activar(i, a.value("activo", true), [p, i, done](json e) {
                if (e.value("estado", "") != "corriendo" && e.value("estado", "") != "apagado" && p)
                    e["registro"] = p->registro(i, 30);
                done(ToolResult().data(e));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_reiniciar";
        t.title = "Reiniciar un plugin";
        t.category = "plugins";
        t.description = "Lo cierra y lo vuelve a arrancar (después de cambiar su código con plugin_escribir).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string i = a.value("id", std::string());
            p->reiniciar(i, [p, i, done](json e) {
                if (e.value("estado", "") != "corriendo" && p) e["registro"] = p->registro(i, 30);
                done(ToolResult().data(e));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_registro";
        t.title = "El registro de un plugin";
        t.category = "plugins";
        t.readOnly = true;
        t.description = "Las últimas `lineas` de lo que escribió (print, errores de Python, pip).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}, {"lineas", {{"type", "integer"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            done(ToolResult::ok(p->registro(a.value("id", std::string()), a.value("lineas", 60))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_control";
        t.title = "Pedirle algo a un plugin";
        t.category = "plugins";
        t.description =
            "Llama el control `control` del plugin (los que declaró con @plugin.control; plugin_listar los muestra) con "
            "`args`, y devuelve lo que conteste. Sin `control`: le pide `info` (lo que declaró y cómo andan sus medios).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", id}, {"control", {{"type", "string"}}}, {"args", {{"type", "object"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string c = a.value("control", std::string());
            auto fin = [done](json r) {
                if (!r.value("ok", false)) return done(ToolResult::error(r.value("error", std::string("falló"))));
                done(ToolResult().data(r.contains("resultado") ? r["resultado"] : json::object()));
            };
            if (c.empty()) p->pedir(a.value("id", std::string()), "info", json::object(), fin);
            else p->pedir(a.value("id", std::string()), "control", {{"nombre", c}, {"args", a.value("args", json::object())}}, fin, 30000);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_panel";
        t.title = "El panel de un plugin (lo mismo que ve el operador)";
        t.category = "plugins";
        t.description =
            "Sin `clave`: el panel del plugin (sus controles: deslizador, interruptor, opcion, texto, numero, boton, "
            "indicador, medidor) con los valores de ahora. Con `clave` y `valor`: mueve ese control (o toca el botón, "
            "sin `valor`) como si fuera el operador; queda guardado. Los indicadores y medidores los pone el plugin.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", id}, {"clave", {{"type", "string"}}}, {"valor", json::object()}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string pid = a.value("id", std::string()), clave = a.value("clave", std::string());
            if (!clave.empty()) {
                if (const std::string e = p->ponerValor(pid, clave, a.contains("valor") ? a["valor"] : json(true)); !e.empty())
                    return done(ToolResult::error(e));
            }
            const json pn = p->panel(pid);
            if (pn.is_null()) return done(ToolResult::error("«" + pid + "» no tiene panel (o no está prendido)"));
            done(ToolResult().data({{"panel", pn}, {"valores", p->valores(pid)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_eventos";
        t.title = "Lo que publican los plugins (canales de datos)";
        t.category = "plugins";
        t.readOnly = true;
        t.description =
            "El último evento de cada canal (p. ej. «kandao-1/hablando») y los más recientes, de un plugin (`id`) o de "
            "todos, de un `canal` o de todos.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", id}, {"canal", {{"type", "string"}}}, {"cuantos", {{"type", "integer"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            done(ToolResult().data(p->eventos(a.value("id", std::string()), a.value("canal", std::string()), a.value("cuantos", 20))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_tarjeta";
        t.title = "Poner una tarjeta de un plugin";
        t.category = "plugins";
        t.description =
            "Una tarjeta de VIDEO del plugin al final del sub-grafo de la `entrada` (como cualquier tarjeta: se mueve, "
            "se ajusta y se saca con las herramientas de video; también va en video_pipeline con `tipo: \"plugin\"`), "
            "o una de AUDIO como filtro de la `fuente` (un micrófono, una entrada con audio). `parametros`: sus "
            "ajustes. El plugin tiene que estar corriendo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "tarjeta"}},
                         {"properties",
                          {{"id", id},
                           {"tarjeta", {{"type", "string"}}},
                           {"entrada", {{"type", "string"}, {"description", "para una de video: la entrada"}}},
                           {"fuente", {{"type", "string"}, {"description", "para una de audio: la fuente"}}},
                           {"parametros", {{"type", "object"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            json hecho;
            std::string e;
            if (!a.value("fuente", std::string()).empty())
                e = p->ponerFiltroAudio(a.value("id", std::string()), a.value("tarjeta", std::string()), a.value("fuente", std::string()),
                                        a.value("parametros", json::object()), &hecho);
            else
                e = p->ponerTarjeta(a.value("id", std::string()), a.value("tarjeta", std::string()), a.value("entrada", std::string()),
                                    a.value("parametros", json::object()), &hecho);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(hecho));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_credenciales";
        t.title = "Pedir las claves de un plugin";
        t.category = "plugins";
        t.description =
            "Muestra al operador UN formulario seguro en la barra con las claves que declara el plugin (\"secretos\" en "
            "plugin.json: [{\"clave\": \"clave\", \"etiqueta\": \"Clave de la cámara\"}]) y vuelve enseguida. Se guardan "
            "cifradas; vos nunca las ves; el plugin las lee con plugin.secreto(\"clave\") (None si todavía no están). "
            "Decile antes al operador de dónde sale cada una. Nunca pidas una clave por el chat.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}, {"texto", {{"type", "string"}}}}}};
        auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
        t.run = [p, msgs](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            if (msgs == nullptr) return done(ToolResult::error("no hay barra del operador"));
            const std::string i = a.value("id", std::string());
            const json sec = p->secretosDe(i);
            if (sec.empty())
                return done(ToolResult::error("el plugin «" + i + "» no declara claves: agregá \"secretos\" a su plugin.json con plugin_escribir"));
            json campos = json::array();
            for (const auto& x : sec)
                campos.push_back({{"key", x.value("clave", "")},
                                  {"label", x.value("etiqueta", "") + (x.value("cargada", false) ? " (ya cargada: vacío = no cambiar)" : "")},
                                  {"secret", true}});
            const int n = msgs->post({{"kind", "credential"},
                                      {"platform", "plugin:" + i},
                                      {"priority", "info"},
                                      {"title", "Claves del plugin «" + i + "»"},
                                      {"text", a.value("texto", std::string("Se guardan cifradas en este equipo; el asistente no las ve."))},
                                      {"fields", campos}});
            done(ToolResult::ok("Formulario en la barra del operador (id " + std::to_string(n) +
                                "). Cuando lo complete te llega a tu chat; el plugin las lee con plugin.secreto()."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_exportar";
        t.title = "Exportar un plugin (para otro aparato)";
        t.category = "plugins";
        t.description = "Arma un .zip con el plugin (código, plugin.json y sus archivos; sin las bibliotecas ni sus datos) en Datos/Exportado. "
                        "En el otro aparato: plugin_importar.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            p->exportar(a.value("id", std::string()), [done](json r) {
                if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_importar";
        t.title = "Instalar un plugin desde un .zip";
        t.category = "plugins";
        t.description = "Instala el plugin de un .zip exportado (plugin_exportar). Si ya existe, `reemplazar: true` pisa su código (conserva "
                        "_libs y datos). Queda apagado: después plugin_probar y plugin_activar.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties", {{"archivo", {{"type", "string"}, {"description", "ruta del .zip"}}}, {"reemplazar", {{"type", "boolean"}}}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            p->importar(a.value("archivo", std::string()), a.value("reemplazar", false), [done](json r) {
                if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plugin_borrar";
        t.title = "Borrar un plugin";
        t.category = "plugins";
        t.destructive = true;
        t.description = "Lo apaga y mueve su carpeta a Plugins/.papelera (no se pierde el código). Sus entradas quedan sin señal.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [p](const json& a, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            const std::string e = p->borrar(a.value("id", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Borrado (está en Plugins/.papelera)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dispositivos_listar";
        t.title = "Qué equipos hay conectados (cámaras, micrófonos, USB, HID, serie)";
        t.category = "plugins";
        t.readOnly = true;
        t.description =
            "Las cámaras y micrófonos que ve GStreamer, cada uno con el `gst` listo para el pipeline de una entrada "
            "(p. ej. «avfvideosrc device-index=1») y sus formatos; y los equipos USB (vid:pid, marca, si es UVC), HID "
            "y puertos serie. Es el primer paso para un equipo raro: saber qué es y por dónde se le habla. La primera "
            "vez baja pyusb/hidapi/pyserial (unos segundos).";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [p](const json&, Done done) {
            if (!p) return done(ToolResult::error("los plugins no están disponibles"));
            p->dispositivos([done](json r) { done(ToolResult().data(r)); });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
