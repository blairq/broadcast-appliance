#pragma once
// Una lista de objetos (QVariantMap) como modelo de Qt que se actualiza EN SU LUGAR por
// clave (ui/ListaConClave con filas de QVariantMap).
//
// Los view models entregan muchas listas como QVariantList entera (los tótems, las
// grillas, los tiempos, los recuadros de un gráfico...). Usadas tal cual como `model:`,
// cada cambio rearma todos los delegados: se pierde el foco, el scroll y lo que se
// estaba arrastrando. Antes cada página lo resolvía con un ListModel y un «sincronizar»
// a mano en JavaScript (seis copias). Esto es la única: recibe la lista nueva y avisa lo
// justo (filas que entran, salen, se mueven, y los roles que cambiaron).
//
// Dos formas de usarla:
//  - Desde C++: el view model la tiene como miembro, la expone como
//    Q_PROPERTY(QAbstractItemModel* x READ x CONSTANT) y le llama poner(lista) cada vez
//    que su lista cambia.
//  - Desde QML (registrada como «BA.ListaConClave»), para listas que vienen anidadas
//    adentro de otra (las caras de cada grilla):
//        ListaConClave { id: caras; clave: "clave"; roles: ["nombre", "fijo"]; lista: fila.personas }
//
// Roles de cada fila: la clave, los de `roles` y `datos` (el objeto entero). Un
// delegado los toma con `required property` o, para cualquier otro campo, `datos.x`.
// Las filas sin clave se emparejan por su posición («#3»).

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "ui/ListaConClave.hpp"

namespace ba::app {

struct FilaObjeto {
    QString id; ///< el valor de la clave (o «#posición»)
    QVariantMap datos;
    bool operator==(const FilaObjeto& o) const { return id == o.id && datos == o.datos; }
};

class ListaObjetos : public ListaConClave<FilaObjeto> {
    Q_OBJECT
    Q_PROPERTY(QString clave READ clave WRITE setClave NOTIFY claveChanged)
    Q_PROPERTY(QStringList roles READ roles WRITE setRoles NOTIFY rolesChanged)
    Q_PROPERTY(QVariantList lista READ lista WRITE poner NOTIFY listaChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

   public:
    explicit ListaObjetos(QObject* parent = nullptr);
    ListaObjetos(QString clave, QStringList roles, QObject* parent = nullptr);

    QString clave() const { return clave_; }
    void setClave(const QString& c);
    QStringList roles() const { return roles_; }
    void setRoles(const QStringList& r);
    QVariantList lista() const;
    int count() const { return rowCount(); }

    /// La lista nueva: avisa por clave lo que entró, salió, se movió o cambió.
    void poner(const QVariantList& lista);
    /// El objeto de la fila (vacío si no hay).
    QVariantMap fila(int i) const;
    /// En qué fila está esa clave (-1 si no está).
    int indiceDe(const QString& clave) const;

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

   signals:
    void claveChanged();
    void rolesChanged();
    void listaChanged();
    void countChanged();

   protected:
    QList<int> rolesQueCambian(const FilaObjeto& antes, const FilaObjeto& ahora) const override;

   private:
    std::vector<FilaObjeto> filasDe(const QVariantList& l) const;

    static constexpr int kRolDatos = Qt::UserRole + 1;
    static constexpr int kRolClave = Qt::UserRole + 2;
    static constexpr int kRolPrimero = Qt::UserRole + 3;

    QString clave_ = QStringLiteral("id");
    QStringList roles_;
};

} // namespace ba::app
