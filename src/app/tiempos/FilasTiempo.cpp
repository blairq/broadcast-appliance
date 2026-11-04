// ============================================================================
// src/app/tiempos/FilasTiempo.cpp — ver FilasTiempo.hpp
// ============================================================================
#include "tiempos/FilasTiempo.hpp"

#include <QSet>

namespace ba::app::tiempos {

int FilasTiempo::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(filas_.size());
}

QVariant FilasTiempo::data(const QModelIndex& i, int rol) const {
    if (!i.isValid() || i.row() < 0 || i.row() >= rowCount()) return {};
    const Fila& f = filas_[static_cast<std::size_t>(i.row())];
    switch (rol) {
        case ClaveRol: return f.clave;
        case DatoRol: return f.dato;
        case HistoriaRol: return f.historia;
        default: return {};
    }
}

QHash<int, QByteArray> FilasTiempo::roleNames() const {
    return {{ClaveRol, "clave"}, {DatoRol, "dato"}, {HistoriaRol, "historia"}};
}

void FilasTiempo::reindexar() {
    indice_.clear();
    for (int i = 0; i < static_cast<int>(filas_.size()); ++i) indice_.insert(filas_[static_cast<std::size_t>(i)].clave, i);
}

int FilasTiempo::poner(const std::vector<Entrada>& filas) {
    int cambios = 0;
    const int antes = rowCount();
    QSet<QString> vienen;
    for (const Entrada& e : filas) vienen.insert(e.clave);
    // Las que se fueron, de atrás para adelante (los índices de adelante no se mueven).
    for (int i = static_cast<int>(filas_.size()) - 1; i >= 0; --i) {
        if (vienen.contains(filas_[static_cast<std::size_t>(i)].clave)) continue;
        beginRemoveRows({}, i, i);
        filas_.erase(filas_.begin() + i);
        endRemoveRows();
        ++cambios;
    }
    reindexar();
    for (const Entrada& e : filas) {
        const auto it = indice_.constFind(e.clave);
        if (it == indice_.constEnd()) {
            const int n = rowCount();
            beginInsertRows({}, n, n);
            filas_.push_back({e.clave, e.dato, {}});
            indice_.insert(e.clave, n);
            endInsertRows();
            ++cambios;
            continue;
        }
        Fila& f = filas_[static_cast<std::size_t>(*it)];
        if (!e.cambio || f.dato == e.dato) continue;
        f.dato = e.dato;
        const QModelIndex i = index(*it);
        emit dataChanged(i, i, {DatoRol});
        ++cambios;
    }
    if (rowCount() != antes) emit countChanged();
    return cambios;
}

void FilasTiempo::ponerHistorias(const QHash<QString, QVariantList>& historias) {
    for (auto h = historias.constBegin(); h != historias.constEnd(); ++h) {
        const auto it = indice_.constFind(h.key());
        if (it == indice_.constEnd()) continue;
        filas_[static_cast<std::size_t>(*it)].historia = h.value();
        const QModelIndex i = index(*it);
        emit dataChanged(i, i, {HistoriaRol});
    }
}

} // namespace ba::app::tiempos

#include "moc_FilasTiempo.cpp"
