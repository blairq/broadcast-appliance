// ============================================================================
// src/app/MainWindow.cpp — Implementación de la ventana del operador (Qt 6)
//
// Propósito:
//   Implementa `ba::app::MainWindow` (ver MainWindow.hpp): el layout inicial
//   del Proceso A (área de monitor program + panel de canales) y el cableado
//   de la UI al `OperatorEngine` (presets por canal, medidores de nivel y
//   ganancia del automixer, estado IPC/watchdog y cierre ordenado).
//
// Modelo de threading:
//   Todo en el thread de la UI. Un `QTimer` de 50 ms llama a `onTick()`
//   (`engine.tick()` + refresco). `closeEvent` llama a `engine.shutdown()`
//   antes de aceptar el evento.
//
// Estrategia de errores:
//   No lanza. Si el motor no está inicializado, la UI se construye igual y las
//   operaciones se vuelven no-op seguros.
// ============================================================================

#if defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)

#include "MainWindow.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSplitter>
#include <QVBoxLayout>

#include <cmath>

namespace ba::app {

namespace {

// Intervalo del tick de la UI (ms): refresco de medidores + tick del motor.
constexpr int kTickMs = 50;

// Nombres de los presets para el QComboBox (orden == ChannelPreset).
const char* kPresetLabels[3] = {
    "Atril (Podium)",
    "Corbetero (Lavalier)",
    "De mano (Handheld)",
};

// Título de los canales (los tres micrófonos de broadcast, spec 7).
const char* kChannelTitles[3] = {
    "Canal 1 — Atril",
    "Canal 2 — Corbetero",
    "Canal 3 — De mano",
};

} // namespace

MainWindow::MainWindow(OperatorEngine& engine)
    : engine_(engine) {
    setWindowTitle(QStringLiteral("Broadcast Appliance — Operador"));
    resize(1180, 640);
    buildUi();

    // Timer de UI: tick del motor + refresco de medidores.
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);
    timer_->start(kTickMs);
    onTick(); // refresco inicial
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    // --- Área de monitor program (izquierda) -------------------------------
    // En la fase completa se incrustará la vista previa de libobs (display del
    // programa). Aquí va el layout inicial: un marco con una etiqueta.
    auto* monitorFrame = new QFrame(this);
    monitorFrame->setFrameShape(QFrame::StyledPanel);
    auto* monitorLayout = new QVBoxLayout(monitorFrame);
    monitorLabel_ = new QLabel(QStringLiteral("Monitor de programa\n(-preview de libobs)"),
                               monitorFrame);
    monitorLabel_->setAlignment(Qt::AlignCenter);
    monitorLayout->addWidget(monitorLabel_);

    // --- Panel de canales (derecha) ----------------------------------------
    auto* panel = new QWidget(this);
    auto* panelLayout = new QVBoxLayout(panel);
    auto* grid = new QGridLayout();

    channels_.resize(engine_.numChannels());
    for (int ch = 0; ch < engine_.numChannels(); ++ch) {
        const int row = ch * 3; // 3 filas por canal: título, combo, medidor

        // Título del canal.
        auto* title = new QLabel(QString::fromUtf8(kChannelTitles[ch]), panel);
        title->setStyleSheet("font-weight: bold;");
        grid->addWidget(title, row, 0, 1, 2);

        // Selector de preset (opciones: Podium/Lavalier/De mano).
        auto* combo = new QComboBox(panel);
        for (int i = 0; i < 3; ++i) {
            combo->addItem(QString::fromUtf8(kPresetLabels[i]), i);
        }
        // Preset inicial del motor (sin disparar el slot).
        const int initial = static_cast<int>(engine_.presetFor(ch));
        combo->blockSignals(true);
        combo->setCurrentIndex(initial);
        combo->blockSignals(false);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, ch](int idx) { onPresetChanged(ch); (void)idx; });
        grid->addWidget(combo, row + 1, 0, 1, 2);

        // Medidor de nivel (RMS 0..100).
        auto* level = new QProgressBar(panel);
        level->setRange(0, 100);
        level->setValue(0);
        level->setTextVisible(false);
        grid->addWidget(level, row + 2, 0);

        // Ganancia dinámica del automixer (dB).
        auto* gain = new QLabel(QStringLiteral("0.0 dB"), panel);
        grid->addWidget(gain, row + 2, 1);

        channels_[ch].preset = combo;
        channels_[ch].level = level;
        channels_[ch].gain = gain;
    }
    panelLayout->addLayout(grid);
    panelLayout->addStretch(1);

    // --- Splitter monitor | canales ----------------------------------------
    auto* splitter = new QSplitter(this);
    splitter->addWidget(monitorFrame);
    splitter->addWidget(panel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    // --- Barra de estado (IPC/watchdog) ------------------------------------
    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_);
}

float MainWindow::gainToDb(float gain) {
    // lineal (0..1) -> dB, con piso para no mostrar -inf.
    if (gain <= 0.0f) {
        return -60.0f;
    }
    const float db = 20.0f * std::log10(gain);
    return db < -60.0f ? -60.0f : db;
}

void MainWindow::onPresetChanged(int channel) {
    if (channel < 0 || channel >= engine_.numChannels()) {
        return;
    }
    QComboBox* combo = channels_[channel].preset;
    if (combo == nullptr) {
        return;
    }
    const int preset = combo->currentData().toInt();
    // Cambio en caliente: el motor suelta el fade de ganancia (sin clicks).
    engine_.applyPreset(channel, static_cast<ChannelPreset>(preset), 5.0);
}

void MainWindow::onTick() {
    // Tick del motor: heartbeat + poll del canal de control.
    engine_.tick();

    // Refresco de medidores por canal.
    for (int ch = 0; ch < engine_.numChannels(); ++ch) {
        const float level = engine_.levelFor(ch); // RMS 0..1
        channels_[ch].level->setValue(static_cast<int>(level * 100.0f + 0.5f));
        const float gainDb = gainToDb(engine_.automixerGainFor(ch));
        channels_[ch].gain->setText(
            QStringLiteral("%1 dB").arg(gainDb, 0, 'f', 1));
    }

    // Estado IPC/watchdog en la barra de estado.
    const bool ring = engine_.guestRingOpen();
    const bool ctl = engine_.controlConnected();
    const bool stale = engine_.watchedIsStale();
    statusLabel_->setText(QStringLiteral(
        "IPC: ring %1 · control %2 · watchdog %3")
        .arg(ring ? QStringLiteral("OK") : QStringLiteral("—"),
             ctl ? QStringLiteral("OK") : QStringLiteral("—"),
             stale ? QStringLiteral("STALE") : QStringLiteral("OK")));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Cierre ordenado (spec §11.2): persiste el StateDoc antes de salir.
    if (timer_ != nullptr) {
        timer_->stop();
    }
    engine_.shutdown();
    event->accept();
}

} // namespace ba::app

#endif // defined(_WIN32) && defined(BA_APP_HAVE_QT_OBS)
