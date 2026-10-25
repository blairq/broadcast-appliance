// ============================================================================
// src/app/pantallas/ListaConClave.hpp — un modelo de lista que se actualiza EN SU LUGAR
//
// El catálogo y los monitores se rearman enteros cada vez que algo cambia (una
// escena nueva, un monitor que se enchufa), pero la vista no tiene que
// enterarse de eso: si el modelo se resetea, las listas saltan al principio,
// los ComboBox pierden lo elegido y las ventanas de los monitores se cierran y
// se vuelven a abrir. `aplicar` compara por `id` y avisa fila por fila: alta,
// baja, movida o cambio. Una sola implementación para los dos.
//
// T necesita un `QString id` y `operator==`.
// ============================================================================
#pragma once

#include <QAbstractListModel>

#include <set>
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
    /// Pasa de `filas_` a `nuevas`. true si cambió algo.
    bool aplicar(std::vector<T> nuevas) {
        bool hubo = false;
        // 1) Bajas: las que ya no están (de atrás para adelante).
        std::set<QString> quedan;
        for (const T& c : nuevas) quedan.insert(c.id);
        for (int r = static_cast<int>(filas_.size()) - 1; r >= 0; --r) {
            if (quedan.count(filas_[static_cast<std::size_t>(r)].id) != 0) continue;
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
                filas_[static_cast<std::size_t>(i)] = std::move(n);
                emit dataChanged(index(i), index(i));
                hubo = true;
            }
        }
        return hubo;
    }

    std::vector<T> filas_;
};

} // namespace ba::app
