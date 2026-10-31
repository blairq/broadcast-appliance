// ============================================================================
// src/app/mcp/McpSystemTools.cpp — El equipo: monitores, audio, video, GPU
//
// Lo que el asistente necesita saber de la maquina para decidir: cuantos
// monitores y de que resolucion (para repartir ventanas de salida), que placas
// de audio de entrada y salida hay, que camaras y capturadoras (con sus
// formatos), con que GPU dibuja y que codificadores de hardware hay, CPU,
// memoria y disco. Los dispositivos salen del registro de la maquina
// (dispositivos::Dispositivos): la misma lista, con los mismos nombres, de la
// que eligen las paginas de entradas (nada que pueda diferir entre lo listado y
// lo que se puede agregar). Y cuánto se está usando de la máquina ahora
// (sistema_recursos, de ui/RecursosViewModel): CPU, GPU, RAM, red, con picos,
// estados por umbral y el margen, para saber cuántas salidas más aguanta.
// ============================================================================

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QStorageInfo>
#include <QSysInfo>

#include <algorithm>
#include <cstdio>
#include <map>
#include <thread>

#include "i18n/I18n.hpp"
#include "diag/VigiaBucle.hpp"
#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "arranque/InicioConElSistema.hpp"
#include "video/ElementosGst.hpp"
#include "audio/EntradasAudio.hpp"
#include "video/Camaras.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "pantallas/Monitores.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
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

json listToJson(const std::vector<BroadcastEngine::ListItem>& items) {
    json out = json::array();
    for (const auto& i : items) {
        json e{{"name", i.name}, {"value", i.value}};
        if (i.disabled) e["disabled"] = true;
        out.push_back(e);
    }
    return out;
}

/// Los dispositivos de un tipo del registro de la máquina (dispositivos::Dispositivos,
/// la página «Dispositivos y equipos»): con el NOMBRE que les puso el operador y el
/// técnico al lado, si están conectados, y `value` (lo que guarda una entrada).
/// Es la misma lista de la que eligen las páginas de entradas: lo que el
/// asistente nombra es lo que el operador ve.
json delRegistro(const dispositivos::Dispositivos& d, const QString& tipo) {
    json out = json::array();
    for (const QVariant& v : d.equipos(tipo)) {
        const QVariantMap e = v.toMap();
        json j{{"name", e.value("nombre").toString().toStdString()},
               {"tecnico", e.value("tecnico").toString().toStdString()},
               {"value", e.value("valor").toString().toStdString()},
               {"id", e.value("id").toString().toStdString()},
               {"conectado", e.value("conectado").toBool()}};
        if (!e.value("conectado").toBool()) j["estado"] = e.value("estado").toString().toStdString();
        out.push_back(j);
    }
    return out;
}

/// El `value` de un dispositivo de video por su value, su id del registro o su nombre.
std::string valorDeVideo(const dispositivos::Dispositivos* d, const std::string& quien) {
    if (d == nullptr) return quien;
    const QString q = QString::fromStdString(quien);
    for (const QVariant& v : d->equipos("camara_local")) {
        const QVariantMap e = v.toMap();
        if (e.value("valor").toString() == q || e.value("id").toString() == q ||
            e.value("nombre").toString().compare(q, Qt::CaseInsensitive) == 0 ||
            e.value("tecnico").toString().compare(q, Qt::CaseInsensitive) == 0)
            return e.value("valor").toString().toStdString();
    }
    return quien;
}

/// Los monitores en su ORDEN FÍSICO y con su id estable (los de Salidas →
/// Pantallas, MonitoresModel), con lo que dice Qt de cada uno (escala, Hz…). La
/// misma lista que la página, monitor_listar y los menús de las ventanas.
json screensJson(MonitoresModel* mon) {
    json out = json::array();
    const QScreen* primary = QGuiApplication::primaryScreen();
    const auto datos = [&](const QScreen* s, json j) {
        const QRect g = s->geometry();
        const QRect a = s->availableGeometry();
        const qreal dpr = s->devicePixelRatio();
        j["name"] = s->name().toStdString();
        j["manufacturer"] = s->manufacturer().toStdString();
        j["model"] = s->model().toStdString();
        j["primary"] = s == primary;
        j["geometry"] = {{"x", g.x()}, {"y", g.y()}, {"width", g.width()}, {"height", g.height()}};
        j["available"] = {{"x", a.x()}, {"y", a.y()}, {"width", a.width()}, {"height", a.height()}};
        j["pixels"] = {{"width", static_cast<int>(g.width() * dpr)}, {"height", static_cast<int>(g.height() * dpr)}};
        j["scale"] = dpr;
        j["refreshHz"] = s->refreshRate();
        j["physicalMm"] = {{"width", s->physicalSize().width()}, {"height", s->physicalSize().height()}};
        return j;
    };
    if (mon == nullptr) {
        int idx = 0;
        for (const QScreen* s : QGuiApplication::screens()) out.push_back(datos(s, json{{"index", idx++}}));
        return out;
    }
    for (const QVariant& v : mon->conectadosEnOrden()) {
        const QVariantMap m = v.toMap();
        for (const QScreen* s : QGuiApplication::screens())
            if (s->name() == m.value("sistema").toString())
                out.push_back(datos(s, json{{"id", m.value("id").toString().toStdString()},
                                            {"orden", m.value("orden").toInt()},
                                            {"nombre", m.value("nombre").toString().toStdString()}}));
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
        "equipo", ba::i18n::traducir("mcp", "El equipo"),
        ba::i18n::traducir("mcp", "Monitores (para repartir ventanas de salida), placas de audio, cámaras y "
                                  "capturadoras con sus formatos, GPU y codificadores, CPU, memoria y disco."));
    AppRuntime& rt = rt_;
    {
        Tool t;
        t.name = "system_hardware";
        t.title = ba::i18n::traducir("mcp", "El equipo: monitores, audio, video, GPU");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "El hardware y cuanto queda: espacio en DISCO, memoria, procesador, temperatura. Todo el "
            "hardware de una vez: monitores (nombre, resolucion, posicion, escala, Hz, principal), "
            "placas de audio de entrada y de salida, camaras y capturadoras, GPU y con que dibuja la app, "
            "codificadores de video (los de hardware marcados), CPU, memoria, disco y sistema operativo. "
            "Para los formatos de una camara: video_devices.");
        dispositivos::Dispositivos* reg = vm_.equipos();
        MonitoresModel* mon = vm_.monitoresModelo();
        t.run = [&rt, reg, mon](const json&, Done done) {
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
                     {"monitors", screensJson(mon)},
                     // Del registro (con el nombre del operador), como audio_devices y video_devices.
                     {"audioInputs", reg ? delRegistro(*reg, "microfono") : listToJson(ba::app::audio::entradasDeAudio(e))},
                     {"audioOutputs", reg ? delRegistro(*reg, "salida_audio") : json::array()},
                     {"videoDevices", reg ? delRegistro(*reg, "camara_local") : listToJson(ba::app::video::camarasDelEquipo(e))},
                     {"gpu", {{"renderer", e.graphicsDeviceName()}, {"cards", gpusJson()}}},
                     {"videoEncoders", encoders}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sistema_recursos";
        t.title = ba::i18n::traducir("mcp", "Cuánto se usa de la máquina (CPU, GPU, RAM, red)");
        t.category = "equipo";
        t.readOnly = true;
        t.description = ba::i18n::traducir(
            "mcp",
            "Lo que se está usando AHORA de la máquina, medido cada segundo: CPU de toda la máquina y de nuestros "
            "procesos (app, gateway, launcher: % y núcleos, memoria residente), RAM usada y total, GPU (% de uso) y "
            "memoria de video, red (Mbps que entran y salen por cada interfaz física; % del enlace si se sabe su "
            "velocidad), escritura de disco y el motor (cuadros por segundo, % de cuadros perdidos, kbps que salen). "
            "Cada recurso trae `pico` (de los últimos 10 minutos), `promedio_10s`, `estado` (normal, aviso desde el "
            "80 %, critico desde el 92 %, sostenido `umbrales.sostenido_s` segundos; desconocido si el sistema no lo "
            "dice: nunca se inventa) y `margen` (lo que le queda). `margen` general: el del recurso más cargado y cuál "
            "`limita`. Sirve para decidir si la máquina aguanta otra salida (otra composición con sus codificadores): "
            "medí antes y después de sumarla y compará el margen. Con `historial_s` (hasta 600), la historia "
            "(columnas + filas, agrupada de a `paso_s` segundos con el máximo de cada grupo). La alarma `recursos` "
            "(alert_create) avisa sola cuando la máquina se acerca al límite. `bucle_ui`: cuánto se demora el bucle "
            "de eventos de la interfaz (un turno cada 50 ms: histograma desde el arranque, p50/p99/máximo del último "
            "minuto, y los peores `culpables`: el evento o la herramienta MCP que corría cuando el turno llegó tarde).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"historial_s",
               {{"type", "integer"},
                {"minimum", 0},
                {"maximum", 600},
                {"description", ba::i18n::traducir("mcp", "segundos de historia a devolver (0 = sin historia; hasta 600)")}}},
              {"paso_s",
               {{"type", "integer"},
                {"minimum", 1},
                {"maximum", 60},
                {"description", ba::i18n::traducir("mcp", "agrupar la historia de a N segundos (el máximo de cada grupo; por "
                                                          "defecto 1, o 10 si se piden más de 120 s)")}}}}}};
        RecursosViewModel* rec = vm_.recursos();
        t.run = [rec](const json& a, Done done) {
            if (rec == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La interfaz no esta lista.")));
            const int hist = std::clamp(a.value("historial_s", 0), 0, 600);
            const int paso = std::clamp(a.value("paso_s", hist > 120 ? 10 : 1), 1, 60);
            json out = rec->muestreador().informe(hist, paso);
            if (out.value("muestras", 0) == 0)
                out["nota"] = ba::i18n::traducir("mcp", "Todavía no hay mediciones (se mide una vez por segundo desde el arranque).");
            out["bucle_ui"] = ba::diag::VigiaBucle::interfaz().informe();
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitors";
        t.title = ba::i18n::traducir("mcp", "Monitores");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Monitores conectados en su ORDEN FISICO (de izquierda a derecha y de arriba abajo, el numero que "
            "se ve al identificarlos), con su `id` estable (fabricante|modelo|serie: no cambia con el cable ni "
            "con el orden en que prenden), resolucion en pixeles, posicion, escala, Hz y si es el principal. "
            "Es la misma lista de Salidas -> Pantallas y monitor_listar.");
        MonitoresModel* mon = vm_.monitoresModelo();
        t.run = [mon](const json&, Done done) {
            done(ToolResult().data(
                json{{"monitors", screensJson(mon)},
                     {"hint", ba::i18n::traducir("mcp", "Para mostrar algo: monitor_asignar (fijo, se espera si no esta) u "
                                                        "output_window (ventana suelta; screen = id, orden o "
                                                        "nombre). Lo abierto: dashboard_list.")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_devices";
        t.title = ba::i18n::traducir("mcp", "Placas de audio");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Que placas y conectores de audio tiene la maquina: entradas (microfonos, lineas, interfaces) y "
            "salidas (parlantes, auriculares, HDMI). Salen del registro de la maquina (Dispositivos y equipos): "
            "`name` es el nombre que les puso el operador (o el del sistema si no le puso), `tecnico` el del "
            "sistema, `conectado` si esta ahora (los que la maquina ya vio y no estan figuran ausentes). El "
            "`value` de una entrada es el `device` de audio_input_add.");
        dispositivos::Dispositivos* reg = vm_.equipos();
        t.run = [&rt, reg](const json&, Done done) {
            if (reg == nullptr) {
                BroadcastEngine& e = rt.engine();
                return done(ToolResult().data(json{{"inputs", listToJson(ba::app::audio::entradasDeAudio(e))},
                                                   {"outputs", json::array()}}));
            }
            // Se busca de nuevo (lo recién enchufado aparece) y se contesta con el registro.
            reg->escanear("local", [reg, done](QVariantMap) {
                done(ToolResult().data(json{{"inputs", delRegistro(*reg, "microfono")},
                                            {"outputs", delRegistro(*reg, "salida_audio")},
                                            {"nota", ba::i18n::traducir("mcp", "Para ponerles nombre: equipo_renombrar (o la "
                                                                               "pagina Dispositivos y equipos).")}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_input_add";
        t.title = ba::i18n::traducir("mcp", "Agregar un microfono o entrada de audio");
        t.category = "equipo";
        t.description =
            ba::i18n::traducir("mcp", "QUE ES: suma a la emision una entrada de audio de esta maquina (microfono, linea, interfaz). "
            "PARA QUE: el audio del lugar, un microfono de mano, una consola. COMO SE USA: `device` es el "
            "`value` que informa `audio_devices` en `inputs` (\"default\" = la predeterminada del "
            "sistema). Va a un canal GLOBAL, como el audio de los invitados: suena en TODAS las escenas, "
            "no es un item de escena y no ocupa lugar en el lienzo. Queda guardada y vuelve al arrancar. "
            "Para procesarla (gate, compresor, EQ): `audio_rack` y `audio_module_add` con su nombre.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"device", {{"type", "string"}}}, {"name", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "opcional")}}}}},
            {"required", {"device"}}};
        // Por el mismo camino que la página Entradas de audio (InputsViewModel): el
        // mismo nombre por defecto (el del operador) y la misma validación.
        auto* entradas = static_cast<InputsViewModel*>(vm_.inputs());
        t.run = [entradas](const json& a, Done done) {
            if (entradas == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La interfaz no esta lista.")));
            const QString dev = QString::fromStdString(a.value("device", std::string()));
            QString name = QString::fromStdString(a.value("name", std::string())).trimmed();
            if (name.isEmpty()) name = entradas->audioInputName(dev);
            const QString err = entradas->addAudioInput(dev, name);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(json{{"nombre", name.toStdString()},
                                        {"dispositivo", dev.toStdString()},
                                        {"nota", ba::i18n::traducir("mcp", "Suena en todas las escenas (canal global).")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "gst_elementos";
        t.title = ba::i18n::traducir("mcp", "Elementos de GStreamer que hacen falta");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Si están los elementos de GStreamer que usa el aparato: los imprescindibles (sin ellos algo de hoy no anda: "
            "invitados, racks, voces, NDI…) y los del motor que viene (mezcla, composición, robustez, subtítulos). "
            "Para diagnosticar silencio o negro en una plataforma: un plugin que falta aparece acá.");
        t.run = [](const json&, Done done) {
            const json l = video::revisarElementosGst();
            json faltan = json::array();
            for (const auto& e : l)
                if (!e.value("esta", true)) faltan.push_back(e);
            done(ToolResult().data({{"faltan", faltan}, {"todos", l}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "inicio_con_el_equipo";
        t.title = ba::i18n::traducir("mcp", "Arrancar solo al encender el equipo");
        t.category = "equipo";
        t.description =
            ba::i18n::traducir("mcp", "Que el aparato vuelva solo después de un corte de luz. Sin argumentos: si arranca solo con el equipo y "
            "qué falta del sistema (que la máquina se encienda al volver la luz y que la sesión se inicie sola: eso "
            "lo hace una persona con permisos de administrador). Con `activo`: lo prende o lo apaga. Al volver, el "
            "programa retoma solo la transmisión y la grabación que estaban andando.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        t.run = [](const json& a, Done done) {
            if (a.contains("activo")) {
                if (const std::string e = inicio::poner(a.value("activo", false)); !e.empty())
                    return done(ToolResult::error(e));
            }
            done(ToolResult().data(inicio::estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_input_remove";
        t.title = ba::i18n::traducir("mcp", "Quitar un microfono o entrada de audio");
        t.category = "equipo";
        t.destructive = true;
        t.description =
            ba::i18n::traducir("mcp", "Saca de la emision una entrada de audio del equipo (las que agrega audio_input_add; mixer_list "
            "las muestra). Deja de sonar en todas las escenas y no vuelve al arrancar.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string name = a.value("name", "");
            const auto& l = rt.config().audioInputs;
            if (std::none_of(l.begin(), l.end(), [&](const SourceSpec& s) { return s.name == name; }))
                return done(ToolResult::error(
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay una entrada de audio «%1»"), {name})));
            rt.removeAudioInput(name);
            rt.save();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Quitada: %1"), {name})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_devices";
        t.title = ba::i18n::traducir("mcp", "Camaras y capturadoras");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Camaras USB, capturadoras y cualquier dispositivo de video (por GStreamer: V4L2 en Linux, Media "
            "Foundation en Windows, AVFoundation en macOS), del registro de la maquina: `name` es el nombre que "
            "les puso el operador, `tecnico` el del sistema, `conectado` si esta ahora. Con `device` (el value, "
            "el id o el nombre): sus `modos` "
            "(tamaño, cps, formato real: crudo, mjpeg, h264, h265; quién lo decodifica y si la GPU; "
            "`convertido_por_el_sistema`: un crudo que el sistema fabrica decodificando en la CPU) y `automatico` "
            "(el que se elige solo para el lienzo, con `porque`). Para todo lo que anuncia el dispositivo (API, "
            "propiedades, caps crudas): dispositivos_video. Para agregarla: video_input_add (`modo` = un `valor`).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"device", {{"type", "string"}}}}}};
        dispositivos::Dispositivos* reg = vm_.equipos();
        t.run = [&rt, reg](const json& a, Done done) {
            // La lista, del registro de la máquina (nombre del operador y técnico al
            // lado); los modos, por GStreamer, del dispositivo pedido.
            auto contestar = [&rt, reg, a, done]() {
                BroadcastEngine& e = rt.engine();
                json out{{"available", e.sourceTypeExists(ba::app::inputs::kMicType)},
                         {"devices", reg ? delRegistro(*reg, "camara_local")
                                         : listToJson(ba::app::video::camarasDelEquipo(e))}};
                if (a.contains("device")) {
                    const std::string dev = valorDeVideo(reg, a.value("device", std::string()));
                    out["device"] = dev;
                    json modos = json::array();
                    for (const auto& m : ba::app::video::modosDeCamara(dev)) modos.push_back(m.json());
                    out["modos"] = modos;
                    std::string porque;
                    const auto am = ba::app::video::modoAutomatico(dev, &porque);
                    json auto_ = am.ancho > 0 ? am.json() : json::object();
                    auto_["porque"] = porque;
                    out["automatico"] = auto_;
                }
                done(ToolResult().data(out));
            };
            if (reg == nullptr) return contestar();
            reg->escanear("local", [contestar](QVariantMap) { contestar(); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dispositivos_video";
        t.title = ba::i18n::traducir("mcp", "Todo lo que anuncia cada dispositivo de video");
        t.category = "equipo";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Como gst-device-monitor, pero de la app: cada dispositivo de video con su API (mediafoundation, "
            "ksvideo, avf, v4l2), propiedades, el elemento que lo abre, si la app lo usa (`usable`; en Windows la "
            "misma cámara aparece también por KS y se usa la de Media Foundation), las `caps` crudas tal cual las "
            "anuncia, los `modos` interpretados (formato real y decodificador) y el `automatico` para el lienzo con su "
            "porqué. `device` para uno solo. Sirve para entender por qué una cámara no da lo que se espera.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"device", {{"type", "string"}}}, {"sinCaps", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "sin las caps crudas (más corto)")}}}}}};
        t.run = [](const json& a, Done done) {
            json f = ba::app::video::fichasDeVideo(a.value("device", std::string()));
            if (a.value("sinCaps", false))
                for (auto& x : f) x.erase("caps");
            done(ToolResult().data({{"dispositivos", f}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
