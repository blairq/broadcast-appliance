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

std::string BusEventos::temaDeSalida(const std::string& tema, const std::string& salida, bool porOmision) {
    std::string t = tema;
    const std::string k = "{{salida}}";
    for (auto p = t.find(k); p != std::string::npos; p = t.find(k, p + salida.size())) t.replace(p, k.size(), salida);
    if (porOmision || salida.empty()) return t;
    // Solo los temas que son de UNA salida; los demás (audio, sala, alarmas…) son de todas.
    static const std::set<std::string> deAire{"aire/escena", "aire/armada", "aire/tally", "aire/medios", "aire/dsk", "aire/tbar"};
    static const std::set<std::string> deSalida{"salida/stream", "salida/grabacion"};
    if (deAire.count(t)) return "aire/" + salida + t.substr(4);
    if (deSalida.count(t)) return "salida/" + salida + t.substr(6);
    // El fader y el mudo de una fuente son de cada salida (su mezcla).
    if (t.rfind("audio/nivel/", 0) == 0) return "audio/" + salida + t.substr(5);
    return t;
}

void BusEventos::publicarSalidas(const json& salidas) {
    if (!salidas.is_array()) return;
    std::set<std::string> ahora;
    json lista = json::array();
    for (const auto& s : salidas) {
        const std::string id = s.value("id", std::string());
        if (id.empty()) continue;
        ahora.insert(id);
        const json esc = s.value("escena", std::string()), arm = s.value("armada", std::string());
        const bool tx = s.value("transmitiendo", false), rec = s.value("grabando", false);
        lista.push_back({{"id", id}, {"nombre", s.value("nombre", id)}, {"color", s.value("color", std::string())},
                         {"por_omision", s.value("por_omision", false)}});
        publicar("aire/" + id + "/escena", {{"escena", esc}});
        publicar("aire/" + id + "/armada", {{"escena", arm}});
        publicar("aire/" + id + "/tally", {{"programa", esc},
                                           {"preview", arm},
                                           {"fuentes_programa", s.value("fuentes_programa", json::array())},
                                           {"fuentes_preview", s.value("fuentes_preview", json::array())},
                                           {"transmitiendo", tx},
                                           {"grabando", rec},
                                           {"salida", id},
                                           {"color", s.value("color", std::string())}});
        // Lo que viene solo si quien publica lo tiene (los tests y lo viejo, sin esto).
        if (s.contains("medios")) publicar("aire/" + id + "/medios", s["medios"]);
        if (s.contains("dsk")) publicar("aire/" + id + "/dsk", s["dsk"]);
        if (s.contains("tbar")) publicar("aire/" + id + "/tbar", s["tbar"]);
        if (s.contains("niveles") && s["niveles"].is_object())
            for (const auto& [fuente, n] : s["niveles"].items()) publicar("audio/" + id + "/nivel/" + fuente, n);
        publicar("salida/" + id + "/stream", {{"activo", tx}, {"reconectando", s.value("reconectando", false)}});
        publicar("salida/" + id + "/grabacion", {{"activo", rec}});
        publicar("salida/" + id + "/estado", {{"id", id},
                                              {"nombre", s.value("nombre", id)},
                                              {"color", s.value("color", std::string())},
                                              {"por_omision", s.value("por_omision", false)},
                                              {"escena", esc},
                                              {"armada", arm},
                                              {"transmitiendo", tx},
                                              {"grabando", rec}});
    }
    publicar("salidas", {{"lista", lista}});
    // Una salida borrada: sus temas no quedan con el último valor (una celda que la
    // miraba mostraría un aire que ya no existe).
    std::vector<std::string> idas;
    for (const auto& id : salidasPublicadas_)
        if (!ahora.count(id)) idas.push_back(id);
    salidasPublicadas_ = std::move(ahora);
    if (idas.empty()) return;
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& id : idas)
        for (auto it = ultimos_.begin(); it != ultimos_.end();)
            if (coincide("aire/" + id + "/#", it->first) || coincide("salida/" + id + "/#", it->first) ||
                coincide("audio/" + id + "/nivel/#", it->first))
                it = ultimos_.erase(it);
            else ++it;
}

} // namespace ba::app::control
