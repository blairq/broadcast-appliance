#include "ui/ListaConClave.hpp"

#include <QSet>

namespace ba::app {

ListaConClave::ListaConClave(QObject* parent) : QAbstractListModel(parent) {}

ListaConClave::ListaConClave(QString clave, QStringList roles, QObject* parent)
    : QAbstractListModel(parent), clave_(std::move(clave)), roles_(std::move(roles)) {
    roles_.removeAll(clave_);
}

void ListaConClave::setClave(const QString& c) {
    if (c == clave_) return;
    // Otra clave cambia los nombres de los roles y el emparejamiento: se rearma todo.
    const QVariantList actual = lista();
    beginResetModel();
    clave_ = c;
    roles_.removeAll(clave_);
    filas_.clear();
    for (int i = 0; i < actual.size(); ++i) filas_.push_back({claveDe(actual[i], i), actual[i].toMap()});
    endResetModel();
    emit claveChanged();
}

void ListaConClave::setRoles(const QStringList& r) {
    QStringList sinClave = r;
    sinClave.removeAll(clave_);
    if (sinClave == roles_) return;
    beginResetModel();
    roles_ = sinClave;
    endResetModel();
    emit rolesChanged();
}

QVariantList ListaConClave::lista() const {
    QVariantList l;
    l.reserve(filas_.size());
    for (const auto& f : filas_) l.push_back(f.datos);
    return l;
}

QString ListaConClave::claveDe(const QVariant& v, int pos) const {
    const QVariant c = v.toMap().value(clave_);
    return c.isValid() && !c.toString().isEmpty() ? c.toString() : QStringLiteral("#%1").arg(pos);
}

int ListaConClave::indiceDe(const QString& clave) const {
    for (int i = 0; i < filas_.size(); ++i)
        if (filas_[i].clave == clave) return i;
    return -1;
}

QList<int> ListaConClave::rolesQueCambian(const QVariantMap& antes, const QVariantMap& ahora) const {
    if (antes == ahora) return {};
    QList<int> r{kRolDatos};
    for (int i = 0; i < roles_.size(); ++i)
        if (antes.value(roles_[i]) != ahora.value(roles_[i])) r.push_back(kRolPrimero + i);
    return r;
}

void ListaConClave::poner(const QVariantList& nueva) {
    const int antes = filas_.size();
    QVector<Fila> llegan;
    llegan.reserve(nueva.size());
    for (int i = 0; i < nueva.size(); ++i) llegan.push_back({claveDe(nueva[i], i), nueva[i].toMap()});

    // 1) Salen las filas cuya clave ya no está (contando repetidas: si antes había dos «x»
    //    y ahora una, sale la última).
    QHash<QString, int> quedan;
    for (const auto& f : llegan) ++quedan[f.clave];
    QHash<QString, int> vistas;
    for (int i = 0; i < filas_.size(); ++i) ++vistas[filas_[i].clave];
    for (int i = filas_.size() - 1; i >= 0; --i) {
        const QString& c = filas_[i].clave;
        if (vistas[c] > quedan.value(c)) {
            --vistas[c];
            beginRemoveRows({}, i, i);
            filas_.remove(i);
            endRemoveRows();
        }
    }
    // 2) En orden: cada fila nueva se busca desde su lugar en adelante; si está más abajo
    //    se sube, si no está entra, y si cambió se avisa solo lo que cambió.
    for (int j = 0; j < llegan.size(); ++j) {
        int k = -1;
        for (int i = j; i < filas_.size(); ++i)
            if (filas_[i].clave == llegan[j].clave) { k = i; break; }
        if (k < 0) {
            beginInsertRows({}, j, j);
            filas_.insert(j, llegan[j]);
            endInsertRows();
            continue;
        }
        if (k != j) {
            beginMoveRows({}, k, k, {}, j);
            filas_.move(k, j);
            endMoveRows();
        }
        const QList<int> cambian = rolesQueCambian(filas_[j].datos, llegan[j].datos);
        if (!cambian.isEmpty()) {
            filas_[j].datos = llegan[j].datos;
            const QModelIndex ix = index(j);
            emit dataChanged(ix, ix, cambian);
        }
    }
    if (filas_.size() != antes) emit countChanged();
    emit listaChanged();
}

int ListaConClave::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(filas_.size());
}

QVariant ListaConClave::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= filas_.size()) return {};
    const Fila& f = filas_[index.row()];
    if (role == kRolDatos || role == Qt::DisplayRole) return f.datos;
    if (role == kRolClave) return f.datos.value(clave_, f.clave);
    const int i = role - kRolPrimero;
    if (i >= 0 && i < roles_.size()) return f.datos.value(roles_[i]);
    return {};
}

QHash<int, QByteArray> ListaConClave::roleNames() const {
    QHash<int, QByteArray> r{{kRolDatos, "datos"}, {kRolClave, clave_.toUtf8()}};
    for (int i = 0; i < roles_.size(); ++i) r.insert(kRolPrimero + i, roles_[i].toUtf8());
    return r;
}

} // namespace ba::app
