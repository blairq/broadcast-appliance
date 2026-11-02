// ============================================================================
// src/app/ui/JsonListModel.hpp — Una lista genérica para la vista sobre un arreglo JSON
// (la sala, las salidas, las redes, los lugares de invitado). Aparte de ListModels para
// poder probarla sola (tests/test_json_list_model.cpp).
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

namespace ba::app {

/// Lista generica sobre un arreglo JSON de objetos con campo "id". Cada clave
/// de `keys` es un rol de QML con el mismo nombre ("id" es palabra reservada
/// en QML: las filas lo repiten como "uid"). Si los ids no cambiaron se
/// actualiza en el lugar (sin rearmar los delegados: la vista no parpadea).
class JsonListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
   public:
    JsonListModel(QStringList keys, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override { return items_.size(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return items_.size(); }
    /// Una fila por posicion, con sus claves. Vacio si no existe. Es lo que
    /// deja manejar estas listas CON EL TECLADO sin que la vista tenga que
    /// pedir datos por numero de rol.
    Q_INVOKABLE QVariantMap fila(int i) const;
    void setItems(const QJsonArray& items);
    void append(const QJsonObject& item);
    const QJsonArray& items() const { return items_; }

   signals:
    void countChanged();

   private:
    QStringList keys_;
    QJsonArray items_;
};

} // namespace ba::app
