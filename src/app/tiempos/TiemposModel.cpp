// ============================================================================
// src/app/tiempos/TiemposModel.cpp — ver TiemposModel.hpp
// ============================================================================
#include "tiempos/TiemposModel.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

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

TiemposModel::TiemposModel(std::function<Fotos()> fotos, QObject* parent) : QObject(parent), fotos_(std::move(fotos)) {
    timer_.setInterval(kPeriodoMs);
    connect(&timer_, &QTimer::timeout, this, &TiemposModel::tic);
}

void TiemposModel::setActivo(bool a) {
    if (a == activo()) return;
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
    mapaUi_ = aVariant(mapa_).toMap();
    emit cambio();
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

} // namespace ba::app::tiempos
