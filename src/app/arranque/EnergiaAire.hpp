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
// Vive en el hilo de la interfaz: leer la energía es barato (una consulta al sistema).
// La alarma no espera a la interfaz: mide por su lado con estadoAhora().
// Lo que hace sobre el aire va por `Acciones` (la app le pasa las de AppRuntime:
// cerrar/retomarGrabacionesPorEnergia; el test, falsas).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
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
    };
    explicit EnergiaAire(Acciones acciones, QObject* parent = nullptr);
    /// Mira la energía del sistema cada tanto (la app; los tests dan las vueltas con `evaluar`).
    void vigilar(int cadaMs = 5000);

    /// {conocido, hay_bateria, en_bateria, porcentaje, minutos, fuente, accion, emergencia, umbral{…}}
    nlohmann::json estado() const;
    /// DESDE CUALQUIER HILO (F3-03): el mismo estado, medido AHORA (una consulta al
    /// sistema) en vez de el de la última vuelta, con `emergencia` de la última vuelta. Lo
    /// lee el vigía de las alarmas en el plano de control: con la interfaz trabada, el
    /// corte de luz igual se avisa.
    nlohmann::json estadoAhora() const;
    /// Una vuelta con este estado de la energía.
    void evaluar(const platform::EstadoEnergia& e);

   signals:
    /// Cambió algo de lo que muestra `estado()`.
    void cambio();
    /// Para el operador (ya traducido): se cerraron las grabaciones, volvieron.
    void aviso(const QString& texto, const QString& tipo);

   private:
    Acciones acciones_;
    QTimer timer_;
    platform::EstadoEnergia ultimo_;
    platform::AccionEnergia accion_ = platform::AccionEnergia::Nada;
    std::atomic<bool> emergencia_{false}; ///< acciones_.emergencia() de la última vuelta (estadoAhora)
};

} // namespace ba::app
