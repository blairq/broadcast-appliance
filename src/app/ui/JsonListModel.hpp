// ============================================================================
// src/app/ui/JsonListModel.hpp — Una lista genérica para la vista sobre un arreglo JSON
// (la sala, las salidas, las redes, los lugares de invitado). Aparte de ListModels para
// poder probarla sola (tests/test_json_list_model.cpp).
//
// Los cambios se pueden calcular EN OTRO HILO (planear: pura, sin el modelo) y aplicarse
// después en el de la interfaz (aplicar): así la interfaz hace trabajo proporcional a lo
// que cambió, no a la lista entera (la foto de la sala: ResumenSala, en el hilo del lector).
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QVariantMap>

namespace ba::app {

/// Lo que hay que hacerle a una lista para que quede como `final`: los pasos, en orden, con
/// los roles que avisa cada fila. Lo arma JsonListModel::planear (en cualquier hilo).
struct PlanFilas {
    enum class Tipo { Quitar, Insertar, Mover, Cambiar };
    struct Paso {
        Tipo tipo = Tipo::Cambiar;
        int i = 0;        ///< la fila (Quitar, Insertar, Cambiar) o adónde va (Mover)
        int desde = 0;    ///< de dónde sale (Mover; siempre > i)
        QString id;       ///< el id que tiene que estar ahí (se verifica antes de aplicar)
        QJsonObject valor; ///< Insertar, Cambiar
        QList<int> roles;  ///< Cambiar: los que avisa (vacío = cambió algo que no es rol: se guarda callado)
    };
    bool rehacer = false; ///< sin ids únicos y otra cantidad: se rehace entero (reset)
    int antes = 0;        ///< cuántas filas tenía la lista sobre la que se planeó
    QJsonArray final;     ///< cómo queda (el respaldo si la lista no era la planeada)
    QList<Paso> pasos;
    bool vacio() const { return !rehacer && pasos.isEmpty(); }
};

/// Lista generica sobre un arreglo JSON de objetos con campo "id". Cada clave
/// de `keys` es un rol de QML con el mismo nombre ("id" es palabra reservada
/// en QML: las filas lo repiten como "uid"). Si los ids no cambiaron se
/// actualiza en el lugar (sin rearmar los delegados: la vista no parpadea);
/// si cambiaron y son únicos, se quitan, insertan y mueven solo esas filas.
/// Sin ids (o repetidos) y con otra cantidad, se rehace entero.
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
    /// La lista entera: compara con la de ahora (O(filas)) y aplica lo que cambió.
    void setItems(const QJsonArray& items);
    void append(const QJsonObject& item);
    /// Una fila (en el lugar, mismo id): avisa solo los roles que le cambiaron.
    void ponerFila(int i, const QJsonObject& item);
    const QJsonArray& items() const { return items_; }

    /// Los pasos para pasar de `antes` a `ahora` con estos roles (`claves`, en el orden del
    /// modelo). Pura: corre en cualquier hilo (el lector de la sala).
    static PlanFilas planear(const QJsonArray& antes, const QJsonArray& ahora, const QStringList& claves);
    /// Aplica un plan armado en otro hilo: trabajo proporcional a sus pasos. Si la lista no
    /// es la que se planeó (otra cantidad, otro id donde va un paso), queda como `final` por
    /// el camino de siempre (setItems).
    void aplicar(const PlanFilas& plan);

   signals:
    void countChanged();

   private:
    static QString idDe(const QJsonValue& fila);
    static bool idsUnicos(const QJsonArray& filas);
    /// Los pasos, verificando cada uno; false si la lista no era la planeada.
    bool aplicarPasos(const PlanFilas& plan);
    void reiniciar(const QJsonArray& items);

    QStringList keys_;
    QJsonArray items_;
};

} // namespace ba::app
