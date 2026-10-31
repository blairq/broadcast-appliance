// ============================================================================
// src/app/ui/ListModels.cpp — ver ListModels.hpp
// ============================================================================

#include "ui/ListModels.hpp"

#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QSaveFile>
#include <QFile>

#include <algorithm>
#include <filesystem>
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
        case LiveRole: return n == rt_.engine().currentScene(salida_);
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

void ScenesModel::setSalida(const std::string& salida) {
    if (salida == salida_) return;
    salida_ = salida;
    liveChanged();
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(ef.aJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
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
        spec.settingsJson = nlohmann::json{{inputs::kVideoProp, camaras.empty() ? std::string() : camaras.front().value}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    if (!rt_.addSource(scene_.toStdString(), spec)) {
        emit error(qs(rt_.lastError()));
        return {};
    }
    reload();
    return qs(name);
}

QString SourcesModel::addConAjustes(const QString& typeId, const QString& baseName, const QVariantMap& ajustes) {
    std::string name = baseName.trimmed().isEmpty() ? typeId.toStdString() : baseName.trimmed().toStdString();
    const std::string base = name;
    for (int i = 2; !rt_.engine().sourceTypeId(name).empty(); ++i) name = base + " " + std::to_string(i);
    SourceSpec spec;
    spec.id = typeId.toStdString();
    spec.name = name;
    spec.rect = CanvasRect{0.7f, 0.55f, 0.25f, 0.4f}; // abajo a la derecha: se mueve con el mouse
    spec.settingsJson = QJsonDocument(QJsonObject::fromVariantMap(ajustes)).toJson(QJsonDocument::Compact).toStdString();
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
        emit const_cast<SourcesModel*>(this)->error(tr("No existe la fuente '%1'.").arg(name));
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

namespace {
/// Los grupos compactados se recuerdan entre arranques: con muchos invitados, cada vez.
std::filesystem::path archivoVistaMezclador(AppRuntime& rt) { return rt.dataRoot() / "Config" / "mezclador-vista.json"; }
} // namespace

MixerModel::MixerModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {
    QFile f(QString::fromStdString(archivoVistaMezclador(rt_).string()));
    if (f.open(QIODevice::ReadOnly)) {
        const nlohmann::json j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
        if (j.is_object() && j.contains("compactos") && j["compactos"].is_array())
            for (const auto& g : j["compactos"])
                if (g.is_string()) compactos_.insert(g.get<std::string>());
    }
    reload();
}

int MixerModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

QVariant MixerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::size_t i = static_cast<std::size_t>(index.row());
    switch (role) {
        case NameRole: return qs(names_[i]);
        case PeakDbRole: return peaks_[i];
        case VolumeDbRole: return mulToDb(rt_.engine().sourceVolume(names_[i], salida_));
        case MutedRole: return rt_.engine().sourceMuted(names_[i], salida_);
        case RetardoMsRole: return rt_.engine().sourceSyncOffset(names_[i]);
        case GrupoRole: return qs(grupos_[i]);
        case PrimeroDeGrupoRole: return !grupos_[i].empty() && (i == 0 || grupos_[i - 1] != grupos_[i]);
        case TamGrupoRole: {
            if (grupos_[i].empty()) return 1;
            int n = 0;
            for (const std::string& g : grupos_) n += g == grupos_[i] ? 1 : 0;
            return n;
        }
        case CompactoRole: return !grupos_[i].empty() && compactos_.count(grupos_[i]) > 0;
        case GananciaGrupoDbRole:
            return grupos_[i].empty() ? 0.0 : mulToDb(rt_.engine().gananciaGrupo(grupos_[i]));
        case NombreGrupoRole: {
            const auto it = nombresGrupo_.find(grupos_[i]);
            return qs(it != nombresGrupo_.end() ? it->second : grupos_[i]);
        }
        default: return {};
    }
}

QHash<int, QByteArray> MixerModel::roleNames() const {
    return {{NameRole, "name"},
            {PeakDbRole, "peakDb"},
            {VolumeDbRole, "volumeDb"},
            {MutedRole, "muted"},
            {RetardoMsRole, "retardoMs"},
            {GrupoRole, "grupo"},
            {PrimeroDeGrupoRole, "primeroDeGrupo"},
            {TamGrupoRole, "tamGrupo"},
            {CompactoRole, "compacto"},
            {NombreGrupoRole, "nombreGrupo"},
            {GananciaGrupoDbRole, "gananciaGrupoDb"}};
}

void MixerModel::reload() {
    beginResetModel();
    names_ = rt_.engine().audioSources();
    peaks_.assign(names_.size(), -100.0f);
    // Los grupos: tiras seguidas con la misma clave (dos o más); las demás, sueltas.
    grupos_.assign(names_.size(), std::string());
    for (std::size_t i = 0; i < names_.size();) {
        const std::string g = BroadcastEngine::grupoDeAudio(names_[i]);
        std::size_t j = i + 1;
        while (j < names_.size() && BroadcastEngine::grupoDeAudio(names_[j]) == g) ++j;
        if (j - i >= 2) {
            for (std::size_t k = i; k < j; ++k) grupos_[k] = g;
            // La gente conectada se llama como en Audio y Video: Conferencia o Asamblea.
            const std::string& n = names_[i];
            const nlohmann::json ajustes = nlohmann::json::parse(rt_.engine().sourceSettingsJson(n), nullptr, false);
            const char* gente = inputs::grupoDeGente(inputs::tipoLogico(rt_.engine().sourceTypeId(n), ajustes), ajustes);
            nombresGrupo_[g] = gente != nullptr ? gente : g;
        }
        i = j;
    }
    endResetModel();
}

QAbstractItemModel* MixerModel::delGrupo(const QString& grupo) {
    auto& m = delGrupo_[grupo];
    if (m == nullptr) {
        auto* f = new QSortFilterProxyModel(this);
        f->setSourceModel(this);
        f->setFilterRole(GrupoRole);
        f->setFilterRegularExpression(
            QRegularExpression("^" + QRegularExpression::escape(grupo) + "$"));
        m = f;
    }
    return m;
}

QVariantList MixerModel::grupos() const {
    QVariantList out;
    for (std::size_t i = 0; i < grupos_.size(); ++i) {
        if (grupos_[i].empty() || (i > 0 && grupos_[i - 1] == grupos_[i])) continue;
        QStringList tiras;
        for (std::size_t k = i; k < grupos_.size() && grupos_[k] == grupos_[i]; ++k) tiras << qs(names_[k]);
        const auto nom = nombresGrupo_.find(grupos_[i]);
        out.push_back(QVariantMap{{"grupo", qs(grupos_[i])},
                                  {"nombre", qs(nom != nombresGrupo_.end() ? nom->second : grupos_[i])},
                                  {"tiras", tiras},
                                  {"volumenDb", mulToDb(rt_.engine().gananciaGrupo(grupos_[i]))},
                                  {"compacto", compactos_.count(grupos_[i]) > 0}});
    }
    return out;
}

bool MixerModel::setGananciaGrupoDb(const QString& grupo, double db) {
    if (grupo.isEmpty()) return false;
    const double d = std::clamp(db, -60.0, 12.0);
    return rt_.setGananciaGrupo(grupo.toStdString(), d <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, d / 20.0)));
}

void MixerModel::setMutedGrupo(const QString& grupo, bool muted) {
    const std::string g = grupo.toStdString();
    for (std::size_t i = 0; i < grupos_.size(); ++i)
        if (!g.empty() && grupos_[i] == g) rt_.setMuted(names_[i], muted, salida_);
}

void MixerModel::setCompacto(const QString& grupo, bool compacto) {
    const std::string g = grupo.toStdString();
    if (g.empty() || (compactos_.count(g) > 0) == compacto) return;
    if (compacto) compactos_.insert(g);
    else compactos_.erase(g);
    std::error_code ec;
    std::filesystem::create_directories(archivoVistaMezclador(rt_).parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivoVistaMezclador(rt_).string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromStdString(nlohmann::json{{"compactos", compactos_}}.dump(1)));
        f.commit();
    }
    for (std::size_t i = 0; i < grupos_.size(); ++i)
        if (grupos_[i] == g) emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {CompactoRole});
}

void MixerModel::refreshLevels() {
    if (names_ != rt_.engine().audioSources()) {
        reload();
        return;
    }
    // Solo se avisa lo que cambió (en silencio todo queda en -100: sin avisos, las
    // vistas no reevalúan nada diez veces por segundo).
    int desde = -1, hasta = -1;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        // Caida suave: 1,5 dB por vuelta del tick (100 ms).
        const float antes = peaks_[i];
        peaks_[i] = std::max(rt_.engine().sourcePeakDb(names_[i]), peaks_[i] - 1.5f);
        if (peaks_[i] < -100.0f) peaks_[i] = -100.0f;
        if (std::abs(peaks_[i] - antes) > 0.1f) {
            if (desde < 0) desde = static_cast<int>(i);
            hasta = static_cast<int>(i);
        }
    }
    if (desde >= 0) emit dataChanged(index(desde), index(hasta), {PeakDbRole});
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

// El aviso a las vistas lo trae el motor (avisarCambio): igual que si lo cambia el
// asistente, el AFV o la otra vista.
bool MixerModel::setVolumeDb(const QString& name, double db) {
    const float mul = db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
    return rt_.setVolume(name.toStdString(), mul, salida_);
}

bool MixerModel::setMuted(const QString& name, bool muted) { return rt_.setMuted(name.toStdString(), muted, salida_); }

void MixerModel::setSalida(const std::string& salida) {
    if (salida == salida_) return;
    salida_ = salida;
    if (!names_.empty()) emit dataChanged(index(0), index(rowCount() - 1), {VolumeDbRole, MutedRole});
}

void MixerModel::avisarCambio(const std::string& fuente) {
    bool primero = false;
    {
        std::lock_guard<std::mutex> l(cambiosM_);
        primero = cambios_.empty();
        cambios_.insert(fuente);
    }
    if (!primero) return; // ya hay una vuelta en camino
    QMetaObject::invokeMethod(
        this,
        [this]() {
            std::set<std::string> c;
            {
                std::lock_guard<std::mutex> l(cambiosM_);
                c.swap(cambios_);
            }
            for (const std::string& f : c) {
                const auto it = std::find(names_.begin(), names_.end(), f);
                if (it == names_.end()) continue;
                const int row = static_cast<int>(it - names_.begin());
                emit dataChanged(index(row), index(row), {VolumeDbRole, MutedRole, RetardoMsRole, GananciaGrupoDbRole});
            }
        },
        Qt::QueuedConnection);
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
