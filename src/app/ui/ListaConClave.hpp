#pragma once
// Una lista de objetos (QVariantMap) como modelo de Qt que se actualiza EN SU LUGAR.
//
// Los view models entregan muchas listas como QVariantList entera (los tótems, las
// grillas, los tiempos, los recuadros de un gráfico...). Si la vista las usa tal cual
// como `model:`, cada cambio rearma todos los delegados: se pierde el foco, el scroll
// y lo que se estaba arrastrando. Antes cada página lo resolvía con un ListModel y un
// «sincronizar» a mano en JavaScript (seis copias). Esto es la única copia: recibe la
// lista nueva y avisa lo justo por clave (filas que entran, salen, se mueven o cambian),
// como lo haría un modelo hecho a medida.
//
// Dos formas de usarlo:
//  - Desde C++: el view model lo tiene como miembro, lo expone como
//    Q_PROPERTY(QAbstractItemModel* x READ x CONSTANT) y le llama poner(lista) cada vez
//    que su lista cambia.
//  - Desde QML (registrado como «BA.ListaConClave»), para listas que vienen anidadas
//    adentro de otra (las caras de una grilla):
//        ListaConClave { id: caras; clave: "clave"; roles: ["nombre", "fijo"]; lista: fila.personas }
//
// Roles de cada fila: la clave, los de `roles` y `datos` (el objeto entero). Un
// delegado los toma con `required property` o, para cualquier otro campo, `datos.x`.
// Las filas con la misma clave se emparejan en orden; las que no tienen clave usan la
// posición («#3»).

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace ba::app {

class ListaConClave : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString clave READ clave WRITE setClave NOTIFY claveChanged)
    Q_PROPERTY(QStringList roles READ roles WRITE setRoles NOTIFY rolesChanged)
    Q_PROPERTY(QVariantList lista READ lista WRITE poner NOTIFY listaChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit ListaConClave(QObject* parent = nullptr);
    ListaConClave(QString clave, QStringList roles, QObject* parent = nullptr);

    QString clave() const { return clave_; }
    void setClave(const QString& c);
    QStringList roles() const { return roles_; }
    void setRoles(const QStringList& r);
    QVariantList lista() const;

    /// La lista nueva: avisa por clave lo que entró, salió, se movió o cambió.
    void poner(const QVariantList& lista);
    /// El objeto de la fila (vacío si no hay).
    QVariantMap fila(int i) const { return i >= 0 && i < filas_.size() ? filas_[i].datos : QVariantMap{}; }
    /// En qué fila está esa clave (-1 si no está).
    int indiceDe(const QString& clave) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void claveChanged();
    void rolesChanged();
    void listaChanged();
    void countChanged();

private:
    struct Fila {
        QString clave;
        QVariantMap datos;
    };
    QString claveDe(const QVariant& v, int pos) const;
    /// Los roles que cambian entre dos objetos (vacío si son iguales).
    QList<int> rolesQueCambian(const QVariantMap& antes, const QVariantMap& ahora) const;

    static constexpr int kRolDatos = Qt::UserRole + 1;
    static constexpr int kRolClave = Qt::UserRole + 2;
    static constexpr int kRolPrimero = Qt::UserRole + 3;

    QString clave_ = QStringLiteral("id");
    QStringList roles_;
    QVector<Fila> filas_;
};

} // namespace ba::app
