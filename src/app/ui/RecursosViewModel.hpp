// ============================================================================
// src/app/ui/RecursosViewModel.hpp — los recursos de la máquina para la tarjeta «Sistema»
//
// Dueño del muestreador (diag::Recursos, que mide en su hilo una vez por
// segundo) y su cara para la interfaz: una fila por recurso (CPU, GPU, RAM,
// VRAM, red) con el valor, el pico, el estado por umbral y la historia de los
// últimos minutos para la línea chiquita. Todo por propiedades con aviso
// (`cambio`): la vista se entera sola, no pregunta.
//
// Le pasa al muestreador lo que el sistema no sabe: el pid del gateway (cambia
// si se relanza) y lo del motor (cuadros por segundo, perdidos, lo que sale por
// la transmisión). Lo leen también la herramienta MCP `sistema_recursos` y el
// disparador de alarmas `recursos` (por `muestreador()`).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <chrono>
#include <cstdint>
#include <memory>

#include "diag/Recursos.hpp"

namespace ba::app {

class AppRuntime;

class RecursosViewModel : public QObject {
    Q_OBJECT
    /// [{clave, titulo, pct (0–100, -1 desconocido), texto, conocido, pico, estado, historia: [0–100|-1],
    ///   historia2 (la red: la entrada), techo (la escala de la historia)}]
    Q_PROPERTY(QVariantList filas READ filas NOTIFY cambio)
    /// El peor estado: normal | aviso | critico | desconocido.
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)
    /// Lo que le queda al recurso más cargado (%; -1 si no se sabe) y cuál es.
    Q_PROPERTY(double margen READ margen NOTIFY cambio)
    Q_PROPERTY(QString limita READ limita NOTIFY cambio)
    /// Nuestros procesos: [{nombre, texto}] («app 12 % · 850 MB»).
    Q_PROPERTY(QVariantList procesos READ procesos NOTIFY cambio)
    /// Cuántos segundos de historia muestra la línea de cada fila.
    Q_PROPERTY(int segundosHistoria READ segundosHistoria CONSTANT)
   public:
    explicit RecursosViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~RecursosViewModel() override;

    QVariantList filas() const { return filas_; }
    QString estado() const { return estado_; }
    double margen() const { return margen_; }
    QString limita() const { return limita_; }
    QVariantList procesos() const { return procesos_; }
    int segundosHistoria() const { return kHistoriaS; }

    diag::Recursos& muestreador() { return *recursos_; }

   signals:
    void cambio();

   private:
    static constexpr int kHistoriaS = 180;
    /// En el hilo de la interfaz, después de cada medición: el motor, los
    /// procesos y las filas.
    void publicar();
    QString titulo(const QString& clave) const;

    AppRuntime& rt_;
    std::unique_ptr<diag::Recursos> recursos_;
    QVariantList filas_;
    QString estado_ = QStringLiteral("desconocido");
    double margen_ = -1;
    QString limita_;
    QVariantList procesos_;
    std::uint32_t gatewayPid_ = 0;
    // Para los cuadros perdidos y lo que sale, entre una vuelta y la otra.
    int perdidosAntes_ = -1, totalAntes_ = -1;
    std::uint64_t bytesAntes_ = 0;
    std::chrono::steady_clock::time_point tAntes_{};
};

} // namespace ba::app
