// ============================================================================
// src/app/ui/ListModels.cpp — ver ListModels.hpp
// ============================================================================

#include "ui/ListModels.hpp"

#include <algorithm>
#include <cmath>

#include <QJsonDocument>
#include <QJsonObject>

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "video/Camaras.hpp"

#include <nlohmann/json.hpp>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

double mulToDb(float mul) { return mul <= 0.00001f ? -100.0 : 20.0 * std::log10(mul); }

} // namespace

// ============================================================================
// ScenesModel
// ============================================================================

ScenesModel::ScenesModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) { reload(); }

int ScenesModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

QVariant ScenesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::string& n = names_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case NameRole: return qs(n);
        case LiveRole: return n == rt_.engine().currentScene();
        case FolderRole: return qs(rt_.sceneFolder(n));
        case ProtegidaRole: return rt_.sceneProtected(n);
        case ArmadaRole: return qs(n) == armada_;
        default: return {};
    }
}

QHash<int, QByteArray> ScenesModel::roleNames() const {
    return {{NameRole, "name"},
            {LiveRole, "live"},
            {FolderRole, "folder"},
            {ProtegidaRole, "protegida"},
            {ArmadaRole, "armada"}};
}

void ScenesModel::setArmada(const QString& n) {
    if (n == armada_) return;
    armada_ = n;
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {ArmadaRole});
}

void ScenesModel::reload() {
    beginResetModel();
    names_ = rt_.orderedSceneNames();
    endResetModel();
}

void ScenesModel::liveChanged() {
    if (!names_.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {LiveRole});
    }
}

// ============================================================================
// SourcesModel
// ============================================================================

SourcesModel::SourcesModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {
    for (const auto& t : rt.engine().inputTypes()) {
        types_[t.id] = TypeInfo{qs(t.displayName), t.video, t.audio};
    }
}

int SourcesModel::rowOf(const std::string& name) const {
    const auto it = std::find(names_.begin(), names_.end(), name);
    return it == names_.end()
               ? -1
               : static_cast<int>(names_.size() - 1 - static_cast<std::size_t>(it - names_.begin()));
}

int SourcesModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

QVariant SourcesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    // Se muestra de ARRIBA hacia abajo (como en cualquier editor de capas).
    const std::string& n = names_[names_.size() - 1 - static_cast<std::size_t>(index.row())];
    const SceneConfig* sc = findScene(const_cast<AppConfig&>(rt_.config()), scene_.toStdString());
    const SourceSpec* spec = nullptr;
    if (sc != nullptr) {
        for (const auto& s : sc->sources) {
            if (s.name == n) {
                spec = &s;
            }
        }
    }
    const std::string typeId = rt_.engine().sourceTypeId(n);
    switch (role) {
        case NameRole: return qs(n);
        case TypeIdRole: return qs(typeId);
        case TypeNameRole: {
            const auto t = types_.find(typeId);
            return t != types_.end() ? t->second.name : qs(typeId);
        }
        case VisibleRole: return rt_.engine().sourceVisible(scene_.toStdString(), n);
        case IsVideoRole:
        case IsAudioRole: {
            const auto t = types_.find(typeId);
            return t != types_.end() && (role == IsVideoRole ? t->second.video : t->second.audio);
        }
        case RectXRole: return spec != nullptr ? spec->rect.x : 0.0;
        case RectYRole: return spec != nullptr ? spec->rect.y : 0.0;
        case RectWRole: return spec != nullptr ? spec->rect.w : 1.0;
        case RectHRole: return spec != nullptr ? spec->rect.h : 1.0;
        default: return {};
    }
}

QHash<int, QByteArray> SourcesModel::roleNames() const {
    return {{NameRole, "name"},     {TypeIdRole, "typeId"},   {TypeNameRole, "typeName"},
            {VisibleRole, "shown"}, {IsVideoRole, "isVideo"}, {IsAudioRole, "isAudio"},
            {RectXRole, "rx"},      {RectYRole, "ry"},        {RectWRole, "rw"},
            {RectHRole, "rh"}};
}

void SourcesModel::setScene(const QString& s) {
    if (s == scene_) {
        return;
    }
    scene_ = s;
    emit sceneChanged();
    reload();
}

void SourcesModel::reload() {
    beginResetModel();
    names_ = rt_.engine().sceneSources(scene_.toStdString());
    endResetModel();
}

QVariantMap SourcesModel::capa(int fila) const {
    if (fila < 0 || fila >= rowCount()) return {};
    const QModelIndex i = index(fila, 0);
    return QVariantMap{{"nombre", data(i, NameRole)}, {"visible", data(i, VisibleRole)}};
}

bool SourcesModel::setVisible(const QString& name, bool visible) {
    const bool ok = rt_.setSourceVisible(scene_.toStdString(), name.toStdString(), visible);
    const int row = rowOf(name.toStdString());
    if (ok && row >= 0) {
        emit dataChanged(index(row), index(row), {VisibleRole});
    }
    return ok;
}

bool SourcesModel::move(const QString& name, int delta) {
    const bool ok = rt_.moveSource(scene_.toStdString(), name.toStdString(), delta);
    if (ok) {
        reload();
    }
    return ok;
}

bool SourcesModel::remove(const QString& name) {
    const bool ok = rt_.removeSource(scene_.toStdString(), name.toStdString());
    if (ok) {
        reload();
    } else {
        emit error(qs(rt_.lastError()));
    }
    return ok;
}

bool SourcesModel::setRect(const QString& name, double x, double y, double w, double h) {
    const bool ok = rt_.setSourceRect(scene_.toStdString(), name.toStdString(),
                                      CanvasRect{static_cast<float>(x), static_cast<float>(y),
                                                 static_cast<float>(w), static_cast<float>(h)});
    const int row = rowOf(name.toStdString());
    if (ok && row >= 0) {
        emit dataChanged(index(row), index(row), {RectXRole, RectYRole, RectWRole, RectHRole});
    }
    return ok;
}

QVariantMap SourcesModel::efectos(const QString& name) const {
    const EfectosCapa ef = rt_.engine().sourceEfectos(scene_.toStdString(), name.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(ef.aJson().dump())).object().toVariantMap();
}

bool SourcesModel::setEfectos(const QString& name, const QVariantMap& cambios) {
    EfectosCapa ef = rt_.engine().sourceEfectos(scene_.toStdString(), name.toStdString());
    const QByteArray j = QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson(QJsonDocument::Compact);
    std::string err;
    if (!ef.aplicar(nlohmann::json::parse(j.toStdString(), nullptr, false), &err)) {
        emit error(qs(err));
        return false;
    }
    if (!rt_.setSourceEfectos(scene_.toStdString(), name.toStdString(), ef)) {
        emit error(qs(rt_.lastError()));
        return false;
    }
    return true;
}

QString SourcesModel::add(const QString& typeId, const QString& baseName) {
    // Nombre unico en todo libobs: "Camara", "Camara 2", ...
    std::string name = baseName.trimmed().isEmpty() ? typeId.toStdString() : baseName.trimmed().toStdString();
    const std::string base = name;
    for (int i = 2; !rt_.engine().sourceTypeId(name).empty(); ++i) {
        name = base + " " + std::to_string(i);
    }
    SourceSpec spec;
    spec.id = typeId.toStdString();
    spec.name = name;
    spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
    if (spec.id == inputs::kVideoType) {
        // Una cámara: la primera conectada ya elegida (en Propiedades se cambia).
        const auto camaras = video::camarasDelEquipo(rt_.engine());
        spec.settingsJson = nlohmann::json{{inputs::kVideoProp, camaras.empty() ? std::string() : camaras.front().value}}.dump();
    }
    if (!rt_.addSource(scene_.toStdString(), spec)) {
        emit error(qs(rt_.lastError()));
        return {};
    }
    reload();
    return qs(name);
}

int SourcesModel::guestSlots() const { return rt_.config().guests; }

QVariantList SourcesModel::otherSources() const {
    QVariantList out;
    for (const std::string& n : rt_.engine().sourceNames()) {
        if (rowOf(n) >= 0) continue; // ya esta en esta escena
        const std::string type = rt_.engine().sourceTypeId(n);
        const auto t = types_.find(type);
        out.push_back(
            QVariantMap{{"name", qs(n)}, {"typeName", t != types_.end() ? t->second.name : qs(type)}});
    }
    return out;
}

QString SourcesModel::addExisting(const QString& name) {
    const std::string n = name.toStdString();
    if (rowOf(n) >= 0) return name; // ya esta
    SourceSpec spec;
    spec.id = rt_.engine().sourceTypeId(n);
    spec.name = n;
    if (spec.id.empty()) {
        emit const_cast<SourcesModel*>(this)->error("No existe la fuente '" + name + "'.");
        return {};
    }
    // Sin settings: addSource ve que ya existe una fuente con ese nombre y
    // agrega LA MISMA a esta escena (no abre el dispositivo otra vez).
    spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
    if (!rt_.addSource(scene_.toStdString(), spec)) {
        emit error(qs(rt_.lastError()));
        return {};
    }
    reload();
    return name;
}

QString SourcesModel::addGuest(int slot, bool share) {
    if (slot < 1 || slot > guestSlots()) return {};
    // Mismos nombres que crea el arranque: si la fuente ya existe se agrega
    // la MISMA a esta escena (libobs comparte la fuente entre escenas).
    SourceSpec spec;
    spec.id = "ba_guest_video";
    spec.name = "Invitado " + std::to_string(slot) + (share ? " — pantalla" : "");
    if (rowOf(spec.name) >= 0) return qs(spec.name); // ya esta en esta escena
    spec.settingsJson = "{\"slot\":" + std::to_string(slot) + (share ? ",\"channel\":\"share\"}" : "}");
    spec.rect = share ? CanvasRect{0.0f, 0.0f, 1.0f, 1.0f} : CanvasRect{0.5f, 0.5f, 0.5f, 0.5f};
    if (!rt_.addSource(scene_.toStdString(), spec)) {
        emit error(qs(rt_.lastError()));
        return {};
    }
    reload();
    return qs(spec.name);
}

// ============================================================================
// MixerModel
// ============================================================================

MixerModel::MixerModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) { reload(); }

int MixerModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

QVariant MixerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::size_t i = static_cast<std::size_t>(index.row());
    switch (role) {
        case NameRole: return qs(names_[i]);
        case PeakDbRole: return peaks_[i];
        case VolumeDbRole: return mulToDb(rt_.engine().sourceVolume(names_[i]));
        case MutedRole: return rt_.engine().sourceMuted(names_[i]);
        default: return {};
    }
}

QHash<int, QByteArray> MixerModel::roleNames() const {
    return {{NameRole, "name"}, {PeakDbRole, "peakDb"}, {VolumeDbRole, "volumeDb"}, {MutedRole, "muted"}};
}

void MixerModel::reload() {
    beginResetModel();
    names_ = rt_.engine().audioSources();
    peaks_.assign(names_.size(), -100.0f);
    endResetModel();
}

void MixerModel::refreshLevels() {
    if (names_ != rt_.engine().audioSources()) {
        reload();
        return;
    }
    for (std::size_t i = 0; i < names_.size(); ++i) {
        // Caida suave: 1,5 dB por vuelta del tick (100 ms).
        peaks_[i] = std::max(rt_.engine().sourcePeakDb(names_[i]), peaks_[i] - 1.5f);
    }
    if (!names_.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {PeakDbRole});
    }
}

double MixerModel::picoDe(const std::string& nombre) const {
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == nombre) return peaks_[i];
    return -100.0;
}

QVariantMap MixerModel::canal(int fila) const {
    if (fila < 0 || fila >= static_cast<int>(names_.size())) return {};
    const QModelIndex i = index(fila, 0);
    return QVariantMap{
        {"nombre", data(i, NameRole)}, {"volumenDb", data(i, VolumeDbRole)}, {"mudo", data(i, MutedRole)}};
}

bool MixerModel::setVolumeDb(const QString& name, double db) {
    const float mul = db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
    const bool ok = rt_.setVolume(name.toStdString(), mul);
    if (ok) {
        const auto it = std::find(names_.begin(), names_.end(), name.toStdString());
        const int row = static_cast<int>(it - names_.begin());
        emit dataChanged(index(row), index(row), {VolumeDbRole});
    }
    return ok;
}

bool MixerModel::setMuted(const QString& name, bool muted) {
    const bool ok = rt_.setMuted(name.toStdString(), muted);
    if (ok) {
        const auto it = std::find(names_.begin(), names_.end(), name.toStdString());
        const int row = static_cast<int>(it - names_.begin());
        emit dataChanged(index(row), index(row), {MutedRole});
    }
    return ok;
}

// ============================================================================
// SourceTypesModel
// ============================================================================

SourceTypesModel::SourceTypesModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent) {
    for (const auto& t : rt.engine().inputTypes()) {
        // ba_guest_* se manejan desde Invitados, no se agregan a mano.
        if (t.id.rfind("ba_guest", 0) == 0) {
            continue;
        }
        rows_.push_back(Row{qs(t.id), qs(t.displayName), t.video, t.audio});
    }
    std::sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) { return a.name < b.name; });
}

int SourceTypesModel::rowCount(const QModelIndex&) const { return static_cast<int>(rows_.size()); }

QVariant SourceTypesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Row& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return r.id;
        case DisplayNameRole: return r.name;
        case VideoRole: return r.video;
        case AudioRole: return r.audio;
        default: return {};
    }
}

QHash<int, QByteArray> SourceTypesModel::roleNames() const {
    return {{IdRole, "typeId"}, {DisplayNameRole, "displayName"}, {VideoRole, "video"}, {AudioRole, "audio"}};
}

} // namespace ba::app
