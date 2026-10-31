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
/// Lo que la agenda espera el turno cada vez antes de soltar el hilo y reintentar: corto, así
/// el control sigue atendiendo lo suyo (el MCP, el launcher) mientras la interfaz lo tiene.
constexpr int kAgendaEsperaMs = 20;
constexpr int kAgendaReintentoMs = 10;
} // namespace

PlanoControl::PlanoControl(Tareas t, QObject* parent) : QObject(parent), tareas_(std::move(t)) {
    interfaz_ = QThread::currentThread();
    hilo_.setObjectName("control");
    contexto_ = new QObject();
    contexto_->moveToThread(&hilo_);
    connect(&relojInterfaz_, &QTimer::timeout, this, &PlanoControl::vueltaInterfaz);
}

PlanoControl::~PlanoControl() {
    detener();
    // El hilo ya terminó: lo que vivía en él se borra desde acá sin carreras.
    delete contexto_;
}

void PlanoControl::arrancar(int controlMs, int interfazMs) {
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
    relojInterfaz_.start(std::max(10, interfazMs));
}

void PlanoControl::detener() {
    relojInterfaz_.stop();
    if (!hilo_.isRunning()) return;
    // En el hilo de control: el reloj se va y lo adoptado vuelve al hilo de la interfaz (un
    // objeto solo se puede «empujar» desde el suyo). Bloquea: el control nunca espera a la
    // interfaz, así que no hay abrazo mortal. Lo que quedó en la agenda no se hace (el
    // cierre ordenado lo hace el runtime después, en este hilo).
    QMetaObject::invokeMethod(
        contexto_,
        [this]() {
            delete relojControl_;
            relojControl_ = nullptr;
            agenda_.clear();
            agendaPendientes_ = 0;
            for (const QPointer<QObject>& o : adoptados_)
                if (o && o->thread() == &hilo_) o->moveToThread(interfaz_);
        },
        Qt::BlockingQueuedConnection);
    adoptados_.clear();
    hilo_.quit();
    hilo_.wait();
}

void PlanoControl::encolar(std::function<void()> fn) {
    QMetaObject::invokeMethod(contexto_, std::move(fn), Qt::QueuedConnection);
}

void PlanoControl::encolarConTurno(std::function<void()> fn) {
    ++agendaPendientes_;
    // A la agenda en su hilo (la deque es solo del control) y se atiende después de lo que
    // esté corriendo: nunca adentro de quien la encoló.
    encolar([this, fn = std::move(fn)]() mutable {
        if (agenda_.empty()) agendaDesde_ = std::chrono::steady_clock::now();
        agenda_.push_back(std::move(fn));
        if (!agendaReintento_) atenderAgenda();
    });
}

void PlanoControl::atenderAgenda() {
    agendaReintento_ = false;
    while (!agenda_.empty()) {
        const bool conTurno = static_cast<bool>(turno_.tomarHasta);
        if (conTurno && !turno_.tomarHasta(kAgendaEsperaMs)) {
            // La interfaz lo tiene (una vista que entra directo al motor, trabada adentro):
            // el hilo sigue con lo suyo y se vuelve a probar enseguida, en orden.
            ++agendaReintentos_;
            agendaReintento_ = true;
            QTimer::singleShot(kAgendaReintentoMs, contexto_, [this]() { atenderAgenda(); });
            return;
        }
        const auto espera = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - agendaDesde_).count();
        std::int64_t max = agendaEsperaMaxMs_.load();
        while (espera > max && !agendaEsperaMaxMs_.compare_exchange_weak(max, espera)) {
        }
        std::function<void()> fn = std::move(agenda_.front());
        agenda_.pop_front();
        agendaDesde_ = std::chrono::steady_clock::now();
        if (fn) fn();
        if (conTurno) turno_.soltar();
        --agendaPendientes_;
        ++agendaHechas_;
    }
}

void PlanoControl::despues(int ms, std::function<void()> fn) {
    // El temporizador se arma en el hilo de control: corre ahí aunque la interfaz no ande.
    encolar([this, ms, fn = std::move(fn)]() mutable { QTimer::singleShot(ms, contexto_, std::move(fn)); });
}

void PlanoControl::encolarEnInterfaz(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

void PlanoControl::enControlYEsperar(const std::function<void()>& fn) {
    if (!hilo_.isRunning() || enControl()) return fn();
    QMetaObject::invokeMethod(contexto_, [&fn]() { fn(); }, Qt::BlockingQueuedConnection);
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
    // El reloj del motor: su dueño es este hilo (lo que toca del motor lo hace con el turno,
    // y si la interfaz lo tiene esta vuelta se saltea: AppRuntime::ticMotor).
    ++vueltasMotor_;
    if (tareas_.motor) tareas_.motor();
    const std::int64_t ms = (ahoraNs() - desde) / 1000000;
    std::int64_t max = controlMsMax_.load();
    while (ms > max && !controlMsMax_.compare_exchange_weak(max, ms)) {
    }
    ultimaControlNs_.store(ahoraNs());
    ++vueltasControl_;
}

void PlanoControl::vueltaInterfaz() {
    ++vueltasInterfaz_;
    if (tareas_.interfaz) tareas_.interfaz();
    if (!cierreAvisado_ && cierre_ && cierre_()) {
        cierreAvisado_ = true;
        emit cierrePedido();
    }
}

nlohmann::json PlanoControl::estado() const {
    const std::int64_t ultima = ultimaControlNs_.load();
    return {{"vueltas_control", vueltasControl_.load()},
            {"vueltas_motor", vueltasMotor_.load()},
            {"vueltas_interfaz", vueltasInterfaz_.load()},
            {"control_ms_max", controlMsMax_.load()},
            {"control_atraso_ms", ultima == 0 ? -1 : (ahoraNs() - ultima) / 1000000},
            {"agenda",
             {{"pendientes", agendaPendientes_.load()},
              {"hechas", agendaHechas_.load()},
              {"reintentos", agendaReintentos_.load()},
              {"espera_max_ms", agendaEsperaMaxMs_.load()}}}};
}

} // namespace ba::app
