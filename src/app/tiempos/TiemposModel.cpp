// ============================================================================
// src/app/tiempos/TiemposModel.cpp — ver TiemposModel.hpp
// ============================================================================
#include "tiempos/TiemposModel.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace ba::app::tiempos {

using json = nlohmann::json;

namespace {
QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("x")
        .toVariant();
}
} // namespace

TiemposModel::TiemposModel(std::function<Fotos()> fotos, std::filesystem::path archivo, QObject* parent)
    : QObject(parent), fotos_(std::move(fotos)), archivo_(std::move(archivo)) {
    timer_.setInterval(kPeriodoMs);
    connect(&timer_, &QTimer::timeout, this, &TiemposModel::tic);
    muestreo_.setInterval(100);
    connect(&muestreo_, &QTimer::timeout, this, &TiemposModel::muestrear);
    // Los análisis de antes de este evento.
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_array()) analisis_ = j;
    for (auto& a : analisis_)
        if (a.value("estado", std::string()) == "midiendo") a["estado"] = "fallo", a["error"] = "se cerró la app midiendo";
}

void TiemposModel::setActivo(bool a) {
    if (a == activo()) return;
    qInfo("[tiempos] %s", a ? "midiendo (la página se ve)" : "en reposo");
    if (a) {
        tic(); // que la página no arranque vacía
        timer_.start();
    } else {
        timer_.stop();
        historia_.clear();
    }
    emit activoCambio();
}

bool TiemposModel::rehacer() {
    // Una foto con algo inesperado no tumba la página (ni el MCP): queda el último grafo bueno.
    try {
        mapa_ = armar(fotos_());
        mapaUi_ = aVariant(mapa_).toMap();
        emit cambio();
        return true;
    } catch (const std::exception& e) {
        qWarning("[tiempos] no se pudo armar el grafo: %s", e.what());
        return false;
    }
}

json TiemposModel::ahora() {
    if (!activo()) rehacer();
    return mapa_;
}

void TiemposModel::tic() {
    if (!rehacer()) return;
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    std::set<std::string> vivos;
    for (const auto& n : mapa_["nodos"]) {
        const std::string id = n.value("id", std::string());
        vivos.insert(id);
        if (!n["ms"].is_number()) continue;
        auto& h = historia_[id];
        h.push_back({t, n["ms"].get<double>(), n["acumulado_ms"].is_number() ? n["acumulado_ms"].get<double>() : 0.0});
        while (!h.empty() && t - h.front().t > kHistoriaS * 1000) h.pop_front();
    }
    for (auto it = historia_.begin(); it != historia_.end();)
        it = vivos.count(it->first) ? std::next(it) : historia_.erase(it);
}

json TiemposModel::nodo(const std::string& id) const {
    for (const auto& n : mapa_.value("nodos", json::array()))
        if (n.value("id", std::string()) == id) {
            json out = n;
            json h = json::array();
            if (const auto it = historia_.find(id); it != historia_.end())
                for (const auto& m : it->second) h.push_back({{"t_ms", m.t}, {"ms", m.ms}, {"acumulado_ms", m.acumulado}});
            out["historia"] = h;
            return out;
        }
    return nullptr;
}

QVariantList TiemposModel::historia(const QString& id) const {
    QVariantList out;
    if (const auto it = historia_.find(id.toStdString()); it != historia_.end())
        for (const auto& m : it->second) out << QVariantMap{{"t", m.t}, {"ms", m.ms}, {"acumulado", m.acumulado}};
    return out;
}

// --- Análisis -----------------------------------------------------------------------------

namespace {
double percentil(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
}
double redondo(double ms) { return std::round(ms * 10.0) / 10.0; }
} // namespace

QVariantList TiemposModel::analisis() const { return aVariant(analisis_).toList(); }

QString TiemposModel::analizar(int segundos, std::function<void(const json&)> listo) {
    if (muestreo_.isActive()) return QStringLiteral("ya hay un análisis midiendo");
    segundos_ = std::clamp(segundos, 2, 120);
    desde_ = QDateTime::currentMSecsSinceEpoch();
    muestrasNodo_.clear();
    muestrasSalida_.clear();
    titulosSalida_.clear();
    listo_ = std::move(listo);
    int id = 1;
    for (const auto& a : analisis_) id = std::max(id, a.value("id", 0) + 1);
    analisis_.insert(analisis_.begin(), json{{"id", id}, {"estado", "midiendo"}, {"progreso", 0.0}, {"desde_ms", desde_},
                                             {"segundos", segundos_}, {"tipo", "tiempos"}});
    while (analisis_.size() > 50) analisis_.erase(analisis_.end() - 1);
    muestreo_.start();
    emit analisisCambio();
    return {};
}

void TiemposModel::muestrear() {
    if (!rehacer()) return;
    for (const auto& n : mapa_["nodos"])
        if (n["ms"].is_number()) muestrasNodo_[n.value("id", std::string())].push_back(n["ms"].get<double>());
    for (const auto& sa : mapa_["salidas"]) {
        const std::string id = sa.value("id", std::string());
        if (sa["total_ms"].is_number()) muestrasSalida_[id].push_back(sa["total_ms"].get<double>());
        titulosSalida_[id] = sa.value("titulo", id);
    }
    const qint64 pasado = QDateTime::currentMSecsSinceEpoch() - desde_;
    analisis_[0]["progreso"] = std::min(1.0, static_cast<double>(pasado) / (segundos_ * 1000.0));
    if (pasado >= segundos_ * 1000) terminar();
    else emit analisisCambio();
}

void TiemposModel::terminar() {
    muestreo_.stop();
    json& a = analisis_[0];
    json nodos = json::object();
    for (const auto& [id, v] : muestrasNodo_)
        nodos[id] = {{"p50_ms", redondo(percentil(v, 0.5))}, {"p95_ms", redondo(percentil(v, 0.95))}, {"n", v.size()}};
    json salidas = json::array();
    for (const auto& [id, v] : muestrasSalida_)
        salidas.push_back({{"id", id}, {"titulo", titulosSalida_[id]}, {"total_ms", redondo(percentil(v, 0.5))},
                           {"p95_ms", redondo(percentil(v, 0.95))}});
    a["nodos"] = nodos;
    a["salidas"] = salidas;
    a["progreso"] = 1.0;
    if (nodos.empty()) {
        a["estado"] = "fallo";
        a["error"] = "no llegó ninguna medición (¿el motor está andando?)";
    } else {
        a["estado"] = "listo";
    }
    guardar();
    emit analisisCambio();
    if (listo_) {
        auto f = std::move(listo_);
        listo_ = {};
        f(a);
    }
}

void TiemposModel::borrarAnalisis() {
    if (muestreo_.isActive()) return;
    analisis_ = json::array();
    guardar();
    emit analisisCambio();
}

void TiemposModel::guardar() const {
    if (archivo_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << analisis_.dump(1, ' ', false, json::error_handler_t::replace);
}

} // namespace ba::app::tiempos
