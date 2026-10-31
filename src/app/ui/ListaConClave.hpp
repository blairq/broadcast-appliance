// ============================================================================
// src/app/ui/ListaConClave.hpp — un modelo de lista que se actualiza EN SU LUGAR
//
// Muchas listas se rearman enteras cada vez que algo cambia (el catálogo de lo que
// se puede mostrar, los monitores, los tótems, las grillas de personas, los recuadros
// de un gráfico...), pero la vista no tiene que enterarse de eso: si el modelo se
// resetea (o QML recibe un arreglo nuevo como `model:`), las listas saltan al
// principio, los ComboBox pierden lo elegido, se pierde el foco y lo que se estaba
// arrastrando. `aplicar` compara por `id` y avisa fila por fila: alta, baja, movida o
// cambio (con los roles que cambiaron, si la fila lo sabe decir). Una sola
// implementación para todos:
//   - filas con tipo propio: se hereda de ListaConClave<T> (pantallas/Catalogo,
//     pantallas/Monitores);
//   - filas que ya vienen como QVariantMap (lo que entregan los view models):
//     ui/ListaObjetos, que se usa también desde QML.
//
// T necesita un `QString id` y `operator==`. Las ids repetidas se emparejan en orden.
// ============================================================================
#pragma once

#include <QAbstractListModel>

#include <map>
#include <vector>

namespace ba::app {

template <typename T>
class ListaConClave : public QAbstractListModel {
   public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& padre = {}) const override {
        return padre.isValid() ? 0 : static_cast<int>(filas_.size());
    }

   protected:
    /// Los roles que cambian de `antes` a `ahora` (vacío = todos). Para avisar lo justo.
    virtual QList<int> rolesQueCambian(const T& antes, const T& ahora) const {
        (void)antes;
        (void)ahora;
        return {};
    }

    /// Pasa de `filas_` a `nuevas`. true si cambió algo.
    bool aplicar(std::vector<T> nuevas) {
        bool hubo = false;
        // 1) Bajas: las que ya no están (de atrás para adelante). Con ids repetidas,
        //    salen las que sobran (las últimas).
        std::map<QString, int> quedan, hay;
        for (const T& c : nuevas) ++quedan[c.id];
        for (const T& c : filas_) ++hay[c.id];
        for (int r = static_cast<int>(filas_.size()) - 1; r >= 0; --r) {
            const QString& id = filas_[static_cast<std::size_t>(r)].id;
            const auto q = quedan.find(id);
            if (hay[id] <= (q == quedan.end() ? 0 : q->second)) continue;
            --hay[id];
            beginRemoveRows({}, r, r);
            filas_.erase(filas_.begin() + r);
            endRemoveRows();
            hubo = true;
        }
        // 2) En orden: la de la fila i ya está ahí, o más abajo (se la sube), o
        //    es nueva (se la inserta). Las que estaban, si cambiaron, avisan.
        for (int i = 0; i < static_cast<int>(nuevas.size()); ++i) {
            T& n = nuevas[static_cast<std::size_t>(i)];
            int j = -1;
            for (int k = i; k < static_cast<int>(filas_.size()); ++k)
                if (filas_[static_cast<std::size_t>(k)].id == n.id) {
                    j = k;
                    break;
                }
            if (j < 0) {
                beginInsertRows({}, i, i);
                filas_.insert(filas_.begin() + i, std::move(n));
                endInsertRows();
                hubo = true;
                continue;
            }
            if (j != i) {
                beginMoveRows({}, j, j, {}, i);
                T c = std::move(filas_[static_cast<std::size_t>(j)]);
                filas_.erase(filas_.begin() + j);
                filas_.insert(filas_.begin() + i, std::move(c));
                endMoveRows();
                hubo = true;
            }
            if (!(filas_[static_cast<std::size_t>(i)] == n)) {
                const QList<int> roles = rolesQueCambian(filas_[static_cast<std::size_t>(i)], n);
                filas_[static_cast<std::size_t>(i)] = std::move(n);
                emit dataChanged(index(i), index(i), roles);
                hubo = true;
            }
        }
        return hubo;
    }

    std::vector<T> filas_;
};

} // namespace ba::app
