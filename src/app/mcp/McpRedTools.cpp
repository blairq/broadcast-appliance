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

#include "i18n/I18n.hpp"
#include "mcp/EsperarEntrada.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "red/Red.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Diagnostico.hpp"
#include "video/VideoGrafos.hpp"

#include <QPointer>

#include <algorithm>

namespace ba::app::mcp {

void McpService::registerRedTools() {
    red::Red* rd = vm_.redEquipos();
    if (rd == nullptr) return;
    const QPointer<red::Red> r = rd;
    auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
    video::VideoGrafos* vg = vm_.videoGrafos();
    const json ip = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "La IP del equipo (como en red_escanear).")}};
    {
        Tool t;
        t.name = "red_escanear";
        t.title = ba::i18n::traducir("mcp", "Buscar cámaras y equipos de video en la red");
        t.category = "entradas";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Escanea la red local (unos 10 s): ONVIF, los puertos de video de toda la subred "
                        "(RTSP, web, RTMP, NDI, los "
                        "SDK de Hikvision y Dahua), el fabricante por la MAC, la marca (con el porqué) y "
                        "prueba sus URLs RTSP "
                        "típicas (biblioteca propia: Axis, Hikvision y sus grabadores, EZVIZ, Dahua/Imou, "
                        "Reolink, Uniview, Tapo, "
                        "Foscam, Hanwha, Bosch, Vivotek, Panasonic, Sony…). Cada equipo trae "
                        "`siguiente_paso`: SEGUILO. Si pide "
                        "clave: camara_credenciales con `pedir` (el operador la escribe en un formulario "
                        "seguro). Después "
                        "camara_ficha y camara_agregar. `notas` dice lo propio de la marca (p. ej. EZVIZ: la "
                        "clave es el código "
                        "de la etiqueta y el RTSP se habilita en su app).");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [r](const json&, Done done) {
            if (!r) return done(ToolResult::error(ba::i18n::traducir("mcp", "el escáner no está disponible")));
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
        t.title = ba::i18n::traducir("mcp", "Usuario y clave de cámaras de la red");
        t.category = "entradas";
        t.clase = Clase::Escritura;
        t.description =
            //: no traducir [OPERADOR]
            QT_TRANSLATE_NOOP("mcp", "Con `pedir: true` (lo correcto): muestra al operador UN formulario seguro en la barra con "
            "usuario y clave de "
            "cada cámara de `ips` (vos nunca los ves; se guardan cifrados) y vuelve enseguida: cuando lo "
            "complete te "
            "llega a tu chat «[OPERADOR] guardo…». Mientras, seguí con lo demás. Decile antes en el chat qué "
            "usuario va "
            "y de dónde sale la clave (las `notas` del escaneo: EZVIZ = código de verificación de la "
            "etiqueta, Axis = un "
            "usuario ONVIF, grabador = su clave). `esperar_seg` > 0 espera la respuesta. Con `usuario` y "
            "`clave` (de una "
            "sola `ip`): los guarda directo, solo si el operador te los dio. Después: camara_ficha.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"ips", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"ip", ip},
              {"pedir", {{"type", "boolean"}}},
              {"texto",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "lo que se le dice al operador en el formulario")}}},
              {"esperar_seg", {{"type", "integer"}}},
              {"usuario", {{"type", "string"}}},
              {"clave", {{"type", "string"}}}}}};
        t.examples = {R"({"ips": ["192.168.1.140", "192.168.1.226"], "pedir": true})"};
        t.run = [r, msgs](const json& a, Done done) {
            if (!r) return done(ToolResult::error(ba::i18n::traducir("mcp", "el escáner no está disponible")));
            std::vector<std::string> ips;
            for (const auto& x : a.value("ips", json::array()))
                ips.push_back(x.get<std::string>());
            if (a.contains("ip")) ips.push_back(a.value("ip", std::string()));
            if (ips.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta `ips` (o `ip`)")));
            if (!a.value("pedir", false)) {
                if (!a.contains("usuario") || ips.size() != 1)
                    return done(
                        ToolResult::error(ba::i18n::traducir("mcp", "con `usuario` y `clave` va una sola `ip` (si no, `pedir: true`)")));
                const QString e = r->guardarCredenciales(
                    QString::fromStdString(ips[0]), QString::fromStdString(a.value("usuario", std::string())),
                    QString::fromStdString(a.value("clave", std::string())));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "Guardadas (cifradas). Siguiente: camara_ficha {\"ip\": \"%1\"}."),
                    {ips[0]})));
            }
            if (msgs == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay barra del operador")));
            // Si ya hay un formulario de claves de cámaras abierto, se le suman
            // estas: el operador completa uno solo.
            int abierto = 0;
            for (const auto& m : msgs->list(false)) {
                const std::string pl = m.value("platform", std::string());
                if (m.value("kind", std::string()) != "credential" ||
                    m.value("status", std::string()) != "open")
                    continue;
                if (pl.rfind("camaras:", 0) != 0 && pl.rfind("camara:", 0) != 0) continue;
                abierto = m.value("id", 0);
                const std::string previas = pl.substr(pl.find(':') + 1);
                std::vector<std::string> todas;
                size_t i0 = 0;
                while (i0 <= previas.size()) {
                    const size_t c = previas.find(',', i0);
                    const std::string x =
                        previas.substr(i0, c == std::string::npos ? std::string::npos : c - i0);
                    if (!x.empty()) todas.push_back(x);
                    if (c == std::string::npos) break;
                    i0 = c + 1;
                }
                for (const auto& x : ips)
                    if (std::find(todas.begin(), todas.end(), x) == todas.end()) todas.push_back(x);
                ips = todas;
            }
            json campos = json::array();
            std::string lista, plataforma = "camaras:";
            for (size_t k = 0; k < ips.size(); ++k) {
                const json eq = r->equipo(ips[k]);
                const std::string quien = eq.empty() ? ips[k]
                                                     : eq.value("marca", json::object())
                                                               .value("nombre", ba::i18n::traducir("mcp", "cámara"))
                                                               .substr(0, 24) +
                                                           " " + ips[k];
                const std::string usuarioFab =
                    eq.empty() ? std::string("admin") : eq.value("usuario_de_fabrica", std::string("admin"));
                campos.push_back({{"key", "usuario|" + ips[k]},
                                  {"label", ba::i18n::llenar(ba::i18n::traducir("mcp", "Usuario · %1"), {quien})},
                                  {"secret", false},
                                  {"placeholder", usuarioFab}});
                campos.push_back(
                    {{"key", "clave|" + ips[k]}, {"label", ba::i18n::llenar(ba::i18n::traducir("mcp", "Clave · %1"), {quien})}, {"secret", true}});
                lista += (k ? ", " : "") + quien;
                plataforma += (k ? "," : "") + ips[k];
            }
            json spec{
                {"kind", "credential"},
                {"platform", plataforma},
                {"priority", "info"},
                {"title", ips.size() == 1
                              ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Usuario y clave de %1"), {lista})
                              : ba::i18n::traducir("mcp", "Usuario y clave de %n cámara", nullptr,
                                                   static_cast<int>(ips.size()))},
                {"text", a.value("texto", ba::i18n::llenar(
                                              ba::i18n::traducir("mcp", "Para conectar: %1. Se guarda cifrado en este "
                                                                        "equipo; el asistente no lo ve. Si dejás el "
                                                                        "usuario vacío, va el de fábrica."),
                                              {lista}))},
                {"fields", campos}};
            const bool sumado = abierto > 0 && msgs->revise(abierto, spec);
            const int id = sumado ? abierto : msgs->post(spec);
            const int seg = a.value("esperar_seg", 0);
            if (seg <= 0)
                return done(ToolResult::ok(
                    sumado ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Sumadas al formulario que ya estaba en la "
                                                                        "barra (id %1; ahora con %2). Cuando lo "
                                                                        "complete te llega a tu chat; mientras, "
                                                                        "seguí con lo demás."),
                                              {std::to_string(id), lista})
                           : ba::i18n::llenar(ba::i18n::traducir("mcp", "Formulario en la barra del operador (id %1). "
                                                                        "Cuando lo complete te llega a tu chat; "
                                                                        "mientras, seguí con lo demás."),
                                              {std::to_string(id)})));
            msgs->waitAnswer(id, seg * 1000, [done, id](json item) {
                if (item.is_null())
                    return done(ToolResult::ok(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "Todavía no lo completó (el formulario %1 sigue en la barra; te "
                                                  "aviso cuando lo haga)."),
                        {std::to_string(id)})));
                if (item.value("status", "") != "saved")
                    return done(ToolResult::ok(ba::i18n::traducir("mcp", "El operador cerró el formulario sin guardar.")));
                done(ToolResult::ok(ba::i18n::traducir("mcp", "Guardadas (cifradas). Siguiente: camara_ficha de cada una.")));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_ficha";
        t.title = ba::i18n::traducir("mcp", "Todo sobre una cámara de la red y cómo conectarla");
        t.category = "entradas";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Con la clave guardada: ONVIF completo —el equipo (marca, modelo, firmware), los "
                        "perfiles de video (códec, "
                        "tamaño, fps, bitrate y su URL exacta), el PTZ (si tiene, su perfil, presets, "
                        "posición de inicio, rangos), "
                        "la imagen (brillo, infrarrojo, exposición, foco)— y el ANÁLISIS con GStreamer del "
                        "stream que conviene (lo "
                        "que de verdad manda: tamaño, fps, entrelazado, píxel, colorimetría, audio). Con "
                        "eso, el `plan`: qué "
                        "perfil (el más chico que cubra el lienzo; H.264/H.265 se decodifican por hardware, "
                        "MJPEG por CPU), qué "
                        "normalizar (solo lo que hace falta: el color lo convierte la entrada al del lienzo), la "
                        "latencia y el PTZ. "
                        "`plan.argumentos` va tal cual a camara_agregar (sumale `escena` y `nombre`). Sin "
                        "ONVIF, analiza la URL "
                        "que se vio en el escaneo. Tarda unos segundos.");
        t.inputSchema = {{"type", "object"}, {"required", {"ip"}}, {"properties", {{"ip", ip}}}};
        t.run = [r](const json& a, Done done) {
            if (!r) return done(ToolResult::error(ba::i18n::traducir("mcp", "el escáner no está disponible")));
            r->ficha(a.value("ip", std::string()), [done](json f) { done(ToolResult().data(f)); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_agregar";
        t.title = ba::i18n::traducir("mcp", "Conectar una cámara de la red al estudio");
        t.category = "entradas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Conecta la cámara `ip` como entrada de video sincronizada (con su clave guardada): "
                        "la URL del `perfil` "
                        "ONVIF (o `url`, o la del plan de camara_ficha), con la `normalizar` y `latencia_ms` "
                        "del plan, y su PTZ por "
                        "ONVIF configurado si tiene (`ptz: false` para no). Va a la `escena` (obligatoria; "
                        "si no existe, la crea). Si YA hay una entrada "
                        "con esa URL, no abre otro stream: `reusar: true` suma la misma a la escena. "
                        "`solo_ptz: true` + `entrada`: "
                        "solo configura el PTZ de una entrada que ya existe (cuando la clave llega después). "
                        "Espera a que conecte "
                        "(hasta `esperar_seg`, 5 de fábrica) y devuelve el estado con su diagnóstico: aplicá "
                        "los arreglos que "
                        "proponga (p. ej. `ntp_sync: false` si el reloj de la cámara está corrido) con "
                        "video_entrada_red sobre esa "
                        "entrada.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"ip"}},
            {"properties",
             {{"ip", ip},
              {"escena", {{"type", "string"}}},
              {"reusar", {{"type", "boolean"}}},
              {"duplicar", {{"type", "boolean"}}},
              {"solo_ptz", {{"type", "boolean"}}},
              {"entrada",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con solo_ptz: la entrada que ya muestra esta cámara")}}},
              {"nombre", {{"type", "string"}}},
              {"perfil", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "token del perfil ONVIF (de camara_ficha)")}}},
              {"url",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "una URL RTSP a mano (sin clave: se le pone la guardada)")}}},
              {"normalizar", {{"type", "boolean"}}},
              {"latencia_ms", {{"type", "integer"}}},
              {"ptz", {{"type", "boolean"}}},
              {"esperar_seg",
               {{"type", "integer"}, {"description", QT_TRANSLATE_NOOP("mcp", "cuánto esperar a que conecte (0: no esperar)")}}}}}};
        t.run = [r, vg](const json& a, Done done) {
            if (!r) return done(ToolResult::error(ba::i18n::traducir("mcp", "el escáner no está disponible")));
            json hecho;
            const std::string e = r->agregar(a.value("ip", std::string()), a, &hecho);
            if (!e.empty()) return done(ToolResult::error(e));
            if (vg == nullptr || a.value("solo_ptz", false)) return done(ToolResult().data(hecho));
            // Espera a que conecte: vuelve con la resolución y el diagnóstico (el
            // reloj corrido de la cámara, por ejemplo) sin otro video_estado.
            esperarEntrada(QPointer<video::VideoGrafos>(vg), hecho.value("entrada", std::string()),
                           a.value("esperar_seg", 5) * 1000, [done, hecho](json st) mutable {
                               hecho["estado"] = std::move(st);
                               done(ToolResult().data(hecho));
                           });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "camara_urls";
        t.title = ba::i18n::traducir("mcp", "Volver a probar las URLs de una cámara");
        t.category = "entradas";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Vuelve a probar las URLs RTSP típicas de la cámara `ip` (con la clave guardada, si hay): útil "
            "después de guardar la clave o de habilitar el RTSP en la cámara.");
        t.inputSchema = {{"type", "object"}, {"required", {"ip"}}, {"properties", {{"ip", ip}}}};
        t.run = [r](const json& a, Done done) {
            if (!r) return done(ToolResult::error(ba::i18n::traducir("mcp", "el escáner no está disponible")));
            r->probarUrls(a.value("ip", std::string()), [done](json e) {
                if (e.contains("error")) return done(ToolResult::error(e.value("error", std::string())));
                done(ToolResult().data(e));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
