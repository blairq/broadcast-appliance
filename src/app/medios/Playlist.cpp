// SPDX-License-Identifier: GPL-2.0-or-later
#include "medios/Playlist.hpp"
#include "i18n/I18n.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "platform/Paths.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <fstream>

namespace ba::app::medios {

using json = nlohmann::json;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
QVariant aQv(const json& j) {
    return QVariant::fromValue(QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant());
}
} // namespace

Playlist::Playlist(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    archivo_ = rt.dataRoot() / "Config" / "playlist.json";
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_object()) {
        if (j.contains("items") && j["items"].is_array()) items_ = j["items"];
        autoNext_ = j.value("auto_next", true);
        loop_ = j.value("loop", false);
    }
    reloj_.setInterval(250);
    connect(&reloj_, &QTimer::timeout, this, &Playlist::vigilar);
}

void Playlist::guardar() const {
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    const std::filesystem::path tmp = archivo_.string() + ".tmp";
    std::ofstream(tmp) << json{{"items", items_}, {"auto_next", autoNext_}, {"loop", loop_}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::filesystem::rename(tmp, archivo_, ec);
}

void Playlist::anotarAire() const {
    double tiempo = 0;
    for (const auto& m : rt_.engine().mediaSources())
        if (m.name == kFuente) tiempo = m.timeMs / 1000.0;
    const std::filesystem::path dest = ba::platform::stateDir() / "playlist-aire.json";
    const std::filesystem::path tmp = dest.string() + ".tmp";
    std::ofstream(tmp) << json{{"indice", indice_},
                               {"reproduciendo", reproduciendo_},
                               {"tiempo_s", tiempo},
                               {"archivo", indice_ >= 0 && indice_ < static_cast<int>(items_.size())
                                               ? items_[static_cast<size_t>(indice_)].value("archivo", std::string())
                                               : std::string()}}
                              .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
}

std::string Playlist::retomar() {
    std::ifstream f(ba::platform::stateDir() / "playlist-aire.json");
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object() || !j.value("reproduciendo", false)) return ba::i18n::traducir("medios", "no estaba sonando");
    const int i = j.value("indice", -1);
    // Si la lista cambió, el mismo ítem es el del mismo archivo.
    if (i < 0 || i >= static_cast<int>(items_.size()) ||
        items_[static_cast<size_t>(i)].value("archivo", std::string()) != j.value("archivo", std::string()))
        return ba::i18n::traducir("medios", "la playlist cambió");
    if (const std::string e = cargarItem(i); !e.empty()) return e;
    const double t = j.value("tiempo_s", 0.0);
    if (t > 1) {
        destinoMs_ = static_cast<std::int64_t>(t * 1000);
        buscarIn_ = true;
    }
    return {};
}

std::string Playlist::setItems(const json& items) {
    if (!items.is_array()) return ba::i18n::traducir("medios", "`items` tiene que ser una lista");
    json limpios = json::array();
    for (const auto& it : items) {
        json x = it.is_string() ? json{{"archivo", it}} : it;
        const std::string f = x.value("archivo", std::string());
        if (f.empty() || !QFileInfo::exists(qs(f))) return ba::i18n::llenar(ba::i18n::traducir("medios", "no existe el archivo «%1»"), {f});
        const double in = std::max(0.0, x.value("in_s", 0.0)), out = std::max(0.0, x.value("out_s", 0.0));
        if (out > 0 && out <= in)
            return ba::i18n::llenar(ba::i18n::traducir("medios", "en «%1» el out tiene que ser mayor que el in"), {f});
        limpios.push_back({{"archivo", f},
                           {"nombre", x.value("nombre", QFileInfo(qs(f)).completeBaseName().toStdString())},
                           {"in_s", in},
                           {"out_s", out}});
    }
    items_ = limpios;
    if (indice_ >= static_cast<int>(items_.size())) indice_ = -1;
    guardar();
    emit cambio();
    emit estadoCambio();
    return {};
}

std::string Playlist::asegurarEscena() {
    BroadcastEngine& e = rt_.engine();
    const auto escenas = e.sceneNames();
    if (std::find(escenas.begin(), escenas.end(), kEscena) == escenas.end() && !rt_.addScene(kEscena)) return rt_.lastError();
    const auto fuentes = e.sceneSources(kEscena);
    if (std::find(fuentes.begin(), fuentes.end(), kFuente) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = kFuente;
        spec.settingsJson = json{{"clear_on_media_end", false}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!rt_.addSource(kEscena, spec)) return rt_.lastError();
        rt_.save();
    }
    return {};
}

std::string Playlist::cargarItem(int i) {
    if (i < 0 || i >= static_cast<int>(items_.size()))
        return ba::i18n::llenar(ba::i18n::traducir("medios", "no hay un ítem %1"), {std::to_string(i + 1)});
    if (const std::string e = asegurarEscena(); !e.empty()) return e;
    const json& it = items_[static_cast<size_t>(i)];
    const std::string ajustes = json{{"local_file", it.value("archivo", std::string())},
                                     {"looping", false},
                                     {"clear_on_media_end", false}}
                                    .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!rt_.updateSourceSettings(kFuente, ajustes)) return rt_.lastError();
    // libobs no hace correr un medio que no se ve en ningún lado: si la escena no
    // está al aire, se arma en PVW (el programa no se toca).
    if (rt_.engine().currentScene() != kEscena && armar_) armar_(kEscena);
    rt_.engine().restartMedia(kFuente);
    indice_ = i;
    destinoMs_ = static_cast<std::int64_t>(it.value("in_s", 0.0) * 1000);
    buscarIn_ = destinoMs_ > 0;
    intentosIn_ = 0;
    reproduciendo_ = true;
    reloj_.start();
    anotarAire();
    emit estadoCambio();
    return {};
}

std::string Playlist::control(const std::string& accion, int indice) {
    if (items_.empty()) return ba::i18n::traducir("medios", "la playlist está vacía");
    if (accion == "play") return cargarItem(indice >= 0 ? indice : std::max(0, indice_));
    if (accion == "next") {
        int n = indice_ + 1;
        if (n >= static_cast<int>(items_.size())) {
            if (!loop_) return control("stop");
            n = 0;
        }
        return cargarItem(n);
    }
    if (accion == "prev") return cargarItem(std::max(0, indice_ - 1));
    if (accion == "ir") { // `indice` = segundo del clip actual
        if (!rt_.engine().seekMedia(kFuente, static_cast<std::int64_t>(std::max(0, indice)) * 1000))
            return ba::i18n::traducir("medios", "no hay medio");
        return {};
    }
    if (accion == "stop") {
        reloj_.stop();
        reproduciendo_ = false;
        rt_.engine().pararMedia(kFuente); // en su escena: se para, no se saca
        anotarAire();
        emit estadoCambio();
        return {};
    }
    return ba::i18n::traducir("medios", "acción: play, next, prev, stop o ir");
}

void Playlist::vigilar() {
    if (!reproduciendo_ || indice_ < 0 || indice_ >= static_cast<int>(items_.size())) return;
    const json& it = items_[static_cast<size_t>(indice_)];
    for (const auto& m : rt_.engine().mediaSources()) {
        if (m.name != kFuente) continue;
        // El «in»: el salto recién vale cuando el medio ya arrancó de nuevo (justo
        // después del restart todavía puede informar el clip anterior), así que se
        // insiste hasta que el tiempo quede pasado el in (o se agotan los intentos).
        if (buscarIn_ && m.state == "playing") {
            if (m.timeMs + 300 < destinoMs_ && intentosIn_++ < 12) rt_.engine().seekMedia(kFuente, destinoMs_);
            else buscarIn_ = false;
        }
        if (++vueltas_ % 8 == 0) anotarAire();
        const double out = it.value("out_s", 0.0);
        const bool llegoOut = out > 0 && m.timeMs >= static_cast<std::int64_t>(out * 1000);
        if (m.state == "ended" || llegoOut) {
            if (autoNext_) control("next");
            else control("stop");
        }
        break;
    }
    emit estadoCambio();
}

void Playlist::setAutoNext(bool on) {
    autoNext_ = on;
    guardar();
    emit estadoCambio();
}

void Playlist::setLoop(bool on) {
    loop_ = on;
    guardar();
    emit estadoCambio();
}

json Playlist::estado() const {
    double restante = 0, dur = 0, tiempo = 0;
    std::string medio = "sin fuente";
    if (indice_ >= 0 && indice_ < static_cast<int>(items_.size()))
        for (const auto& m : rt_.engine().mediaSources())
            if (m.name == kFuente) {
                medio = m.state;
                tiempo = m.timeMs / 1000.0;
                const json& it = items_[static_cast<size_t>(indice_)];
                const double out = it.value("out_s", 0.0) > 0 ? it.value("out_s", 0.0) : m.durationMs / 1000.0;
                dur = out - it.value("in_s", 0.0);
                restante = std::max(0.0, out - m.timeMs / 1000.0);
            }
    return {{"indice", indice_},
            {"reproduciendo", reproduciendo_},
            {"auto_next", autoNext_},
            {"loop", loop_},
            {"restante_s", std::round(restante * 10) / 10},
            {"duracion_s", std::round(dur * 10) / 10},
            {"items", items_.size()},
            {"medio", medio},
            {"tiempo_s", std::round(tiempo * 10) / 10}};
}

QVariantList Playlist::itemsUi() const { return aQv(items_).toList(); }
QVariantMap Playlist::estadoUi() const { return aQv(estado()).toMap(); }

QString Playlist::agregarUi(const QString& archivo) {
    json nuevos = items_;
    nuevos.push_back({{"archivo", archivo.toStdString()}});
    return qs(setItems(nuevos));
}

QString Playlist::agregarVariosUi(const QStringList& archivos) {
    json nuevos = items_;
    for (const QString& a : archivos) nuevos.push_back({{"archivo", a.toStdString()}});
    return qs(setItems(nuevos));
}

QString Playlist::quitarUi(int indice) {
    if (indice < 0 || indice >= static_cast<int>(items_.size())) return tr("no hay ese ítem");
    json nuevos = items_;
    nuevos.erase(nuevos.begin() + indice);
    if (indice_ == indice) control("stop");
    return qs(setItems(nuevos));
}

QString Playlist::moverUi(int indice, int destino) {
    const int n = static_cast<int>(items_.size());
    if (indice < 0 || indice >= n || destino < 0 || destino >= n) return tr("fuera de la lista");
    json nuevos = items_;
    const json x = nuevos[static_cast<size_t>(indice)];
    nuevos.erase(nuevos.begin() + indice);
    nuevos.insert(nuevos.begin() + destino, x);
    return qs(setItems(nuevos));
}

QString Playlist::controlUi(const QString& accion, int indice) { return qs(control(accion.toStdString(), indice)); }

std::string Playlist::armar() {
    if (const std::string e = asegurarEscena(); !e.empty()) return e;
    return armar_ ? armar_(kEscena) : std::string();
}

void Playlist::armarUi() {
    if (const std::string e = armar(); !e.empty()) emit notify(qs(e), "error");
}

} // namespace ba::app::medios
