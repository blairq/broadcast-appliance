#pragma once
// Un hilo del chat de la sala, como modelo: los mensajes de la sala (`hilo` vacío) o el
// privado con alguien (sus dos direcciones: lo que le mandamos y lo que contestó). Filtra
// el chat de la conferencia (Studio.conference.chat) sin copiarlo: cada mensaje nuevo
// entra solo, sin que la vista vuelva a pedir la lista. Cada vista tiene el suyo (dos
// páginas abiertas pueden leer hilos distintos).
//
// En QML (registrado como «BA.ChatHilo»):
//     ChatHilo { id: hilo; fuente: conf.chat; hilo: page.hiloChat }
//     ListView { model: hilo; delegate: … required property string fromName … }

#include <QSortFilterProxyModel>
#include <QString>

namespace ba::app {

class ChatHilo : public QSortFilterProxyModel {
    Q_OBJECT
    /// El chat entero (un modelo con los roles `to` y `fromId`).
    Q_PROPERTY(QObject* fuente READ fuente WRITE setFuente NOTIFY fuenteCambio)
    /// "" = la sala; si no, el uid de con quién es el privado.
    Q_PROPERTY(QString hilo READ hilo WRITE setHilo NOTIFY hiloCambio)

   public:
    explicit ChatHilo(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    QObject* fuente() const { return sourceModel(); }
    void setFuente(QObject* f);
    QString hilo() const { return hilo_; }
    void setHilo(const QString& h);
    /// ¿El mensaje (a quién va, de quién viene) es del hilo `hilo`? (Pública para las pruebas.)
    static bool esDelHilo(const QString& hilo, const QString& para, const QString& de);

   signals:
    void fuenteCambio();
    void hiloCambio();

   protected:
    bool filterAcceptsRow(int fila, const QModelIndex& padre) const override;

   private:
    QString hilo_;
    int rolPara_ = -1, rolDe_ = -1;
};

} // namespace ba::app
