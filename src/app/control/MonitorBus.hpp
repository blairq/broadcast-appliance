// ============================================================================
// src/app/control/MonitorBus.hpp — lo que publica el bus, para mirarlo (Botonera → Eventos)
//
// Un modelo por vista (tipo QML `MonitorBus`, con su filtro): una fila por tema con su
// último valor, y los recientes. Mientras está `activo` escucha el bus y se pone al día a
// lo sumo cuatro veces por segundo, en su lugar (sin rearmar la lista: no salta ni pierde
// el desplazamiento). Apagado no hace nada.
//
//   MonitorBus { bus: Studio.bus; filtro: campo.text; activo: pestana.visible }
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include <vector>

namespace ba::app::control {

class BusEventos;

class MonitorBus : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QObject* bus READ bus WRITE setBus NOTIFY configCambio)
    /// Un tema, un comodín (aire/#, audio/nivel/+) o un pedazo del nombre ("" = todos).
    Q_PROPERTY(QString filtro READ filtro WRITE setFiltro NOTIFY configCambio)
    /// Los recientes incluyen las mediciones (niveles, loudness, tiempos…).
    Q_PROPERTY(bool conMediciones READ conMediciones WRITE setConMediciones NOTIFY configCambio)
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY configCambio)
    /// Lo último que pasó: [{hora, tema, valor}], el más nuevo primero.
    Q_PROPERTY(QVariantList recientes READ recientes NOTIFY recientesCambio)

   public:
    enum Roles { TemaRole = Qt::UserRole + 1, ValorRole };
    explicit MonitorBus(QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(filas_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* bus() const;
    void setBus(QObject* b);
    QString filtro() const { return filtro_; }
    void setFiltro(const QString& f);
    bool conMediciones() const { return conMediciones_; }
    void setConMediciones(bool c);
    bool activo() const { return activo_; }
    void setActivo(bool a);
    QVariantList recientes() const { return recientes_; }

    /// Se pone al día ya (lo usa el temporizador; también las pruebas).
    void actualizar();

   signals:
    void configCambio();
    void recientesCambio();

   private:
    void conectar();
    void pronto();

    struct Fila {
        QString tema, valor;
    };
    QPointer<BusEventos> bus_;
    QMetaObject::Connection conexion_;
    QString filtro_;
    bool conMediciones_ = false;
    bool activo_ = false;
    std::vector<Fila> filas_;
    QVariantList recientes_;
    QTimer espera_;
};

} // namespace ba::app::control
