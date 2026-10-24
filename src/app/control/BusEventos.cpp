// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/BusEventos.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <vector>

namespace ba::app::control {

using json = nlohmann::json;

namespace {
std::vector<std::string> niveles(const std::string& s) {
    std::vector<std::string> v;
    std::size_t a = 0;
    while (true) {
        const auto b = s.find('/', a);
        v.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}

constexpr std::size_t kRecientes = 400;
} // namespace

BusEventos::BusEventos(QObject* parent) : QObject(parent) {}

bool BusEventos::coincide(const std::string& patron, const std::string& tema) {
    if (patron.empty() || patron == "#" || patron == "*") return true;
    const auto p = niveles(patron), t = niveles(tema);
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == "#" || (p[i] == "*" && i + 1 == p.size())) return true;
        if (i >= t.size()) return false;
        if (p[i] != "+" && p[i] != "*" && p[i] != t[i]) return false;
    }
    return p.size() == t.size();
}

void BusEventos::publicar(const std::string& tema, const json& valor, bool siempre) {
    std::vector<Oyente> avisar;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = ultimos_.find(tema);
        if (!siempre && it != ultimos_.end() && it->second.valor == valor) return;
        const std::int64_t ms = QDateTime::currentMSecsSinceEpoch();
        ultimos_[tema] = {valor, ms, ++seq_};
        // Los niveles de audio no llenan el registro (diez por segundo por fuente).
        if (tema.rfind("audio/nivel/", 0) != 0) {
            recientes_.push_back({{"tema", tema}, {"valor", valor}, {"ms", ms}, {"seq", seq_}});
            while (recientes_.size() > kRecientes) recientes_.pop_front();
        }
        for (const auto& [id, o] : oyentes_)
            if (coincide(o.first, tema)) avisar.push_back(o.second);
    }
    for (const auto& fn : avisar) fn(tema, valor);
    emit publicado(QString::fromStdString(tema));
}

json BusEventos::ultimo(const std::string& tema) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ultimos_.find(tema);
    if (it == ultimos_.end()) return nullptr;
    return {{"tema", tema}, {"valor", it->second.valor}, {"ms", it->second.ms}, {"seq", it->second.seq}};
}

json BusEventos::temas(const std::string& patron) const {
    std::lock_guard<std::mutex> l(mu_);
    json out = json::array();
    for (const auto& [t, e] : ultimos_)
        if (coincide(patron, t)) out.push_back({{"tema", t}, {"valor", e.valor}, {"ms", e.ms}});
    return out;
}

json BusEventos::desde(std::uint64_t d, const std::vector<std::string>& patrones) const {
    std::lock_guard<std::mutex> l(mu_);
    json out = json::array();
    const auto sirve = [&](const std::string& t) {
        if (patrones.empty()) return true;
        for (const auto& p : patrones)
            if (coincide(p, t)) return true;
        return false;
    };
    // Los niveles no están en recientes: se miran en los últimos valores.
    for (const auto& e : recientes_)
        if (e.value("seq", 0ULL) > d && sirve(e.value("tema", std::string()))) out.push_back(e);
    for (const auto& [t, e] : ultimos_)
        if (t.rfind("audio/nivel/", 0) == 0 && e.seq > d && sirve(t))
            out.push_back({{"tema", t}, {"valor", e.valor}, {"ms", e.ms}, {"seq", e.seq}});
    return out;
}

std::uint64_t BusEventos::seq() const {
    std::lock_guard<std::mutex> l(mu_);
    return seq_;
}

int BusEventos::suscribir(const std::string& patron, Oyente fn) {
    std::lock_guard<std::mutex> l(mu_);
    const int id = proximo_++;
    oyentes_[id] = {patron, std::move(fn)};
    return id;
}

void BusEventos::desuscribir(int id) {
    std::lock_guard<std::mutex> l(mu_);
    oyentes_.erase(id);
}

void BusEventos::setMediciones(std::vector<std::string> patrones) {
    std::lock_guard<std::mutex> l(mu_);
    mediciones_ = std::move(patrones);
}

QVariantList BusEventos::recientesUi(const QString& filtro, bool conMediciones) const {
    std::lock_guard<std::mutex> l(mu_);
    QVariantList out;
    const std::string f = filtro.toStdString();
    const auto esMedicion = [this](const std::string& t) {
        for (const auto& p : mediciones_)
            if (coincide(p, t)) return true;
        return false;
    };
    for (auto it = recientes_.rbegin(); it != recientes_.rend() && out.size() < 150; ++it) {
        const std::string t = it->value("tema", std::string());
        if (!f.empty() && t.find(f) == std::string::npos && !coincide(f, t)) continue;
        if (f.empty() && !conMediciones && esMedicion(t)) continue; // con filtro, lo pidió
        out.append(QVariantMap{
            {"tema", QString::fromStdString(t)},
            {"valor", QString::fromStdString((*it)["valor"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))},
            {"hora", QDateTime::fromMSecsSinceEpoch(it->value("ms", 0LL)).toString("HH:mm:ss.zzz")}});
    }
    return out;
}

QVariantList BusEventos::temasUi(const QString& filtro) const {
    std::lock_guard<std::mutex> l(mu_);
    QVariantList out;
    const std::string f = filtro.toStdString();
    for (const auto& [t, e] : ultimos_) {
        if (!f.empty() && t.find(f) == std::string::npos && !coincide(f, t)) continue;
        out.append(QVariantMap{{"tema", QString::fromStdString(t)},
                               {"valor", QString::fromStdString(e.valor.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))},
                               {"datos", aQv(e.valor)}});
    }
    return out;
}

} // namespace ba::app::control
