#pragma once
// Una fila de un modelo de lista, al día: sus roles como mapa, que avisa cuando esa fila
// cambia (y solo entonces). Para lo que no conviene que sea un rol de cada delegado —cambia
// seguido y despertaría a todos— pero sí se muestra de UNA fila mientras se la mira (las
// cifras de red de un invitado en la ayuda de su «⋯»): la vista crea un FilaViva solo
// mientras mira y se enlaza a `datos`, en vez de pedirle la fila al modelo en un enlace.
//
// En QML (registrado como «BA.FilaViva»):
//     FilaViva { id: f; modelo: grilla.model; fila: lugar.index }
//     … f.datos.red …
//
// `datos` trae cada rol por su nombre (los de roleNames); vacío si la fila no existe. Con
// filas que entran, salen o se mueven se relee (la vista le da el índice de su delegado,
// que se corre solo).

#include <QAbstractItemModel>
#include <QPointer>
#include <QVariantMap>

namespace ba::app {

class FilaViva : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* modelo READ modelo WRITE setModelo NOTIFY entradaCambio)
    Q_PROPERTY(int fila READ fila WRITE setFila NOTIFY entradaCambio)
    Q_PROPERTY(QVariantMap datos READ datos NOTIFY datosCambio)

   public:
    explicit FilaViva(QObject* parent = nullptr) : QObject(parent) {}

    QObject* modelo() const { return modelo_; }
    void setModelo(QObject* m);
    int fila() const { return fila_; }
    void setFila(int f);
    QVariantMap datos() const { return datos_; }

   signals:
    void entradaCambio();
    void datosCambio();

   private:
    /// Relee la fila y avisa si cambió.
    void leer();
    void alCambiarDatos(const QModelIndex& desde, const QModelIndex& hasta);

    QPointer<QAbstractItemModel> modelo_;
    int fila_ = -1;
    QVariantMap datos_;
};

} // namespace ba::app
