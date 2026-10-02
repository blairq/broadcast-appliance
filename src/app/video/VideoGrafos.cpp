// src/app/video/VideoGrafos.cpp — ver VideoGrafos.hpp

#include "video/VideoGrafos.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"

#include <obs.h>

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <set>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
constexpr const char* kFiltro = "BA · Grafo de video";
constexpr const char* kTipoSalida = "ba_video_salida";

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).toVariant().toMap().value("v");
}
} // namespace

VideoGrafos::VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), archivo_(dataRoot / "Config" / "video-grafos.json") {
    cargar();
    revision_.setInterval(1500);
    connect(&revision_, &QTimer::timeout, this, &VideoGrafos::sincronizar);
    revision_.start();
    QTimer::singleShot(0, this, &VideoGrafos::sincronizar);
}

bool VideoGrafos::esEntrada(const std::string& n) const {
    BroadcastEngine& e = rt_.engine();
    const std::string type = e.sourceTypeId(n);
    if (type == inputs::kVideoType) return true;
    return inputs::isNetworkInput(type, json::parse(e.sourceSettingsJson(n), nullptr, false));
}

QStringList VideoGrafos::entradas() const { return entradas_; }

void VideoGrafos::seleccionar(const QString& entrada) {
    if (entrada == entrada_) return;
    entrada_ = entrada;
    emit cambio();
}

QVariantMap VideoGrafos::grafo() const {
    if (entrada_.isEmpty()) return {};
    return aVariant(describir(entrada_.toStdString())).toMap();
}

QVariantList VideoGrafos::catalogo() { return aVariant(GrafoVideo::catalogo()).toList(); }

json VideoGrafos::describir(const std::string& entrada) const {
    if (!esEntrada(entrada)) return {{"error", "«" + entrada + "» no es una entrada de video"}};
    const auto it = grafos_.find(entrada);
    json d = (it == grafos_.end() ? GrafoVideo() : it->second).describir();
    d["entrada"] = entrada;
    // Si cada salida ya está como fuente (y en qué escenas se la puede poner).
    for (auto& n : d["nodos"])
        if (n.value("tipo", std::string()) == "salida") {
            const std::string nombre = n["params"].value("nombre", std::string());
            n["publicada"] = !nombre.empty() && rt_.engine().sourceTypeId(nombre) == kTipoSalida;
        }
    return d;
}

std::string VideoGrafos::editar(const std::string& entrada, const std::function<std::string(GrafoVideo&)>& f) {
    if (!esEntrada(entrada)) return "«" + entrada + "» no es una entrada de video";
    GrafoVideo& g = grafos_[entrada];
    GrafoVideo antes = g;
    const std::string error = f(g);
    if (!error.empty()) {
        g = antes;
        return error;
    }
    nombrarSalidas(entrada, g);
    if (g.aJson() != antes.aJson()) {
        if (g.trivial()) grafos_.erase(entrada);
        guardar();
        aplicar(entrada);
        emit cambio();
    }
    return {};
}

void VideoGrafos::nombrarSalidas(const std::string& entrada, GrafoVideo& g) const {
    std::set<std::string> usados;
    for (const auto& [e, otro] : grafos_)
        if (e != entrada)
            for (const auto& s : otro.salidas()) usados.insert(s);
    for (const auto& n : g.nodos()) {
        if (n.tipo != "salida") continue;
        std::string nombre = n.params.value("nombre", std::string());
        if (!nombre.empty()) {
            usados.insert(nombre);
            continue;
        }
        // «Cámara 1 · salida», «Cámara 1 · salida 2»… sin chocar con otra fuente.
        const std::string base = entrada + " · salida";
        for (int i = 1;; ++i) {
            nombre = i == 1 ? base : base + " " + std::to_string(i);
            const std::string tipo = rt_.engine().sourceTypeId(nombre);
            if (!usados.count(nombre) && (tipo.empty() || tipo == kTipoSalida)) break;
        }
        usados.insert(nombre);
        g.ajustar(n.id, json{{"nombre", nombre}}, nullptr);
    }
}

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
} // namespace

QString VideoGrafos::agregar(const QString& tipo, const QString& despues) {
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        std::string e;
        g.agregar(tipo.toStdString(), despues.toStdString(), json::object(), &e);
        return e;
    }));
}
QString VideoGrafos::quitar(const QString& id) {
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        std::string e;
        g.quitar(id.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::conectar(const QString& desde, const QString& hacia) {
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        std::string e;
        g.conectar(desde.toStdString(), hacia.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::desconectar(const QString& desde, const QString& hacia) {
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        g.desconectar(desde.toStdString(), hacia.toStdString());
        return std::string();
    }));
}
QString VideoGrafos::mover(const QString& id, int x, int y) {
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        g.mover(id.toStdString(), x, y);
        return std::string();
    }));
}
QString VideoGrafos::ajustar(const QString& id, const QVariantMap& params) {
    const json p = json::parse(QJsonDocument::fromVariant(params).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return q(editar(entrada_.toStdString(), [&](GrafoVideo& g) {
        std::string e;
        g.ajustar(id.toStdString(), p, &e);
        return e;
    }));
}
QString VideoGrafos::limpiar() {
    return q(editar(entrada_.toStdString(), [](GrafoVideo& g) {
        g = GrafoVideo();
        return std::string();
    }));
}
QString VideoGrafos::aEscena(const QString& id, const QString& escena) {
    return q(ponerEnEscena(entrada_.toStdString(), id.toStdString(), escena.toStdString()));
}

std::string VideoGrafos::ponerEnEscena(const std::string& entrada, const std::string& id, const std::string& escena) {
    const auto it = grafos_.find(entrada);
    const GrafoVideo::Nodo* n = it == grafos_.end() ? nullptr : it->second.nodo(id);
    if (n == nullptr || n->tipo != "salida") return "«" + id + "» no es una salida del grafo de «" + entrada + "»";
    const std::string nombre = n->params.value("nombre", std::string());
    const std::string tipo = rt_.engine().sourceTypeId(nombre);
    if (!tipo.empty() && tipo != kTipoSalida) return "ya hay otra fuente que se llama «" + nombre + "»";
    SourceSpec spec;
    spec.id = kTipoSalida;
    spec.name = nombre;
    spec.settingsJson = json{{"fuente", entrada}}.dump();
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? "no se pudo agregar" : rt_.lastError();
    rt_.save();
    emit cambio();
    return {};
}

void VideoGrafos::sincronizar() {
    if (!obs_initialized()) return;
    QStringList hay;
    for (const std::string& n : rt_.engine().sourceNames())
        if (esEntrada(n)) hay << q(n);
    hay.sort();
    const bool cambiaron = hay != entradas_;
    entradas_ = hay;
    if (entrada_.isEmpty() && !entradas_.isEmpty()) entrada_ = entradas_.first();
    for (const QString& e : entradas_) aplicar(e.toStdString());
    if (cambiaron) emit cambio();
}

void VideoGrafos::aplicar(const std::string& entrada) {
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return;
    const auto it = grafos_.find(entrada);
    obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro);
    if (it == grafos_.end() || it->second.trivial()) {
        if (f != nullptr) obs_source_filter_remove(src, f);
        aplicado_.erase(entrada);
    } else {
        const std::string js = it->second.paraFiltro().dump();
        if (f == nullptr) {
            obs_data_t* s = obs_data_create();
            obs_data_set_string(s, "grafo", js.c_str());
            obs_source_t* nuevo = obs_source_create_private("ba_video_grafo", kFiltro, s);
            obs_data_release(s);
            if (nuevo != nullptr) {
                obs_source_filter_add(src, nuevo);
                obs_source_release(nuevo);
            }
            aplicado_[entrada] = js;
        } else if (aplicado_[entrada] != js) {
            obs_data_t* s = obs_data_create();
            obs_data_set_string(s, "grafo", js.c_str());
            obs_source_update(f, s);
            obs_data_release(s);
            aplicado_[entrada] = js;
        }
        // El último en aplicarse («abajo» en libobs: índice 0), después del
        // PTZ digital, que entra también abajo cuando se lo prende.
        if (obs_source_t* g = f != nullptr ? obs_source_get_ref(f) : obs_source_get_filter_by_name(src, kFiltro)) {
            obs_source_filter_set_order(src, g, OBS_ORDER_MOVE_BOTTOM);
            obs_source_release(g);
        }
    }
    if (f != nullptr) obs_source_release(f);
    obs_source_release(src);
}

void VideoGrafos::guardar() const {
    json j = json::object();
    for (const auto& [e, g] : grafos_)
        if (!g.trivial()) j[e] = g.aJson();
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QByteArray::fromStdString(j.dump(2)));
    f.commit();
}

void VideoGrafos::cargar() {
    QFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const json j = json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
        GrafoVideo g = GrafoVideo::desde(it.value());
        if (!g.trivial()) grafos_[it.key()] = std::move(g);
    }
}

} // namespace ba::app::video
