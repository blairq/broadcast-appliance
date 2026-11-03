#include "ui/ListaObjetos.hpp"

#include "diag/Acoples.hpp"

namespace ba::app {

ListaObjetos::ListaObjetos(QObject* parent) : ListaConClave<FilaObjeto>(parent) {}

ListaObjetos::ListaObjetos(QString clave, QStringList roles, QObject* parent)
    : ListaConClave<FilaObjeto>(parent), clave_(std::move(clave)), roles_(std::move(roles)) {
    roles_.removeAll(clave_);
}

std::vector<FilaObjeto> ListaObjetos::filasDe(const QVariantList& l) const {
    std::vector<FilaObjeto> out;
    out.reserve(static_cast<std::size_t>(l.size()));
    for (int i = 0; i < l.size(); ++i) {
        QVariantMap m = l[i].toMap();
        const QString c = m.value(clave_).toString();
        out.push_back({c.isEmpty() ? QStringLiteral("#%1").arg(i) : c, std::move(m)});
    }
    return out;
}

void ListaObjetos::setClave(const QString& c) {
    if (c == clave_) return;
    // Otra clave cambia el nombre de un rol y el emparejamiento: se rearma todo.
    const QVariantList actual = lista();
    beginResetModel();
    clave_ = c;
    roles_.removeAll(clave_);
    filas_ = filasDe(actual);
    endResetModel();
    emit claveChanged();
}

void ListaObjetos::setRoles(const QStringList& r) {
    QStringList sinClave = r;
    sinClave.removeAll(clave_);
    if (sinClave == roles_) return;
    beginResetModel();
    roles_ = sinClave;
    endResetModel();
    emit rolesChanged();
}

QVariantList ListaObjetos::lista() const {
    QVariantList l;
    l.reserve(static_cast<qsizetype>(filas_.size()));
    for (const auto& f : filas_) l.push_back(f.datos);
    return l;
}

QVariantMap ListaObjetos::fila(int i) const {
    BA_ACOPLE("invocable", "ListaObjetos::fila");
    return i >= 0 && i < rowCount() ? filas_[static_cast<std::size_t>(i)].datos : QVariantMap{};
}

const QVariantMap& ListaObjetos::datosDe(int i) const {
    static const QVariantMap vacio;
    return i >= 0 && i < rowCount() ? filas_[static_cast<std::size_t>(i)].datos : vacio;
}

int ListaObjetos::indiceDe(const QString& clave) const {
    for (std::size_t i = 0; i < filas_.size(); ++i)
        if (filas_[i].id == clave) return static_cast<int>(i);
    return -1;
}

QList<int> ListaObjetos::rolesQueCambian(const FilaObjeto& antes, const FilaObjeto& ahora) const {
    QList<int> r{kRolDatos};
    for (int i = 0; i < roles_.size(); ++i)
        if (antes.datos.value(roles_[i]) != ahora.datos.value(roles_[i])) r.push_back(kRolPrimero + i);
    return r;
}

void ListaObjetos::poner(const QVariantList& nueva) {
    const int antes = rowCount();
    if (aplicar(filasDe(nueva))) emit listaChanged();
    if (rowCount() != antes) emit countChanged();
}

bool ListaObjetos::ponerEn(int i, const QVariantMap& fila) {
    if (i < 0 || i >= rowCount()) return false;
    FilaObjeto n{fila.value(clave_).toString(), fila};
    FilaObjeto& f = filas_[static_cast<std::size_t>(i)];
    if (n.id.isEmpty() || f.id != n.id) return false;
    if (f == n) return true;
    const QList<int> roles = rolesQueCambian(f, n);
    f = std::move(n);
    emit dataChanged(index(i), index(i), roles);
    emit listaChanged();
    return true;
}

QVariant ListaObjetos::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const FilaObjeto& f = filas_[static_cast<std::size_t>(index.row())];
    if (role == kRolDatos || role == Qt::DisplayRole) return f.datos;
    if (role == kRolClave) return f.datos.value(clave_, f.id);
    const int i = role - kRolPrimero;
    if (i >= 0 && i < roles_.size()) return f.datos.value(roles_[i]);
    return {};
}

QHash<int, QByteArray> ListaObjetos::roleNames() const {
    QHash<int, QByteArray> r{{kRolDatos, "datos"}, {kRolClave, clave_.toUtf8()}};
    for (int i = 0; i < roles_.size(); ++i) r.insert(kRolPrimero + i, roles_[i].toUtf8());
    return r;
}

} // namespace ba::app
