// ============================================================================
// src/app/mcp/McpRedTools.cpp — encontrar cámaras en la red y conectarlas bien, por MCP
//
// El camino que sigue el asistente (y que la ayuda le cuenta):
//   red_escanear -> camara_credenciales {pedir} (el operador las tipea en un
//   formulario seguro: el asistente nunca ve la clave) -> camara_ficha (ONVIF
//   completo + análisis de GStreamer + plan) -> camara_agregar (la entrada
//   sincronizada, la normalización mínima y el PTZ) -> video_estado.
// Lo mismo que el botón «Buscar en la red» de Entradas (red/Red).
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "red/Red.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Diagnostico.hpp"
#include "video/VideoGrafos.hpp"

#include <QPointer>

namespace ba::app::mcp {

void McpService::registerRedTools() {
    red::Red* rd = vm_.redEquipos();
    if (rd == nullptr) return;
    const QPointer<red::Red> r = rd;
    auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
    video::VideoGrafos* vg = vm_.videoGrafos();
    const json ip = {{"type", "string"}, {"description", "La IP del equipo (como en red_escanear)."}};
    {
        Tool t;
        t.name = "red_escanear";
        t.title = "Buscar cámaras y equipos de video en la red";
        t.category = "entradas";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            "Escanea la red local (unos 10 s): ONVIF, los puertos de video de toda la subred (RTSP, web, RTMP, NDI, los "
            "SDK de Hikvision y Dahua), el fabricante por la MAC, la marca (con el porqué) y prueba sus URLs RTSP "
            "típicas (biblioteca propia: Axis, Hikvision y sus grabadores, EZVIZ, Dahua/Imou, Reolink, Uniview, Tapo, "
            "Foscam, Hanwha, Bosch, Vivotek, Panasonic, Sony…). Cada equipo trae `siguiente_paso`: SEGUILO. Si pide "
            "clave: camara_credenciales con `pedir` (el operador la escribe en un formulario seguro). Después "
            "camara_ficha y camara_agregar. `notas` dice lo propio de la marca (p. ej. EZVIZ: la clave es el código "
            "de la etiqueta y el RTSP se habilita en su app).";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [r](const json&, Done done) {
            if (!r) return done(ToolResult::error("el escáner no está disponible"));
            r->escanear([done](json res) {
                if (res.contains("error")) return done(ToolResult::error(res.value("error", std::string())));
                done(ToolResult().data(res));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_credenciales";
        t.title = "Usuario y clave de una cámara de la red";
        t.category = "entradas";
        t.description =
            "Con `pedir: true` (lo correcto): le muestra al operador un formulario seguro en la barra para que escriba "
            "usuario y clave de la cámara `ip` (vos no los ves; se guardan cifrados) y espera hasta `esperar_seg` "
            "(de fábrica 300). Decile antes cuál es el usuario de fábrica y de dónde sale la clave (las `notas` del "
            "escaneo: EZVIZ = código de verificación de la etiqueta). Con `usuario` y `clave`: los guarda directo "
            "(solo si el operador te los dio). Después: camara_ficha.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"ip"}},
                         {"properties",
                          {{"ip", ip},
                           {"pedir", {{"type", "boolean"}}},
                           {"texto", {{"type", "string"}, {"description", "lo que se le dice al operador en el formulario"}}},
                           {"esperar_seg", {{"type", "integer"}}},
                           {"usuario", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}}}}};
        t.run = [r, msgs](const json& a, Done done) {
            if (!r) return done(ToolResult::error("el escáner no está disponible"));
            const std::string i = a.value("ip", std::string());
            if (!a.value("pedir", false)) {
                if (!a.contains("usuario")) return done(ToolResult::error("falta `usuario` (o `pedir: true`)"));
                const QString e = r->guardarCredenciales(QString::fromStdString(i), QString::fromStdString(a.value("usuario", std::string())),
                                                         QString::fromStdString(a.value("clave", std::string())));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok("Guardadas (cifradas). Siguiente: camara_ficha {\"ip\": \"" + i + "\"}."));
            }
            if (msgs == nullptr) return done(ToolResult::error("no hay barra del operador"));
            const json eq = r->equipo(i);
            const std::string quien = eq.is_null() ? i
                                                   : eq.value("marca", json::object()).value("nombre", std::string("cámara")) + " (" + i + ")";
            json spec{{"kind", "credential"},
                      {"platform", "camara:" + i},
                      {"priority", "info"},
                      {"title", "Usuario y clave de " + quien},
                      {"text", a.value("texto", "Para conectar la cámara " + quien + ". Usuario de fábrica: " +
                                                     (eq.is_null() ? std::string("admin") : eq.value("usuario_de_fabrica", std::string("admin"))) +
                                                     ". Se guarda cifrado en este equipo; el asistente no lo ve.")},
                      {"fields",
                       {{{"key", "usuario"}, {"label", "Usuario"}, {"secret", false}, {"placeholder", "admin"}},
                        {{"key", "clave"}, {"label", "Clave (o código de verificación)"}, {"secret", true}}}}};
            const int id = msgs->post(spec);
            const int seg = a.value("esperar_seg", 300);
            if (seg <= 0) return done(ToolResult::ok("Formulario en la barra (id " + std::to_string(id) + ")."));
            msgs->waitAnswer(id, seg * 1000, [done, id, i](json item) {
                if (item.is_null())
                    return done(ToolResult::ok("Todavía no lo completó (el formulario " + std::to_string(id) + " sigue en la barra)."));
                if (item.value("status", "") != "saved") return done(ToolResult::ok("El operador cerró el formulario sin guardar."));
                done(ToolResult::ok("Guardadas (cifradas). Siguiente: camara_ficha {\"ip\": \"" + i + "\"}."));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_ficha";
        t.title = "Todo sobre una cámara de la red y cómo conectarla";
        t.category = "entradas";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            "Con la clave guardada: ONVIF completo —el equipo (marca, modelo, firmware), los perfiles de video (códec, "
            "tamaño, fps, bitrate y su URL exacta), el PTZ (si tiene, su perfil, presets, posición de inicio, rangos), "
            "la imagen (brillo, infrarrojo, exposición, foco)— y el ANÁLISIS con GStreamer del stream que conviene (lo "
            "que de verdad manda: tamaño, fps, entrelazado, píxel, colorimetría, audio). Con eso, el `plan`: qué "
            "perfil (el más chico que cubra el lienzo; H.264/H.265 se decodifican por hardware, MJPEG por CPU), qué "
            "normalizar (solo lo que hace falta: el color lo convierte libobs en la GPU), la latencia y el PTZ. "
            "`plan.argumentos` va tal cual a camara_agregar (sumale `escena` y `nombre`). Sin ONVIF, analiza la URL "
            "que se vio en el escaneo. Tarda unos segundos.";
        t.inputSchema = {{"type", "object"}, {"required", {"ip"}}, {"properties", {{"ip", ip}}}};
        t.run = [r](const json& a, Done done) {
            if (!r) return done(ToolResult::error("el escáner no está disponible"));
            r->ficha(a.value("ip", std::string()), [done](json f) { done(ToolResult().data(f)); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_agregar";
        t.title = "Conectar una cámara de la red al estudio";
        t.category = "entradas";
        t.description =
            "Conecta la cámara `ip` como entrada de video sincronizada (con su clave guardada): la URL del `perfil` "
            "ONVIF (o `url`, o la del plan de camara_ficha), con la `normalizar` y `latencia_ms` del plan, y su PTZ por "
            "ONVIF configurado si tiene (`ptz: false` para no). Va a la `escena` (obligatoria). Devuelve el estado "
            "de la entrada con su diagnóstico: revisalo (video_estado de nuevo a los segundos: la conexión tarda).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"ip", "escena"}},
                         {"properties",
                          {{"ip", ip},
                           {"escena", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"perfil", {{"type", "string"}, {"description", "token del perfil ONVIF (de camara_ficha)"}}},
                           {"url", {{"type", "string"}, {"description", "una URL RTSP a mano (sin clave: se le pone la guardada)"}}},
                           {"normalizar", {{"type", "boolean"}}},
                           {"latencia_ms", {{"type", "integer"}}},
                           {"ptz", {{"type", "boolean"}}}}}};
        t.run = [r, vg](const json& a, Done done) {
            if (!r) return done(ToolResult::error("el escáner no está disponible"));
            json hecho;
            const std::string e = r->agregar(a.value("ip", std::string()), a, &hecho);
            if (!e.empty()) return done(ToolResult::error(e));
            if (vg != nullptr) {
                json st = vg->estadoEntrada(hecho.value("entrada", std::string()));
                st["diagnostico"] = video::diagnosticar(st);
                hecho["estado"] = st;
            }
            hecho["nota"] = "la conexión tarda unos segundos: video_estado de nuevo para ver resolución y diagnóstico";
            done(ToolResult().data(hecho));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_urls";
        t.title = "Volver a probar las URLs de una cámara";
        t.category = "entradas";
        t.readOnly = true;
        t.openWorld = true;
        t.description = "Vuelve a probar las URLs RTSP típicas de la cámara `ip` (con la clave guardada, si hay): útil "
                        "después de guardar la clave o de habilitar el RTSP en la cámara.";
        t.inputSchema = {{"type", "object"}, {"required", {"ip"}}, {"properties", {{"ip", ip}}}};
        t.run = [r](const json& a, Done done) {
            if (!r) return done(ToolResult::error("el escáner no está disponible"));
            r->probarUrls(a.value("ip", std::string()), [done](json e) {
                if (e.contains("error")) return done(ToolResult::error(e.value("error", std::string())));
                done(ToolResult().data(e));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
