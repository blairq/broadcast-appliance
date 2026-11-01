// src/app/control/MonitorBus.cpp — ver MonitorBus.hpp

#include "control/MonitorBus.hpp"

#include "control/BusEventos.hpp"

#include <map>

namespace ba::app::control {

MonitorBus::MonitorBus(QObject* parent) : QAbstractListModel(parent) {
    // A lo sumo cuatro veces por segundo: los niveles se publican diez veces por segundo
    // por fuente y la lista no necesita más.
    espera_.setSingleShot(true);
    espera_.setInterval(250);
    connect(&espera_, &QTimer::timeout, this, &MonitorBus::actualizar);
}

QVariant MonitorBus::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Fila& f = filas_[static_cast<std::size_t>(index.row())];
    return role == TemaRole ? f.tema : role == ValorRole ? f.valor : QVariant();
}

QHash<int, QByteArray> MonitorBus::roleNames() const { return {{TemaRole, "tema"}, {ValorRole, "valor"}}; }

QObject* MonitorBus::bus() const { return bus_.data(); }

void MonitorBus::setBus(QObject* b) {
    auto* nuevo = qobject_cast<BusEventos*>(b);
    if (nuevo == bus_) return;
    bus_ = nuevo;
    conectar();
    emit configCambio();
}

void MonitorBus::setFiltro(const QString& f) {
    if (f == filtro_) return;
    filtro_ = f;
    pedir();
    emit configCambio();
    if (activo_) actualizar();
}

void MonitorBus::setConMediciones(bool c) {
    if (c == conMediciones_) return;
    conMediciones_ = c;
    pedir();
    emit configCambio();
    if (activo_) actualizar();
}

void MonitorBus::setActivo(bool a) {
    if (a == activo_) return;
    activo_ = a;
    conectar();
    emit configCambio();
}

void MonitorBus::conectar() {
    disconnect(conexion_);
    espera_.stop();
    interes_.setBus(bus_);
    pedir();
    if (!activo_ || !bus_) return;
    conexion_ = connect(bus_, &BusEventos::publicado, this, &MonitorBus::pronto);
    actualizar();
}

void MonitorBus::pedir() {
    if (!activo_ || (!conMediciones_ && filtro_.isEmpty())) return interes_.poner({});
    interes_.poner({filtro_.contains('/') ? filtro_.toStdString() : std::string("#")});
}

void MonitorBus::pronto() {
    if (!espera_.isActive()) espera_.start();
}

void MonitorBus::actualizar() {
    if (!bus_) return;
    // Los temas: cada fila se actualiza en su lugar; las nuevas entran en su orden y las
    // que ya no pasan el filtro se van.
    const QVariantList temas = bus_->temasUi(filtro_);
    std::map<QString, QString> nuevos;
    std::vector<QString> orden;
    orden.reserve(static_cast<std::size_t>(temas.size()));
    for (const QVariant& v : temas) {
        const QVariantMap m = v.toMap();
        const QString t = m.value("tema").toString();
        nuevos[t] = m.value("valor").toString();
        orden.push_back(t);
    }
    for (int i = rowCount() - 1; i >= 0; --i)
        if (!nuevos.count(filas_[static_cast<std::size_t>(i)].tema)) {
            beginRemoveRows({}, i, i);
            filas_.erase(filas_.begin() + i);
            endRemoveRows();
        }
    for (std::size_t i = 0; i < orden.size(); ++i) {
        const QString& t = orden[i];
        const QString& valor = nuevos[t];
        if (i < filas_.size() && filas_[i].tema == t) {
            if (filas_[i].valor != valor) {
                filas_[i].valor = valor;
                emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {ValorRole});
            }
            continue;
        }
        // (Los temas del bus vienen ordenados: lo que no está en su lugar es nuevo.)
        beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
        filas_.insert(filas_.begin() + static_cast<std::ptrdiff_t>(i), Fila{t, valor});
        endInsertRows();
    }
    // Los recientes: solo si llegó algo nuevo (si no, la lista se queda quieta).
    QVariantList r = bus_->recientesUi(filtro_, conMediciones_);
    if (r != recientes_) {
        recientes_ = std::move(r);
        emit recientesCambio();
    }
}

} // namespace ba::app::control
