// ============================================================================
// src/app/MainWindow.hpp — Ventana principal del operador (Qt 6)  [Proceso A]
//
// Propósito:
//   UI de operación del Proceso A (spec §5/§6): un `QMainWindow` con el
//   layout inicial pedido — un área de MONITOR PROGRAM (a la izquierda, donde
//   en la fase completa se incrustará la vista previa de libobs) y un PANEL
//   DE CANALES (a la derecha) con, por cada uno de los tres micrófonos, un
//   `QComboBox` que expone los tres presets de fábrica (`presetPodium` /
//   `presetLavalier` / `presetHandheld`) y un medidor de nivel + ganancia del
//   automixer. Toda la lógica de negocio vive en `OperatorEngine` (ba_core);
//   esta ventana solo la refleja y la opera desde su thread de UI.
//
//   Solo existe en Windows con Qt 6 + libobs disponibles: el archivo completo
//   va tras `#if defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)`. En un
//   cross-compile de validación (sin Qt/libobs) el archivo compila a una TU
//   vacía y el modo headless de `main.cpp` hace las veces de UI.
//
// Modelo de threading:
//   Todo vive en el thread de la UI (el event loop de Qt). Un `QTimer` de
//   50 ms invoca `onTick()` (que llama a `OperatorEngine::tick()` y refresca
//   los medidores). `closeEvent` llama a `OperatorEngine::shutdown()` (cierre
//   ordenado, spec §11.2) antes de que la ventana se destruya. No hay threads
//   propios: el motor es operado desde este thread (ver OperatorEngine.hpp).
//
// Estrategia de errores:
//   No lanza. Si el motor no está inicializado (`engine.initialized()` false),
//   la ventana se construye igualmente (los medidores en 0) y `onTick`/
//   `closeEvent` se vuelven no-op seguros. Los fallos del motor se reflejan en
//   la barra de estado, no en excepciones.
// ============================================================================

#pragma once

#if defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)

#include <memory>
#include <vector>

#include <QMainWindow>

#include "OperatorEngine.hpp"

class QTimer;
class QComboBox;
class QProgressBar;
class QLabel;

namespace ba::app {

/// Ventana principal del operador (spec §5/§6).
///
/// Layout inicial: área de monitor program + panel de canales. Refleja y opera
/// un `OperatorEngine` (no lo posee: `main.cpp` lo crea y lo pasa por
/// referencia, para que viva toda la vida de la app).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// Construye la ventana y el layout inicial.
    ///
    /// :param engine: motor de operación a reflejar/operar (no se toma la
    ///                propiedad; debe seguir vivo mientras exista la ventana).
    explicit MainWindow(OperatorEngine& engine);

    /// Destructor: detiene el timer.
    ~MainWindow() override;

protected:
    /// Al cerrar: `OperatorEngine::shutdown()` (cierre ordenado, spec §11.2)
    /// antes de aceptar el evento.
    void closeEvent(QCloseEvent* event) override;

private slots:
    /// Cambio de preset en el canal `channel`: `engine.applyPreset()`.
    ///
    /// :param channel: índice de canal (dato del `QComboBox` que emite).
    void onPresetChanged(int channel);

    /// Tick periódico (50 ms): `engine.tick()` + refresco de medidores/estado.
    void onTick();

private:
    /// Construye el layout inicial (monitor program + panel de canales).
    void buildUi();

    /// Convierte la ganancia lineal (0..1) del automixer a dB para mostrar.
    static float gainToDb(float gain);

    OperatorEngine& engine_;
    QTimer* timer_ = nullptr;

    /// Widgets por canal (índice == índice de canal del motor).
    struct ChannelUi {
        QComboBox* preset = nullptr;    // opciones: Podium/Lavalier/De mano
        QProgressBar* level = nullptr;  // medidor de nivel (RMS 0..100)
        QLabel* gain = nullptr;         // ganancia dinámica del automixer (dB)
    };
    std::vector<ChannelUi> channels_;

    QLabel* monitorLabel_ = nullptr;  // etiqueta del área de monitor program
    QLabel* statusLabel_ = nullptr;   // estado IPC/watchdog (barra de estado)
};

} // namespace ba::app

#endif // defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)
