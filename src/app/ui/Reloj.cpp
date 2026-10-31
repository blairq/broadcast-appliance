// src/app/ui/Reloj.cpp — ver Reloj.hpp

#include "ui/Reloj.hpp"

#include <QDateTime>
#include <QMetaMethod>
#include <QThread>

#include <algorithm>

namespace ba::app {

Reloj::Reloj(QObject* parent) : QObject(parent) {
    segundo_.setInterval(1000);
    cuarto_.setInterval(250);
    connect(&segundo_, &QTimer::timeout, this, &Reloj::ahoraCambio);
    connect(&cuarto_, &QTimer::timeout, this, &Reloj::ahoraFinoCambio);
}

double Reloj::ahora() const {
    // Redondeado al segundo: con lectores que lo comparan, el valor no cambia entre avisos.
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return static_cast<double>(ms - ms % 1000);
}

double Reloj::ahoraFino() const { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()); }

void Reloj::connectNotify(const QMetaMethod& signal) {
    if (signal == QMetaMethod::fromSignal(&Reloj::ahoraCambio)) ++lectores_;
    else if (signal == QMetaMethod::fromSignal(&Reloj::ahoraFinoCambio)) ++lectoresFinos_;
    revisar();
}

void Reloj::disconnectNotify(const QMetaMethod& signal) {
    // (Con un método inválido se desconectó todo: se recuenta con lo que queda conectado.)
    if (!signal.isValid()) {
        lectores_ = isSignalConnected(QMetaMethod::fromSignal(&Reloj::ahoraCambio)) ? std::max(lectores_, 1) : 0;
        lectoresFinos_ = isSignalConnected(QMetaMethod::fromSignal(&Reloj::ahoraFinoCambio)) ? std::max(lectoresFinos_, 1) : 0;
    } else if (signal == QMetaMethod::fromSignal(&Reloj::ahoraCambio)) {
        lectores_ = std::max(0, lectores_ - 1);
    } else if (signal == QMetaMethod::fromSignal(&Reloj::ahoraFinoCambio)) {
        lectoresFinos_ = std::max(0, lectoresFinos_ - 1);
    }
    revisar();
}

void Reloj::revisar() {
    // Los temporizadores son de este hilo; connectNotify puede llegar de otro (raro): allá se encola.
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, &Reloj::revisar, Qt::QueuedConnection);
        return;
    }
    if (lectores_ > 0 && !segundo_.isActive() && !alineando_) {
        // Alineado al segundo, para que todos los relojes de la pantalla cambien juntos.
        alineando_ = true;
        QTimer::singleShot(1000 - static_cast<int>(QDateTime::currentMSecsSinceEpoch() % 1000) + 2, this, [this]() {
            alineando_ = false;
            if (lectores_ == 0) return;
            emit ahoraCambio();
            segundo_.start();
        });
    } else if (lectores_ == 0 && segundo_.isActive()) {
        segundo_.stop();
    }
    if (lectoresFinos_ > 0 && !cuarto_.isActive()) cuarto_.start();
    else if (lectoresFinos_ == 0 && cuarto_.isActive()) cuarto_.stop();
}

} // namespace ba::app
