// ============================================================================
// src/app/mcp/McpInputTools.cpp — Entradas de video: hardware y red
//
// Hardware: camaras USB y capturadoras (V4L2 / DirectShow / AVFoundation) con
//   el formato, resolucion y cuadros elegidos.
// Red: camaras IP y señales por URL — RTSP, RTMP, SRT, HLS/HTTP(S), UDP/RTP —
//   con FFmpeg (el del bundle), latencia baja, reconexion sola y decodificacion
//   por hardware.
// Las dos quedan como fuentes de escena normales (source_update, capturas...).
// ============================================================================

#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"
#include <QTimer>

#include <algorithm>

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#if defined(BA_HAVE_NDI)
#include "emision/NdiSource.hpp"
#endif
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Camaras.hpp"

namespace ba::app::mcp {


void McpService::registerInputTools() {
    core_.addCategory(
        "entradas", ba::i18n::traducir("mcp", "Entradas de video"),
        ba::i18n::traducir("mcp", "Cámaras y capturadoras conectadas a la máquina (hardware) y cámaras/señales "
                                  "por la red (RTSP, RTMP, SRT, HLS, UDP). Quedan como fuentes de las escenas."));
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    const auto layoutOf = [](const json& a, SourceSpec& spec, std::string& err) {
        spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
        return !a.contains("layout") ||
               parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err);
    };
    {
        Tool t;
        t.name = "entrada_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar una entrada");
        t.category = "entradas";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra la entrada `nombre` (cámara, capturadora, de red, de un plugin): la saca de "
                        "TODAS las escenas y de "
                        "la app, con su grafo de video. Para sacarla de una sola escena, source_remove.");
        t.inputSchema = {
            {"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", {{"type", "string"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            if (rt.engine().sourceTypeId(n).empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay una entrada «%1»"), {n})));
            auto* inp = static_cast<InputsViewModel*>(vm.inputs());
            if (inp == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "las entradas no están disponibles")));
            inp->remove(QString::fromStdString(n));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Borrada: «%1»."), {n})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_input_add";
        t.title = ba::i18n::traducir("mcp", "Agregar una camara o capturadora");
        t.category = "entradas";
        t.description =
            ba::i18n::traducir("mcp", "Agrega a una escena una camara USB o capturadora (el `device` de video_devices). Entra por "
            "GStreamer (ba_gst_fuente: sus tarjetas y filtros corren en su cadena). `modo` opcional: tamaño y "
            "cuadros, uno de los `modos` de video_devices {device} (p. ej. \"1920x1080@30\"); sin modo, el de "
            "la camara (igual se normaliza al lienzo). Si la misma camara ya es una fuente en otra escena, "
            "conviene usar esa (crear_fuente con su nombre) en vez de abrirla dos veces.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"device", {{"type", "string"}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Por defecto la escena al aire.")}}},
              {"layout",
               {{"description",
                 ba::i18n::traducir("mcp", "Preset (full, pip-br, left, ...) o {x,y,w,h}. Por defecto pantalla completa.")}}},
              {"modo", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "«ANCHOxALTO@cps», de video_devices {device}")}}}}},
            {"required", {"device"}}};
        t.run = [&rt, layoutOf](const json& a, Done done) {
            BroadcastEngine& e = rt.engine();
            if (!e.sourceTypeExists(inputs::kMicType))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "La entrada de GStreamer (ba_gst_fuente) no esta cargada.")));
            std::string devName;
            for (const auto& d : video::camarasDelEquipo(e))
                if (d.value == a["device"].get<std::string>()) devName = d.name;
            if (devName.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "No existe ese dispositivo: video_devices lista los que hay.")));
            SourceSpec spec =
                inputs::hardwareSpec(a["device"].get<std::string>(), a.value("name", ba::i18n::llenar(ba::i18n::traducir("mcp", "Camara %1"), {devName})), a);
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", e.currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            done(ToolResult::ok(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "Agregada '%1' a '%2'. Verifica con foto_fuente (la camara tarda un "
                                          "instante en abrir)."),
                {spec.name, scene})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "network_input_add";
        t.title = ba::i18n::traducir("mcp", "Agregar una camara o señal de red");
        t.category = "entradas";
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "SI HAY QUE SINCRONIZARLA con otras (varios streams, varias camaras de red), usá "
            "video_entrada_red: esa "
            "tiene colchon y reloj del estudio y se mide con video_sincronia; esta no. "
            "Agrega a una escena una señal por la red: camara IP (rtsp://usuario:clave@ip:554/ruta), RTMP, "
            "SRT "
            "(srt://ip:puerto?mode=caller), HLS/HTTP(S) o UDP/RTP. Con reconexion automatica y latencia baja "
            "por defecto. Despues de unos segundos devuelve si llega imagen y de que tamaño.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"url", {{"type", "string"}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}}},
              {"layout", {{"description", ba::i18n::traducir("mcp", "Preset o {x,y,w,h}. Por defecto pantalla completa.")}}},
              {"latency", {{"type", "string"}, {"enum", {"low", "normal"}}, {"default", "low"}}},
              {"rtspTransport", {{"type", "string"}, {"enum", {"tcp", "udp"}}, {"default", "tcp"}}},
              {"reconnectSec", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}, {"default", 5}}},
              {"audio",
               {{"type", "boolean"},
                {"default", true},
                {"description", ba::i18n::traducir("mcp", "false = solo video (silencia la fuente).")}}}}},
            {"required", {"url"}}};
        t.examples = {
            R"({"url": "rtsp://admin:clave@192.168.1.50:554/stream1", "name": "Camara IP escenario", "layout": "pip-br"})",
            R"({"url": "srt://192.168.1.20:9000?mode=caller&latency=120000", "name": "Movil SRT"})"};
        t.run = [&rt, &vm, layoutOf](const json& a, Done done) {
            const std::string url = a["url"];
            if (inputs::networkScheme(url).empty())
                return done(ToolResult::error(
                    ba::i18n::traducir("mcp", "URL no soportada: rtsp, rtsps, rtmp, rtmps, srt, http(s), udp, rtp o tcp.")));
            // GStreamer elige el decodificador (por hardware si hay): no se puede forzar por software.
            if (!a.value("hwDecode", true))
                return done(ToolResult::error(ba::i18n::traducir(
                    "mcp", "hwDecode: el motor elige el decodificador (por hardware si hay); no se puede forzar por software.")));
            const std::string ca = (ba::platform::bundleRoot() / "data" / "cacert.pem").string();
            SourceSpec spec = inputs::networkSpec(
                url, a.value("name", ba::i18n::llenar(ba::i18n::traducir("mcp", "Red %1"), {inputs::redactUrl(url).substr(0, 40)})), a, ca);
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", rt.engine().currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            // Esperar un poco a que llegue imagen para contestar algo util.
            const std::string name = spec.name;
            auto* timer = new QTimer(&vm);
            auto tries = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, &vm, [&rt, timer, tries, name, scene, done]() {
                int w = 0, h = 0;
                rt.engine().sourceSize(name, w, h);
                if (w == 0 && ++*tries < 24) return; // hasta ~6 s
                timer->deleteLater();
                if (w > 0)
                    done(ToolResult::ok(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "Agregada '%1' a '%2': llega imagen de %3x%4. Verifica con "
                                                  "foto_fuente."),
                        {name, scene, std::to_string(w), std::to_string(h)})));
                else
                    done(ToolResult::ok(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "Agregada '%1' a '%2', pero todavia no llega imagen (se sigue "
                                                  "reintentando). Revisa la URL, usuario/clave y que la camara "
                                                  "este en la red; input_status muestra el estado."),
                        {name, scene})));
            });
            timer->start(250);
        };
        core_.addTool(std::move(t));
    }
#if defined(BA_HAVE_NDI)
    {
        Tool t;
        t.name = "ndi_sources";
        t.title = ba::i18n::traducir("mcp", "Fuentes NDI en la red");
        t.category = "entradas";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Busca emisores NDI(R) en la red local (mDNS) durante `seconds` y los lista con IP y puerto. "
            "Recepcion con libNoIDea (abierta, ALFA): NDI completo (SpeedHQ); NDI|HX (H.264) no.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"seconds", {{"type", "integer"}, {"minimum", 1}, {"maximum", 15}, {"default", 4}}}}}};
        t.run = [](const json& a, Done done) {
            json out = json::array();
            for (const auto& f : ndi::discover(a.value("seconds", 4) * 1000))
                out.push_back({{"name", f.name}, {"host", f.host}, {"port", f.port}});
            ToolResult r;
            r.data(json{{"sources", out}});
            r.untrusted = !out.empty(); // los nombres los pone quien emite
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ndi_input_add";
        t.title = ba::i18n::traducir("mcp", "Agregar una fuente NDI");
        t.category = "entradas";
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Agrega a una escena una señal NDI(R) por su `ndiName` (de ndi_sources) o por `host` + `port`. "
            "Reconecta sola. Devuelve si llega imagen.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"ndiName", {{"type", "string"}}},
              {"host", {{"type", "string"}}},
              {"port", {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}}},
              {"layout", {{"description", ba::i18n::traducir("mcp", "Preset o {x,y,w,h}. Por defecto pantalla completa.")}}}}}};
        t.run = [&rt, &vm, layoutOf](const json& a, Done done) {
            if (!a.contains("ndiName") && !a.contains("host"))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta `ndiName` (ndi_sources) o `host` + `port`.")));
            SourceSpec spec;
            spec.id = ndi::kSourceId;
            spec.name = a.value("name", "NDI " + a.value("ndiName", a.value("host", std::string())));
            spec.settingsJson = json{{"ndi_name", a.value("ndiName", "")},
                                     {"host", a.value("host", "")},
                                     {"port", a.value("port", 0)}}
                                    .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", rt.engine().currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            const std::string name = spec.name;
            auto* timer = new QTimer(&vm);
            auto tries = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, &vm, [&rt, timer, tries, name, scene, done]() {
                int w = 0, h = 0;
                rt.engine().sourceSize(name, w, h);
                if (w == 0 && ++*tries < 32) return; // hasta ~8 s (incluye buscarla en la red)
                timer->deleteLater();
                std::string why;
                for (const auto& s : ndi::statuses())
                    if (!s.error.empty()) why = s.error;
                done(w > 0 ? ToolResult::ok(ba::i18n::llenar(
                                 ba::i18n::traducir("mcp", "Agregada '%1' a '%2': llega imagen de %3x%4."),
                                 {name, scene, std::to_string(w), std::to_string(h)}))
                     : why.empty()
                         ? ToolResult::ok(ba::i18n::llenar(
                               ba::i18n::traducir("mcp", "Agregada '%1' a '%2', pero todavia no llega imagen. "
                                                         "Sigue reintentando."),
                               {name, scene}))
                         : ToolResult::ok(ba::i18n::llenar(
                               ba::i18n::traducir("mcp", "Agregada '%1' a '%2', pero todavia no llega imagen "
                                                         "(%3). Sigue reintentando."),
                               {name, scene, why})));
            });
            timer->start(250);
        };
        core_.addTool(std::move(t));
    }
#endif
    {
        Tool t;
        t.name = "input_status";
        t.title = ba::i18n::traducir("mcp", "Estado de las entradas");
        t.category = "entradas";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Las fuentes de camara/capturadora y de red: tipo, dispositivo o URL (sin clave), si llega "
            "imagen y de que tamaño, y el estado de reproduccion de las de red.");
        t.run = [&rt](const json&, Done done) {
            BroadcastEngine& e = rt.engine();
            json out = json::array();
            std::map<std::string, std::string> media;
            for (const auto& m : e.mediaSources())
                media[m.name] = m.state;
            for (const std::string& n : e.sourceNames()) {
                const std::string type = e.sourceTypeId(n);
                const bool isNdi = inputs::tipoLogico(type, json::parse(e.sourceSettingsJson(n), nullptr, false)) == "ba_ndi_input";
                const json s = json::parse(e.sourceSettingsJson(n), nullptr, false);
                const bool cam = inputs::esCamara(type, s);
                if (!cam && !isNdi && !(inputs::isNetworkInput(type, s) && s.is_object() && !s.value("uri", std::string()).empty()))
                    continue;
                int w = 0, h = 0;
                e.sourceSize(n, w, h);
                json it{{"name", n},
                        {"type", cam     ? "hardware"
                                 : isNdi ? "ndi"
                                         : "red"},
                        {"width", w},
                        {"height", h},
                        {"receiving", w > 0}};
                if (isNdi) {
                    it["ndiName"] = s.value("ndi_name", "");
                    it["host"] = s.value("host", "");
                } else if (cam) {
                    it["device"] = inputs::camaraDe(type, s);
                    it["modo"] = s.value("camara_modo", "");
                } else {
                    it["url"] = inputs::redactUrl(s.value("uri", "")); // sin credenciales
                    it["state"] = media.count(n) ? media[n] : "";
                }
                out.push_back(it);
            }
            done(ToolResult().data(json{{"inputs", out}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
