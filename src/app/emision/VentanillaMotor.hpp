// ============================================================================
// src/app/emision/VentanillaMotor.hpp — la interfaz le pide al motor y lee su foto (F3-03)
//
// docs/arquitectura/05-motor-del-plano.md (el patrón, para migrar las áreas que faltan).
// El motor (AppRuntime, BroadcastEngine, la configuración) es del plano de control: su
// hilo es el DUEÑO. La interfaz no lo toca: va a la ventanilla.
//
//   (a) PEDIR: `pedir(quien, que, orden, listo)`. La orden (una función que usa el
//       runtime) va a la agenda del dueño (PlanoControl::encolarConTurno: en orden, con el
//       turno del motor, sin esperar a la interfaz) y corre allá. Lo que devuelve vuelve
//       ENCOLADO al hilo de `quien` (un QObject de la interfaz: si se fue, la respuesta no
//       llega) y ahí corre `listo`. La interfaz nunca espera: ni BlockingQueuedConnection
//       ni esperas con tope. Después de cada orden el dueño publica la foto: cuando llega
//       la respuesta, lo que la orden cambió ya está en la foto.
//   (b) LEER: `foto()` es la última foto que el dueño publicó (emision/FotoMotor.hpp) y que
//       la interfaz ya recibió: cambia SOLO en su hilo, cuando llega `cambio(partes)` (MVC:
//       el modelo avisa; los view models escuchan las partes que muestran y emiten sus
//       NOTIFY). Leerla no toma candados ni el turno.
//
// El dueño publica con `publicar` (desde su hilo, cualquier cantidad de veces): las que
// llegan mientras la interfaz está trabada se juntan en una sola (la última, con las
// partes de todas), y la interfaz la recibe cuando vuelve.
//
// Hilos: vive en el de la interfaz. `pedir` desde la interfaz; `publicar` y las órdenes en
// el dueño. Sin QML: anda en un test con QCoreApplication (test_plano_control).
// ============================================================================
#pragma once

#include "emision/FotoMotor.hpp"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace ba::app {

class VentanillaMotor : public QObject {
    Q_OBJECT
   public:
    /// Lo que pone el dueño (GuiMode con el plano; el test con uno sintético).
    struct Dueno {
        /// Corre la orden en el hilo dueño, en orden, con el turno (la agenda del plano).
        std::function<void(std::function<void()>)> encolar;
        /// En el dueño, después de cada orden: publicar la foto (AppRuntime::publicarFoto).
        std::function<void()> alTerminarOrden;
        /// Muda un modelo del aire al hilo del dueño (PlanoControl::adoptar).
        std::function<void(QObject*)> adoptar;
    };
    explicit VentanillaMotor(Dueno d, QObject* parent = nullptr);
    ~VentanillaMotor() override;

    /// Pide `orden` al dueño y, con lo que devuelva, corre `listo` en el hilo de `quien`.
    /// `que`: un nombre corto para las métricas (`ba_ventanilla_orden_ms{orden}`).
    template <class Orden, class Listo>
    void pedir(QObject* quien, const char* que, Orden orden, Listo listo) {
        using R = std::invoke_result_t<Orden&>;
        const auto desde = std::chrono::steady_clock::now();
        ++pedidas_;
        // La respuesta va a la ventanilla (vive lo que la app) y, en el hilo de la interfaz,
        // a `quien` si sigue estando (un QPointer se mira en el hilo de su objeto).
        d_.encolar([this, q = QPointer<QObject>(quien), que, desde, orden = std::move(orden),
                    listo = std::move(listo)]() mutable {
            if constexpr (std::is_void_v<R>) {
                orden();
                terminar(que, desde);
                QMetaObject::invokeMethod(
                    this, [q, listo = std::move(listo)]() mutable {
                        if (q) listo();
                    },
                    Qt::QueuedConnection);
            } else {
                R r = orden();
                terminar(que, desde);
                QMetaObject::invokeMethod(
                    this, [q, listo = std::move(listo), r = std::move(r)]() mutable {
                        if (q) listo(std::move(r));
                    },
                    Qt::QueuedConnection);
            }
        });
    }
    /// Lo mismo, sin respuesta.
    template <class Orden>
    void pedir(const char* que, Orden orden) {
        const auto desde = std::chrono::steady_clock::now();
        ++pedidas_;
        d_.encolar([this, que, desde, orden = std::move(orden)]() mutable {
            orden();
            terminar(que, desde);
        });
    }

    /// La foto que tiene la interfaz (ver arriba). Solo en el hilo de la interfaz.
    const FotoMotor& foto() const { return *vista_; }
    /// Una copia compartida de la misma (para guardarla en un modelo de lista).
    std::shared_ptr<const FotoMotor> fotoCompartida() const { return vista_; }
    /// Cuánto hace que el dueño publicó la última (ms; -1 si nunca).
    std::int64_t edadMs() const;

    /// DESDE EL DUEÑO: la foto nueva y qué partes cambiaron respecto de la anterior.
    void publicar(std::shared_ptr<const FotoMotor> foto, unsigned partes);
    /// La publicada, ya (al arrancar, antes de armar las vistas: la primera foto).
    void recibirAhora();
    /// Muda un modelo del aire al hilo del dueño (los conmutadores de escenas).
    void adoptar(QObject* o) {
        if (d_.adoptar) d_.adoptar(o);
    }

    /// {pedidas, hechas, publicadas, recibidas, edad_ms}: para el diagnóstico y el test.
    nlohmann::json estado() const;

   signals:
    /// En el hilo de la interfaz: llegó una foto y cambiaron estas partes (FotoMotor::Parte).
    void cambio(unsigned partes);

   private:
    /// En el dueño, después de cada orden (la foto, las métricas).
    void terminar(const char* que, std::chrono::steady_clock::time_point desde);
    void alLlegar();

    Dueno d_;
    std::shared_ptr<const FotoMotor> vista_; ///< la de la interfaz (solo su hilo)
    mutable std::mutex mu_;
    std::shared_ptr<const FotoMotor> pendiente_; ///< la última publicada (con mu_)
    unsigned partesPendientes_ = 0;              ///< (con mu_)
    std::chrono::steady_clock::time_point publicadaEn_{}; ///< (con mu_)
    std::atomic<bool> avisoEnCamino_{false};
    std::atomic<std::uint64_t> pedidas_{0}, hechas_{0}, publicadas_{0}, recibidas_{0};
};

} // namespace ba::app
