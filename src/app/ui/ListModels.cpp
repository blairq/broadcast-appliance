// ============================================================================
// src/app/ui/ListModels.cpp — ver ListModels.hpp
// ============================================================================

#include "ui/ListModels.hpp"
#include "arranque/ArchivoConfig.hpp"

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
#include "emision/VentanillaMotor.hpp"
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

ScenesModel::ScenesModel(VentanillaMotor& ventanilla, QObject* parent)
    : QAbstractListModel(parent), v_(ventanilla), foto_(ventanilla.fotoCompartida()) {
    for (const auto& e : foto_->escenas) names_.push_back(e.nombre);
    connect(&v_, &VentanillaMotor::cambio, this, &ScenesModel::alCambiar);
}

int ScenesModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

const FotoMotor::Escena* ScenesModel::escenaDe(int fila) const {
    if (fila < 0 || fila >= rowCount()) return nullptr;
    return foto_->escena(names_[static_cast<std::size_t>(fila)]);
}

QVariant ScenesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::string& n = names_[static_cast<std::size_t>(index.row())];
    const FotoMotor::Escena* e = escenaDe(index.row());
    const FotoMotor::Salida* s = foto_->salida(salida_);
    switch (role) {
        case NameRole: return qs(n);
        case LiveRole: return s != nullptr && n == s->alAire;
        case FolderRole: return e != nullptr ? qs(e->carpeta) : QString();
        case ProtegidaRole: return e != nullptr && e->protegida;
        case ArmadaRole: return s != nullptr && n == s->armada;
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

QString ScenesModel::armada() const {
    const FotoMotor::Salida* s = foto_->salida(salida_);
    return s != nullptr ? qs(s->armada) : QString();
}

void ScenesModel::setSalida(const std::string& salida) {
    if (salida == salida_) return;
    salida_ = salida;
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {LiveRole, ArmadaRole});
}

void ScenesModel::alCambiar(unsigned partes) {
    if ((partes & (FotoMotor::Escenas | FotoMotor::Salidas)) == 0) return;
    const std::shared_ptr<const FotoMotor> antes = foto_;
    std::vector<std::string> nombres;
    for (const auto& e : v_.foto().escenas) nombres.push_back(e.nombre);
    if (nombres != names_) {
        // Se agregó, borró, renombró o movió una escena: la lista entera.
        beginResetModel();
        foto_ = v_.fotoCompartida();
        names_ = std::move(nombres);
        endResetModel();
        return;
    }
    foto_ = v_.fotoCompartida();
    if (rowCount() == 0) return;
    // Lo de cada fila, sin rehacer la lista (no se pierde la selección): la carpeta y la
    // protección, y lo que está al aire y armado en su salida.
    QList<int> roles;
    if ((partes & FotoMotor::Escenas) != 0) roles << FolderRole << ProtegidaRole;
    const FotoMotor::Salida* a = antes->salida(salida_);
    const FotoMotor::Salida* n = foto_->salida(salida_);
    if ((a != nullptr ? a->alAire : std::string()) != (n != nullptr ? n->alAire : std::string())) roles << LiveRole;
    if ((a != nullptr ? a->armada : std::string()) != (n != nullptr ? n->armada : std::string())) roles << ArmadaRole;
    if (!roles.isEmpty()) emit dataChanged(index(0), index(rowCount() - 1), roles);
}

// ============================================================================
// SourcesModel
// ============================================================================

SourcesModel::SourcesModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : QAbstractListModel(parent), rt_(rt), v_(ventanilla), foto_(ventanilla.fotoCompartida()) {
    connect(&v_, &VentanillaMotor::cambio, this, &SourcesModel::alCambiar);
}

int SourcesModel::rowOf(const std::string& name) const {
    const auto it = std::find(names_.begin(), names_.end(), name);
    return it == names_.end()
               ? -1
               : static_cast<int>(names_.size() - 1 - static_cast<std::size_t>(it - names_.begin()));
}

int SourcesModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

const FotoMotor::Fuente* SourcesModel::fuenteDe(const std::string& name) const {
    if (const FotoMotor::Escena* e = foto_->escena(scene_.toStdString()))
        for (const FotoMotor::Fuente& f : e->fuentes)
            if (f.nombre == name) return &f;
    return nullptr;
}

QVariant SourcesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    // Se muestra de ARRIBA hacia abajo (como en cualquier editor de capas).
    const std::string& n = names_[names_.size() - 1 - static_cast<std::size_t>(index.row())];
    const FotoMotor::Fuente* f = fuenteDe(n);
    const auto p = pendientes_.find(n);
    const Pendiente* pend = p != pendientes_.end() ? &p->second : nullptr;
    const std::string typeId = f != nullptr ? f->tipo : foto_->tipoDe(n);
    const FotoMotor::Tipo* tipo = nullptr;
    for (const FotoMotor::Tipo& t : foto_->tipos)
        if (t.id == typeId) tipo = &t;
    const FotoMotor::Lugar lugar = pend != nullptr && pend->lugar ? *pend->lugar
                                   : f != nullptr                 ? f->lugar
                                                                  : FotoMotor::Lugar{};
    switch (role) {
        case NameRole: return qs(n);
        case TypeIdRole: return qs(typeId);
        case TypeNameRole: return tipo != nullptr ? qs(tipo->nombre) : qs(typeId);
        case VisibleRole: return pend != nullptr && pend->visible ? *pend->visible : f == nullptr || f->visible;
        case IsVideoRole: return tipo != nullptr && tipo->video;
        case IsAudioRole: return tipo != nullptr && tipo->audio;
        case RectXRole: return static_cast<double>(lugar.x);
        case RectYRole: return static_cast<double>(lugar.y);
        case RectWRole: return static_cast<double>(lugar.w);
        case RectHRole: return static_cast<double>(lugar.h);
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
    releer();
}

void SourcesModel::releer() {
    beginResetModel();
    foto_ = v_.fotoCompartida();
    names_ = foto_->fuentesDe(scene_.toStdString());
    endResetModel();
    emit efectosCambio();
}

void SourcesModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Ajustes) != 0) emit guestSlotsChanged();
    if ((partes & FotoMotor::Escenas) == 0) return;
    // Si cambiaron las fuentes de la escena que se edita o su orden, se rehace; si no, solo
    // cómo está cada capa (sin rehacer: no se corta un arrastre a medias).
    if (v_.foto().fuentesDe(scene_.toStdString()) != names_) return releer();
    foto_ = v_.fotoCompartida();
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
    emit efectosCambio();
}

void SourcesModel::pedir(const char* que, const std::string& name, std::function<std::string(AppRuntime&)> orden) {
    ++pendientes_[name].pedidos;
    v_.pedir(
        this, que, [&rt = rt_, orden = std::move(orden)]() { return orden(rt); },
        [this, name](const std::string& e) {
            // La respuesta llega después de la foto con el cambio: lo pendiente se suelta.
            if (auto it = pendientes_.find(name); it != pendientes_.end() && --it->second.pedidos <= 0) {
                pendientes_.erase(it);
                if (const int row = rowOf(name); row >= 0) emit dataChanged(index(row), index(row));
                emit efectosCambio();
            }
            if (!e.empty()) emit error(qs(e));
        });
}

QVariantMap SourcesModel::capa(int fila) const {
    if (fila < 0 || fila >= rowCount()) return {};
    const QModelIndex i = index(fila, 0);
    return QVariantMap{{"nombre", data(i, NameRole)}, {"visible", data(i, VisibleRole)}};
}

bool SourcesModel::setVisible(const QString& name, bool visible) {
    const std::string n = name.toStdString();
    const int row = rowOf(n);
    if (row < 0) return false;
    pendientes_[n].visible = visible;
    emit dataChanged(index(row), index(row), {VisibleRole});
    pedir("fuente_visible", n, [escena = scene_.toStdString(), n, visible](AppRuntime& rt) {
        return rt.setSourceVisible(escena, n, visible) ? std::string() : rt.lastError();
    });
    return true;
}

bool SourcesModel::move(const QString& name, int delta) {
    const std::string n = name.toStdString();
    if (rowOf(n) < 0) return false;
    pedir("fuente_mover", n, [escena = scene_.toStdString(), n, delta](AppRuntime& rt) {
        return rt.moveSource(escena, n, delta) ? std::string() : rt.lastError();
    });
    return true;
}

bool SourcesModel::remove(const QString& name) {
    const std::string n = name.toStdString();
    if (rowOf(n) < 0) return false;
    pedir("fuente_quitar", n, [escena = scene_.toStdString(), n](AppRuntime& rt) {
        return rt.removeSource(escena, n) ? std::string() : rt.lastError();
    });
    return true;
}

bool SourcesModel::setRect(const QString& name, double x, double y, double w, double h) {
    const std::string n = name.toStdString();
    const int row = rowOf(n);
    if (row < 0) return false;
    const FotoMotor::Lugar l{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    // Se ve ya donde se soltó (la vista vuelve a enlazar su lugar a estos roles).
    pendientes_[n].lugar = l;
    emit dataChanged(index(row), index(row), {RectXRole, RectYRole, RectWRole, RectHRole});
    pedir("fuente_lugar", n, [escena = scene_.toStdString(), n, l](AppRuntime& rt) {
        return rt.setSourceRect(escena, n, CanvasRect{l.x, l.y, l.w, l.h}) ? std::string() : rt.lastError();
    });
    return true;
}

QVariantMap SourcesModel::efectos(const QString& name) const {
    const std::string n = name.toStdString();
    nlohmann::json j = nlohmann::json::object();
    if (const auto p = pendientes_.find(n); p != pendientes_.end() && p->second.efectos)
        j = *p->second.efectos;
    else if (const FotoMotor::Fuente* f = fuenteDe(n))
        j = f->efectos;
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

bool SourcesModel::setEfectos(const QString& name, const QVariantMap& cambios) {
    const std::string n = name.toStdString();
    if (rowOf(n) < 0) return false;
    // Lo que tiene ahora (lo pedido o la foto) con los cambios: la validación es del modelo
    // de la capa (EfectosCapa) y no necesita el motor.
    EfectosCapa ef;
    ef.aplicar(nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(efectos(name))).toJson(QJsonDocument::Compact).toStdString(),
                                     nullptr, false),
               nullptr);
    const QByteArray j = QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson(QJsonDocument::Compact);
    std::string err;
    if (!ef.aplicar(nlohmann::json::parse(j.toStdString(), nullptr, false), &err)) {
        emit error(qs(err));
        return false;
    }
    pendientes_[n].efectos = ef.aJson();
    emit efectosCambio();
    pedir("fuente_efectos", n, [escena = scene_.toStdString(), n, ef](AppRuntime& rt) {
        return rt.setSourceEfectos(escena, n, ef) ? std::string() : rt.lastError();
    });
    return true;
}

std::string SourcesModel::nombreLibre(const std::string& pedido) const {
    // Nombre unico entre todas las fuentes: "Camara", "Camara 2", ... (las que ya se
    // pidieron y todavía no llegaron a la foto, también).
    const auto usado = [this](const std::string& n) { return !v_.foto().tipoDe(n).empty() || pendientes_.count(n) > 0; };
    std::string name = pedido;
    for (int i = 2; usado(name); ++i) name = pedido + " " + std::to_string(i);
    return name;
}

void SourcesModel::pedirAgregar(SourceSpec spec, bool elegirCamara) {
    const std::string n = spec.name;
    pedir("fuente_agregar", n, [escena = scene_.toStdString(), spec = std::move(spec), elegirCamara](AppRuntime& rt) mutable {
        if (elegirCamara && spec.id == inputs::kVideoType) {
            // Una cámara: la primera conectada ya elegida (en Propiedades se cambia). Se
            // pregunta acá, en el dueño: enumerar las cámaras toca el motor.
            const auto camaras = video::camarasDelEquipo(rt.engine());
            spec.settingsJson = nlohmann::json{{inputs::kVideoProp, camaras.empty() ? std::string() : camaras.front().value}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        return rt.addSource(escena, spec) ? std::string() : rt.lastError();
    });
}

QString SourcesModel::add(const QString& typeId, const QString& baseName) {
    SourceSpec spec;
    spec.id = typeId.toStdString();
    spec.name = nombreLibre(baseName.trimmed().isEmpty() ? typeId.toStdString() : baseName.trimmed().toStdString());
    spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
    const QString nombre = qs(spec.name);
    pedirAgregar(std::move(spec), /*elegirCamara=*/true);
    return nombre;
}

QString SourcesModel::addConAjustes(const QString& typeId, const QString& baseName, const QVariantMap& ajustes) {
    SourceSpec spec;
    spec.id = typeId.toStdString();
    spec.name = nombreLibre(baseName.trimmed().isEmpty() ? typeId.toStdString() : baseName.trimmed().toStdString());
    spec.rect = CanvasRect{0.7f, 0.55f, 0.25f, 0.4f}; // abajo a la derecha: se mueve con el mouse
    spec.settingsJson = QJsonDocument(QJsonObject::fromVariantMap(ajustes)).toJson(QJsonDocument::Compact).toStdString();
    const QString nombre = qs(spec.name);
    pedirAgregar(std::move(spec));
    return nombre;
}

int SourcesModel::guestSlots() const { return v_.foto().ajustes.invitados; }

QVariantList SourcesModel::otherSources() const {
    QVariantList out;
    const FotoMotor& f = v_.foto();
    for (const auto& [n, type] : f.fuentes) {
        if (rowOf(n) >= 0) continue; // ya esta en esta escena
        QString nombreTipo = qs(type);
        for (const FotoMotor::Tipo& t : f.tipos)
            if (t.id == type) nombreTipo = qs(t.nombre);
        out.push_back(QVariantMap{{"name", qs(n)}, {"typeName", nombreTipo}});
    }
    return out;
}

QString SourcesModel::addExisting(const QString& name) {
    const std::string n = name.toStdString();
    if (rowOf(n) >= 0) return name; // ya esta
    SourceSpec spec;
    spec.id = v_.foto().tipoDe(n);
    spec.name = n;
    if (spec.id.empty()) {
        emit error(tr("No existe la fuente '%1'.").arg(name));
        return {};
    }
    // Sin settings: addSource ve que ya existe una fuente con ese nombre y
    // agrega LA MISMA a esta escena (no abre el dispositivo otra vez).
    spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
    pedirAgregar(std::move(spec));
    return name;
}

QString SourcesModel::addGuest(int slot, bool share) {
    if (slot < 1 || slot > guestSlots()) return {};
    // Mismos nombres que crea el arranque: si la fuente ya existe se agrega
    // la MISMA a esta escena (una fuente puede estar en varias escenas).
    SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
    if (rowOf(spec.name) >= 0) return qs(spec.name); // ya esta en esta escena
    if (!share) spec.rect = CanvasRect{0.5f, 0.5f, 0.5f, 0.5f};
    const QString nombre = qs(spec.name);
    pedirAgregar(std::move(spec));
    return nombre;
}

// ============================================================================
// MixerModel
// ============================================================================

namespace {
/// Los grupos compactados se recuerdan entre arranques: con muchos invitados, cada vez.
std::filesystem::path archivoVistaMezclador(AppRuntime& rt) { return rt.dataRoot() / "Config" / "mezclador-vista.json"; }
} // namespace

MixerModel::MixerModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {
    const nlohmann::json j = config::leerObjeto(archivoVistaMezclador(rt_));
    if (j.contains("compactos") && j["compactos"].is_array())
        for (const auto& g : j["compactos"])
            if (g.is_string()) compactos_.insert(g.get<std::string>());
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
            return grupos_[i].empty() ? 0.0 : mulToDb(rt_.engine().gananciaGrupo(grupos_[i], salida_));
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

QVariantList MixerModel::gruposEn(const std::string& salida) const {
    QVariantList out;
    for (std::size_t i = 0; i < grupos_.size(); ++i) {
        if (grupos_[i].empty() || (i > 0 && grupos_[i - 1] == grupos_[i])) continue;
        QStringList tiras;
        bool mudo = true;
        for (std::size_t k = i; k < grupos_.size() && grupos_[k] == grupos_[i]; ++k) {
            tiras << qs(names_[k]);
            mudo = mudo && rt_.engine().sourceMuted(names_[k], salida);
        }
        const auto nom = nombresGrupo_.find(grupos_[i]);
        out.push_back(QVariantMap{{"grupo", qs(grupos_[i])},
                                  {"nombre", qs(nom != nombresGrupo_.end() ? nom->second : grupos_[i])},
                                  {"tiras", tiras},
                                  {"volumenDb", mulToDb(rt_.engine().gananciaGrupo(grupos_[i], salida))},
                                  {"compacto", compactos_.count(grupos_[i]) > 0},
                                  {"mudo", mudo}});
    }
    return out;
}

bool MixerModel::setGananciaGrupoDb(const QString& grupo, double db) {
    if (grupo.isEmpty()) return false;
    const double d = std::clamp(db, -60.0, 12.0);
    return rt_.setGananciaGrupo(grupo.toStdString(), d <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, d / 20.0)), salida_);
}

void MixerModel::setMutedGrupo(const QString& grupo, bool muted) { setMutedGrupoEn(grupo.toStdString(), muted, salida_); }

void MixerModel::setMutedGrupoEn(const std::string& g, bool muted, const std::string& salida) {
    for (std::size_t i = 0; i < grupos_.size(); ++i)
        if (!g.empty() && grupos_[i] == g) rt_.setMuted(names_[i], muted, salida);
}

nlohmann::json MixerModel::nivelEn(int fila, const std::string& salida) const {
    if (fila < 0 || fila >= static_cast<int>(names_.size())) return {};
    const std::string& n = names_[static_cast<std::size_t>(fila)];
    return {{"pico_db", std::max(-60.0, std::round(static_cast<double>(peaks_[static_cast<std::size_t>(fila)])))},
            {"volumen_db", std::round(mulToDb(rt_.engine().sourceVolume(n, salida)) * 10) / 10},
            {"mudo", rt_.engine().sourceMuted(n, salida)}};
}

void MixerModel::setCompacto(const QString& grupo, bool compacto) {
    const std::string g = grupo.toStdString();
    if (g.empty() || (compactos_.count(g) > 0) == compacto) return;
    if (compacto) compactos_.insert(g);
    else compactos_.erase(g);
    config::guardar(archivoVistaMezclador(rt_), {{"compactos", compactos_}});
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
    if (!names_.empty()) emit dataChanged(index(0), index(rowCount() - 1), {VolumeDbRole, MutedRole, GananciaGrupoDbRole});
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

SourceTypesModel::SourceTypesModel(AppRuntime&, QObject* parent) : QAbstractListModel(parent) {
    for (const auto& t : BroadcastEngine::tiposDeFuente()) {
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
