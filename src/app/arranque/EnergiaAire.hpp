// ============================================================================
// src/app/arranque/EnergiaAire.hpp — el corte de luz con el aire andando (F1-11)
//
// Cada 5 s mira de dónde viene la energía (platform::leerEnergia: lo que da el
// sistema, sin servicios aparte) y aplica la política (energia.corte.*):
//
//   en batería/UPS            la alarma «en batería» (AlertEngine, tipo `energia`)
//   a X % o Y min             EOS ordenado de las grabaciones; la transmisión
//                             sigue; desde ahí un cierre cuenta como corte
//                             (AppRuntime::cerrarGrabacionesPorEnergia), así
//                             que si la máquina se apaga, al volver todo se retoma
//   vuelve la energía antes   las grabaciones vuelven solas
//
// El estado (`estado()`) lo leen la alarma, `status` y la tarjeta de preparación.
// ES DEL DUEÑO DEL MOTOR (el plano de control; docs/arquitectura/05-motor-del-plano.md):
// se arma en la interfaz y se adopta (VentanillaMotor::adoptar) con su reloj, que es hijo;
// `vigilar` se llama ya mudado. Así, con la interfaz trabada, las grabaciones igual se
// cierran antes de que se apague la UPS. `estado()` se lee desde cualquier hilo (el vigía
// de las alarmas, en el mismo dueño; la interfaz); las señales salen en el dueño (a la
// interfaz le llegan encoladas).
// Lo que hace sobre el aire va por `Acciones` (la app le pasa las de AppRuntime:
// cerrar/retomarGrabacionesPorEnergia; el test, falsas).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "platform/Energia.hpp"

namespace ba::app {

class EnergiaAire : public QObject {
    Q_OBJECT
   public:
    struct Acciones {
        std::function<std::vector<std::string>()> cerrarGrabaciones; ///< devuelve las salidas que cerró
        std::function<void()> retomarGrabaciones;
        std::function<bool()> emergencia; ///< ¿ya se cerraron por el corte?
        std::function<platform::EstadoEnergia()> leer; ///< vacío: la del sistema (platform::leerEnergia)
    };
    explicit EnergiaAire(Acciones acciones, QObject* parent = nullptr);
    /// Mira la energía del sistema cada tanto (la app; los tests dan las vueltas con `evaluar`).
    void vigilar(int cadaMs = 5000);

    /// {conocido, hay_bateria, en_bateria, porcentaje, minutos, fuente, accion, emergencia, umbral{…}}:
    /// lo de la última vuelta. Desde cualquier hilo.
    nlohmann::json estado() const;
    /// Una vuelta con este estado de la energía.
    void evaluar(const platform::EstadoEnergia& e);

   signals:
    /// Cambió algo de lo que muestra `estado()`.
    void cambio();
    /// Para el operador (ya traducido): se cerraron las grabaciones, volvieron.
    void aviso(const QString& texto, const QString& tipo);

   private:
    platform::EstadoEnergia leer() const { return acciones_.leer ? acciones_.leer() : platform::leerEnergia(); }

    Acciones acciones_;
    QTimer timer_{this}; ///< hijo: se muda con él al dueño
    mutable std::mutex mu_; ///< ultimo_ y accion_ (los escribe el dueño, se leen de cualquiera)
    platform::EstadoEnergia ultimo_;
    platform::AccionEnergia accion_ = platform::AccionEnergia::Nada;
    std::atomic<bool> emergencia_{false}; ///< acciones_.emergencia() de la última vuelta
};

} // namespace ba::app
