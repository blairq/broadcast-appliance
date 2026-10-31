// ============================================================================
// src/app/arranque/PlanoControl.hpp — el plano de control, fuera del hilo de la interfaz (F3-03)
//
// docs/arquitectura/04-plan-unificado.md, F3-03 (02-C2, M2, O5); el patrón, en
// docs/arquitectura/05-motor-del-plano.md. Hasta acá el reloj del runtime era el QTimer de
// StudioViewModel: con la interfaz trabada (un QML pesado, un diálogo, un disco lento) no
// había canal de control, ni MCP, ni alarmas, ni guardado del aire. «En vuelo sin mandos».
//
// El plano de control tiene SU hilo («control», con su bucle de eventos de Qt), que es el
// DUEÑO DEL MOTOR (AppRuntime, BroadcastEngine y la configuración), y estos relojes:
//
//   - `control` y `motor`, en su hilo, cada 100 ms: lo que no depende de la interfaz (el
//     canal de órdenes del launcher, el documento de aire que escribe su dueño) y lo del
//     motor (la sección de salidas del documento, la GPU, los recortes, la FOTO que lee la
//     interfaz: AppRuntime::ticMotor).
//   - `interfaz`, en el hilo que lo crea (el de la interfaz), cada 100 ms: el latido de la
//     interfaz (AppRuntime::latirInterfaz; el supervisor sabe así si la interfaz vive) y el
//     cierre pedido. No toca el motor.
//
// Y lo que se le encarga corre ahí: el servidor MCP (McpHttp y el núcleo, que despacha
// cada herramienta según su hilo declarado, ver McpCore::Ejecutores), el motor de alarmas
// (AlertEngine), los modelos del aire que son del dueño (los conmutadores de escenas), lo
// que se encole con `encolar` y LA AGENDA: lo que se encola con `encolarConTurno` corre en
// orden, con el turno del motor tomado (emision/TurnoMotor.hpp) y sin esperar a la interfaz:
// si ella lo tiene (una vista que todavía entra directo al motor), la agenda reintenta
// después en vez de trabar el hilo. Un objeto se muda con `adoptar` y vuelve a su hilo al
// detener (así lo borra su dueño sin carreras).
//
// Sin QML ni interfaz: corre en un test con QCoreApplication (test_plano_control).
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {

class PlanoControl : public QObject {
    Q_OBJECT
   public:
    struct Tareas {
        std::function<void()> control;  ///< en el hilo de control
        std::function<void()> motor;    ///< en el hilo de control: el reloj del motor (el dueño es este hilo)
        std::function<void()> interfaz; ///< en el hilo que crea el plano (la interfaz): su latido
    };
    /// Cómo se toma el turno del motor para la agenda (vacío: sin turno, en el acto).
    struct Turno {
        std::function<bool(int ms)> tomarHasta;
        std::function<void()> soltar;
    };
    explicit PlanoControl(Tareas t, QObject* parent = nullptr);
    ~PlanoControl() override;

    /// Arranca el hilo y los relojes. Idempotente.
    void arrancar(int controlMs = 100, int interfazMs = 100);
    /// Para los relojes, devuelve los objetos adoptados al hilo que creó el plano y
    /// termina el hilo (espera a lo que esté corriendo). Lo que quedó en la agenda no se
    /// hace. Idempotente.
    void detener();
    bool andando() const { return hilo_.isRunning(); }

    /// El turno de la agenda (ver arriba). Antes de arrancar.
    void setTurno(Turno t) { turno_ = std::move(t); }

    /// El hilo de control (para mover un QObject con todos sus hijos).
    QThread* hilo() { return &hilo_; }
    /// Lo encolado acá corre en el hilo de control.
    QObject* contexto() const { return contexto_; }
    /// ¿El que llama es el hilo de control? ¿El que creó el plano (la interfaz)?
    bool enControl() const { return QThread::currentThread() == &hilo_; }
    bool enInterfaz() const { return QThread::currentThread() == interfaz_; }

    /// Corre `fn` en el hilo de control (en orden). Desde cualquier hilo; no espera.
    void encolar(std::function<void()> fn);
    /// LA AGENDA: `fn` en el hilo de control, en orden, con el turno del motor (ver arriba).
    /// Desde cualquier hilo (también desde el control: corre después de lo que está
    /// corriendo); no espera.
    void encolarConTurno(std::function<void()> fn);
    /// Corre `fn` en el hilo de control dentro de `ms`.
    void despues(int ms, std::function<void()> fn);
    /// Corre `fn` en el hilo que creó el plano (el de la interfaz). No espera.
    void encolarEnInterfaz(std::function<void()> fn);
    /// Corre `fn` en el hilo de control y ESPERA a que termine. Solo para el arranque y el
    /// cierre (la mudanza del dueño): el control nunca espera a la interfaz, así que no hay
    /// abrazo mortal; desde el control, en el acto.
    void enControlYEsperar(const std::function<void()>& fn);
    /// Muda `o` (sin padre) al hilo de control, con sus hijos; vuelve en detener().
    /// `alMudarse` corre YA en el hilo de control (arrancar sus temporizadores).
    void adoptar(QObject* o, std::function<void()> alMudarse = {});

    /// {vueltas_control, vueltas_motor, vueltas_interfaz, control_ms_max, control_atraso_ms,
    /// agenda: {pendientes, hechas, reintentos, espera_max_ms}}: para el diagnóstico y el
    /// test (que el control y el motor siguieron con la interfaz trabada).
    nlohmann::json estado() const;
    std::uint64_t vueltasControl() const { return vueltasControl_.load(); }
    std::uint64_t vueltasMotor() const { return vueltasMotor_.load(); }
    std::uint64_t vueltasInterfaz() const { return vueltasInterfaz_.load(); }

   signals:
    /// Alguien pidió cerrar (el launcher, una señal del sistema): lo emite el reloj de la
    /// interfaz, en el hilo que creó el plano. La aplicación sale de su bucle.
    void cierrePedido();

   public:
    /// Lo mira el reloj de la interfaz en cada vuelta para emitir cierrePedido (una vez).
    void setCierre(std::function<bool()> pedido) { cierre_ = std::move(pedido); }

   private:
    void vueltaControl();
    void vueltaInterfaz();
    /// Corre lo que se pueda de la agenda (en el hilo de control).
    void atenderAgenda();

    Tareas tareas_;
    Turno turno_;
    QThread hilo_;
    QThread* interfaz_ = nullptr;
    QObject* contexto_ = nullptr; ///< vive en hilo_
    QTimer* relojControl_ = nullptr; ///< hijo de contexto_ (vive en hilo_)
    QTimer relojInterfaz_;
    std::vector<QPointer<QObject>> adoptados_;
    std::function<bool()> cierre_;
    bool cierreAvisado_ = false;
    // La agenda: solo la toca el hilo de control.
    std::deque<std::function<void()>> agenda_;
    bool agendaReintento_ = false; ///< ya hay un reintento armado
    std::atomic<std::uint64_t> agendaPendientes_{0}, agendaHechas_{0}, agendaReintentos_{0};
    std::atomic<std::int64_t> agendaEsperaMaxMs_{0};
    std::chrono::steady_clock::time_point agendaDesde_{}; ///< desde cuándo espera la primera
    std::atomic<std::uint64_t> vueltasControl_{0}, vueltasMotor_{0}, vueltasInterfaz_{0};
    std::atomic<std::int64_t> controlMsMax_{0};
    std::atomic<std::int64_t> ultimaControlNs_{0};
    int controlMs_ = 100;
};

} // namespace ba::app
