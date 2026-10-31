// src/app/ui/VigiaInterfaz.cpp — ver VigiaInterfaz.hpp

#include "ui/VigiaInterfaz.hpp"

#include "diag/VigiaBucle.hpp"

#include <QEvent>

#include <string>

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
    if (destino == nullptr) return false;
    // Un QTimer no dice de quién es: se nombra al dueño, si tiene, y cada cuánto corre
    // («QTimer de BitacoraEvento, cada 10000 ms»), que es lo que hay que ir a buscar.
    if (e->type() == QEvent::Timer && destino->metaObject() == &QTimer::staticMetaObject) {
        const auto* t = static_cast<const QTimer*>(destino);
        std::string que = "QTimer";
        if (destino->parent() != nullptr) que += std::string(" de ") + destino->parent()->metaObject()->className();
        diag::VigiaBucle::interfaz().anotar(que + ", cada " + std::to_string(t->interval()) + " ms");
        return false;
    }
    diag::VigiaBucle::interfaz().anotar(destino->metaObject()->className(), static_cast<int>(e->type()));
    return false;
}

} // namespace ba::app
