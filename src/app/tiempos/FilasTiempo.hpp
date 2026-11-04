// ============================================================================
// src/app/tiempos/FilasTiempo.hpp — los nodos (o tramos) de Latencias, una fila por clave
//
// La página de Latencias tenía el mapa entero como UNA propiedad: cada vuelta (5 por
// segundo) la vista rehacía todo (el acomodo, los índices, cada tarjeta) aunque cambiara
// un número; con 200 invitados son ~800 tarjetas. Acá cada nodo es una fila con su clave:
// la vuelta cambia solo las filas cuyo dato cambió (dataChanged de esa fila) y entra o sale
// una fila cuando aparece o se va un nodo. La historia de cada tarjeta va aparte y a menos
// ritmo (ponerHistorias).
//
// Roles: clave (el id), dato (el nodo: {id, ms, acumulado_ms, estado, detalle…}),
// historia ([{t, ms, acumulado}]).
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

#include <utility>
#include <vector>

namespace ba::app::tiempos {

class FilasTiempo : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

   public:
    enum Rol { ClaveRol = Qt::UserRole + 1, DatoRol, HistoriaRol };
    explicit FilasTiempo(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& i, int rol) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Entrada {
        QString clave;
        QVariantMap dato;
        bool cambio = true; ///< false: el dato es el mismo que la vuelta anterior (no se compara)
    };
    /// Las filas de esta vuelta, en cualquier orden: las que no vienen salen, las nuevas
    /// entran al final y de las demás se avisa solo si su dato cambió. Devuelve cuántas
    /// filas cambiaron (para los tests).
    int poner(const std::vector<Entrada>& filas);
    /// La historia de las filas que vengan (las que no vienen quedan como estaban).
    void ponerHistorias(const QHash<QString, QVariantList>& historias);

   signals:
    void countChanged();

   private:
    struct Fila {
        QString clave;
        QVariantMap dato;
        QVariantList historia;
    };
    std::vector<Fila> filas_;
    QHash<QString, int> indice_; ///< clave → fila
    void reindexar();
};

} // namespace ba::app::tiempos
