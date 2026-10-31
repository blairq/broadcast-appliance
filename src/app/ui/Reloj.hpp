// ============================================================================
// src/app/ui/Reloj.hpp — la hora para las vistas (singleton QML `Reloj`)
//
// Una sola hora para toda la interfaz, en vez de un `Timer` con `Date.now()` en cada
// archivo: `Reloj.ahora` (ms, cambia una vez por segundo) para relojes y cuentas
// regresivas, y `Reloj.ahoraFino` (cuatro veces por segundo) para lo que necesita más
// (la cuenta de un orador en los últimos segundos). Cada uno corre SOLO mientras algún
// enlace de QML lo lee: Qt avisa cuando un enlace se conecta o se desconecta de su
// señal (connectNotify). Una vista que no se ve y no lo necesita lo lee condicionado:
//   readonly property real ahora: visible ? Reloj.ahora : 0
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

namespace ba::app {

class Reloj : public QObject {
    Q_OBJECT
    Q_PROPERTY(double ahora READ ahora NOTIFY ahoraCambio)
    Q_PROPERTY(double ahoraFino READ ahoraFino NOTIFY ahoraFinoCambio)

   public:
    explicit Reloj(QObject* parent = nullptr);

    double ahora() const;
    double ahoraFino() const;
    /// Cuántos enlaces leen cada uno (las pruebas).
    int lectores() const { return lectores_; }
    int lectoresFinos() const { return lectoresFinos_; }

   signals:
    void ahoraCambio();
    void ahoraFinoCambio();

   protected:
    void connectNotify(const QMetaMethod& signal) override;
    void disconnectNotify(const QMetaMethod& signal) override;

   private:
    void revisar();

    QTimer segundo_, cuarto_;
    int lectores_ = 0, lectoresFinos_ = 0;
    bool alineando_ = false;
};

} // namespace ba::app
