// ============================================================================
// src/app/mcp/McpSystemTools.cpp — El equipo: monitores, audio, video, GPU
//
// Lo que el asistente necesita saber de la maquina para decidir: cuantos
// monitores y de que resolucion (para repartir ventanas de salida), que placas
// de audio de entrada y salida hay, que camaras y capturadoras (con sus
// formatos), con que GPU dibuja y que codificadores de hardware hay, CPU,
// memoria y disco. Los dispositivos se descubren con los mismos plugins de
// libobs que despues los usan (nada que pueda diferir entre lo listado y lo
// que se puede agregar).
// ============================================================================

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QStorageInfo>
#include <QSysInfo>

#include <cstdio>
#include <map>
#include <thread>

#include "AppRuntime.hpp"
#include "InputSpecs.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#if defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <dxgi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace ba::app::mcp {

namespace {

/// Tipos de fuente de libobs por plataforma para cada clase de dispositivo.
struct DeviceKind {
    const char* type;
    const char* prop;
};
#if defined(_WIN32)
const DeviceKind kAudioIn{"wasapi_input_capture", "device_id"};
const DeviceKind kAudioOut{"wasapi_output_capture", "device_id"};
const DeviceKind kVideo{"dshow_input", "video_device_id"};
#elif defined(__APPLE__)
const DeviceKind kAudioIn{"coreaudio_input_capture", "device_id"};
const DeviceKind kAudioOut{"coreaudio_output_capture", "device_id"};
const DeviceKind kVideo{"macos-avcapture", "device"};
#else
const DeviceKind kAudioIn{"pulse_input_capture", "device_id"};
const DeviceKind kAudioOut{"pulse_output_capture", "device_id"};
const DeviceKind kVideo{"v4l2_input", "device_id"};
#endif

json listToJson(const std::vector<BroadcastEngine::ListItem>& items) {
    json out = json::array();
    for (const auto& i : items) {
        json e{{"name", i.name}, {"value", i.value}};
        if (i.disabled) e["disabled"] = true;
        out.push_back(e);
    }
    return out;
}

json screensJson() {
    json out = json::array();
    const QScreen* primary = QGuiApplication::primaryScreen();
    int idx = 0;
    for (const QScreen* s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const QRect a = s->availableGeometry();
        const qreal dpr = s->devicePixelRatio();
        out.push_back(
            {{"index", idx++},
             {"name", s->name().toStdString()},
             {"manufacturer", s->manufacturer().toStdString()},
             {"model", s->model().toStdString()},
             {"primary", s == primary},
             {"geometry", {{"x", g.x()}, {"y", g.y()}, {"width", g.width()}, {"height", g.height()}}},
             {"available", {{"x", a.x()}, {"y", a.y()}, {"width", a.width()}, {"height", a.height()}}},
             {"pixels",
              {{"width", static_cast<int>(g.width() * dpr)}, {"height", static_cast<int>(g.height() * dpr)}}},
             {"scale", dpr},
             {"refreshHz", s->refreshRate()},
             {"physicalMm", {{"width", s->physicalSize().width()}, {"height", s->physicalSize().height()}}}});
    }
    return out;
}

#if defined(__linux__)
std::string readTrim(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll().trimmed().toStdString();
}
#endif

std::string vendorName(const std::string& id) {
    static const std::map<std::string, std::string> kVendors{{"0x10de", "NVIDIA"},
                                                             {"0x1002", "AMD"},
                                                             {"0x8086", "Intel"},
                                                             {"0x1af4", "Virtio"},
                                                             {"0x15ad", "VMware"}};
    const auto v = kVendors.find(id);
    return v != kVendors.end() ? v->second : id;
}

/// Placas de video (Linux: /sys/class/drm; Windows: DXGI).
json gpusJson() {
    json out = json::array();
#if defined(_WIN32)
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                char vid[16];
                char did[16];
                std::snprintf(vid, sizeof(vid), "0x%04x", d.VendorId);
                std::snprintf(did, sizeof(did), "0x%04x", d.DeviceId);
                out.push_back(
                    {{"card", QString::fromWCharArray(d.Description).toStdString()},
                     {"vendor", vendorName(vid)},
                     {"deviceId", did},
                     {"memoryMB", static_cast<std::uint64_t>(d.DedicatedVideoMemory / (1024 * 1024))}});
            }
            a->Release();
        }
        factory->Release();
    }
#elif defined(__linux__)
    QDir drm("/sys/class/drm");
    for (const QString& card : drm.entryList({"card?", "card??"}, QDir::Dirs | QDir::System)) {
        const QString dev = drm.absoluteFilePath(card) + "/device";
        const std::string vendor = readTrim(dev + "/vendor");
        if (vendor.empty()) continue;
        std::string driver;
        QFile ue(dev + "/uevent");
        if (ue.open(QIODevice::ReadOnly))
            for (const QByteArray& l : ue.readAll().split('\n'))
                if (l.startsWith("DRIVER=")) driver = l.mid(7).toStdString();
        out.push_back({{"card", card.toStdString()},
                       {"vendor", vendorName(vendor)},
                       {"deviceId", readTrim(dev + "/device")},
                       {"driver", driver}});
    }
#endif
    return out;
}

json memoryJson() {
#if defined(__linux__)
    struct sysinfo si {};
    if (::sysinfo(&si) == 0)
        return {{"totalGb", static_cast<double>(si.totalram) * si.mem_unit / 1e9},
                {"freeGb", static_cast<double>(si.freeram + si.bufferram) * si.mem_unit / 1e9}};
#elif defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m))
        return {{"totalGb", static_cast<double>(m.ullTotalPhys) / 1e9},
                {"freeGb", static_cast<double>(m.ullAvailPhys) / 1e9}};
#elif defined(__APPLE__)
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
        return {{"totalGb", static_cast<double>(mem) / 1e9}};
#endif
    return json::object();
}

std::string cpuModel() {
#if defined(__linux__)
    QFile f("/proc/cpuinfo");
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray& l : f.readAll().split('\n'))
            if (l.startsWith("model name")) return l.mid(l.indexOf(':') + 1).trimmed().toStdString();
#elif defined(__APPLE__)
    char buf[256];
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return buf;
#endif
    return QSysInfo::currentCpuArchitecture().toStdString();
}

} // namespace

void McpService::registerSystemTools() {
    core_.addCategory(
        "equipo", "El equipo",
        "Monitores (para repartir ventanas de salida), placas de audio, camaras y capturadoras con sus "
        "formatos, GPU y codificadores, CPU, memoria y disco.");
    AppRuntime& rt = rt_;
    {
        Tool t;
        t.name = "system_hardware";
        t.title = "El equipo: monitores, audio, video, GPU";
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            "El hardware y cuanto queda: espacio en DISCO, memoria, procesador, temperatura. Todo el "
            "hardware de una vez: monitores (nombre, resolucion, posicion, escala, Hz, principal), "
            "placas de audio de entrada y de salida, camaras y capturadoras, GPU y con que dibuja la app, "
            "codificadores de video (los de hardware marcados), CPU, memoria, disco y sistema operativo. "
            "Para los formatos de una camara: video_devices.";
        t.run = [&rt](const json&, Done done) {
            BroadcastEngine& e = rt.engine();
            json encoders = json::array();
            for (const auto& enc : e.videoEncoders())
                encoders.push_back(
                    {{"id", enc.id}, {"name", enc.name}, {"codec", enc.codec}, {"hardware", enc.hardware}});
            const QStorageInfo disk(QString::fromStdWString((rt.dataRoot() / "Recordings").wstring()));
            done(ToolResult().data(
                json{{"os", QSysInfo::prettyProductName().toStdString()},
                     {"cpu", {{"model", cpuModel()}, {"threads", std::thread::hardware_concurrency()}}},
                     {"memory", memoryJson()},
                     {"disk",
                      {{"path", disk.rootPath().toStdString()},
                       {"freeGb", static_cast<double>(disk.bytesAvailable()) / 1e9},
                       {"totalGb", static_cast<double>(disk.bytesTotal()) / 1e9},
                       {"filesystem", disk.fileSystemType().toStdString()}}},
                     {"monitors", screensJson()},
                     {"audioInputs", listToJson(e.sourceTypeListItems(kAudioIn.type, kAudioIn.prop))},
                     {"audioOutputs", listToJson(e.sourceTypeListItems(kAudioOut.type, kAudioOut.prop))},
                     {"videoDevices", listToJson(e.sourceTypeListItems(kVideo.type, kVideo.prop))},
                     {"gpu", {{"renderer", e.graphicsDeviceName()}, {"cards", gpusJson()}}},
                     {"videoEncoders", encoders}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitors";
        t.title = "Monitores";
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            "Monitores conectados (nombre, resolucion en pixeles, posicion en el escritorio, escala, Hz, "
            "principal) y las ventanas de salida abiertas. El nombre es el que usa output_window.screen.";
        t.run = [](const json&, Done done) {
            done(ToolResult().data(
                json{{"monitors", screensJson()},
                     {"hint", "Ventanas de salida: dashboard_list las muestra; output_window las abre "
                              "en un monitor (screen = name)."}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_devices";
        t.title = "Placas de audio";
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            "Que placas y conectores de audio tiene la maquina, por nombre de sistema. "
            "Entradas (microfonos, lineas, interfaces) y salidas (parlantes, auriculares) de audio. El "
            "`value` es el que va en la fuente de captura de audio (source_add).";
        t.run = [&rt](const json&, Done done) {
            BroadcastEngine& e = rt.engine();
            done(ToolResult().data(
                json{{"inputs", listToJson(e.sourceTypeListItems(kAudioIn.type, kAudioIn.prop))},
                     {"outputs", listToJson(e.sourceTypeListItems(kAudioOut.type, kAudioOut.prop))},
                     {"inputSourceType", kAudioIn.type},
                     {"outputSourceType", kAudioOut.type}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_input_add";
        t.title = "Agregar un microfono o entrada de audio";
        t.category = "equipo";
        t.description =
            "QUE ES: suma a la emision una entrada de audio de esta maquina (microfono, linea, interfaz). "
            "PARA QUE: el audio del lugar, un microfono de mano, una consola. COMO SE USA: `device` es el "
            "`value` que informa `audio_devices` en `inputs` (\"default\" = la predeterminada del "
            "sistema). Va a un canal GLOBAL, como el audio de los invitados: suena en TODAS las escenas, "
            "no es un item de escena y no ocupa lugar en el lienzo. Queda guardada y vuelve al arrancar. "
            "Para procesarla (gate, compresor, EQ): `audio_rack` y `audio_module_add` con su nombre.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"device", {{"type", "string"}}}, {"name", {{"type", "string"}, {"description", "opcional"}}}}},
            {"required", {"device"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string dev = a.value("device", "");
            BroadcastEngine& e = rt.engine();
            std::string name = a.value("name", "");
            if (name.empty()) {
                if (dev == "default") {
                    name = "Entrada de audio predeterminada";
                } else {
                    for (const auto& d : e.sourceTypeListItems(kAudioIn.type, kAudioIn.prop))
                        if (d.value == dev) name = d.name;
                }
            }
            if (name.empty()) name = "Microfono";
            SourceSpec spec = ba::app::inputs::audioInputSpec(dev, name);
            if (!rt.addAudioInput(spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            done(ToolResult().data(json{{"nombre", name},
                                        {"dispositivo", dev},
                                        {"nota", "Suena en todas las escenas (canal global)."}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_devices";
        t.title = "Camaras y capturadoras";
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            "Camaras USB, capturadoras (V4L2 en Linux, DirectShow en Windows, AVFoundation en macOS). Con "
            "`device` (el value de la lista), sus entradas, formatos, resoluciones y cuadros por segundo. "
            "Para agregarla a una escena: video_input_add.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"device", {{"type", "string"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            BroadcastEngine& e = rt.engine();
            json out{{"sourceType", kVideo.type},
                     {"available", e.sourceTypeExists(kVideo.type)},
                     {"devices", listToJson(e.sourceTypeListItems(kVideo.type, kVideo.prop))}};
            if (a.contains("device")) {
                const json s{{kVideo.prop, a["device"]}};
#if !defined(_WIN32) && !defined(__APPLE__)
                out["inputs"] = listToJson(e.sourceTypeListItems(kVideo.type, "input", s.dump()));
                out["formats"] = listToJson(e.sourceTypeListItems(kVideo.type, "pixelformat", s.dump()));
                json fmts = json::array();
                for (const auto& f : out["formats"]) {
                    json s2 = s;
                    s2["pixelformat"] = std::stoll(f.value("value", std::string("0")));
                    fmts.push_back({{"format", f},
                                    {"resolutions",
                                     listToJson(e.sourceTypeListItems(kVideo.type, "resolution", s2.dump()))},
                                    {"framerates", listToJson(e.sourceTypeListItems(kVideo.type, "framerate",
                                                                                    s2.dump()))}});
                }
                out["modes"] = fmts;
#else
                out["resolutions"] = listToJson(e.sourceTypeListItems(kVideo.type, "resolution", s.dump()));
#endif
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
