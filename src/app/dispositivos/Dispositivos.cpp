// src/app/dispositivos/Dispositivos.cpp — ver Dispositivos.hpp

#include "dispositivos/Dispositivos.hpp"

#include "audio/UnificarEntradas.hpp"

#include <set>

#include "dispositivos/Conocidos.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>

#include <algorithm>
#include <memory>

namespace ba::app::dispositivos {

QVariantMap Parametro::mapa() const {
    QVariantMap m{{"clave", clave},
                  {"etiqueta", etiqueta.isEmpty() ? clave : etiqueta},
                  {"tipo", tipo},
                  {"obligatorio", obligatorio}};
    if (!opciones.isEmpty()) m["opciones"] = opciones;
    if (!ayuda.isEmpty()) m["ayuda"] = ayuda;
    if (defecto.isValid()) m["defecto"] = defecto;
    return m;
}

QVariantMap Accion::mapa() const {
    QVariantList ps;
    for (const auto& p : parametros)
        ps << p.mapa();
    return {{"id", id},
            {"titulo", titulo},
            {"descripcion", descripcion},
            {"parametros", ps},
            {"modifica", modifica}};
}

namespace {
nlohmann::json aJ(const QVariant& v) {
    return nlohmann::json::parse(
        QJsonDocument::fromVariant(QVariantMap{{"v", v}}).toJson(QJsonDocument::Compact).toStdString(), nullptr,
        false)["v"];
}
QVariant aV(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(
                                       nlohmann::json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .toVariant()
        .toMap()
        .value("v");
}
QString qs(const std::string& s) { return QString::fromStdString(s); }

/// El orden de las secciones de la lista: primero lo de video, después lo de
/// audio y al final lo que solo se maneja (red, USB, HID, serie).
int ordenDeTipo(const QString& t) {
    static const QStringList orden{"camara_local", "camara_red", "ndi",  "microfono", "salida_audio",
                                   "equipo_red",   "usb",        "hid",  "serie"};
    const int i = static_cast<int>(orden.indexOf(t));
    return i < 0 ? static_cast<int>(orden.size()) : i;
}
} // namespace

Dispositivos::Dispositivos(std::filesystem::path nombres, QObject* parent)
    : QObject(parent), conocidos_(std::make_unique<Conocidos>(std::move(nombres))) {
    Conocidos::setDeLaMaquina(conocidos_.get());
}

Dispositivos::~Dispositivos() { Conocidos::setDeLaMaquina(nullptr); }

bool Dispositivos::buscado(const QString& detector) const {
    const auto it = estadoDetector_.find(detector);
    return it != estadoDetector_.end() && it->second.contains("cuando");
}

QVariantMap Dispositivos::presentar(QVariantMap e) const {
    const QString tecnico = e.value("nombre").toString();
    e["tecnico"] = tecnico;
    e["conectado"] = true;
    e["estado"] = "conectado";
    if (!e.contains("valor")) e["valor"] = e.value("clave");
    const Conocido c = conocidos_->uno(e.value("id").toString().toStdString());
    e["nombre_propio"] = !c.nombre.empty();
    if (!c.nombre.empty()) e["nombre"] = qs(c.nombre);
    e["config"] = aV(c.config);
    return e;
}

QVariantMap Dispositivos::ausente(const QString& id) const {
    const Conocido c = conocidos_->uno(id.toStdString());
    const QString detector = id.section(':', 0, 0);
    // Antes de la primera búsqueda de su detector no se sabe si está: no se lo
    // marca ausente (la red, p. ej., se busca a pedido y tarda).
    const bool sabido = buscado(detector);
    return {{"id", id},
            {"tipo", qs(c.tipo)},
            {"detector", detector},
            {"nombre", qs(c.mostrar())},
            {"tecnico", qs(c.tecnico)},
            {"nombre_propio", !c.nombre.empty()},
            {"valor", qs(c.valor)},
            {"config", aV(c.config)},
            {"conectado", false},
            {"estado", sabido ? "ausente" : "sin_buscar"},
            {"detalle", sabido ? tr("No está conectado: lo que lo usa lo espera y vuelve solo cuando aparece")
                               : tr("Todavía no se buscó en esta sesión")},
            {"datos", QVariantMap{}},
            {"acciones", QVariantList{}}};
}

void Dispositivos::registrar(Detector d) {
    estadoDetector_[d.id] = {{"escaneando", false}};
    detectores_.push_back(std::move(d));
    emit cambio();
}

void Dispositivos::registrar(const QString& tipo, Accion a) { acciones_[tipo].push_back(std::move(a)); }

QVariantList Dispositivos::accionesDe(const QVariantMap& e) const {
    QVariantList out;
    const auto it = acciones_.find(e.value("tipo").toString());
    if (it == acciones_.end()) return out;
    for (const auto& a : it->second)
        if (!a.aplica || a.aplica(e)) out << a.mapa();
    return out;
}

QVariantMap Dispositivos::conAcciones(QVariantMap e) const {
    e["acciones"] = accionesDe(e);
    return e;
}

QVariantList Dispositivos::equipos(const QString& tipo, const QString& buscar) const {
    std::vector<QVariantMap> todos;
    for (const auto& [id, e] : equipos_)
        todos.push_back(presentar(e));
    // Los que la máquina ya vio y hoy no están: con su nombre, como ausentes.
    // Menos los que son OTRO NOMBRE de uno que está: en Windows el mismo micrófono
    // se vio con dos ids (el del endpoint y el GUID solo, de otro proveedor), y
    // las predeterminadas que inventa el proveedor («Default Audio Capture
    // Device»). Se miran por dispositivo físico (audio/UnificarEntradas.hpp).
    std::set<std::string> fisicos;
    for (const auto& [id, e] : equipos_)
        fisicos.insert(e.value("tipo").toString().toStdString() + "|" +
                       audio::claveFisica(e.value("valor").toString().toStdString()));
    for (const Conocido& c : conocidos_->todos()) {
        if (equipos_.count(qs(c.id)) != 0) continue;
        if (c.tipo == "microfono" || c.tipo == "salida_audio") {
            if (audio::esPredeterminadaDelProveedor(c.valor)) continue;
            if (fisicos.count(c.tipo + "|" + audio::claveFisica(c.valor)) != 0) continue;
        }
        todos.push_back(ausente(qs(c.id)));
    }
    std::stable_sort(todos.begin(), todos.end(), [](const QVariantMap& a, const QVariantMap& b) {
        const int ta = ordenDeTipo(a.value("tipo").toString()), tb = ordenDeTipo(b.value("tipo").toString());
        if (ta != tb) return ta < tb;
        return a.value("nombre").toString().compare(b.value("nombre").toString(), Qt::CaseInsensitive) < 0;
    });
    QVariantList out;
    for (const QVariantMap& e : todos) {
        if (!tipo.isEmpty() && e.value("tipo").toString() != tipo) continue;
        if (!buscar.isEmpty()) {
            const QString todo = e.value("nombre").toString() + " " + e.value("tecnico").toString() + " " +
                                 e.value("detalle").toString() + " " + e.value("id").toString();
            if (!todo.contains(buscar, Qt::CaseInsensitive)) continue;
        }
        out << (e.value("conectado").toBool() ? conAcciones(e) : e);
    }
    return out;
}

QVariantMap Dispositivos::equipo(const QString& id) const {
    const auto it = equipos_.find(id);
    if (it != equipos_.end()) return conAcciones(presentar(it->second));
    return conocidos_->hay(id.toStdString()) ? ausente(id) : QVariantMap{};
}

QVariantMap Dispositivos::porValor(const QString& tipo, const QString& valor) const {
    if (valor.isEmpty()) return {};
    for (const auto& [id, e] : equipos_)
        if (e.value("tipo").toString() == tipo && e.value("valor", e.value("clave")).toString() == valor)
            return presentar(e);
    const Conocido c = conocidos_->porValor(tipo.toStdString(), valor.toStdString());
    return c.id.empty() ? QVariantMap{} : ausente(qs(c.id));
}

QString Dispositivos::renombrar(const QString& id, const QString& nombre) {
    const auto it = equipos_.find(id);
    if (it != equipos_.end() && !seRecuerda(it->second.value("tipo").toString().toStdString()))
        return tr("solo se les pone nombre a los dispositivos de audio y video (cámaras, micrófonos, placas, NDI, "
                  "cámaras de red)");
    const std::string err = conocidos_->renombrar(id.toStdString(), nombre.toStdString());
    if (!err.empty()) return qs(err);
    emit cambio();
    return {};
}

QString Dispositivos::configurar(const QString& id, const QVariantMap& config) {
    const std::string err = conocidos_->configurar(id.toStdString(), aJ(config));
    if (!err.empty()) return qs(err);
    emit cambio();
    return {};
}

QString Dispositivos::olvidar(const QString& id) {
    // Uno conectado se volvería a anotar en la próxima búsqueda: olvidarlo
    // solo borraría su nombre sin querer.
    if (equipos_.count(id) > 0) return tr("está conectado: solo se olvida uno que ya no está");
    const std::string err = conocidos_->olvidar(id.toStdString());
    if (!err.empty()) return qs(err);
    emit cambio();
    return {};
}

void Dispositivos::anotar(const QString& detector, QVariantMap e) {
    guardar(detector, std::move(e));
    emit cambio();
}

void Dispositivos::guardar(const QString& detector, QVariantMap e) {
    const QString id = detector + ":" + e.value("clave").toString();
    e["id"] = id;
    e["detector"] = detector;
    e["visto"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!e.contains("valor")) e["valor"] = e.value("clave");
    // Los de audio y video quedan en la máquina: la próxima vez que no estén se
    // los muestra ausentes, con el nombre que les puso el operador.
    conocidos_->visto(id.toStdString(), e.value("tipo").toString().toStdString(),
                      e.value("nombre").toString().toStdString(), e.value("valor").toString().toStdString(),
                      e.value("visto").toString().toStdString());
    equipos_[id] = e;
}

void Dispositivos::escanear(const QString& detector, Listo listo) {
    std::vector<const Detector*> cuales;
    for (const auto& d : detectores_)
        if (detector.isEmpty() || d.id == detector) cuales.push_back(&d);
    if (cuales.empty()) return listo({{"error", tr("no hay un detector «%1»").arg(detector)}});
    auto faltan = std::make_shared<int>(static_cast<int>(cuales.size()));
    auto resumen = std::make_shared<QVariantMap>();
    QPointer<Dispositivos> yo(this);
    for (const Detector* d : cuales) {
        const QString did = d->id;
        estadoDetector_[did] = {{"escaneando", true}};
        emit cambio();
        d->escanear([yo, did, faltan, resumen, listo](QVariantMap r) {
            if (!yo) return;
            QVariantMap est{{"escaneando", false},
                            {"cuando", QDateTime::currentDateTime().toString("HH:mm:ss")}};
            if (r.contains("error")) {
                est["error"] = r.value("error");
                (*resumen)[did] = QVariantMap{{"error", r.value("error")}};
            } else {
                // Lo de este detector se reemplaza entero (lo que ya no está, se va).
                for (auto it = yo->equipos_.begin(); it != yo->equipos_.end();)
                    it = it->second.value("detector").toString() == did ? yo->equipos_.erase(it)
                                                                        : std::next(it);
                const QVariantList l = r.value("equipos").toList();
                for (const QVariant& e : l)
                    yo->guardar(did, e.toMap()); // un solo aviso al final, no uno por equipo
                est["cuantos"] = l.size();
                (*resumen)[did] = QVariantMap{{"encontrados", l.size()}};
            }
            yo->estadoDetector_[did] = est;
            emit yo->cambio();
            if (--*faltan == 0) listo({{"detectores", *resumen}, {"equipos", yo->equipos()}});
        });
    }
}

void Dispositivos::correr(const QString& id, const QString& accion, const QVariantMap& args, Listo listo) {
    const auto it = equipos_.find(id);
    if (it == equipos_.end())
        return listo({{"error", conocidos_->hay(id.toStdString())
                                    ? tr("«%1» no está conectado ahora").arg(id)
                                    : tr("no hay un equipo «%1» (equipos_escanear primero)").arg(id)}});
    // Con el nombre del operador: «Agregar como entrada» la llama así.
    const QVariantMap e = presentar(it->second);
    const auto at = acciones_.find(e.value("tipo").toString());
    if (at != acciones_.end())
        for (const auto& a : at->second) {
            if (a.id != accion) continue;
            if (a.aplica && !a.aplica(e))
                return listo({{"error", tr("«%1» no aplica a este equipo ahora").arg(accion)}});
            for (const auto& p : a.parametros)
                if (p.obligatorio && args.value(p.clave).toString().isEmpty())
                    return listo({{"error", tr("falta «%1» (%2)").arg(p.clave, p.etiqueta)}});
            return a.correr(e, args, std::move(listo));
        }
    QStringList hay;
    for (const QVariant& a : accionesDe(e))
        hay << a.toMap().value("id").toString();
    listo({{"error", tr("«%1» no es una acción de este equipo (tiene: %2)").arg(accion, hay.join(", "))}});
}

QString Dispositivos::resumenAcciones() const {
    QStringList tipos;
    for (const auto& [tipo, acs] : acciones_) {
        QStringList l;
        for (const auto& a : acs) {
            QStringList ps;
            for (const auto& p : a.parametros)
                ps << p.clave + (p.obligatorio ? "" : "?");
            l << a.id + (ps.isEmpty() ? QString() : "(" + ps.join(", ") + ")");
        }
        tipos << tipo + ": " + l.join(", ");
    }
    return tipos.join("; ");
}

QString Dispositivos::resumenDetectores() const {
    QStringList l;
    for (const auto& d : detectores_)
        l << d.id + " (" + d.nombre + ")";
    return l.join(", ");
}

QVariantList Dispositivos::detectoresQml() const {
    QVariantList out;
    for (const auto& d : detectores_) {
        QVariantMap m{{"id", d.id}, {"nombre", d.nombre}, {"descripcion", d.descripcion}};
        const auto it = estadoDetector_.find(d.id);
        if (it != estadoDetector_.end()) m.insert(it->second);
        out << m;
    }
    return out;
}

void Dispositivos::escanearUi(const QString& detector) {
    escanear(detector, [](QVariantMap) {});
}

void Dispositivos::accionUi(const QString& id, const QString& accion, const QVariantMap& args) {
    QPointer<Dispositivos> yo(this);
    correr(id, accion, args, [yo, id, accion](QVariantMap r) {
        if (yo) emit yo->resultado(id, accion, r);
    });
}

} // namespace ba::app::dispositivos
