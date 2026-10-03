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

#include "platform/Paths.hpp"
#include <QTimer>

#include <algorithm>

#include "AppRuntime.hpp"
#include "InputSpecs.hpp"
#if defined(BA_HAVE_NDI)
#include "NdiSource.hpp"
#endif
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

using inputs::kVideoProp;
using inputs::kVideoType;

void McpService::registerInputTools() {
    core_.addCategory(
        "entradas", "Entradas de video",
        "Camaras y capturadoras conectadas a la maquina (hardware) y camaras/señales por la red "
        "(RTSP, RTMP, SRT, HLS, UDP). Quedan como fuentes de las escenas.");
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    const auto layoutOf = [](const json& a, SourceSpec& spec, std::string& err) {
        spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
        return !a.contains("layout") ||
               parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err);
    };
    {
        Tool t;
        t.name = "video_input_add";
        t.title = "Agregar una camara o capturadora";
        t.category = "entradas";
        t.description =
            "Agrega a una escena una camara USB o capturadora (el `device` de video_devices), con formato, "
            "resolucion y cuadros por segundo opcionales (los `value` de video_devices {device}). Si la "
            "misma "
            "camara ya es una fuente en otra escena, conviene usar esa (source_add con su nombre) en vez de "
            "abrirla dos veces.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"device", {{"type", "string"}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}, {"description", "Por defecto la escena al aire."}}},
              {"layout",
               {{"description",
                 "Preset (full, pip-br, left, ...) o {x,y,w,h}. Por defecto pantalla completa."}}},
              {"input", {{"type", "integer"}}},
              {"pixelformat", {{"type", "integer"}}},
              {"resolution", {{"type", "integer"}}},
              {"framerate", {{"type", "integer"}}}}},
            {"required", {"device"}}};
        t.run = [&rt, &vm, layoutOf](const json& a, Done done) {
            BroadcastEngine& e = rt.engine();
            if (!e.sourceTypeExists(kVideoType))
                return done(ToolResult::error(std::string("El plugin de camaras (") + kVideoType +
                                              ") no esta cargado."));
            std::string devName;
            for (const auto& d : e.sourceTypeListItems(kVideoType, kVideoProp))
                if (d.value == a["device"].get<std::string>()) devName = d.name;
            if (devName.empty())
                return done(ToolResult::error("No existe ese dispositivo: video_devices lista los que hay."));
            SourceSpec spec =
                inputs::hardwareSpec(a["device"].get<std::string>(), a.value("name", "Camara " + devName), a);
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", e.currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            vm.refreshModels();
            done(ToolResult::ok("Agregada '" + spec.name + "' a '" + scene +
                                "'. Verifica con capture_source (la camara tarda un instante en abrir)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "network_input_add";
        t.title = "Agregar una camara o señal de red";
        t.category = "entradas";
        t.openWorld = true;
        t.description =
            "SI HAY QUE SINCRONIZARLA con otras (varios streams, varias camaras de red), usá video_entrada_red: esa "
            "tiene colchon y reloj del estudio y se mide con video_sincronia; esta no. "
            "Agrega a una escena una señal por la red: camara IP (rtsp://usuario:clave@ip:554/ruta), RTMP, "
            "SRT "
            "(srt://ip:puerto?mode=caller), HLS/HTTP(S) o UDP/RTP. Con reconexion automatica y latencia baja "
            "por defecto. Despues de unos segundos devuelve si llega imagen y de que tamaño.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"url", {{"type", "string"}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}}},
              {"layout", {{"description", "Preset o {x,y,w,h}. Por defecto pantalla completa."}}},
              {"latency", {{"type", "string"}, {"enum", {"low", "normal"}}, {"default", "low"}}},
              {"rtspTransport", {{"type", "string"}, {"enum", {"tcp", "udp"}}, {"default", "tcp"}}},
              {"hwDecode", {{"type", "boolean"}, {"default", true}}},
              {"reconnectSec", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}, {"default", 5}}},
              {"audio",
               {{"type", "boolean"},
                {"default", true},
                {"description", "false = solo video (silencia la fuente)."}}}}},
            {"required", {"url"}}};
        t.examples = {
            R"({"url": "rtsp://admin:clave@192.168.1.50:554/stream1", "name": "Camara IP escenario", "layout": "pip-br"})",
            R"({"url": "srt://192.168.1.20:9000?mode=caller&latency=120000", "name": "Movil SRT"})"};
        t.run = [&rt, &vm, layoutOf](const json& a, Done done) {
            const std::string url = a["url"];
            if (inputs::networkScheme(url).empty())
                return done(ToolResult::error(
                    "URL no soportada: rtsp, rtsps, rtmp, rtmps, srt, http(s), udp, rtp o tcp."));
            const std::string ca = (ba::platform::bundleRoot() / "data" / "cacert.pem").string();
            SourceSpec spec = inputs::networkSpec(
                url, a.value("name", "Red " + inputs::redactUrl(url).substr(0, 40)), a, ca);
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", rt.engine().currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            vm.refreshModels();
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
                    done(ToolResult::ok("Agregada '" + name + "' a '" + scene + "': llega imagen de " +
                                        std::to_string(w) + "x" + std::to_string(h) +
                                        ". Verifica con capture_source."));
                else
                    done(ToolResult::ok(
                        "Agregada '" + name + "' a '" + scene +
                        "', pero todavia no llega imagen (se sigue reintentando). Revisa la URL, "
                        "usuario/clave y que la camara este en la red; input_status muestra el estado."));
            });
            timer->start(250);
        };
        core_.addTool(std::move(t));
    }
#if defined(BA_HAVE_NDI)
    {
        Tool t;
        t.name = "ndi_sources";
        t.title = "Fuentes NDI en la red";
        t.category = "entradas";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            "Busca emisores NDI(R) en la red local (mDNS) durante `seconds` y los lista con IP y puerto. "
            "Recepcion con libNoIDea (abierta, ALFA): NDI completo (SpeedHQ); NDI|HX (H.264) no.";
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
        t.title = "Agregar una fuente NDI";
        t.category = "entradas";
        t.openWorld = true;
        t.description =
            "Agrega a una escena una señal NDI(R) por su `ndiName` (de ndi_sources) o por `host` + `port`. "
            "Reconecta sola. Devuelve si llega imagen.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"ndiName", {{"type", "string"}}},
              {"host", {{"type", "string"}}},
              {"port", {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}},
              {"name", {{"type", "string"}}},
              {"scene", {{"type", "string"}}},
              {"layout", {{"description", "Preset o {x,y,w,h}. Por defecto pantalla completa."}}}}}};
        t.run = [&rt, &vm, layoutOf](const json& a, Done done) {
            if (!a.contains("ndiName") && !a.contains("host"))
                return done(ToolResult::error("Falta `ndiName` (ndi_sources) o `host` + `port`."));
            SourceSpec spec;
            spec.id = ndi::kSourceId;
            spec.name = a.value("name", "NDI " + a.value("ndiName", a.value("host", std::string())));
            spec.settingsJson = json{{"ndi_name", a.value("ndiName", "")},
                                     {"host", a.value("host", "")},
                                     {"port", a.value("port", 0)}}
                                    .dump();
            std::string err;
            if (!layoutOf(a, spec, err)) return done(ToolResult::error(err));
            const std::string scene = a.value("scene", rt.engine().currentScene());
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            vm.refreshModels();
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
                done(w > 0 ? ToolResult::ok("Agregada '" + name + "' a '" + scene + "': llega imagen de " +
                                            std::to_string(w) + "x" + std::to_string(h) + ".")
                           : ToolResult::ok("Agregada '" + name + "' a '" + scene +
                                            "', pero todavia no llega imagen" +
                                            (why.empty() ? "" : " (" + why + ")") + ". Sigue reintentando."));
            });
            timer->start(250);
        };
        core_.addTool(std::move(t));
    }
#endif
    {
        Tool t;
        t.name = "input_status";
        t.title = "Estado de las entradas";
        t.category = "entradas";
        t.readOnly = true;
        t.description =
            "Las fuentes de camara/capturadora y de red: tipo, dispositivo o URL (sin clave), si llega "
            "imagen y de que tamaño, y el estado de reproduccion de las de red.";
        t.run = [&rt](const json&, Done done) {
            BroadcastEngine& e = rt.engine();
            json out = json::array();
            std::map<std::string, std::string> media;
            for (const auto& m : e.mediaSources())
                media[m.name] = m.state;
            for (const std::string& n : e.sourceNames()) {
                const std::string type = e.sourceTypeId(n);
                const bool isNdi = type == "ba_ndi_input";
                if (type != kVideoType && type != "ffmpeg_source" && !isNdi) continue;
                const json s = json::parse(e.sourceSettingsJson(n), nullptr, false);
                if (type == "ffmpeg_source" && s.value("is_local_file", true)) continue;
                int w = 0, h = 0;
                e.sourceSize(n, w, h);
                json it{{"name", n},
                        {"type", type == kVideoType ? "hardware"
                                 : isNdi            ? "ndi"
                                                    : "red"},
                        {"width", w},
                        {"height", h},
                        {"receiving", w > 0}};
                if (isNdi) {
                    it["ndiName"] = s.value("ndi_name", "");
                    it["host"] = s.value("host", "");
                } else if (type == kVideoType) {
                    it["device"] = s.value(kVideoProp, "");
                } else {
                    it["url"] = inputs::redactUrl(s.value("input", "")); // sin credenciales
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
