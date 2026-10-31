// ============================================================================
// src/app/arranque/PlanoControl.hpp — el plano de control, fuera del hilo de la interfaz (F3-03)
//
// docs/arquitectura/04-plan-unificado.md, F3-03 (02-C2, M2, O5). Hasta acá el reloj
// del runtime era el QTimer de StudioViewModel: con la interfaz trabada (un QML
// pesado, un diálogo, un disco lento) no había canal de control, ni MCP, ni alarmas,
// ni guardado del aire. «En vuelo sin mandos».
//
// El plano de control tiene SU hilo («control», con su bucle de eventos de Qt) y SUS
// relojes:
//
//   - `control`, en su hilo, cada 100 ms: lo que no depende de la interfaz (el canal
//     de órdenes del launcher, el documento de aire que escribe su dueño).
//   - `motor`, en el hilo que lo crea (el dueño del motor: hoy el de la interfaz),
//     cada 100 ms: lo del motor que todavía es de ese hilo (BroadcastEngine no es
//     seguro entre hilos) y el latido de la interfaz. Es del runtime, no de una vista:
//     StudioViewModel ya no es el reloj de nadie.
//
// Y lo que se le encarga corre ahí: el servidor MCP (McpHttp y el núcleo, que despacha
// cada herramienta según su hilo declarado, ver McpCore::Ejecutores), el motor de
// alarmas (AlertEngine) y lo que se encole con `encolar`. Un objeto se muda con
// `adoptar` y vuelve a su hilo al detener (así lo borra su dueño sin carreras).
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
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {

class PlanoControl : public QObject {
    Q_OBJECT
   public:
    struct Tareas {
        std::function<void()> control; ///< en el hilo de control
        std::function<void()> motor;   ///< en el hilo que crea el plano (el dueño del motor)
    };
    explicit PlanoControl(Tareas t, QObject* parent = nullptr);
    ~PlanoControl() override;

    /// Arranca el hilo y los dos relojes. Idempotente.
    void arrancar(int controlMs = 100, int motorMs = 100);
    /// Para los relojes, devuelve los objetos adoptados al hilo que creó el plano y
    /// termina el hilo (espera a lo que esté corriendo). Idempotente.
    void detener();
    bool andando() const { return hilo_.isRunning(); }

    /// El hilo de control (para mover un QObject con todos sus hijos).
    QThread* hilo() { return &hilo_; }
    /// Lo encolado acá corre en el hilo de control.
    QObject* contexto() const { return contexto_; }
    /// ¿El que llama es el hilo de control? ¿El que creó el plano?
    bool enControl() const { return QThread::currentThread() == &hilo_; }
    bool enDueno() const { return QThread::currentThread() == dueno_; }

    /// Corre `fn` en el hilo de control (en orden). Desde cualquier hilo; no espera.
    void encolar(std::function<void()> fn);
    /// Corre `fn` en el hilo de control dentro de `ms`.
    void despues(int ms, std::function<void()> fn);
    /// Corre `fn` en el hilo que creó el plano (el de la interfaz). No espera.
    void encolarEnDueno(std::function<void()> fn);
    /// Muda `o` (sin padre) al hilo de control, con sus hijos; vuelve en detener().
    /// `alMudarse` corre YA en el hilo de control (arrancar sus temporizadores).
    void adoptar(QObject* o, std::function<void()> alMudarse = {});

    /// {vueltas_control, vueltas_motor, control_ms_max, control_atraso_ms}: para el
    /// diagnóstico y el test (que el control siguió con la interfaz trabada).
    nlohmann::json estado() const;
    std::uint64_t vueltasControl() const { return vueltasControl_.load(); }
    std::uint64_t vueltasMotor() const { return vueltasMotor_.load(); }

   signals:
    /// Alguien pidió cerrar (el launcher, una señal del sistema): lo emite el reloj del
    /// motor, en el hilo que creó el plano. La aplicación sale de su bucle.
    void cierrePedido();

   public:
    /// Lo mira el reloj del motor en cada vuelta para emitir cierrePedido (una vez).
    void setCierre(std::function<bool()> pedido) { cierre_ = std::move(pedido); }

   private:
    void vueltaControl();
    void vueltaMotor();

    Tareas tareas_;
    QThread hilo_;
    QThread* dueno_ = nullptr;
    QObject* contexto_ = nullptr; ///< vive en hilo_
    QTimer* relojControl_ = nullptr; ///< hijo de contexto_ (vive en hilo_)
    QTimer relojMotor_;
    std::vector<QPointer<QObject>> adoptados_;
    std::function<bool()> cierre_;
    bool cierreAvisado_ = false;
    std::atomic<std::uint64_t> vueltasControl_{0}, vueltasMotor_{0};
    std::atomic<std::int64_t> controlMsMax_{0};
    std::atomic<std::int64_t> ultimaControlNs_{0};
    int controlMs_ = 100;
};

} // namespace ba::app
