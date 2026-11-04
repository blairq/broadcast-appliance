// SPDX-License-Identifier: GPL-2.0-or-later
//
// ObjetoReplica: un objeto del modelo de la app (Studio, Studio.aire, Studio.dashboard…) del
// lado del navegador. Lo arma ClienteReplica con el ESQUEMA que manda la app
// (control/ReplicaPublicador): las mismas propiedades (con aviso, para que los bindings se
// reevalúen), los mismos roles si es un modelo de lista y las mismas funciones. Así la QML
// del dashboard —el mismo archivo que usa la app— no nota la diferencia.
//
// Cómo:
//   - Propiedades: un metaobjeto dinámico (QQmlOpenMetaObject, el de QQmlPropertyMap) sobre
//     un QAbstractListModel, para que el mismo objeto sea modelo Y tenga propiedades (como
//     Studio.dashboard: Repeater { model: Studio.dashboard } y Studio.dashboard.gridCols).
//   - Funciones: cada una es una propiedad que guarda una función de JavaScript que llama
//     replicaLlamar(nombre, argumentos). Las que no devuelven nada se mandan y listo. Las
//     que devuelven algo no pueden esperar a la red dentro de un binding: devuelven lo último
//     que contestó la app para esos argumentos (al principio, undefined), y cuando llega la
//     respuesta cambia `replicaRev`, que la función leyó: el binding se reevalúa solo.
//     Cuando el objeto cambia, lo que devolvieron sus funciones puede estar viejo: se marca
//     sucio y sube `replicaRev`, así los bindings VIVOS se reevalúan y piden de nuevo (cada
//     clave, como mucho una vez por segundo). Lo que ningún binding volvió a leer se olvida.
//   - Perezoso: los valores se piden («abrir») la primera vez que la vista lee una
//     propiedad; hasta entonces valen lo de su tipo (0, "", [], {}, null).
//   - Escribir una propiedad desde la QML (Studio.dashboard.backgroundDim += 0.1) la cambia
//     acá enseguida y se la manda a la app, que contesta con el valor de verdad.
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVector>

class ClienteReplica;
class MetaReplica;

class ObjetoReplica : public QAbstractListModel {
    Q_OBJECT
   public:
    ObjetoReplica(ClienteReplica* cliente, const QString& id, const QJsonObject& esquema, QObject* parent);
    ~ObjetoReplica() override;

    QString id() const { return id_; }
    bool abierto() const { return abierto_; }
    bool esModelo() const { return modelo_; }

    // --- Modelo de lista (si la app dice que lo es) ---
    int rowCount(const QModelIndex& padre = {}) const override;
    QVariant data(const QModelIndex& i, int rol) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Lo que llega de la app ---
    void ponerValores(const QVariantMap& v);
    void ponerFilas(const QVector<QVariantList>& filas);
    void resultado(const QString& clave, const QVariant& v);
    /// Las funciones: necesitan el motor de JavaScript (se arman al tenerlo).
    void armarFunciones();
    /// El valor de una propiedad (para las pruebas y el cliente).
    QVariant valor(const QString& nombre) const;

    /// La llama la función de JavaScript de cada método (ver arriba).
    Q_INVOKABLE QVariant replicaLlamar(const QString& nombre, const QVariantList& args, bool conValor);

   private:
    friend class MetaReplica;
    void leida(int indice);
    void escrita(int indice);
    void pedirOtraVez();
    /// Que los bindings vivos vuelvan a leer (sube replicaRev) dentro de `ms`, una vez.
    void reevaluarEn(qint64 ms);

    ClienteReplica* cliente_;
    QString id_;
    MetaReplica* meta_ = nullptr;
    bool modelo_ = false, abierto_ = false, pedido_ = false;
    QHash<QByteArray, char> tipos_;          ///< propiedad → tipo corto
    QVector<QPair<QString, bool>> metodos_;  ///< nombre, devuelve algo
    QSet<QByteArray> nombresMetodos_;
    QSet<QByteArray> locales_;               ///< propiedades que en el navegador son locales
    QSet<QByteArray> vistaEscrita_;          ///< de la vista, ya escritas acá (la app no las pisa)
    QList<QByteArray> roles_;
    QVector<QVariantList> filas_;
    QHash<QString, QVariant> cache_;          ///< «nombre(args)» → lo último que contestó
    QHash<QString, QPair<QString, QVariantList>> llamadas_; ///< clave → nombre, args (para volver a pedir)
    QHash<QString, qint64> pedidoMs_;
    QHash<QString, qint64> usadoMs_;   ///< la última vez que un binding la leyó
    QSet<QString> enCamino_;            ///< pedidas que todavía no volvieron (no se piden de nuevo)
    QSet<QString> sucias_;              ///< el objeto cambió después de su respuesta: hay que volver a pedirlas
    bool reevaluacionProgramada_ = false;
    int rev_ = 0;
    bool funcionesArmadas_ = false;
};
