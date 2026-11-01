// src/app/arranque/PlanoControl.cpp — ver el .hpp

#include "arranque/PlanoControl.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
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
    // Lo que quedó sin correr (nadie llamó a atenderPendientes después del último pedido):
    // ya no hay a quién dárselo; que quede anotado.
    std::size_t quedan = 0;
    {
        const std::lock_guard<std::mutex> l(paradoMu_);
        quedan = pendientes_.size();
        pendientes_.clear();
    }
    if (quedan > 0)
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "plano", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("plano", "al cerrar quedaron %1 órdenes sin correr"),
                                                    {std::to_string(quedan)}));
    // El hilo ya terminó: lo que vivía en él se borra desde acá sin carreras.
    delete contexto_;
}

void PlanoControl::arrancar(int controlMs, int interfazMs) {
    if (hilo_.isRunning()) return;
    {
        const std::lock_guard<std::mutex> l(paradoMu_);
        parado_ = false;
    }
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

void PlanoControl::detener(const std::function<void()>& alRecibir) {
    relojInterfaz_.stop();
    if (!hilo_.isRunning()) {
        // Ya parado (o nunca arrancó): el motor se recibe una sola vez; lo pendiente, ya.
        bool recibir = false;
        {
            const std::lock_guard<std::mutex> l(paradoMu_);
            parado_ = true;
            recibir = !recibido_;
            recibido_ = true;
        }
        if (recibir && alRecibir) alRecibir();
        atenderPendientes();
        return;
    }
    // Desde acá lo que se encola va al hilo que recibe el motor; lo que ya estaba encolado
    // en el control está antes que lo de abajo en su cola, así que llega a la agenda.
    {
        const std::lock_guard<std::mutex> l(paradoMu_);
        parado_ = true;
    }
    // En el hilo de control: el reloj se va, la agenda pendiente se lleva y lo adoptado vuelve
    // al hilo de la interfaz (un objeto solo se puede «empujar» desde el suyo). Bloquea: el
    // control nunca espera a la interfaz, así que no hay abrazo mortal.
    std::deque<std::function<void()>> quedo;
    QMetaObject::invokeMethod(
        contexto_,
        [this, &quedo]() {
            delete relojControl_;
            relojControl_ = nullptr;
            quedo = std::move(agenda_);
            agenda_.clear();
            for (const QPointer<QObject>& o : adoptados_)
                if (o && o->thread() == &hilo_) o->moveToThread(interfaz_);
        },
        Qt::BlockingQueuedConnection);
    adoptados_.clear();
    hilo_.quit();
    hilo_.wait();
    // Este hilo es el dueño ahora: lo pendiente corre acá, en orden (el turno lo toma cada
    // método del motor, como el dueño).
    {
        const std::lock_guard<std::mutex> l(paradoMu_);
        recibido_ = true;
    }
    if (alRecibir) alRecibir();
    for (std::function<void()>& fn : quedo) {
        if (fn) fn();
        --agendaPendientes_;
        ++agendaHechas_;
    }
    // Y lo que se encoló mientras se paraba (ya para el dueño nuevo), detrás.
    atenderPendientes();
}

void PlanoControl::alDuenoNuevo(std::function<void()> fn) {
    // Pendiente (no al bucle de eventos: al cerrar ya no corre, y se perdía) y, por si el
    // bucle anda, un aviso para atenderlo ahí en orden.
    pendientes_.push_back(std::move(fn));
    QMetaObject::invokeMethod(this, [this]() { atenderPendientes(); }, Qt::QueuedConnection);
}

int PlanoControl::atenderPendientes() {
    int hechas = 0;
    for (;;) {
        std::function<void()> fn;
        {
            const std::lock_guard<std::mutex> l(paradoMu_);
            if (pendientes_.empty()) return hechas;
            fn = std::move(pendientes_.front());
            pendientes_.pop_front();
        }
        if (fn) fn();
        ++hechas;
    }
}

void PlanoControl::encolar(std::function<void()> fn) {
    const std::lock_guard<std::mutex> l(paradoMu_);
    if (parado_) return alDuenoNuevo(std::move(fn));
    QMetaObject::invokeMethod(contexto_, std::move(fn), Qt::QueuedConnection);
}

void PlanoControl::encolarConTurno(std::function<void()> fn) {
    const std::lock_guard<std::mutex> l(paradoMu_);
    if (parado_) return alDuenoNuevo(std::move(fn));
    ++agendaPendientes_;
    // A la agenda en su hilo (la deque es solo del control) y se atiende después de lo que
    // esté corriendo: nunca adentro de quien la encoló.
    QMetaObject::invokeMethod(
        contexto_,
        [this, fn = std::move(fn)]() mutable {
            if (agenda_.empty()) agendaDesde_ = std::chrono::steady_clock::now();
            agenda_.push_back(std::move(fn));
            if (!agendaReintento_) atenderAgenda();
        },
        Qt::QueuedConnection);
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
        {
            // El turno se suelta pase lo que pase en la orden.
            struct Soltar {
                const Turno* t;
                ~Soltar() {
                    if (t != nullptr) t->soltar();
                }
            } soltar{conTurno ? &turno_ : nullptr};
            if (fn) fn();
        }
        --agendaPendientes_;
        ++agendaHechas_;
    }
}

void PlanoControl::despues(int ms, std::function<void()> fn) {
    // El temporizador se arma en el hilo de control: corre ahí aunque la interfaz no ande.
    // Con el plano parado (el cierre) no hay a quién esperar: va con lo pendiente, sin
    // temporizador (sin bucle no vencería nunca).
    {
        const std::lock_guard<std::mutex> l(paradoMu_);
        if (parado_) return alDuenoNuevo(std::move(fn));
    }
    encolar([this, ms, fn = std::move(fn)]() mutable {
        if (enControl()) return QTimer::singleShot(ms, contexto_, std::move(fn));
        // Se paró mientras tanto: con lo pendiente del dueño nuevo.
        const std::lock_guard<std::mutex> l(paradoMu_);
        alDuenoNuevo(std::move(fn));
    });
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
