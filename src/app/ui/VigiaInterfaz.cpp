// src/app/ui/VigiaInterfaz.cpp — ver VigiaInterfaz.hpp

#include "ui/VigiaInterfaz.hpp"

#include "ajustes/Ajustes.hpp"
#include "diag/Caidas.hpp"
#include "diag/VigiaBucle.hpp"
#include "platform/Paths.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QQuickWindow>
#include <QSet>

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ba::app {

VigiaInterfaz::VigiaInterfaz(QObject* app) : QObject(app) {
    auto& v = diag::VigiaBucle::interfaz();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(50); // el período de VigiaBucle::interfaz()
    connect(&timer_, &QTimer::timeout, this, [&v]() { v.tic(); });
    // Cuántas veces se despierta el hilo (lo que cuesta la interfaz quieta).
    if (QAbstractEventDispatcher* d = QAbstractEventDispatcher::instance(thread()))
        connect(d, &QAbstractEventDispatcher::awake, this, [&v]() { v.despertar(); });
    app->installEventFilter(this);
    QCoreApplication::instance()->installNativeEventFilter(this);
    timer_.start();

    // Las trabas largas: las pilas de todos los hilos mientras dura (diag/VigiaTraba).
    diag::VigiaTraba::Opciones o;
    o.dir = ba::platform::dataRoot() / "Logs" / "dumps";
    o.proceso = "A";
    o.edadMs = [&v]() { return v.edadMs(); };
    o.umbralMs = []() { return ba::ajustes::entero("diag.trabaInterfazMs", 2000); };
    o.cadaS = []() { return ba::ajustes::entero("diag.trabaInterfazCadaS", 60); };
    o.corria = [&v]() {
        v.fijarCulpable();
        return v.enCurso();
    };
    traba_.iniciar(std::move(o), diag::idDelHilo()); // este es el hilo de la interfaz
    // Al cerrar, el bucle deja de dar vueltas a propósito y el cierre ordenado (apagar el motor)
    // tarda lo suyo en el mismo hilo: eso no es una traba. Sin esto cada cierre de más de 2 s
    // dejaba un volcado «traba-interfaz» (PC de pruebas, 2026-11-07: AppRuntime::stop).
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() { traba_.detener(); });
}

VigiaInterfaz::~VigiaInterfaz() {
    traba_.detener();
    if (QCoreApplication::instance() != nullptr) QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool VigiaInterfaz::nativeEventFilter(const QByteArray& tipo, void* mensaje, qintptr* resultado) {
    (void)resultado;
    int detalle = 0;
#if defined(_WIN32)
    if (mensaje != nullptr && tipo == "windows_generic_MSG") detalle = static_cast<int>(static_cast<MSG*>(mensaje)->message);
#else
    (void)tipo;
    (void)mensaje;
#endif
    diag::VigiaBucle::interfaz().anotar(diag::VigiaBucle::kNativo, detalle);
    return false;
}

void VigiaInterfaz::engancharVentana(QObject* ventana) {
    auto* w = qobject_cast<QQuickWindow*>(ventana);
    // Una sola vez por ventana: el registro es de este vigía (en la interfaz), no una
    // propiedad dinámica leída por su nombre (que compila aunque no exista).
    static QSet<const QQuickWindow*> enganchadas;
    if (w == nullptr || enganchadas.contains(w)) return;
    enganchadas.insert(w);
    QObject::connect(w, &QObject::destroyed, [w]() { enganchadas.remove(w); });
    auto& v = diag::VigiaBucle::interfaz();
    // Directas: afterAnimating corre en la interfaz justo antes de pedirle la
    // sincronización al hilo de render (y quedarse esperándolo); las otras dos, en
    // el hilo de render (con la interfaz parada). Con el render loop básico, todo en
    // la interfaz. Solo anotan (un candado y la hora): nada de Qt.
    connect(w, &QQuickWindow::afterAnimating, w, [&v]() { v.anotar(diag::VigiaBucle::kQsgPidio); }, Qt::DirectConnection);
    connect(w, &QQuickWindow::beforeSynchronizing, w, [&v]() { v.anotar(diag::VigiaBucle::kQsgSincronizando); },
            Qt::DirectConnection);
    connect(w, &QQuickWindow::afterSynchronizing, w, [&v]() { v.anotar(diag::VigiaBucle::kQsgSincronizado); },
            Qt::DirectConnection);
}

bool VigiaInterfaz::eventFilter(QObject* destino, QEvent* e) {
    // Solo los del hilo de la interfaz llegan al filtro de la aplicación (Qt 6).
    if (destino == nullptr) return false;
    if (e->type() == QEvent::Show && destino->isWindowType()) engancharVentana(destino);
    // Un QTimer no dice de quién es: se nombra al dueño, si tiene, y cada cuánto corre
    // («QTimer de BitacoraEvento, cada 10000 ms»), que es lo que hay que ir a buscar.
    if (e->type() == QEvent::Timer && destino->metaObject() == &QTimer::staticMetaObject) {
        const auto* t = static_cast<const QTimer*>(destino);
        std::string que = "QTimer";
        if (destino->parent() != nullptr) que += std::string(" de ") + destino->parent()->metaObject()->className();
        que += ", cada " + std::to_string(t->interval()) + " ms";
        diag::VigiaBucle::interfaz().contarTemporizador(que);
        diag::VigiaBucle::interfaz().anotar(que);
        return false;
    }
    diag::VigiaBucle::interfaz().anotar(destino->metaObject()->className(), static_cast<int>(e->type()));
    return false;
}

} // namespace ba::app
