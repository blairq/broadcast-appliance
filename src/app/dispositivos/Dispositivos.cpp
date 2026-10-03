// src/app/dispositivos/Dispositivos.cpp — ver Dispositivos.hpp

#include "dispositivos/Dispositivos.hpp"

#include <QDateTime>
#include <QPointer>

#include <memory>

namespace ba::app::dispositivos {

QVariantMap Parametro::mapa() const {
    QVariantMap m{{"clave", clave}, {"etiqueta", etiqueta.isEmpty() ? clave : etiqueta}, {"tipo", tipo}, {"obligatorio", obligatorio}};
    if (!opciones.isEmpty()) m["opciones"] = opciones;
    if (!ayuda.isEmpty()) m["ayuda"] = ayuda;
    if (defecto.isValid()) m["defecto"] = defecto;
    return m;
}

QVariantMap Accion::mapa() const {
    QVariantList ps;
    for (const auto& p : parametros) ps << p.mapa();
    return {{"id", id}, {"titulo", titulo}, {"descripcion", descripcion}, {"parametros", ps}, {"modifica", modifica}};
}

Dispositivos::Dispositivos(QObject* parent) : QObject(parent) {}

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
    QVariantList out;
    for (const auto& [id, e] : equipos_) {
        if (!tipo.isEmpty() && e.value("tipo").toString() != tipo) continue;
        if (!buscar.isEmpty()) {
            const QString todo = e.value("nombre").toString() + " " + e.value("detalle").toString() + " " + id;
            if (!todo.contains(buscar, Qt::CaseInsensitive)) continue;
        }
        out << conAcciones(e);
    }
    return out;
}

QVariantMap Dispositivos::equipo(const QString& id) const {
    const auto it = equipos_.find(id);
    return it == equipos_.end() ? QVariantMap{} : conAcciones(it->second);
}

void Dispositivos::anotar(const QString& detector, QVariantMap e) {
    const QString id = detector + ":" + e.value("clave").toString();
    e["id"] = id;
    e["detector"] = detector;
    e["visto"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    equipos_[id] = e;
    emit cambio();
}

void Dispositivos::escanear(const QString& detector, Listo listo) {
    std::vector<const Detector*> cuales;
    for (const auto& d : detectores_)
        if (detector.isEmpty() || d.id == detector) cuales.push_back(&d);
    if (cuales.empty()) return listo({{"error", "no hay un detector «" + detector + "»"}});
    auto faltan = std::make_shared<int>(static_cast<int>(cuales.size()));
    auto resumen = std::make_shared<QVariantMap>();
    QPointer<Dispositivos> yo(this);
    for (const Detector* d : cuales) {
        const QString did = d->id;
        estadoDetector_[did] = {{"escaneando", true}};
        emit cambio();
        d->escanear([yo, did, faltan, resumen, listo](QVariantMap r) {
            if (!yo) return;
            QVariantMap est{{"escaneando", false}, {"cuando", QDateTime::currentDateTime().toString("HH:mm:ss")}};
            if (r.contains("error")) {
                est["error"] = r.value("error");
                (*resumen)[did] = QVariantMap{{"error", r.value("error")}};
            } else {
                // Lo de este detector se reemplaza entero (lo que ya no está, se va).
                for (auto it = yo->equipos_.begin(); it != yo->equipos_.end();)
                    it = it->second.value("detector").toString() == did ? yo->equipos_.erase(it) : std::next(it);
                const QVariantList l = r.value("equipos").toList();
                for (const QVariant& e : l) yo->anotar(did, e.toMap());
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
    if (it == equipos_.end()) return listo({{"error", "no hay un equipo «" + id + "» (equipos_escanear primero)"}});
    const QVariantMap e = it->second;
    const auto at = acciones_.find(e.value("tipo").toString());
    if (at != acciones_.end())
        for (const auto& a : at->second) {
            if (a.id != accion) continue;
            if (a.aplica && !a.aplica(e)) return listo({{"error", "«" + accion + "» no aplica a este equipo ahora"}});
            for (const auto& p : a.parametros)
                if (p.obligatorio && args.value(p.clave).toString().isEmpty())
                    return listo({{"error", "falta «" + p.clave + "» (" + p.etiqueta + ")"}});
            return a.correr(e, args, std::move(listo));
        }
    QStringList hay;
    for (const QVariant& a : accionesDe(e)) hay << a.toMap().value("id").toString();
    listo({{"error", "«" + accion + "» no es una acción de este equipo (tiene: " + hay.join(", ") + ")"}});
}

QString Dispositivos::resumenAcciones() const {
    QStringList tipos;
    for (const auto& [tipo, acs] : acciones_) {
        QStringList l;
        for (const auto& a : acs) {
            QStringList ps;
            for (const auto& p : a.parametros) ps << p.clave + (p.obligatorio ? "" : "?");
            l << a.id + (ps.isEmpty() ? QString() : "(" + ps.join(", ") + ")");
        }
        tipos << tipo + ": " + l.join(", ");
    }
    return tipos.join("; ");
}

QString Dispositivos::resumenDetectores() const {
    QStringList l;
    for (const auto& d : detectores_) l << d.id + " (" + d.nombre + ")";
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
