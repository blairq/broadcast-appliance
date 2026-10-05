// ============================================================================
// src/app/audio/AudioViewModel.cpp — ver AudioViewModel.hpp
// ============================================================================

#include "audio/AudioViewModel.hpp"

#include <QFile>

#include <regex>

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "audio/Modules.hpp"
#include "audio/RackManager.hpp"

#include <obs.h>

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
    // OJO: `d.value(...)` devuelve una COPIA. Iterando sobre ella, el erase no
    // sacaba los medidores de `d`: el grafo "cambiaba" en cada vuelta con audio
    // vivo, se rehacian los delegados del patchbay y no se podia arrastrar un
    // nodo (con fuentes en silencio no se notaba, porque el medidor no variaba).
    if (d.contains("nodos")) {
        for (auto& n : d["nodos"]) {
            meters[n.value("id", "")] = n.value("medidores", json::object());
            n.erase("medidores");
        }
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
        const json descNodos = r->describe(); // en una variable: el temporal no vive durante el for
        for (const auto& n : descNodos["nodos"])
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
            const json descNodos = r->describe(); // en una variable: el temporal no vive durante el for
            for (const auto& n : descNodos["nodos"])
                if (n["id"] == id) {
                    j["bypass"] = n.value("bypass", false);
                    for (const char* k : {"bypass_auto", "ms_ultimo", "ms_medio", "ms_max", "ms_bloque"})
                        if (n.contains(k)) j[k] = n[k];
                }
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
        if (r->kindOf(id) == "entrada") {
            const json descNodos = r->describe(); // en una variable: el temporal no vive durante el for
            for (const auto& n : descNodos["nodos"])
                if (n["id"] == id) j["fuente"] = n.value("fuente", "");
            j["ayuda"] =
                "Trae el audio de otra fuente (después de su propio rack) a este grafo. No se conecta "
                "sola: cableala a donde la quieras (la mezcla, el sidechain de un compresor, la voz a "
                "texto). Si la fuente es una entrada de placa, su captura se abre sola.";
        }
        if (r->kindOf(id) == "habla") {
            for (const auto& h : r->hablas())
                if (h.value("id", "") == id) j["habla"] = h;
            j["ayuda"] =
                "Dice con la voz elegida lo que llega por una salida de texto (o lo que escribas abajo). "
                "Suena por su puerto: cableala a la mezcla, a la salida de audio o a otra voz a texto. "
                "Ponela en una fuente que siempre tenga audio (una entrada virtual, un micrófono): el "
                "rack procesa mientras su fuente suena.";
        }
        if (r->kindOf(id) == "traducir") {
            j["traduccion_id"] = source_.toStdString() + "#" + id;
            j["ayuda"] = "Traduce lo que publica una salida de texto (la de una «Voz a texto») y lo publica "
                         "en otra, en otro "
                         "idioma: esa se usa como cualquiera (subtítulos, archivo, una tarjeta «Texto a voz» "
                         "con una voz del "
                         "idioma). No lleva cables: no toca el audio.";
        }
        if (r->kindOf(id) == "texto") {
            std::string modo = "dictado";
            for (const auto& t : r->textos())
                if (t.id == id) {
                    modo = t.modo;
                    j["flujo"] = t.flujo;
                    j["salidas"] = t.salidas.is_object() ? t.salidas : json::object();
                }
            j["modo"] = modo;
            j["ayuda"] =
                modo == "continuo"
                    ? "Transcribe sin parar lo que le llega (subtítulos, acta): frase por frase, 1 a 3 s "
                      "después. El texto se ve abajo y por MCP (voz_transcripcion). No toca lo que sale al "
                      "aire."
                    : "Dictado al asistente: mientras se aprieta F9 (o el micrófono de la barra), lo que le "
                      "llega se transcribe y va a la barra del asistente. Si hay varias fuentes con dictado, "
                      "se usa la que más habló.";
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

QString AudioViewModel::addTexto(const QString& modo) {
    if (rack(true) == nullptr) return "Elegí una fuente con audio.";
    std::string e;
    const std::string id = mgr_.addTexto(source_.toStdString(), modo.toStdString(), {}, &e);
    if (id.empty()) return QString::fromStdString(e);
    select(QString::fromStdString(id));
    rebuildGraph();
    rebuildDetail();
    return {};
}

QString AudioViewModel::setTextoOpciones(const QString& node, const QVariantMap& cambios) {
    const json c = json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(),
                               nullptr, false);
    const std::string e = mgr_.setTextoOpciones(source_.toStdString(), node.toStdString(), c);
    rebuildGraph();
    rebuildDetail();
    return QString::fromStdString(e);
}

QStringList AudioViewModel::fuentesDeTexto() const {
    QStringList out;
    obs_enum_sources(
        [](void* p, obs_source_t* s) {
            const char* id = obs_source_get_unversioned_id(s);
            if (id != nullptr && std::string(id).rfind("text_", 0) == 0)
                static_cast<QStringList*>(p)->append(QString::fromUtf8(obs_source_get_name(s)));
            return true;
        },
        &out);
    out.sort();
    return out;
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

QString AudioViewModel::addEntrada(const QString& fuente) {
    if (source_.isEmpty()) return "Elegí una fuente con audio.";
    std::string e;
    const std::string id = mgr_.addEntrada(source_.toStdString(), fuente.toStdString(), &e);
    if (id.empty()) return QString::fromStdString(e);
    autoLayout();
    select(QString::fromStdString(id));
    rebuildGraph();
    rebuildDetail();
    return {};
}

QString AudioViewModel::setEntradaFuente(const QString& node, const QString& fuente) {
    const std::string e =
        mgr_.setEntradaFuente(source_.toStdString(), node.toStdString(), fuente.toStdString());
    rebuildGraph();
    rebuildDetail();
    return QString::fromStdString(e);
}

QString AudioViewModel::addHabla() {
    if (source_.isEmpty()) return "Elegí una fuente con audio.";
    std::string e;
    const std::string id = mgr_.addHabla(source_.toStdString(), json::object(), &e);
    if (id.empty()) return QString::fromStdString(e);
    autoLayout();
    select(QString::fromStdString(id));
    rebuildGraph();
    rebuildDetail();
    return {};
}

QString AudioViewModel::addTraduccion() {
    if (source_.isEmpty()) return "Elegí una fuente con audio.";
    // Si este rack tiene una voz a texto, traduce lo que ella publica.
    std::string desde;
    for (const auto& t : mgr_.textos())
        if (t.value("fuente", std::string()) == source_.toStdString()) {
            desde = t.value("flujo", std::string());
            break;
        }
    std::string e;
    const std::string id =
        mgr_.addTraduccion({{"fuente", source_.toStdString()}, {"desde", desde}, {"a", "en"}}, &e);
    if (id.empty()) return QString::fromStdString(e);
    autoLayout();
    select(QString::fromStdString(id.substr(id.rfind('#') + 1)));
    rebuildGraph();
    rebuildDetail();
    return {};
}

QString AudioViewModel::setHablaOpciones(const QString& node, const QVariantMap& cambios) {
    const json c = json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(),
                               nullptr, false);
    const std::string e = mgr_.setHablaOpciones(source_.toStdString(), node.toStdString(), c);
    rebuildDetail();
    return QString::fromStdString(e);
}

QString AudioViewModel::setGrabar(bool grabar) {
    const std::string e = mgr_.setGrabar(source_.toStdString(), grabar);
    rebuildDetail();
    return QString::fromStdString(e);
}

QVariantMap AudioViewModel::grabacion() const {
    QVariantMap m = toVariant(mgr_.grabacion(source_.toStdString())).toMap();
    m["carpeta"] = QString::fromStdString(mgr_.carpetaGrabaciones().string());
    return m;
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

QVariantList AudioViewModel::outputDevices() const {
    QVariantList out;
    for (const auto& d : mgr_.outputDevices()) {
        QVariantList salidas;
        for (const auto& o : d.value("salidas", nlohmann::json::array()))
            salidas << QVariantMap{{"id", QString::fromStdString(o.value("id", ""))},
                                   {"nombre", QString::fromStdString(o.value("nombre", ""))}};
        out << QVariantMap{{"dispositivo", QString::fromStdString(d.value("dispositivo", ""))},
                           {"salidas", salidas}};
    }
    return out;
}

QString AudioViewModel::addOutput(const QString& device) {
    if (rack(true) == nullptr) return "Elegí una fuente con audio.";
    const std::string e = mgr_.setOutput(source_.toStdString(), device.toStdString());
    if (!e.empty()) return QString::fromStdString(e);
    select("salida-audio");
    rebuildGraph();
    return {};
}

// --- Plantillas -------------------------------------------------------------

QVariantList AudioViewModel::plantillas() const {
    QVariantList out;
    for (const auto& p : mgr_.plantillas()) {
        out.append(QVariantMap{{"nombre", QString::fromStdString(p.value("nombre", std::string()))},
                               {"modulos", static_cast<int>(p.value("modulos", 0))},
                               {"paraTipo", QString::fromStdString(p.value("paraTipo", std::string()))}});
    }
    return out;
}

QString AudioViewModel::guardarPlantilla(const QString& nombre) {
    const std::string e = mgr_.guardarPlantilla(source_.toStdString(), nombre.trimmed().toStdString());
    if (e.empty()) emit changed();
    return QString::fromStdString(e);
}

QString AudioViewModel::aplicarPlantilla(const QString& nombre) {
    const std::string e = mgr_.aplicarPlantilla(nombre.toStdString(), source_.toStdString());
    if (e.empty()) {
        // La plantilla trae otros nodos: lo que estaba elegido ya no existe.
        selected_.clear();
        rebuildGraph();
        emit selectionChanged();
    }
    return QString::fromStdString(e);
}

QString AudioViewModel::borrarPlantilla(const QString& nombre) {
    const std::string e = mgr_.borrarPlantilla(nombre.toStdString());
    if (e.empty()) emit changed();
    return QString::fromStdString(e);
}

QString AudioViewModel::plantillaPorDefecto(const QString& nombre, const QString& tipo) {
    const std::string e = mgr_.plantillaPorDefecto(nombre.toStdString(), tipo.toStdString());
    if (e.empty()) emit changed();
    return QString::fromStdString(e);
}

QString AudioViewModel::tipoDeFuente() const {
    for (const auto& s : mgr_.audioSources())
        if (s.value("nombre", std::string()) == source_.toStdString())
            return QString::fromStdString(s.value("tipo", std::string()));
    return {};
}

double AudioViewModel::gridStep() const { return AudioRack::gridStep(); }

void AudioViewModel::tidy() {
    AudioRack* r = rack();
    if (r == nullptr) return;
    r->layoutGrid();
    mgr_.markDirty();
    rebuildGraph();
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
                                     const QString& in, int outCh, int inCh) {
    AudioRack* r = rack(true);
    if (r == nullptr) return "sin rack";
    std::string e;
    if (!r->connect(from.toStdString(), out.toStdString(), to.toStdString(), in.toStdString(), &e, outCh,
                    inCh))
        return QString::fromStdString(e);
    mgr_.markDirty();
    rebuildGraph();
    return {};
}

void AudioViewModel::disconnectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in, int outCh, int inCh) {
    AudioRack* r = rack();
    if (r == nullptr) return;
    if (r->disconnect(from.toStdString(), out.toStdString(), to.toStdString(), in.toStdString(), outCh, inCh))
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

void AudioViewModel::rearmar(const QString& node) {
    AudioRack* r = rack();
    if (r != nullptr) r->rearmar(node.toStdString());
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

QString AudioViewModel::restablecerInterfazLsp() {
    rebuildDetail();
    return QString::fromStdString(mgr_.restablecerInterfazLsp());
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

QVariantList AudioViewModel::catalogoLsp() const {
    QVariantList out;
    auto lib = const_cast<RackManager&>(mgr_).library();
    QFile f(":/audio/lsp-catalogo.json");
    if (!f.open(QIODevice::ReadOnly)) return out;
    const json cat = json::parse(f.readAll().toStdString(), nullptr, false);
    if (!cat.is_object()) return out;
    // La familia de un id: sin «sc_», sin canales (mono/stereo/lr/ms) ni cantidades (x8...).
    static const std::regex sufijo("_(mono|stereo|lr|ms|x[0-9]+)$");
    const auto familia = [](std::string b) {
        if (b.rfind("sc_", 0) == 0) b = b.substr(3);
        for (int i = 0; i < 4; ++i)
            b = std::regex_replace(b, sufijo, "");
        return b;
    };
    const auto etiqueta = [](const std::string& b) {
        QStringList partes;
        static const std::regex cant("_x([0-9]+)");
        std::smatch m;
        if (std::regex_search(b, m, cant)) partes << QString("×%1").arg(QString::fromStdString(m[1]));
        const auto termina = [&b](const char* s) {
            const std::string x = s;
            return b.size() >= x.size() && b.compare(b.size() - x.size(), x.size(), x) == 0;
        };
        partes << (termina("_mono")     ? "Mono"
                   : termina("_lr")     ? "Izq/Der"
                   : termina("_ms")     ? "Medio/Lados"
                   : termina("_stereo") ? "Estéreo"
                                        : "");
        if (b.rfind("sc_", 0) == 0) partes << "con sidechain";
        partes.removeAll(QString());
        return partes.join(" · ");
    };
    std::map<std::string, std::vector<PluginDesc>> porFamilia;
    if (lib)
        for (const auto& p : lib->plugins()) {
            const std::string base = p.id.substr(p.id.rfind('.') + 1);
            porFamilia[familia(base)].push_back(p);
        }
    for (const auto& fam : cat.value("familias", json::array())) {
        const std::string clave = fam.value("clave", std::string());
        const auto it = porFamilia.find(clave);
        if (it == porFamilia.end()) continue; // no está en este build de LSP
        QVariantMap v = toVariant(fam).toMap();
        QVariantList variantes;
        std::string original;
        for (const auto& p : it->second) {
            const std::string base = p.id.substr(p.id.rfind('.') + 1);
            const QString e = etiqueta(base);
            variantes << QVariantMap{{"id", QString::fromStdString(p.id)},
                                     {"etiqueta", e.isEmpty() ? QString::fromStdString(p.name) : e},
                                     {"estereo", base.find("_mono") == std::string::npos},
                                     {"sidechain", base.rfind("sc_", 0) == 0}};
            if (original.empty()) original = p.description;
        }
        v["variantes"] = variantes;
        v["descripcionOriginal"] = QString::fromStdString(original);
        v["esFiltro"] = fam.value("categoria", std::string()) != "Instrumentos";
        // La captura del módulo (assets/lsp-capturas), si la hay.
        const QString cap = ":/lsp/" + QString::fromStdString(clave) + ".jpg";
        v["captura"] = QFile::exists(cap) ? "qrc" + cap : QString();
        out << v;
    }
    return out;
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
