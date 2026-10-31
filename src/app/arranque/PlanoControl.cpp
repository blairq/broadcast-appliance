// src/app/arranque/PlanoControl.cpp — ver el .hpp

#include "arranque/PlanoControl.hpp"

#include "platform/Hilo.hpp"

#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace ba::app {

namespace {
std::int64_t ahoraNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

PlanoControl::PlanoControl(Tareas t, QObject* parent) : QObject(parent), tareas_(std::move(t)) {
    dueno_ = QThread::currentThread();
    hilo_.setObjectName("control");
    contexto_ = new QObject();
    contexto_->moveToThread(&hilo_);
    connect(&relojMotor_, &QTimer::timeout, this, &PlanoControl::vueltaMotor);
}

PlanoControl::~PlanoControl() {
    detener();
    // El hilo ya terminó: lo que vivía en él se borra desde acá sin carreras.
    delete contexto_;
}

void PlanoControl::arrancar(int controlMs, int motorMs) {
    if (hilo_.isRunning()) return;
    controlMs_ = std::max(10, controlMs);
    hilo_.start();
    // El reloj del control se crea y arranca en SU hilo (un QTimer solo anda en el suyo).
    QMetaObject::invokeMethod(
        contexto_,
        [this]() {
            ba::platform::nombrarHilo("control");
            relojControl_ = new QTimer(contexto_);
            connect(relojControl_, &QTimer::timeout, contexto_, [this]() { vueltaControl(); });
            relojControl_->start(controlMs_);
        },
        Qt::BlockingQueuedConnection);
    relojMotor_.start(std::max(10, motorMs));
}

void PlanoControl::detener() {
    relojMotor_.stop();
    if (!hilo_.isRunning()) return;
    // En el hilo de control: el reloj se va y lo adoptado vuelve al hilo del dueño (un
    // objeto solo se puede «empujar» desde el suyo). Bloquea: el control nunca espera al
    // dueño, así que no hay abrazo mortal.
    QMetaObject::invokeMethod(
        contexto_,
        [this]() {
            delete relojControl_;
            relojControl_ = nullptr;
            for (const QPointer<QObject>& o : adoptados_)
                if (o && o->thread() == &hilo_) o->moveToThread(dueno_);
        },
        Qt::BlockingQueuedConnection);
    adoptados_.clear();
    hilo_.quit();
    hilo_.wait();
}

void PlanoControl::encolar(std::function<void()> fn) {
    QMetaObject::invokeMethod(contexto_, std::move(fn), Qt::QueuedConnection);
}

void PlanoControl::despues(int ms, std::function<void()> fn) {
    // El temporizador se arma en el hilo de control: corre ahí aunque la interfaz no ande.
    encolar([this, ms, fn = std::move(fn)]() mutable { QTimer::singleShot(ms, contexto_, std::move(fn)); });
}

void PlanoControl::encolarEnDueno(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

void PlanoControl::adoptar(QObject* o, std::function<void()> alMudarse) {
    if (o == nullptr) return;
    o->moveToThread(&hilo_);
    adoptados_.emplace_back(o);
    if (alMudarse) encolar(std::move(alMudarse));
}

void PlanoControl::vueltaControl() {
    const std::int64_t desde = ahoraNs();
    if (tareas_.control) tareas_.control();
    const std::int64_t ms = (ahoraNs() - desde) / 1000000;
    std::int64_t max = controlMsMax_.load();
    while (ms > max && !controlMsMax_.compare_exchange_weak(max, ms)) {
    }
    ultimaControlNs_.store(ahoraNs());
    ++vueltasControl_;
}

void PlanoControl::vueltaMotor() {
    ++vueltasMotor_;
    if (tareas_.motor) tareas_.motor();
    if (!cierreAvisado_ && cierre_ && cierre_()) {
        cierreAvisado_ = true;
        emit cierrePedido();
    }
}

nlohmann::json PlanoControl::estado() const {
    const std::int64_t ultima = ultimaControlNs_.load();
    return {{"vueltas_control", vueltasControl_.load()},
            {"vueltas_motor", vueltasMotor_.load()},
            {"control_ms_max", controlMsMax_.load()},
            {"control_atraso_ms", ultima == 0 ? -1 : (ahoraNs() - ultima) / 1000000}};
}

} // namespace ba::app
