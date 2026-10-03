// ============================================================================
// src/app/ui/InputsViewModel.cpp — ver InputsViewModel.hpp
// ============================================================================

#include "platform/Paths.hpp"
#include "ui/InputsViewModel.hpp"

#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <thread>

#include <nlohmann/json.hpp>

#include "AppRuntime.hpp"
#include "InputSpecs.hpp"
#if defined(BA_HAVE_NDI)
#include "NdiSource.hpp"
#endif
#include "ui/StudioViewModel.hpp"

namespace ba::app {

using json = nlohmann::json;

InputsViewModel::InputsViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm) {
    // Estado de las entradas (llega imagen, tamaño) y miniaturas, 1 vez por segundo
    // mientras la pagina esta a la vista.
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, [this]() {
        rebuildInputs();
        ++stamp_;
        emit stampChanged();
    });
}

InputsViewModel::~InputsViewModel() {
    for (const auto& n : held_)
        rt_.engine().holdShowing(n, false);
}

QStringList InputsViewModel::scenes() const {
    QStringList out;
    for (const auto& s : rt_.engine().sceneNames())
        out << QString::fromStdString(s);
    return out;
}

bool InputsViewModel::ndiAvailable() const {
#if defined(BA_HAVE_NDI)
    return true;
#else
    return false;
#endif
}

void InputsViewModel::searchNdi() {
#if defined(BA_HAVE_NDI)
    if (ndiSearching_) return;
    ndiSearching_ = true;
    emit ndiChanged();
    QPointer<InputsViewModel> self(this);
    std::thread([self]() {
        const auto found = ndi::discover(4000);
        QVariantList list;
        for (const auto& f : found)
            list << QVariantMap{{"name", QString::fromStdString(f.name)},
                                {"host", QString::fromStdString(f.host)},
                                {"port", f.port}};
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, list]() {
            if (!self) return;
            self->ndiFound_ = list;
            self->ndiSearching_ = false;
            emit self->ndiChanged();
        });
    }).detach();
#endif
}

QString InputsViewModel::addNdi(const QString& ndiName, const QString& name, const QString& scene) {
#if defined(BA_HAVE_NDI)
    if (ndiName.isEmpty()) return "Elegi una fuente NDI.";
    SourceSpec spec;
    spec.id = ndi::kSourceId;
    spec.name = (name.trimmed().isEmpty() ? "NDI " + ndiName : name.trimmed()).toStdString();
    spec.settingsJson = json{{"ndi_name", ndiName.toStdString()}}.dump();
    spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
    const std::string sc = scene.isEmpty() ? rt_.engine().currentScene() : scene.toStdString();
    if (!rt_.addSource(sc, spec)) return QString::fromStdString(rt_.lastError());
    rt_.save();
    vm_.refreshModels();
    refresh();
    return {};
#else
    (void)ndiName;
    (void)name;
    (void)scene;
    return "Esta version no trae NDI.";
#endif
}

bool InputsViewModel::hardwareAvailable() const { return rt_.engine().sourceTypeExists(inputs::kVideoType); }

void InputsViewModel::refresh() {
    devices_.clear();
    for (const auto& d : rt_.engine().sourceTypeListItems(inputs::kVideoType, inputs::kVideoProp))
        devices_ << QVariantMap{{"name", QString::fromStdString(d.name)},
                                {"value", QString::fromStdString(d.value)}};
    audioDevices_.clear();
    for (const auto& d : rt_.engine().sourceTypeListItems(inputs::kAudioInType, inputs::kAudioInProp))
        audioDevices_ << QVariantMap{{"name", QString::fromStdString(d.name)},
                                     {"value", QString::fromStdString(d.value)}};
    rebuildInputs();
    emit changed();
}

QString InputsViewModel::audioInputName(const QString& device) const {
    // El dispositivo "default" se llama "Default": con ese nombre el operador
    // no sabe que es. El resto ya trae el nombre del equipo.
    if (device == "default") return QStringLiteral("Entrada de audio predeterminada");
    for (const QVariant& d : audioDevices_)
        if (d.toMap().value("value").toString() == device) return d.toMap().value("name").toString();
    return QStringLiteral("Micrófono");
}

QString InputsViewModel::addAudioInput(const QString& device, const QString& name) {
    if (device.isEmpty()) return "Elegí una entrada de audio.";
    QString nm = name.trimmed();
    if (nm.isEmpty()) nm = audioInputName(device);
    const SourceSpec spec = inputs::audioInputSpec(device.toStdString(), nm.toStdString());
    // Canal GLOBAL, como el audio de los invitados: suena en todas las escenas
    // (un microfono no pertenece a una escena).
    if (!rt_.addAudioInput(spec)) return QString::fromStdString(rt_.lastError());
    rt_.save();
    vm_.refreshModels();
    refresh();
    return {};
}

void InputsViewModel::rebuildInputs() {
    BroadcastEngine& e = rt_.engine();
    QVariantList out;
    std::map<std::string, std::string> media;
    for (const auto& m : e.mediaSources())
        media[m.name] = m.state;
    for (const std::string& n : e.sourceNames()) {
        const std::string type = e.sourceTypeId(n);
        const json s = json::parse(e.sourceSettingsJson(n), nullptr, false);
        const bool hw = type == inputs::kVideoType;
        const bool isNdi = type == "ba_ndi_input";
        const bool net = inputs::isNetworkInput(type, s);
        if (!hw && !net) continue;
        int w = 0, h = 0;
        e.sourceSize(n, w, h);
        QStringList inScenes;
        for (const auto& sc : e.sceneNames())
            for (const auto& src : e.sceneSources(sc))
                if (src == n) inScenes << QString::fromStdString(sc);
        out << QVariantMap{
            {"name", QString::fromStdString(n)},
            {"kind", hw      ? "hardware"
                     : isNdi ? "ndi"
                             : "red"},
            {"where",
             QString::fromStdString(hw      ? s.value(inputs::kVideoProp, std::string())
                                    : isNdi ? "NDI: " + s.value("ndi_name", s.value("host", std::string()))
                                    : type == "ba_gst_fuente"
                                        ? (s.contains("ba_plugin") ? "Plugin: " + s.value("ba_plugin", std::string())
                                                                    : inputs::redactUrl(s.value("uri", s.value("pipeline", std::string()))))
                                            : inputs::redactUrl(s.value("input", std::string())))},
            {"width", w},
            {"height", h},
            {"receiving", w > 0},
            {"state", QString::fromStdString(media.count(n) ? media[n] : std::string())},
            {"scenes", inScenes}};
    }
    // Entradas de audio del equipo: no son items de escena (van por un canal
    // global), asi que no salen del recorrido de arriba. Se listan igual: para
    // el operador son entradas como cualquier otra.
    for (const std::string& n : e.globalAudioInputs()) {
        const json s = json::parse(e.sourceSettingsJson(n), nullptr, false);
        out << QVariantMap{{"name", QString::fromStdString(n)},
                           {"kind", "audio"},
                           {"where", QString::fromStdString(s.value(inputs::kAudioInProp, std::string()))},
                           {"width", 0},
                           {"height", 0},
                           {"receiving", true},
                           {"state", QString()},
                           {"scenes", QStringList{QStringLiteral("todas (audio)")}}};
    }
    if (out != inputs_) {
        inputs_ = out;
        emit changed();
    }
    syncHolds();
}

void InputsViewModel::syncHolds() {
    std::set<std::string> want;
    if (active_)
        for (const QVariant& v : inputs_)
            want.insert(v.toMap().value("name").toString().toStdString());
    for (const auto& n : held_)
        if (!want.count(n)) rt_.engine().holdShowing(n, false);
    for (const auto& n : want)
        if (!held_.count(n)) rt_.engine().holdShowing(n, true);
    held_ = want;
}

void InputsViewModel::setActive(bool on) {
    active_ = on;
    if (on) {
        refresh();
        tick_.start();
    } else {
        tick_.stop();
    }
    syncHolds();
}

QVariantList InputsViewModel::modes(const QString& device) const {
    QVariantList out;
    BroadcastEngine& e = rt_.engine();
    json s{{inputs::kVideoProp, device.toStdString()}};
#if !defined(_WIN32) && !defined(__APPLE__)
    for (const auto& f : e.sourceTypeListItems(inputs::kVideoType, "pixelformat", s.dump())) {
        json s2 = s;
        s2["pixelformat"] = std::stoll(f.value.empty() ? std::string("0") : f.value);
        for (const auto& r : e.sourceTypeListItems(inputs::kVideoType, "resolution", s2.dump())) {
            if (r.value == "-1") continue;
            out << QVariantMap{{"label", QString::fromStdString(r.name + "  ·  " + f.name)},
                               {"pixelformat", QString::fromStdString(f.value)},
                               {"resolution", QString::fromStdString(r.value)}};
        }
    }
#else
    for (const auto& r : e.sourceTypeListItems(inputs::kVideoType, "resolution", s.dump()))
        out << QVariantMap{{"label", QString::fromStdString(r.name)},
                           {"resolution", QString::fromStdString(r.value)}};
#endif
    return out;
}

QString InputsViewModel::addHardware(const QString& device, const QString& name, const QString& scene,
                                     const QVariantMap& mode) {
    if (device.isEmpty()) return "Elegi una camara.";
    json opts = json::object();
    if (mode.contains("pixelformat")) opts["pixelformat"] = mode.value("pixelformat").toLongLong();
    if (mode.contains("resolution")) opts["resolution"] = mode.value("resolution").toLongLong();
    QString nm = name.trimmed();
    if (nm.isEmpty())
        for (const QVariant& d : devices_)
            if (d.toMap().value("value").toString() == device)
                nm = "Camara " + d.toMap().value("name").toString();
    const SourceSpec spec = inputs::hardwareSpec(device.toStdString(), nm.toStdString(), opts);
    const std::string sc = scene.isEmpty() ? rt_.engine().currentScene() : scene.toStdString();
    if (!rt_.addSource(sc, spec)) return QString::fromStdString(rt_.lastError());
    rt_.save();
    vm_.refreshModels();
    refresh();
    return {};
}

QString InputsViewModel::addNetwork(const QString& url, const QString& name, const QString& scene,
                                    bool lowLatency) {
    const std::string u = url.trimmed().toStdString();
    if (inputs::networkScheme(u).empty())
        return "La direccion tiene que empezar con rtsp://, rtmp://, srt://, http(s)://, udp:// o rtp://.";
    const std::string nm =
        name.trimmed().isEmpty() ? "Red " + inputs::redactUrl(u).substr(0, 40) : name.trimmed().toStdString();
    const SourceSpec spec = inputs::networkSpec(u, nm, json{{"latency", lowLatency ? "low" : "normal"}},
                                                (ba::platform::bundleRoot() / "data" / "cacert.pem").string());
    const std::string sc = scene.isEmpty() ? rt_.engine().currentScene() : scene.toStdString();
    if (!rt_.addSource(sc, spec)) return QString::fromStdString(rt_.lastError());
    rt_.save();
    vm_.refreshModels();
    refresh();
    return {};
}

void InputsViewModel::remove(const QString& name) {
    const std::string n = name.toStdString();
    if (held_.count(n)) {
        rt_.engine().holdShowing(n, false);
        held_.erase(n);
    }
    for (const auto& sc : rt_.engine().sceneNames())
        for (const auto& src : rt_.engine().sceneSources(sc))
            if (src == n) rt_.removeSource(sc, n);
    // Las entradas de audio no estan en ninguna escena: van por su canal global.
    rt_.removeAudioInput(n);
    rt_.save();
    vm_.refreshModels();
    refresh();
}

QImage InputThumbProvider::requestImage(const QString& id, QSize* size, const QSize& requested) {
    const QString name = QUrl::fromPercentEncoding(id.section('?', 0, 0).toUtf8());
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    QImage img;
    if (rt_.engine().captureSource(name.toStdString(), requested.width() > 0 ? requested.width() : 480, rgba,
                                   w, h))
        img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
    if (img.isNull()) {
        img = QImage(16, 9, QImage::Format_RGBA8888);
        img.fill(Qt::black);
    }
    if (size != nullptr) *size = img.size();
    return img;
}

} // namespace ba::app
