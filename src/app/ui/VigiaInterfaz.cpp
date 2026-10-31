// src/app/ui/VigiaInterfaz.cpp — ver VigiaInterfaz.hpp

#include "ui/VigiaInterfaz.hpp"

#include "diag/VigiaBucle.hpp"

#include <QEvent>

namespace ba::app {

VigiaInterfaz::VigiaInterfaz(QObject* app) : QObject(app) {
    auto& v = diag::VigiaBucle::interfaz();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(50); // el período de VigiaBucle::interfaz()
    connect(&timer_, &QTimer::timeout, this, [&v]() { v.tic(); });
    app->installEventFilter(this);
    timer_.start();
}

bool VigiaInterfaz::eventFilter(QObject* destino, QEvent* e) {
    // Solo los del hilo de la interfaz llegan al filtro de la aplicación (Qt 6).
    if (destino != nullptr) diag::VigiaBucle::interfaz().anotar(destino->metaObject()->className(), static_cast<int>(e->type()));
    return false;
}

} // namespace ba::app
