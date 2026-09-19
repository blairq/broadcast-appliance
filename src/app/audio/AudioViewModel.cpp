// ============================================================================
// src/app/audio/AudioViewModel.cpp — ver AudioViewModel.hpp
// ============================================================================

#include "audio/AudioViewModel.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "audio/Modules.hpp"
#include "audio/RackManager.hpp"

namespace ba::app::audio {

using json = nlohmann::json;

namespace {

QVariant toVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump()))
        .object()
        .value("v")
        .toVariant();
}

json fromVariant(const QVariant& v) {
    switch (v.typeId()) {
        case QMetaType::Bool: return v.toBool();
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::Double: return v.toDouble();
        default: return v.toString().toStdString();
    }
}

} // namespace

AudioViewModel::AudioViewModel(RackManager& mgr, QObject* parent) : QObject(parent), mgr_(mgr) {
    tick_.setInterval(160);
    connect(&tick_, &QTimer::timeout, this, [this]() {
        rebuildGraph();
        rebuildDetail();
    });
    connect(&mgr_, &RackManager::changed, this, &AudioViewModel::refresh);
}

bool AudioViewModel::available() const { return const_cast<RackManager&>(mgr_).library() != nullptr; }

QVariantList AudioViewModel::catalog() const {
    QVariantList out;
    for (const auto& m : moduleCatalog()) {
        QVariantList presets;
        for (auto it = m.presets.begin(); it != m.presets.end(); ++it)
            presets << QString::fromStdString(it.key());
        out << QVariantMap{{"clave", QString::fromStdString(m.key)},
                           {"titulo", QString::fromStdString(m.title)},
                           {"ayuda", QString::fromStdString(m.help)},
                           {"presets", presets}};
    }
    return out;
}

void AudioViewModel::setActive(bool on) {
    active_ = on;
    if (on) {
        refresh();
        tick_.start();
    } else {
        tick_.stop();
    }
}

void AudioViewModel::refresh() {
    std::string e;
    mgr_.library(&e);
    error_ = QString::fromStdString(e);
    sources_ = toVariant(mgr_.audioSources()).toList();
    // Por defecto, la primera fuente que ya tiene rack (si no, la primera).
    bool exists = false;
    for (const QVariant& s : sources_)
        exists = exists || s.toMap().value("nombre").toString() == source_;
    if (!exists) source_.clear();
    for (const QVariant& s : sources_)
        if (source_.isEmpty() && s.toMap().value("rack").toBool())
            source_ = s.toMap().value("nombre").toString();
    if (source_.isEmpty() && !sources_.isEmpty())
        source_ = sources_.first().toMap().value("nombre").toString();
    emit changed();
    rebuildGraph();
    rebuildDetail();
}

AudioRack* AudioViewModel::rack(bool create) {
    if (source_.isEmpty()) return nullptr;
    return mgr_.rack(source_.toStdString(), create);
}

void AudioViewModel::autoLayout() {
    AudioRack* r = rack();
    if (r == nullptr) return;
    // Racks guardados sin posiciones (nodos en 0,0): en fila; "out" al final.
    // (Los nodos nuevos ya los ubica el rack.)
    json d = r->describe();
    int col = 1;
    double maxX = 0;
    for (const auto& n : d["nodos"])
        if (n["id"] != "out") maxX = std::max(maxX, n.value("x", 0.0));
    bool moved = false;
    for (const auto& n : d["nodos"]) {
        const std::string id = n["id"];
        if (id == "in" || id == "out") continue;
        if (n.value("x", 0.0) == 0 && n.value("y", 0.0) == 0) {
            maxX = std::max(maxX, 40.0 + 230.0 * col);
            r->setPosition(id, maxX, n.value("tipo", "") == "generator" ? 320.0 : 80.0);
            ++col;
            moved = true;
        }
    }
    // La entrada a la izquierda y la salida despues del ultimo.
    for (const auto& n : d["nodos"])
        if (n["id"] == "in" && n.value("y", 0.0) == 0) r->setPosition("in", 20, 80);
    for (const auto& n : d["nodos"])
        if (n["id"] == "out" && n.value("x", 0.0) < maxX + 200) {
            r->setPosition("out", maxX + 240, 80);
            moved = true;
        }
    if (moved) mgr_.markDirty();
}

void AudioViewModel::rebuildGraph() {
    AudioRack* r = rack();
    json d = r != nullptr ? r->describe() : json::object();
    json meters = json::object();
    for (auto& n : d.value("nodos", json::array())) {
        meters[n.value("id", "")] = n.value("medidores", json::object());
        n.erase("medidores");
    }
    const QVariantMap g = toVariant(d).toMap();
    if (g != graph_) {
        graph_ = g;
        emit graphChanged();
    }
    meters_ = toVariant(meters).toMap();
    emit metersChanged();
}

void AudioViewModel::rebuildDetail() {
    AudioRack* r = rack();
    QVariantMap d;
    if (r != nullptr && !selected_.isEmpty() && r->has(selected_.toStdString())) {
        const std::string id = selected_.toStdString();
        json j{{"nodo", id}, {"tipo", r->kindOf(id)}, {"medidores", r->meters(id)}};
        for (const auto& n : r->describe()["nodos"])
            if (n["id"] == id) j["etiqueta"] = n.value("etiqueta", id);
        if (ClapInstance* c = r->plugin(id)) {
            j["modulo"] = c->desc().id;
            j["nombre"] = c->desc().name;
            j["tieneEditor"] = c->hasEditor();
            j["editorAbierto"] = c->editorOpen();
            if (const ModuleType* t = findModule(c->desc().id)) {
                j["clave"] = t->key;
                j["ayuda"] = t->help;
                j["parametros"] = readModule(*c, *t);
                json presets = json::array();
                for (auto it = t->presets.begin(); it != t->presets.end(); ++it)
                    presets.push_back(it.key());
                j["presets"] = presets;
            }
            for (const auto& n : r->describe()["nodos"])
                if (n["id"] == id) j["bypass"] = n.value("bypass", false);
        }
        if (Analyzer* a = r->analyzer(id)) {
            const Analyzer::Snapshot s = a->snapshot();
            json rep = Analyzer::report(s);
            json bands = json::array();
            for (int b = 0; b < Analyzer::kBands; ++b)
                bands.push_back(std::max(-90.f, s.bandsDb[b]));
            rep["bandas"] = bands;
            j["analisis"] = rep;
        }
        if (Generator* g = r->generator(id)) {
            const auto c = g->config();
            j["senal"] = {
                {"tipo", Generator::kindName(c.kind)}, {"frecuencia", c.freq}, {"nivelDb", c.levelDb}};
        }
        d = toVariant(j).toMap();
    }
    if (d != detail_) {
        detail_ = d;
        emit detailChanged();
    }
}

void AudioViewModel::selectSource(const QString& name) {
    source_ = name;
    selected_.clear();
    autoLayout();
    emit changed();
    emit selectionChanged();
    rebuildGraph();
    rebuildDetail();
}

void AudioViewModel::select(const QString& node) {
    selected_ = node;
    emit selectionChanged();
    rebuildDetail();
}

QString AudioViewModel::addModule(const QString& moduleOrId, const QString& after) {
    std::string e;
    if (mgr_.library(&e) == nullptr) return QString::fromStdString(e);
    AudioRack* r = rack(true);
    if (r == nullptr) return "Elegí una fuente con audio.";
    const ModuleType* t = findModule(moduleOrId.toStdString());
    const std::string id =
        r->addPlugin(t != nullptr ? t->pluginId : moduleOrId.toStdString(), t != nullptr ? t->title : "",
                     after.toStdString(), &e, t != nullptr ? t->key : "");
    if (id.empty()) return QString::fromStdString(e);
    autoLayout();
    mgr_.markDirty();
    selected_ = QString::fromStdString(id);
    emit selectionChanged();
    refresh();
    return {};
}

QString AudioViewModel::addAnalyzer() {
    AudioRack* r = rack(true);
    if (r == nullptr) return "Elegí una fuente con audio.";
    const std::string id = r->addAnalyzer("", "");
    autoLayout();
    mgr_.markDirty();
    select(QString::fromStdString(id));
    rebuildGraph();
    return {};
}

QString AudioViewModel::addGenerator() {
    AudioRack* r = rack(true);
    if (r == nullptr) return "Elegí una fuente con audio.";
    const std::string id = r->addGenerator("");
    autoLayout();
    mgr_.markDirty();
    select(QString::fromStdString(id));
    rebuildGraph();
    return {};
}

void AudioViewModel::removeNode(const QString& node) {
    AudioRack* r = rack();
    std::string e;
    if (r == nullptr || !r->remove(node.toStdString(), &e)) return;
    mgr_.markDirty();
    if (selected_ == node) {
        selected_.clear();
        emit selectionChanged();
    }
    refresh();
}

void AudioViewModel::removeRack() {
    mgr_.removeRack(source_.toStdString());
    selected_.clear();
    emit selectionChanged();
    refresh();
}

QString AudioViewModel::connectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in) {
    AudioRack* r = rack(true);
    if (r == nullptr) return "sin rack";
    std::string e;
    if (!r->connect(from.toStdString(), out.toStdString(), to.toStdString(), in.toStdString(), &e))
        return QString::fromStdString(e);
    mgr_.markDirty();
    rebuildGraph();
    return {};
}

void AudioViewModel::disconnectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in) {
    AudioRack* r = rack();
    if (r == nullptr) return;
    if (r->disconnect(from.toStdString(), out.toStdString(), to.toStdString(), in.toStdString()))
        mgr_.markDirty();
    rebuildGraph();
}

void AudioViewModel::moveNode(const QString& node, double x, double y) {
    AudioRack* r = rack();
    if (r == nullptr) return;
    r->setPosition(node.toStdString(), std::max(0.0, x), std::max(0.0, y));
    mgr_.markDirty();
    rebuildGraph();
}

void AudioViewModel::setBypass(const QString& node, bool bypass) {
    AudioRack* r = rack();
    if (r != nullptr && r->setBypass(node.toStdString(), bypass)) mgr_.markDirty();
    rebuildGraph();
    rebuildDetail();
}

QString AudioViewModel::openEditor(const QString& node) {
    AudioRack* r = rack();
    ClapInstance* c = r != nullptr ? r->plugin(node.toStdString()) : nullptr;
    if (c == nullptr) return "no es un modulo";
    std::string e;
    if (!c->openEditor(c->desc().name + " — " + source_.toStdString(), &e)) return QString::fromStdString(e);
    rebuildDetail();
    return {};
}

QString AudioViewModel::setParam(const QString& node, const QString& key, const QVariant& value, int band) {
    AudioRack* r = rack();
    ClapInstance* c = r != nullptr ? r->plugin(node.toStdString()) : nullptr;
    if (c == nullptr) return "no es un modulo";
    const ModuleType* t = findModule(c->desc().id);
    json values;
    if (band >= 0)
        values = {{"bandas", {{{"banda", band}, {key.toStdString(), fromVariant(value)}}}}};
    else
        values = {{key.toStdString(), fromVariant(value)}};
    const json res = applyModule(*c, t, values);
    mgr_.markDirty();
    rebuildDetail();
    if (!res["errores"].empty())
        return QString::fromStdString(res["errores"].begin().value().get<std::string>());
    return {};
}

QString AudioViewModel::applyPreset(const QString& node, const QString& preset) {
    AudioRack* r = rack();
    ClapInstance* c = r != nullptr ? r->plugin(node.toStdString()) : nullptr;
    const ModuleType* t = c != nullptr ? findModule(c->desc().id) : nullptr;
    if (t == nullptr || !t->presets.contains(preset.toStdString())) return "sin ese preset";
    applyModule(*c, t, t->presets[preset.toStdString()]);
    mgr_.markDirty();
    rebuildDetail();
    return {};
}

void AudioViewModel::setGenerator(const QString& node, const QString& kind, double freq, double levelDb) {
    AudioRack* r = rack();
    Generator* g = r != nullptr ? r->generator(node.toStdString()) : nullptr;
    if (g == nullptr) return;
    Generator::Config c = g->config();
    Generator::parseKind(kind.toStdString(), c.kind);
    c.freq = freq;
    c.levelDb = std::min(0.0, levelDb);
    g->configure(c);
    mgr_.markDirty();
    rebuildDetail();
}

QVariantList AudioViewModel::search(const QString& text) const {
    QVariantList out;
    auto lib = const_cast<RackManager&>(mgr_).library();
    if (!lib || text.trimmed().size() < 2) return out;
    const QString q = text.trimmed().toLower();
    for (const auto& p : lib->plugins()) {
        const QString h = QString::fromStdString(p.id + " " + p.name).toLower();
        if (h.contains(q))
            out << QVariantMap{{"id", QString::fromStdString(p.id)},
                               {"nombre", QString::fromStdString(p.name)}};
        if (out.size() >= 30) break;
    }
    return out;
}

} // namespace ba::app::audio
