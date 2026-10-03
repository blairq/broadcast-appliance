// src/app/plugins/Plugins.cpp — ver Plugins.hpp

#include "plugins/Plugins.hpp"

#include "AppRuntime.hpp"
#include "platform/Paths.hpp"
#include "ptz/ControlCamaras.hpp"
#include "ptz/PtzPlugin.hpp"
#include "ui/OperatorMessages.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QUrl>
#include <QProcessEnvironment>

#include <obs.h>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace ba::app::plugins {

namespace {
using json = nlohmann::json;

QString q(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
QString q(const std::string& s) { return QString::fromStdString(s); }

QVariant qv(const json& j) {
    if (j.is_null()) return {};
    if (j.is_array() || j.is_object()) return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).toVariant();
    return QJsonDocument::fromJson(QByteArray::fromStdString(json::array({j}).dump())).toVariant().toList().value(0);
}

json leerJson(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return json::object();
    json j = json::parse(f, nullptr, false);
    return j.is_discarded() ? json::object() : j;
}

bool escribirTexto(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << s;
    return static_cast<bool>(f);
}

std::string leerTexto(const fs::path& p, size_t max = 200000) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.size() > max) s = s.substr(0, max) + "\n…(cortado)";
    return s;
}

/// Un id es una carpeta: minúsculas, números y guiones.
bool idValido(const std::string& id) {
    if (id.empty() || id.size() > 64 || id[0] == '.' || id[0] == '_') return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

/// Una ruta relativa adentro de la carpeta del plugin (nada de «..», ni absolutas, ni _libs).
bool rutaValida(const std::string& r) {
    if (r.empty() || r[0] == '/' || r[0] == '\\' || r.find(':') != std::string::npos) return false;
    const fs::path p(r);
    for (const auto& parte : p)
        if (parte == ".." || parte == "_libs") return false;
    return true;
}

bool coincide(const std::string& patron, const std::string& nombre) {
    // "plugin/canal", "*/canal", "plugin/*"
    const auto a = patron.find('/'), b = nombre.find('/');
    const std::string pp = a == std::string::npos ? "*" : patron.substr(0, a), pc = a == std::string::npos ? patron : patron.substr(a + 1);
    const std::string np = nombre.substr(0, b), nc = b == std::string::npos ? "" : nombre.substr(b + 1);
    return (pp == "*" || pp == np) && (pc == "*" || pc == nc);
}

/// Que un proceso avance aunque Qt no avise. Pasó de verdad: después de usar
/// el monitor de dispositivos de GStreamer (y alguna otra vez), un QProcess
/// quedaba en «Starting» para siempre —nunca llegaba `started`, ni su salida,
/// ni `finished`— y el plugin «no arrancaba» o una herramienta se colgaba.
/// Cada tanto se le pregunta a mano (esperas de 0 ms: no traba nada).
void vigilar(QProcess* p, int ms = 250) {
    auto* t = new QTimer(p);
    QObject::connect(t, &QTimer::timeout, p, [p]() {
        if (p->state() == QProcess::Starting) p->waitForStarted(0);
        else if (p->state() == QProcess::Running && !p->waitForReadyRead(0)) p->waitForFinished(0);
    });
    t->start(ms);
}

constexpr int kMaxCaidas = 5;
constexpr qint64 kVentanaCaidasMs = 60'000;
} // namespace

struct Plugins::Proceso {
    std::unique_ptr<QProcess> p;
    QByteArray pendiente; ///< stdout sin línea completa
    QByteArray errPendiente;
    std::string estado = "apagado"; ///< apagado | arrancando | corriendo | reiniciando | caido | dependencias
    std::string error;
    json estadoPlugin;               ///< lo que dijo con plugin.estado(...)
    json declarado = json::array();  ///< entradas, tarjetas, ptz, controles, escuchas
    std::vector<std::string> escucha;
    std::deque<std::string> registro;
    int puerto = 0;
    qint64 desde = 0;
    std::vector<qint64> caidas;
    int reinicios = 0;
    bool listo = false;
    bool porPedido = false; ///< se cerró porque se pidió (no es una caída)
    std::vector<std::function<void(json)>> alListo;
    QTimer* reintento = nullptr;
    QTimer* plazo = nullptr;
    std::string python;
    std::string sdkVersion;
};

Plugins::Plugins(AppRuntime& rt, video::VideoGrafos* video, ptz::ControlCamaras* camaras, OperatorMessages* mensajes, QObject* parent)
    : QObject(parent), rt_(rt), video_(video), camaras_(camaras), mensajes_(mensajes) {
    carpeta_ = rt.dataRoot() / "Plugins";
    logs_ = rt.dataRoot() / "Logs" / "plugins";
    config_ = rt.dataRoot() / "Config" / "plugins.json";
    std::error_code ec;
    fs::create_directories(carpeta_, ec);
    fs::create_directories(logs_, ec);
    // El PTZ «plugin» manda sus órdenes por acá.
    auto& e = ptz::PtzPlugin::enlace();
    e.pedir = [this](const QString& plugin, const std::string& accion, const json& args) {
        pedir(plugin.toStdString(), accion, args, [plugin, accion](json r) {
            if (!r.value("ok", false))
                blog(LOG_WARNING, "[plugins] %s %s: %s", plugin.toUtf8().constData(), accion.c_str(), r.value("error", std::string()).c_str());
        });
    };
    e.capacidades = [this](const QString& plugin, const QString& entrada) -> json {
        auto it = procesos_.find(plugin.toStdString());
        if (it == procesos_.end() || it->second->estado != "corriendo") return nullptr;
        for (const auto& d : it->second->declarado)
            if (d.value("t", "") == "ptz" && d.value("entrada", "") == entrada.toStdString()) return d.value("capacidades", json::object());
        return nullptr;
    };
}

Plugins::~Plugins() {
    auto& e = ptz::PtzPlugin::enlace();
    e.pedir = nullptr;
    e.capacidades = nullptr;
    for (auto& [id, pr] : procesos_) {
        if (!pr->p) continue;
        pr->porPedido = true;
        pr->p->disconnect(this);
        pr->p->write("{\"t\":\"chau\"}\n");
        pr->p->closeWriteChannel();
        if (!pr->p->waitForFinished(1500)) pr->p->kill();
        pr->p->waitForFinished(500);
    }
    for (auto& [n, pe] : pendientes_) delete pe.timer;
}

fs::path Plugins::python() const {
#if defined(_WIN32)
    return ba::platform::bundleRoot() / "python" / "python.exe";
#else
    return ba::platform::bundleRoot() / "python" / "bin" / "python3";
#endif
}
fs::path Plugins::sdk() const { return ba::platform::bundleRoot() / "plugins-python" / "sdk"; }
fs::path Plugins::ejemplos() const { return ba::platform::bundleRoot() / "plugins-python" / "ejemplos"; }

// --- Guardado ------------------------------------------------------------------------------

json Plugins::leerActivos() const {
    const json j = leerJson(config_);
    return j.value("activos", json::array());
}

void Plugins::guardarActivos() const {
    json activos = json::array();
    for (const auto& [id, pr] : procesos_)
        if (pr->estado != "apagado") activos.push_back(id);
    escribirTexto(config_, json{{"activos", activos}}.dump(1));
}

void Plugins::arrancarGuardados() {
    for (const auto& id : leerActivos())
        if (id.is_string() && fs::exists(carpetaDe(id.get<std::string>()) / "plugin.json"))
            activar(id.get<std::string>(), true, [](json) {});
}

// --- Lo que hay -----------------------------------------------------------------------------

json Plugins::estado(const std::string& id) const {
    const json man = leerJson(carpetaDe(id) / "plugin.json");
    json e{{"id", id},
           {"nombre", man.value("nombre", id)},
           {"descripcion", man.value("descripcion", std::string())},
           {"version", man.value("version", std::string())},
           {"dependencias", man.value("dependencias", json::array())},
           {"carpeta", carpetaDe(id).string()},
           {"estado", "apagado"}};
    auto it = procesos_.find(id);
    if (it == procesos_.end()) return e;
    const Proceso& p = *it->second;
    e["estado"] = p.estado;
    if (!p.error.empty()) e["error"] = p.error;
    if (!p.estadoPlugin.is_null()) e["dice"] = p.estadoPlugin;
    if (p.estado == "corriendo") {
        e["desde_s"] = (QDateTime::currentMSecsSinceEpoch() - p.desde) / 1000;
        e["puerto_medios"] = p.puerto;
        e["pid"] = p.p ? static_cast<long long>(p.p->processId()) : 0;
    }
    if (p.reinicios) e["reinicios"] = p.reinicios;
    json ent = json::array(), tar = json::array(), ptz = json::array(), ctl = json::array(), esc = json::array();
    for (const auto& d : p.declarado) {
        const std::string t = d.value("t", "");
        json x = d;
        x.erase("t");
        if (t == "entrada") ent.push_back(x);
        else if (t == "tarjeta") tar.push_back(x);
        else if (t == "ptz") ptz.push_back(x);
        else if (t == "control") ctl.push_back(x);
        else if (t == "escuchar") esc.push_back(d.value("canal", ""));
    }
    e["entradas"] = ent;
    e["tarjetas"] = tar;
    e["ptz"] = ptz;
    e["controles"] = ctl;
    if (!esc.empty()) e["escucha"] = esc;
    return e;
}

json Plugins::listar() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(carpeta_, ec)) {
        const std::string id = d.path().filename().string();
        // .herramientas, .papelera: de la app, no plugins.
        if (!d.is_directory() || id.empty() || id[0] == '.' || !fs::exists(d.path() / "plugin.json")) continue;
        out.push_back(estado(id));
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a.value("nombre", "") < b.value("nombre", ""); });
    return out;
}

json Plugins::ejemplosDisponibles() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(ejemplos(), ec)) {
        const json man = leerJson(d.path() / "plugin.json");
        if (man.empty()) continue;
        out.push_back({{"ejemplo", d.path().filename().string()}, {"nombre", man.value("nombre", std::string())},
                       {"descripcion", man.value("descripcion", std::string())}});
    }
    return out;
}

json Plugins::ver(const std::string& id, const std::string& archivo) const {
    const fs::path dir = fs::exists(carpetaDe(id)) ? carpetaDe(id) : ejemplos() / id;
    if (!idValido(id) || !fs::exists(dir / "plugin.json")) return {{"error", "no existe el plugin «" + id + "»"}};
    json archivos = json::array();
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto rel = fs::relative(it->path(), dir, ec);
        const std::string r = rel.generic_string();
        if (r.rfind("_libs", 0) == 0 || r.rfind("datos", 0) == 0 || r.find("__pycache__") != std::string::npos) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file()) archivos.push_back({{"archivo", r}, {"bytes", static_cast<long long>(it->file_size(ec))}});
    }
    const json man = leerJson(dir / "plugin.json");
    const std::string a = archivo.empty() ? man.value("principal", std::string("plugin.py")) : archivo;
    json out{{"id", id}, {"carpeta", dir.string()}, {"manifiesto", man}, {"archivos", archivos}};
    if (rutaValida(a)) {
        out["archivo"] = a;
        out["contenido"] = leerTexto(dir / a);
    }
    if (fs::exists(carpetaDe(id))) out["estado"] = estado(id);
    return out;
}

// --- Escribir ---------------------------------------------------------------------------------

std::string Plugins::escribir(const std::string& id, const json& archivos, const json& manifiesto, json* hecho) {
    if (!idValido(id)) return "id «" + id + "»: minúsculas, números y guiones (es el nombre de la carpeta)";
    const fs::path dir = carpetaDe(id);
    json man = leerJson(dir / "plugin.json");
    const bool nuevo = man.empty();
    if (manifiesto.is_object()) man.update(manifiesto);
    man["id"] = id;
    if (!man.contains("principal")) man["principal"] = "plugin.py";
    if (!man.contains("nombre")) man["nombre"] = id;
    if (!man.contains("version")) man["version"] = "1.0";
    if (!man.contains("dependencias")) man["dependencias"] = json::array();
    json escritos = json::array();
    if (archivos.is_object()) {
        for (const auto& [ruta, contenido] : archivos.items()) {
            if (!rutaValida(ruta) || ruta == "plugin.json") continue;
            if (!contenido.is_string()) return "el contenido de «" + ruta + "» tiene que ser texto";
            if (!escribirTexto(dir / ruta, contenido.get<std::string>())) return "no se pudo escribir «" + ruta + "»";
            escritos.push_back(ruta);
        }
    }
    if (!escribirTexto(dir / "plugin.json", man.dump(2))) return "no se pudo escribir plugin.json";
    std::error_code ec;
    fs::create_directories(dir / "datos", ec);
    if (hecho != nullptr)
        *hecho = {{"id", id}, {"carpeta", dir.string()}, {"nuevo", nuevo}, {"escritos", escritos}, {"manifiesto", man}};
    emit cambio();
    return {};
}

std::string Plugins::desdeEjemplo(const std::string& ejemplo, const std::string& id) {
    if (!idValido(id)) return "id «" + id + "»: minúsculas, números y guiones";
    const fs::path desde = ejemplos() / ejemplo;
    if (!fs::exists(desde / "plugin.json")) return "no existe el ejemplo «" + ejemplo + "»";
    if (fs::exists(carpetaDe(id))) return "ya hay un plugin «" + id + "»";
    std::error_code ec;
    fs::copy(desde, carpetaDe(id), fs::copy_options::recursive, ec);
    if (ec) return "no se pudo copiar: " + ec.message();
    json man = leerJson(carpetaDe(id) / "plugin.json");
    man["id"] = id;
    escribirTexto(carpetaDe(id) / "plugin.json", man.dump(2));
    emit cambio();
    return {};
}

std::string Plugins::borrar(const std::string& id) {
    if (!idValido(id) || !fs::exists(carpetaDe(id))) return "no existe el plugin «" + id + "»";
    detener(id, true);
    procesos_.erase(id);
    guardarActivos();
    // No se borra: se mueve a Plugins/.papelera (un plugin hecho a mano vale).
    std::error_code ec;
    const fs::path pap = carpeta_ / ".papelera" / (id + "-" + std::to_string(QDateTime::currentSecsSinceEpoch()));
    fs::create_directories(pap.parent_path(), ec);
    fs::rename(carpetaDe(id), pap, ec);
    if (ec) return "no se pudo mover a la papelera: " + ec.message();
    emit cambio();
    return {};
}

// --- Dependencias y prueba ----------------------------------------------------------------------

void Plugins::dependencias(const std::string& id, std::function<void(std::string, std::string)> listo) {
    const fs::path dir = carpetaDe(id);
    const json man = leerJson(dir / "plugin.json");
    if (man.empty()) return listo("no existe el plugin «" + id + "»", {});
    json deps = man.value("dependencias", json::array());
    if (!deps.is_array() || deps.empty()) return listo({}, {});
    std::vector<std::string> lista;
    for (const auto& d : deps)
        if (d.is_string()) lista.push_back(d.get<std::string>());
    std::sort(lista.begin(), lista.end());
    std::string firma;
    for (const auto& d : lista) firma += d + "\n";
    const fs::path sello = dir / "_libs" / ".dependencias";
    if (leerTexto(sello) == firma) return listo({}, "ya estaban");
    if (!fs::exists(python())) return listo("falta el Python del programa (" + python().string() + ")", {});
    auto* p = new QProcess(this);
    QStringList args{"-I", "-B", "-m", "pip", "install", "--disable-pip-version-check", "--no-warn-script-location", "--upgrade",
                     "--target", q(dir / "_libs")};
    for (const auto& d : lista) args << q(d);
    p->setProcessChannelMode(QProcess::MergedChannels);
    vigilar(p);
    auto it = procesos_.find(id);
    if (it != procesos_.end() && it->second->estado == "apagado") it->second->estado = "dependencias";
    connect(p, &QProcess::finished, this, [this, p, listo, sello, firma, id](int codigo, QProcess::ExitStatus) {
        const std::string salida = p->readAll().toStdString();
        p->deleteLater();
        escribirRegistro(id, QByteArray::fromStdString("[pip] " + salida));
        if (codigo != 0) return listo("pip no pudo bajar las dependencias", salida.size() > 3000 ? salida.substr(salida.size() - 3000) : salida);
        escribirTexto(sello, firma);
        listo({}, salida.size() > 800 ? salida.substr(salida.size() - 800) : salida);
    });
    p->start(q(python()), args);
}

void Plugins::probar(const std::string& id, double segundos, const json& controles, bool ptz, std::function<void(json)> listo0) {
    // El último informe de cada uno queda para la interfaz.
    auto listo = [this, id, listo0](json r) {
        r["cuando"] = QDateTime::currentDateTime().toString("HH:mm:ss").toStdString();
        pruebas_[id] = r;
        emit cambio();
        listo0(r);
    };
    pruebas_[id] = {{"probando", true}};
    emit cambio();
    dependencias(id, [this, id, segundos, controles, ptz, listo](std::string err, std::string salida) {
        if (!err.empty()) return listo(json{{"ok", false}, {"error", err}, {"pip", salida}});
        auto* p = new QProcess(this);
        QStringList args{"-I", "-B", q(sdk() / "ba_plugin" / "probar.py"), q(carpetaDe(id)), "--segundos", QString::number(segundos)};
        if (controles.is_array())
            for (const auto& c : controles)
                args << "--control" << q(c.value("nombre", std::string())) << q(c.value("args", json::object()).dump());
        if (ptz) args << "--ptz";
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString& k : env.keys())
            if (k.startsWith("PYTHON")) env.remove(k);
        p->setProcessEnvironment(env);
        vigilar(p);
        auto* plazo = new QTimer(p);
        plazo->setSingleShot(true);
        connect(plazo, &QTimer::timeout, p, [p]() { p->kill(); });
        plazo->start(static_cast<int>((segundos + 60) * 1000));
        connect(p, &QProcess::finished, this, [p, listo](int, QProcess::ExitStatus st) {
            const QByteArray out = p->readAllStandardOutput(), err = p->readAllStandardError();
            p->deleteLater();
            json r = json::parse(out.toStdString(), nullptr, false);
            if (r.is_discarded() || !r.is_object())
                r = {{"ok", false},
                     {"error", st == QProcess::CrashExit ? "el probador se cortó (¿tardó demasiado?)" : "el probador no devolvió un informe"},
                     {"salida", out.right(2000).toStdString()},
                     {"registro", err.right(3000).toStdString()}};
            listo(r);
        });
        p->start(q(python()), args);
    });
}

// --- Correr ---------------------------------------------------------------------------------------

void Plugins::activar(const std::string& id, bool activo, std::function<void(json)> listo) {
    if (!idValido(id) || !fs::exists(carpetaDe(id) / "plugin.json")) return listo(json{{"error", "no existe el plugin «" + id + "»"}});
    auto& pr = procesos_[id];
    if (!pr) pr = std::make_unique<Proceso>();
    if (!activo) {
        detener(id, true);
        pr->estado = "apagado";
        guardarActivos();
        emit cambio();
        return listo(estado(id));
    }
    if (pr->estado == "corriendo") return listo(estado(id));
    pr->alListo.push_back(listo);
    if (pr->estado == "arrancando" || pr->estado == "dependencias") return;
    pr->caidas.clear();
    pr->estado = "dependencias";
    pr->error.clear();
    guardarActivos();
    emit cambio();
    dependencias(id, [this, id](std::string err, std::string salida) {
        auto it = procesos_.find(id);
        if (it == procesos_.end() || it->second->estado != "dependencias") return;
        if (!err.empty()) {
            it->second->estado = "caido";
            it->second->error = err + (salida.empty() ? "" : ": " + salida.substr(salida.size() > 600 ? salida.size() - 600 : 0));
            auto cbs = std::move(it->second->alListo);
            for (auto& cb : cbs) cb(estado(id));
            emit cambio();
            return;
        }
        arrancar(id);
    });
}

void Plugins::reiniciar(const std::string& id, std::function<void(json)> listo) {
    detener(id, true);
    auto it = procesos_.find(id);
    if (it != procesos_.end()) it->second->estado = "apagado";
    activar(id, true, std::move(listo));
}

void Plugins::arrancar(const std::string& id) {
    auto& pr = procesos_[id];
    if (!pr) pr = std::make_unique<Proceso>();
    Proceso& p = *pr;
    if (p.reintento) {
        p.reintento->stop();
        p.reintento->deleteLater();
        p.reintento = nullptr;
    }
    if (!fs::exists(python())) {
        p.estado = "caido";
        p.error = "falta el Python del programa (" + python().string() + ")";
        auto cbs = std::move(p.alListo);
        for (auto& cb : cbs) cb(estado(id));
        emit cambio();
        return;
    }
    p.p = std::make_unique<QProcess>();
    p.pendiente.clear();
    p.errPendiente.clear();
    p.declarado = json::array();
    p.escucha.clear();
    p.listo = false;
    p.porPedido = false;
    p.puerto = 0;
    p.estadoPlugin = nullptr;
    p.estado = "arrancando";
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys())
        if (k.startsWith("PYTHON")) env.remove(k);
    env.insert("BA_PLUGIN_DIR", q(carpetaDe(id)));
    env.insert("BA_PLUGIN_ID", q(id));
    env.insert("BA_PLUGIN_CANAL", "stdio");
    env.insert("BA_DATA_ROOT", q(rt_.dataRoot()));
    p.p->setProcessEnvironment(env);
    p.p->setWorkingDirectory(q(carpetaDe(id)));
    QProcess* proc = p.p.get();
    connect(proc, &QProcess::started, this, [this, id, proc]() {
        escribirRegistro(id, "[app] proceso " + QByteArray::number(proc->processId()) + " arrancado\n");
    });
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id]() { alLeer(id); });
    vigilar(proc);
    connect(proc, &QProcess::readyReadStandardError, this, [this, id, proc]() { escribirRegistro(id, proc->readAllStandardError()); });
    connect(proc, &QProcess::finished, this, [this, id](int c, QProcess::ExitStatus st) {
        // Fuera de la señal: alSalir puede soltar el QProcess que la emite.
        QTimer::singleShot(0, this, [this, id, c, st]() { alSalir(id, c, st); });
    });
    connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) QTimer::singleShot(0, this, [this, id]() { alError(id); });
    });
    // Tiene 20 s para declararse.
    if (p.plazo) p.plazo->deleteLater();
    p.plazo = new QTimer(this);
    p.plazo->setSingleShot(true);
    connect(p.plazo, &QTimer::timeout, this, [this, id]() {
        auto it = procesos_.find(id);
        if (it == procesos_.end() || it->second->listo || !it->second->p) return;
        it->second->error = "no terminó de arrancar en 20 s (¿se quedó esperando algo antes de plugin.correr()?)";
        escribirRegistro(id, "[app] 20 s sin «listo»: " + QByteArray::number(it->second->p->bytesAvailable()) +
                                 " bytes sin leer, estado del proceso " + QByteArray::number(static_cast<int>(it->second->p->state())) + "\n");
        it->second->p->kill();
    });
    p.plazo->start(20000);
    escribirRegistro(id, "---- arranca " + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + " ----\n");
    proc->start(q(python()), {"-I", "-B", "-u", q(sdk() / "correr.py"), q(carpetaDe(id))});
    emit cambio();
}

void Plugins::detener(const std::string& id, bool porPedido) {
    auto it = procesos_.find(id);
    if (it == procesos_.end()) return;
    Proceso& p = *it->second;
    if (p.reintento) {
        p.reintento->stop();
        p.reintento->deleteLater();
        p.reintento = nullptr;
    }
    if (!p.p) return;
    p.porPedido = porPedido;
    QProcess* proc = p.p.release();
    proc->disconnect(this);
    proc->setParent(this);
    proc->write("{\"t\":\"chau\"}\n");
    proc->closeWriteChannel();
    // Si en 3 s no se fue, se lo mata. Sin esperar acá (no se traba la app).
    QTimer::singleShot(3000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning) proc->kill();
    });
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    anotarPuerto(id, 0);
    p.listo = false;
    p.puerto = 0;
    if (camaras_) camaras_->refrescarPlugins();
}

void Plugins::alError(const std::string& id) {
    auto it = procesos_.find(id);
    if (it == procesos_.end()) return;
    it->second->error = "no se pudo arrancar Python";
    alSalir(id, -1, QProcess::CrashExit);
}

void Plugins::alSalir(const std::string& id, int codigo, QProcess::ExitStatus st) {
    auto it = procesos_.find(id);
    if (it == procesos_.end() || !it->second->p) return;
    Proceso& p = *it->second;
    escribirRegistro(id, p.p->readAllStandardError());
    p.p.reset();
    anotarPuerto(id, 0);
    p.puerto = 0;
    const bool estabaListo = p.listo;
    p.listo = false;
    if (camaras_) camaras_->refrescarPlugins();
    if (p.porPedido || p.estado == "apagado") {
        emit cambio();
        return;
    }
    // Una caída: se reintenta con espera creciente; muchas seguidas, se apaga.
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    p.caidas.push_back(ahora);
    p.caidas.erase(std::remove_if(p.caidas.begin(), p.caidas.end(), [&](qint64 t) { return ahora - t > kVentanaCaidasMs; }), p.caidas.end());
    if (p.error.empty())
        p.error = st == QProcess::CrashExit ? "se cayó" : "terminó (código " + std::to_string(codigo) + ")";
    {
        // La última línea del registro suele ser el motivo.
        for (auto r = p.registro.rbegin(); r != p.registro.rend(); ++r)
            if (!r->empty() && r->rfind("----", 0) != 0) {
                p.error += ": " + r->substr(0, 300);
                break;
            }
    }
    auto cbs = std::move(p.alListo);
    if (!estabaListo)
        for (auto& cb : cbs) cb(estado(id));
    if (static_cast<int>(p.caidas.size()) >= kMaxCaidas) {
        p.estado = "caido";
        avisar("El plugin «" + id + "» se cayó " + std::to_string(kMaxCaidas) + " veces en un minuto y quedó apagado: " + p.error, "warning");
        emit cambio();
        return;
    }
    p.estado = "reiniciando";
    ++p.reinicios;
    const int espera = 1000 * (1 << std::min<int>(static_cast<int>(p.caidas.size()) - 1, 4));
    p.reintento = new QTimer(this);
    p.reintento->setSingleShot(true);
    connect(p.reintento, &QTimer::timeout, this, [this, id]() { arrancar(id); });
    p.reintento->start(espera);
    emit cambio();
}

void Plugins::escribirRegistro(const std::string& id, const QByteArray& datos) {
    if (datos.isEmpty()) return;
    auto it = procesos_.find(id);
    if (it != procesos_.end()) {
        Proceso& p = *it->second;
        p.errPendiente += datos;
        int nl;
        while ((nl = p.errPendiente.indexOf('\n')) >= 0) {
            p.registro.push_back(p.errPendiente.left(nl).toStdString());
            p.errPendiente.remove(0, nl + 1);
            if (p.registro.size() > 400) p.registro.pop_front();
        }
    }
    const fs::path arch = logs_ / (id + ".log");
    std::error_code ec;
    if (fs::exists(arch, ec) && fs::file_size(arch, ec) > 2'000'000) fs::rename(arch, logs_ / (id + ".log.1"), ec);
    std::ofstream f(arch, std::ios::app | std::ios::binary);
    f.write(datos.constData(), datos.size());
}

std::string Plugins::registro(const std::string& id, int lineas) const {
    std::string out;
    auto it = procesos_.find(id);
    if (it != procesos_.end() && !it->second->registro.empty()) {
        const auto& r = it->second->registro;
        const size_t desde = r.size() > static_cast<size_t>(lineas) ? r.size() - static_cast<size_t>(lineas) : 0;
        for (size_t i = desde; i < r.size(); ++i) out += r[i] + "\n";
        return out;
    }
    // Sin proceso (o recién abierta la app): del archivo.
    const std::string s = leerTexto(logs_ / (id + ".log"), 10'000'000);
    size_t pos = s.size();
    for (int k = 0; k <= lineas && pos > 0; ++k) pos = s.rfind('\n', pos - 1);
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

// --- Protocolo ---------------------------------------------------------------------------------------

void Plugins::alLeer(const std::string& id) {
    auto it = procesos_.find(id);
    if (it == procesos_.end() || !it->second->p) return;
    Proceso& p = *it->second;
    p.pendiente += p.p->readAllStandardOutput();
    int nl;
    while ((nl = p.pendiente.indexOf('\n')) >= 0) {
        const QByteArray linea = p.pendiente.left(nl);
        p.pendiente.remove(0, nl + 1);
        json m = json::parse(linea.toStdString(), nullptr, false);
        if (m.is_discarded() || !m.is_object()) {
            escribirRegistro(id, "[app] línea que no es del protocolo (¿un print a stdout?): " + linea.left(200) + "\n");
            continue;
        }
        try {
            recibir(id, m);
        } catch (const std::exception& e) {
            escribirRegistro(id, QByteArray("[app] error con «") + QByteArray::fromStdString(m.value("t", std::string())) + "»: " + e.what() + "\n");
        }
        // recibir() puede haber terminado con el proceso.
        it = procesos_.find(id);
        if (it == procesos_.end() || !it->second->p) return;
    }
    if (p.pendiente.size() > 4 * 1024 * 1024) p.pendiente.clear(); // un plugin que escribe basura por stdout
}

void Plugins::enviar(const std::string& id, const json& m) {
    auto it = procesos_.find(id);
    if (it == procesos_.end() || !it->second->p || it->second->p->state() != QProcess::Running) return;
    it->second->p->write(QByteArray::fromStdString(m.dump() + "\n"));
}

void Plugins::recibir(const std::string& id, const json& m) {
    Proceso& p = *procesos_.at(id);
    const std::string t = m.value("t", "");
    if (t == "hola") {
        escribirRegistro(id, QByteArray::fromStdString("[app] hola (pid " + std::to_string(m.value("pid", 0)) + ", medios en " +
                                                       std::to_string(m.value("medios", 0)) + ")\n"));
        p.puerto = m.value("medios", 0);
        p.python = m.value("python", std::string());
        anotarPuerto(id, p.puerto);
    } else if (t == "entrada" || t == "tarjeta" || t == "ptz" || t == "control" || t == "escuchar") {
        const std::string clave = m.value("nombre", m.value("entrada", m.value("canal", std::string())));
        auto& d = p.declarado;
        d.erase(std::remove_if(d.begin(), d.end(),
                               [&](const json& x) {
                                   return x.value("t", "") == t && x.value("nombre", x.value("entrada", x.value("canal", std::string()))) == clave;
                               }),
                d.end());
        d.push_back(m);
        if (t == "escuchar") p.escucha.push_back(clave);
        if (p.listo) {
            if (t == "entrada") declararEntrada(id, m);
            if (t == "ptz") declararPtz(id, m);
        }
        emit cambio();
    } else if (t == "listo") {
        escribirRegistro(id, "[app] listo: " + QByteArray::number(static_cast<int>(p.declarado.size())) + " declaraciones\n");
        p.listo = true;
        p.estado = "corriendo";
        p.error.clear();
        p.desde = QDateTime::currentMSecsSinceEpoch();
        if (p.plazo) p.plazo->stop();
        // Las entradas primero (el PTZ va sobre una entrada que exista).
        for (const auto& d : json(p.declarado))
            if (d.value("t", "") == "entrada") declararEntrada(id, d);
        for (const auto& d : json(p.declarado))
            if (d.value("t", "") == "ptz") declararPtz(id, d);
        auto cbs = std::move(p.alListo);
        for (auto& cb : cbs) cb(estado(id));
        emit cambio();
    } else if (t == "estado") {
        p.estadoPlugin = {{"texto", m.value("texto", std::string())}, {"ok", m.value("ok", true)}};
        emit cambio();
    } else if (t == "aviso") {
        avisar("«" + id + "»: " + m.value("texto", std::string()), m.value("prioridad", std::string("info")));
    } else if (t == "evento") {
        publicarEvento(id, m);
    } else if (t == "llamar") {
        const int n = m.value("n", 0);
        if (!llamador_) {
            enviar(id, {{"t", "respuesta"}, {"n", n}, {"ok", false}, {"error", "el sistema todavía no está listo"}});
            return;
        }
        const std::string herramienta = m.value("herramienta", std::string());
        QPointer<Plugins> yo(this);
        llamador_(herramienta, m.value("args", json::object()), [yo, id, n](json r) {
            if (!yo) return;
            // El resultado de una herramienta MCP: {content: [...], isError, structuredContent?}
            json out{{"t", "respuesta"}, {"n", n}};
            const bool error = r.value("isError", false);
            json res;
            if (r.contains("structuredContent")) {
                res = r["structuredContent"];
            } else {
                std::string texto;
                for (const auto& c : r.value("content", json::array()))
                    if (c.value("type", "") == "text") texto += c.value("text", std::string());
                res = json::parse(texto, nullptr, false);
                if (res.is_discarded()) res = texto;
            }
            if (error) {
                out["ok"] = false;
                out["error"] = res.is_string() ? res.get<std::string>() : res.dump();
            } else {
                out["ok"] = true;
                out["resultado"] = res;
            }
            yo->enviar(id, out);
        });
    } else if (t == "respuesta") {
        auto pe = pendientes_.find(m.value("n", 0));
        if (pe == pendientes_.end()) return;
        auto listo = std::move(pe->second.listo);
        pe->second.timer->stop();
        pe->second.timer->deleteLater();
        pendientes_.erase(pe);
        json r = m;
        r.erase("t");
        r.erase("n");
        listo(r);
    }
}

void Plugins::anotarPuerto(const std::string& id, int puerto) {
    calldata_t cd = {};
    calldata_set_string(&cd, "plugin", id.c_str());
    calldata_set_int(&cd, "puerto", puerto);
    proc_handler_call(obs_get_proc_handler(), "ba_plugin_puerto", &cd);
    calldata_free(&cd);
}

void Plugins::declararEntrada(const std::string& id, const json& d) {
    if (!video_) return;
    const std::string nombre = d.value("nombre", std::string());
    if (nombre.empty()) return;
    const bool conVideo = d.value("video", false), conAudio = d.value("audio", false);
    std::string pv = d.value("gst", std::string()), pa = d.value("gst_audio", std::string());
    // Sin pipeline propio: los cuadros los manda el plugin (bapluginsrc).
    auto comillas = [](const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o + "\"";
    };
    if (pv.empty() && conVideo) pv = "bapluginsrc plugin=" + comillas(id) + " entrada=" + comillas(nombre) + " medio=video";
    if (pa.empty() && conAudio && !d.contains("gst_audio"))
        pa = "bapluginsrc plugin=" + comillas(id) + " entrada=" + comillas(nombre) + " medio=audio";
    const bool propio = d.contains("gst");
    const int normalizar = d.contains("normalizar") ? (d.value("normalizar", false) ? 1 : 0) : (propio ? 1 : 0);
    const int latencia = d.value("latencia_ms", propio ? 100 : 60);
    const json man = leerJson(carpetaDe(id) / "plugin.json");
    const std::string escena = d.value("escena", man.value("escena", std::string("Plugins")));
    const std::string e = video_->crearEntradaPipeline(nombre, pv, pa, escena, latencia, normalizar, json{{"ba_plugin", id}});
    if (!e.empty()) {
        blog(LOG_WARNING, "[plugins] %s: entrada «%s»: %s", id.c_str(), nombre.c_str(), e.c_str());
        auto it = procesos_.find(id);
        if (it != procesos_.end()) it->second->error = "entrada «" + nombre + "»: " + e;
    }
}

void Plugins::declararPtz(const std::string& id, const json& d) {
    if (!camaras_) return;
    const QString entrada = q(d.value("entrada", std::string()));
    camaras_->sincronizar();
    const QString e = camaras_->configurar(entrada, "plugin", QVariantMap{{"plugin", q(id)}, {"entrada", entrada}});
    if (!e.isEmpty()) {
        blog(LOG_WARNING, "[plugins] %s: PTZ de «%s»: %s", id.c_str(), entrada.toUtf8().constData(), e.toUtf8().constData());
        auto it = procesos_.find(id);
        if (it != procesos_.end()) it->second->error = "PTZ de «" + entrada.toStdString() + "»: " + e.toStdString();
    }
    camaras_->refrescarPlugins();
}

void Plugins::publicarEvento(const std::string& id, const json& m) {
    const std::string canal = m.value("canal", std::string());
    json ev{{"de", id}, {"canal", canal}, {"datos", m.contains("datos") ? m["datos"] : json()}, {"hora_ns", m.value("hora_ns", 0LL)},
            {"recibido", QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString()}};
    ultimoEvento_[id + "/" + canal] = ev;
    eventos_.push_back(ev);
    while (eventos_.size() > 300) eventos_.pop_front();
    // A los que escuchan (otros plugins o el mismo).
    const std::string nombre = id + "/" + canal;
    for (const auto& [otro, pr] : procesos_) {
        if (!pr->p || !pr->listo) continue;
        for (const auto& patron : pr->escucha)
            if (coincide(patron, nombre)) {
                enviar(otro, {{"t", "evento"}, {"de", id}, {"canal", canal}, {"datos", ev["datos"]}, {"hora_ns", ev["hora_ns"]}});
                break;
            }
    }
    emit evento(q(id), q(canal), q(ev["datos"].dump()));
    emit eventosCambio();
}

json Plugins::eventos(const std::string& id, const std::string& canal, int cuantos) const {
    json ultimos = json::object();
    for (const auto& [k, v] : ultimoEvento_)
        if ((id.empty() || v.value("de", "") == id) && (canal.empty() || v.value("canal", "") == canal)) ultimos[k] = v;
    json recientes = json::array();
    for (auto it = eventos_.rbegin(); it != eventos_.rend() && static_cast<int>(recientes.size()) < cuantos; ++it)
        if ((id.empty() || it->value("de", "") == id) && (canal.empty() || it->value("canal", "") == canal)) recientes.push_back(*it);
    return {{"ultimo_por_canal", ultimos}, {"recientes", recientes}};
}

void Plugins::pedir(const std::string& id, const std::string& accion, const json& args, std::function<void(json)> listo, int timeoutMs) {
    auto it = procesos_.find(id);
    if (it == procesos_.end() || !it->second->p || !it->second->listo)
        return listo(json{{"ok", false}, {"error", "el plugin «" + id + "» no está corriendo"}});
    const int n = ++n_;
    auto* t = new QTimer(this);
    t->setSingleShot(true);
    connect(t, &QTimer::timeout, this, [this, n, accion, timeoutMs]() {
        auto pe = pendientes_.find(n);
        if (pe == pendientes_.end()) return;
        auto cb = std::move(pe->second.listo);
        pe->second.timer->deleteLater();
        pendientes_.erase(pe);
        cb(json{{"ok", false}, {"error", "«" + accion + "» no contestó en " + std::to_string(timeoutMs / 1000) + " s"}});
    });
    pendientes_[n] = {std::move(listo), t};
    t->start(timeoutMs);
    enviar(id, {{"t", "pedido"}, {"n", n}, {"accion", accion}, {"args", args}});
}

void Plugins::avisar(const std::string& texto, const std::string& prioridad) const {
    if (!mensajes_) return;
    mensajes_->post({{"kind", "message"}, {"text", texto}, {"priority", prioridad}});
}

// --- En el grafo y en el audio --------------------------------------------------------------------

std::string Plugins::ponerTarjeta(const std::string& id, const std::string& tarjeta, const std::string& entrada, const json& parametros,
                                  json* hecho) {
    if (!video_) return "el video no está disponible";
    auto it = procesos_.find(id);
    bool existe = false;
    if (it != procesos_.end())
        for (const auto& d : it->second->declarado)
            if (d.value("t", "") == "tarjeta" && d.value("nombre", "") == tarjeta) {
                existe = true;
                if (d.value("medio", "video") == "audio")
                    return "«" + tarjeta + "» es de audio: va como filtro de una fuente (plugin_tarjeta con `fuente`)";
            }
    if (!existe)
        return "el plugin «" + id + "» no declaró la tarjeta «" + tarjeta + "» (¿está corriendo? plugin_activar; plugin_ver dice lo que declara)";
    json props{{"plugin", id}, {"tarjeta", tarjeta}};
    if (parametros.is_object() && !parametros.empty()) props["parametros"] = parametros.dump();
    return video_->agregarTarjetaGst(entrada, "baplugin", props, hecho);
}

std::string Plugins::ponerFiltroAudio(const std::string& id, const std::string& tarjeta, const std::string& fuente, const json& parametros,
                                      json* hecho) {
    obs_source_t* src = obs_get_source_by_name(fuente.c_str());
    if (src == nullptr) return "no hay una fuente «" + fuente + "»";
    if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0) {
        obs_source_release(src);
        return "«" + fuente + "» no tiene audio";
    }
    const std::string nombre = "Plugin · " + tarjeta;
    obs_data_t* d = obs_data_create();
    obs_data_set_string(d, "plugin", id.c_str());
    obs_data_set_string(d, "tarjeta", tarjeta.c_str());
    obs_data_set_string(d, "parametros", (parametros.is_object() ? parametros : json::object()).dump().c_str());
    obs_source_t* existente = obs_source_get_filter_by_name(src, nombre.c_str());
    if (existente != nullptr) {
        obs_source_update(existente, d);
        obs_source_release(existente);
    } else {
        obs_source_t* f = obs_source_create("ba_plugin_audio", nombre.c_str(), d, nullptr);
        if (f == nullptr) {
            obs_data_release(d);
            obs_source_release(src);
            return "no existe el filtro ba_plugin_audio (¿falta GStreamer en el programa?)";
        }
        obs_source_filter_add(src, f);
        obs_source_release(f);
    }
    obs_data_release(d);
    obs_source_release(src);
    rt_.save();
    if (hecho != nullptr) *hecho = {{"fuente", fuente}, {"filtro", nombre}, {"plugin", id}, {"tarjeta", tarjeta}};
    return {};
}

// --- Dispositivos ------------------------------------------------------------------------------------

namespace {
/// Cámaras y micrófonos que ve GStreamer. En un hilo aparte: en macOS el
/// monitor (AVFoundation) despacha al hilo principal y desde ahí se traba.
json camarasGst() {
    json gst = json::array();
#if defined(BA_HAVE_GST)
    if (video::iniciarGStreamer()) {
        GstDeviceMonitor* mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
        gst_device_monitor_add_filter(mon, "Audio/Source", nullptr);
        if (gst_device_monitor_start(mon)) {
            GList* lista = gst_device_monitor_get_devices(mon);
            for (GList* l = lista; l != nullptr; l = l->next) {
                auto* dev = GST_DEVICE(l->data);
                gchar* nombre = gst_device_get_display_name(dev);
                gchar* clase = gst_device_get_device_class(dev);
                json d{{"nombre", nombre ? nombre : ""}, {"clase", clase ? clase : ""}};
                g_free(nombre);
                g_free(clase);
                if (GstElement* e = gst_device_create_element(dev, nullptr)) {
                    GstElementFactory* fab = gst_element_get_factory(e);
                    std::string desc = fab ? GST_OBJECT_NAME(fab) : "";
                    // Las propiedades que lo identifican (device-index, device, device-path…).
                    for (const char* prop : {"device-index", "device-path", "device", "unique-id", "device-name"}) {
                        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
                        if (ps == nullptr || !(ps->flags & G_PARAM_READABLE)) continue;
                        GValue v = G_VALUE_INIT;
                        g_value_init(&v, ps->value_type);
                        g_object_get_property(G_OBJECT(e), prop, &v);
                        gchar* s = gst_value_serialize(&v);
                        if (s && *s && std::string(s) != "NULL" && std::string(s) != "-1") {
                            std::string val = s;
                            if (G_VALUE_HOLDS_STRING(&v)) val = "\"" + val + "\"";
                            desc += std::string(" ") + prop + "=" + val;
                        }
                        g_free(s);
                        g_value_unset(&v);
                    }
                    d["gst"] = desc;
                    gst_object_unref(e);
                }
                if (GstCaps* caps = gst_device_get_caps(dev)) {
                    json formatos = json::array();
                    for (guint i = 0; i < gst_caps_get_size(caps) && formatos.size() < 12; ++i) {
                        gchar* s = gst_structure_to_string(gst_caps_get_structure(caps, i));
                        formatos.push_back(s ? s : "");
                        g_free(s);
                    }
                    d["formatos"] = formatos;
                    gst_caps_unref(caps);
                }
                if (GstStructure* pr = gst_device_get_properties(dev)) {
                    gchar* s = gst_structure_to_string(pr);
                    d["propiedades"] = s ? s : "";
                    g_free(s);
                    gst_structure_free(pr);
                }
                gst.push_back(d);
            }
            g_list_free_full(lista, gst_object_unref);
            gst_device_monitor_stop(mon);
        }
        gst_object_unref(mon);
    }
#endif
    return gst;
}
} // namespace

void Plugins::dispositivos(std::function<void(json)> listo0) {
    buscandoEquipos_ = true;
    emit equiposCambio();
    auto listo = [this, listo0](json r) {
        equipos_ = r;
        buscandoEquipos_ = false;
        emit equiposCambio();
        listo0(r);
    };
    // 1) Cámaras y micrófonos (GStreamer, en otro hilo); 2) USB, HID y serie (Python).
    QPointer<Plugins> yo(this);
    std::thread([yo, listo]() {
        json gst = camarasGst();
        QMetaObject::invokeMethod(
            qApp, [yo, listo, gst]() {
                if (yo) yo->dispositivosPython(gst, listo);
            },
            Qt::QueuedConnection);
    }).detach();
}

void Plugins::dispositivosPython(json gst, std::function<void(json)> listo) {
    // 2) USB, HID y serie: con el SDK (pyusb/hidapi/pyserial en una carpeta propia).
    const fs::path herr = carpeta_ / ".herramientas";
    std::error_code ec;
    fs::create_directories(herr, ec);
    if (!fs::exists(herr / "plugin.json"))
        escribirTexto(herr / "plugin.json",
                      json{{"id", ".herramientas"}, {"dependencias", {"pyusb", "libusb-package", "hidapi", "pyserial"}}}.dump(2));
    const std::string script = R"PY(import json, sys
sys.path[:0] = [sys.argv[1], sys.argv[2]]
from ba_plugin import dispositivos as d
out = {}
for k, f in (("usb", d.usb_listar), ("hid", d.hid_listar), ("serie", d.serie_listar)):
    try:
        out[k] = f()
    except Exception as e:
        out[k + "_error"] = f"{type(e).__name__}: {e}"
print(json.dumps(out, ensure_ascii=False, default=str))
)PY";
    escribirTexto(herr / "listar.py", script);
    dependencias(".herramientas", [this, herr, gst, listo](std::string err, std::string) {
        if (!err.empty()) return listo(json{{"camaras_y_microfonos", gst}, {"usb_error", err}});
        auto* p = new QProcess(this);
        vigilar(p);
        connect(p, &QProcess::finished, this, [p, gst, listo](int, QProcess::ExitStatus) {
            json r = json::parse(p->readAllStandardOutput().toStdString(), nullptr, false);
            const std::string err = p->readAllStandardError().right(1500).toStdString();
            p->deleteLater();
            if (r.is_discarded() || !r.is_object()) r = {{"usb_error", err}};
            r["camaras_y_microfonos"] = gst;
            listo(r);
        });
        p->start(q(python()), {"-I", "-B", q(herr / "listar.py"), q(sdk()), q(herr / "_libs")});
    });
}

// --- Para la interfaz -------------------------------------------------------------------------------

QVariantList Plugins::listaQml() const { return qv(listar()).toList(); }
QVariantList Plugins::ejemplosQml() const { return qv(ejemplosDisponibles()).toList(); }
QVariantMap Plugins::pruebasQml() const {
    json o = json::object();
    for (const auto& [k, v] : pruebas_) o[k] = v;
    return qv(o).toMap();
}
QVariantList Plugins::eventosQml() const { return qv(eventos("", "", 30)["recientes"]).toList(); }
QVariantMap Plugins::equiposQml() const { return equipos_.is_object() ? qv(equipos_).toMap() : QVariantMap{}; }
QString Plugins::carpetaTexto() const { return q(carpeta_); }
bool Plugins::hayPython() const { return fs::exists(python()); }

void Plugins::prender(const QString& id, bool activo) {
    activar(id.toStdString(), activo, [this, id, activo](json e) {
        if (activo && e.value("estado", "") != "corriendo")
            avisar("El plugin «" + id.toStdString() + "» no arrancó: " + e.value("error", std::string("mirá su registro")), "warning");
        emit cambio();
    });
}

void Plugins::reiniciarUi(const QString& id) {
    reiniciar(id.toStdString(), [this](json) { emit cambio(); });
}

void Plugins::probarUi(const QString& id) { probar(id.toStdString(), 3.0, json::array(), false, [](json) {}); }

QString Plugins::registroUi(const QString& id) const { return q(registro(id.toStdString(), 200)); }

QString Plugins::copiarEjemplo(const QString& ejemplo, const QString& id) { return q(desdeEjemplo(ejemplo.toStdString(), id.toStdString())); }

QString Plugins::borrarUi(const QString& id) { return q(borrar(id.toStdString())); }

QString Plugins::configTexto(const QString& id) const {
    return q(leerJson(carpetaDe(id.toStdString()) / "plugin.json").value("config", json::object()).dump(2));
}

QString Plugins::guardarConfig(const QString& id, const QString& texto) {
    const json c = json::parse(texto.toStdString(), nullptr, false);
    if (c.is_discarded() || !c.is_object()) return "No es un JSON válido (tiene que ser un objeto: { … })";
    const fs::path arch = carpetaDe(id.toStdString()) / "plugin.json";
    json man = leerJson(arch);
    if (man.empty()) return "No existe el plugin";
    man["config"] = c;
    if (!escribirTexto(arch, man.dump(2))) return "No se pudo guardar";
    enviar(id.toStdString(), {{"t", "config"}, {"config", c}});
    emit cambio();
    return {};
}

void Plugins::controlUi(const QString& id, const QString& control, const QString& argsJson) {
    json args = json::parse(argsJson.trimmed().isEmpty() ? std::string("{}") : argsJson.toStdString(), nullptr, false);
    if (args.is_discarded() || !args.is_object()) {
        emit respuesta(id, control, "Los argumentos no son un JSON válido", false);
        return;
    }
    QPointer<Plugins> yo(this);
    pedir(id.toStdString(), "control", {{"nombre", control.toStdString()}, {"args", args}}, [yo, id, control](json r) {
        if (!yo) return;
        const bool ok = r.value("ok", false);
        emit yo->respuesta(id, control, q(ok ? (r.contains("resultado") ? r["resultado"].dump(2) : std::string("hecho")) : r.value("error", std::string())), ok);
    }, 30000);
}

QString Plugins::tarjetaUi(const QString& id, const QString& tarjeta, const QString& destino, bool audio) {
    json hecho;
    return q(audio ? ponerFiltroAudio(id.toStdString(), tarjeta.toStdString(), destino.toStdString(), json::object(), &hecho)
                   : ponerTarjeta(id.toStdString(), tarjeta.toStdString(), destino.toStdString(), json::object(), &hecho));
}

QStringList Plugins::entradasVideo() const { return video_ ? video_->entradas() : QStringList{}; }

QStringList Plugins::fuentesConAudio() const {
    QStringList out;
    obs_enum_sources(
        [](void* d, obs_source_t* s) {
            if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) != 0) static_cast<QStringList*>(d)->append(QString::fromUtf8(obs_source_get_name(s)));
            return true;
        },
        &out);
    out.sort(Qt::CaseInsensitive);
    return out;
}

void Plugins::buscarEquipos() {
    if (buscandoEquipos_) return;
    dispositivos([](json) {});
}

void Plugins::abrirCarpeta(const QString& id) const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(q(id.isEmpty() ? carpeta_ : carpetaDe(id.toStdString()))));
}

QString Plugins::codigo(const QString& id) const {
    const json v = ver(id.toStdString(), "");
    return q(v.value("contenido", std::string()));
}

} // namespace ba::app::plugins
