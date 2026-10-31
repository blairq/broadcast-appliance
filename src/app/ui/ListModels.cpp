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

MixerModel::MixerModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : QAbstractListModel(parent), rt_(rt), v_(ventanilla), foto_(ventanilla.fotoCompartida()) {
    const nlohmann::json j = config::leerObjeto(archivoVistaMezclador(rt_));
    if (j.contains("compactos") && j["compactos"].is_array())
        for (const auto& g : j["compactos"])
            if (g.is_string()) compactos_.insert(g.get<std::string>());
    connect(&v_, &VentanillaMotor::cambio, this, &MixerModel::alCambiar);
    reload();
}

int MixerModel::rowCount(const QModelIndex&) const { return static_cast<int>(names_.size()); }

QVariant MixerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::size_t i = static_cast<std::size_t>(index.row());
    const FotoMotor::MezclaAudio& m = foto_->mezcla;
    const auto p = pendientes_.find(names_[i]);
    const Pendiente* pend = p != pendientes_.end() ? &p->second : nullptr;
    switch (role) {
        case NameRole: return qs(names_[i]);
        case PeakDbRole: return peaks_[i];
        case VolumeDbRole:
            return mulToDb(pend != nullptr && pend->volumen ? *pend->volumen : m.volumen(names_[i], salida_));
        case MutedRole: return pend != nullptr && pend->mudo ? *pend->mudo : m.mudo(names_[i], salida_);
        case RetardoMsRole: {
            const FotoMotor::CanalAudio* c = m.canal(names_[i]);
            return c != nullptr ? c->retardoMs : 0;
        }
        case GrupoRole: return qs(grupos_[i]);
        case PrimeroDeGrupoRole: return !grupos_[i].empty() && (i == 0 || grupos_[i - 1] != grupos_[i]);
        case TamGrupoRole: {
            if (grupos_[i].empty()) return 1;
            int n = 0;
            for (const std::string& g : grupos_) n += g == grupos_[i] ? 1 : 0;
            return n;
        }
        case CompactoRole: return !grupos_[i].empty() && compactos_.count(grupos_[i]) > 0;
        case GananciaGrupoDbRole: {
            if (grupos_[i].empty()) return 0.0;
            const auto pg = pendientes_.find("grupo:" + grupos_[i]);
            return mulToDb(pg != pendientes_.end() && pg->second.vca ? *pg->second.vca : m.vca(grupos_[i], salida_));
        }
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
    foto_ = v_.fotoCompartida();
    const FotoMotor::MezclaAudio& m = foto_->mezcla;
    // Los picos de una tira que sigue se quedan (no saltan a -100 al rehacer).
    std::map<std::string, float> antes;
    for (std::size_t i = 0; i < names_.size() && i < peaks_.size(); ++i) antes[names_[i]] = peaks_[i];
    names_ = m.nombres();
    peaks_.assign(names_.size(), -100.0f);
    for (std::size_t i = 0; i < names_.size(); ++i)
        if (const auto it = antes.find(names_[i]); it != antes.end()) peaks_[i] = it->second;
    // Los grupos: tiras seguidas con la misma clave (dos o más); las demás, sueltas.
    grupos_.assign(names_.size(), std::string());
    for (std::size_t i = 0; i < names_.size();) {
        const std::string& g = m.canales[i].grupo;
        std::size_t j = i + 1;
        while (j < names_.size() && m.canales[j].grupo == g) ++j;
        if (j - i >= 2) {
            for (std::size_t k = i; k < j; ++k) grupos_[k] = g;
            // La gente conectada se llama como en Audio y Video: Conferencia o Asamblea.
            const FotoMotor::CanalAudio& c = m.canales[i];
            const char* gente = inputs::grupoDeGente(inputs::tipoLogico(c.tipo, c.ajustes), c.ajustes);
            nombresGrupo_[g] = gente != nullptr ? gente : g;
        }
        i = j;
    }
    endResetModel();
}

void MixerModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Mezcla) == 0) return;
    const std::shared_ptr<const FotoMotor> antes = foto_;
    const FotoMotor::MezclaAudio& n = v_.foto().mezcla;
    // Cambiaron las tiras (una entrada nueva, una que se fue, otro grupo): se rehacen.
    bool rehacer = n.canales.size() != names_.size() || antes->mezcla.canales.size() != names_.size();
    for (std::size_t i = 0; !rehacer && i < names_.size(); ++i)
        rehacer = n.canales[i].nombre != names_[i] || n.canales[i].grupo != antes->mezcla.canales[i].grupo;
    if (rehacer) return reload();
    foto_ = v_.fotoCompartida();
    // Si no, solo las filas que cambiaron (el fader, el mute, el retardo, el VCA del grupo).
    const FotoMotor::MezclaAudio& a = antes->mezcla;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        const bool cambio = a.canales[i].volumen != n.canales[i].volumen || a.canales[i].mudo != n.canales[i].mudo ||
                            a.canales[i].retardoMs != n.canales[i].retardoMs ||
                            (!grupos_[i].empty() && a.vca(grupos_[i], salida_) != n.vca(grupos_[i], salida_));
        if (cambio)
            emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)),
                             {VolumeDbRole, MutedRole, RetardoMsRole, GananciaGrupoDbRole});
    }
}

int MixerModel::filaDe(const std::string& nombre) const {
    const auto it = std::find(names_.begin(), names_.end(), nombre);
    return it == names_.end() ? -1 : static_cast<int>(it - names_.begin());
}

void MixerModel::pedir(const char* que, const std::string& clave, std::function<void(AppRuntime&)> orden,
                       std::function<void()> listo) {
    ++pendientes_[clave].pedidos;
    v_.pedir(
        this, que,
        [&rt = rt_, orden = std::move(orden)]() {
            orden(rt);
            // El VCA no lo avisa el motor: la foto lo vuelve a leer igual.
            rt.fotoSucia(FotoMotor::Mezcla);
        },
        [this, clave, listo = std::move(listo)]() {
            // La respuesta llega después de la foto con el cambio: lo pendiente se suelta.
            soltarPendiente(clave);
            if (listo) listo();
        });
}

void MixerModel::soltarPendiente(const std::string& clave) {
    const auto it = pendientes_.find(clave);
    if (it == pendientes_.end() || --it->second.pedidos > 0) return;
    pendientes_.erase(it);
    if (clave.rfind("grupo:", 0) == 0) {
        const std::string g = clave.substr(6);
        for (std::size_t i = 0; i < grupos_.size(); ++i)
            if (grupos_[i] == g) emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {GananciaGrupoDbRole});
    } else if (const int row = filaDe(clave); row >= 0) {
        emit dataChanged(index(row), index(row), {VolumeDbRole, MutedRole});
    }
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
    const FotoMotor::MezclaAudio& m = foto_->mezcla;
    for (std::size_t i = 0; i < grupos_.size(); ++i) {
        if (grupos_[i].empty() || (i > 0 && grupos_[i - 1] == grupos_[i])) continue;
        QStringList tiras;
        bool mudo = true;
        for (std::size_t k = i; k < grupos_.size() && grupos_[k] == grupos_[i]; ++k) {
            tiras << qs(names_[k]);
            mudo = mudo && m.mudo(names_[k], salida);
        }
        const auto nom = nombresGrupo_.find(grupos_[i]);
        out.push_back(QVariantMap{{"grupo", qs(grupos_[i])},
                                  {"nombre", qs(nom != nombresGrupo_.end() ? nom->second : grupos_[i])},
                                  {"tiras", tiras},
                                  {"volumenDb", mulToDb(m.vca(grupos_[i], salida))},
                                  {"compacto", compactos_.count(grupos_[i]) > 0},
                                  {"mudo", mudo}});
    }
    return out;
}

bool MixerModel::setGananciaGrupoDb(const QString& grupo, double db) {
    if (grupo.isEmpty()) return false;
    const double d = std::clamp(db, -60.0, 12.0);
    const float mul = d <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, d / 20.0));
    const std::string g = grupo.toStdString();
    pendientes_["grupo:" + g].vca = mul;
    for (std::size_t i = 0; i < grupos_.size(); ++i)
        if (grupos_[i] == g) emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {GananciaGrupoDbRole});
    pedir("mezcla_vca", "grupo:" + g, [g, mul, salida = salida_](AppRuntime& rt) { rt.setGananciaGrupo(g, mul, salida); });
    return true;
}

void MixerModel::setMutedGrupo(const QString& grupo, bool muted) { setMutedGrupoEn(grupo.toStdString(), muted, salida_); }

void MixerModel::setMutedGrupoEn(const std::string& g, bool muted, const std::string& salida, std::function<void()> listo) {
    std::vector<std::string> tiras;
    for (std::size_t i = 0; i < grupos_.size(); ++i)
        if (!g.empty() && grupos_[i] == g) tiras.push_back(names_[i]);
    if (tiras.empty()) {
        if (listo) listo();
        return;
    }
    if (salida == salida_)
        for (const std::string& t : tiras) {
            pendientes_[t].mudo = muted;
            if (const int row = filaDe(t); row >= 0) emit dataChanged(index(row), index(row), {MutedRole});
        }
    // Una sola orden para todo el grupo; cada tira queda pendiente hasta que vuelve.
    for (std::size_t k = 1; k < tiras.size(); ++k) ++pendientes_[tiras[k]].pedidos;
    pedir(
        "mezcla_mute_grupo", tiras.front(),
        [tiras, muted, salida](AppRuntime& rt) {
            for (const std::string& t : tiras) rt.setMuted(t, muted, salida);
        },
        [this, tiras, listo = std::move(listo)]() {
            for (std::size_t k = 1; k < tiras.size(); ++k) soltarPendiente(tiras[k]);
            if (listo) listo();
        });
}

nlohmann::json MixerModel::nivelEn(int fila, const std::string& salida) const {
    if (fila < 0 || fila >= static_cast<int>(names_.size())) return {};
    const std::string& n = names_[static_cast<std::size_t>(fila)];
    const FotoMotor::MezclaAudio& m = foto_->mezcla;
    return {{"pico_db", std::max(-60.0, std::round(static_cast<double>(peaks_[static_cast<std::size_t>(fila)])))},
            {"volumen_db", std::round(mulToDb(m.volumen(n, salida)) * 10) / 10},
            {"mudo", m.mudo(n, salida)}};
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
    // Del canal de niveles (los publica el motor cada ~40 ms), sin el turno ni el motor.
    // Solo se avisa lo que cambió (en silencio todo queda en -100: sin avisos, las
    // vistas no reevalúan nada diez veces por segundo).
    const std::shared_ptr<const NivelesMotor::Niveles> nv = rt_.niveles().niveles();
    int desde = -1, hasta = -1;
    for (std::size_t i = 0; i < names_.size(); ++i) {
        float ahora = -100.0f;
        if (nv)
            for (const auto& n : *nv)
                if (!n.bus && n.id == names_[i]) ahora = std::max(n.pico[0], n.pico[1]);
        // Caida suave: 1,5 dB por vuelta del tick (100 ms).
        const float antes = peaks_[i];
        peaks_[i] = std::max(ahora, peaks_[i] - 1.5f);
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

// Se pide al dueño; mientras vuelve, la tira muestra lo pedido. El aviso a las demás
// vistas lo trae la foto: igual que si lo cambia el asistente, el AFV o la otra vista.
bool MixerModel::setVolumeDb(const QString& name, double db) {
    const std::string n = name.toStdString();
    const int row = filaDe(n);
    if (row < 0) return false;
    const float mul = db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
    pendientes_[n].volumen = mul;
    emit dataChanged(index(row), index(row), {VolumeDbRole});
    pedir("mezcla_volumen", n, [n, mul, salida = salida_](AppRuntime& rt) { rt.setVolume(n, mul, salida); });
    return true;
}

bool MixerModel::setMuted(const QString& name, bool muted) {
    const std::string n = name.toStdString();
    const int row = filaDe(n);
    if (row < 0) return false;
    pendientes_[n].mudo = muted;
    emit dataChanged(index(row), index(row), {MutedRole});
    pedir("mezcla_mute", n, [n, muted, salida = salida_](AppRuntime& rt) { rt.setMuted(n, muted, salida); });
    return true;
}

void MixerModel::despues(std::function<void()> listo) {
    // Las órdenes corren en orden en el dueño: una vacía vuelve después de todas.
    v_.pedir(this, "mezcla_despues", []() {}, [listo = std::move(listo)]() {
        if (listo) listo();
    });
}

void MixerModel::setSalida(const std::string& salida) {
    if (salida == salida_) return;
    salida_ = salida;
    if (!names_.empty()) emit dataChanged(index(0), index(rowCount() - 1), {VolumeDbRole, MutedRole, GananciaGrupoDbRole});
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
