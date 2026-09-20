// Supervisor.hpp
//
// Proposito:
//   Corazon del launcher (Proceso 0, spec 3.1): instancia unica, verificacion
//   de integridad, modo de error, lanzamiento de B y luego A, watchdog
//   (crash por salida del proceso + cuelgue por heartbeat), politica de
//   reinicio con backoff y circuit breaker, readopt de hijos vivos y cierre
//   ordenado. Igual en Windows, Linux y macOS: todo lo de SO va por
//   `ba::platform` (via ChildProcess, ControlChannel y SingleInstance).
//
// Modelo de threading:
//   run() se ejecuta en el thread principal. El loop de vigilancia vive en ese
//   mismo thread y sondea cada 250 ms (el umbral de cuelgue es 5 s, spec
//   3.1.7). `requestStop()` es async-signal-safe (solo escribe un atomic) y
//   se puede llamar desde un handler de señal o desde otro thread.
//
// Estrategia de errores:
//   Nunca lanza. Cada fallo (integridad, lanzamiento, dump) se loguea y se
//   traduce en una accion degradada: advertencia, reinicio con backoff o modo
//   seguro. Que A salga con codigo 0 es el cierre ordenado del operador, no
//   un fallo.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ChildProcess.hpp"
#include "ControlChannel.hpp"
#include "Integrity.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/SingleInstance.hpp"
#include "RecoveryUi.hpp"

namespace ba::launcher {

/// Supervisor de los procesos A (app) y B (gateway).
class Supervisor {
   public:
    /// Configuracion del supervisor.
    struct Config {
        std::filesystem::path root;           // raiz del bundle (ba::platform::bundleRoot)
        std::filesystem::path appExe;         // bin/64bit/ba-app(.exe)
        std::filesystem::path gatewayExe;     // bin/64bit/ba-gateway(.exe)
        std::filesystem::path logsDir;        // Logs/ (dumps y salida de A y B)
        std::vector<std::string> appArgs;     // argumentos extra para A
        std::vector<std::string> gatewayArgs; // argumentos extra para B
        // Copia sombra (spec 3.1.5): OPCIONAL y desactivada por defecto. Sin
        // ella A y B corren desde el bundle, donde esta. Se activa con
        // --shadow (o "shadowCopy": true en Config/launcher.json).
        bool shadowCopy = false;
        bool recoveryUi = true;  // dialogo ante fallos (false = reinicio automatico)
        std::uint32_t slotA = 1; // slot de A en BA.Health
        std::uint32_t slotB = 0; // slot de B en BA.Health
    };

    explicit Supervisor(Config cfg);
    ~Supervisor();

    /// Punto de entrada: ejecuta todo el ciclo de vida del launcher.
    ///
    /// :returns: codigo de salida del proceso.
    int run();

    /// Pide el cierre ordenado (Ctrl+C, SIGTERM, fin de sesion).
    /// Async-signal-safe.
    static void requestStop();

   private:
    // --- Ciclo de vida ------------------------------------------------------
    void applyExecRoot(const std::filesystem::path& execRoot);
    IntegrityReport verifyIntegrity();

    // --- Hijos --------------------------------------------------------------
    void launchChildren();
    void launchOne(bool isA);
    bool startChild(bool isA);
    void restartChild(bool isA); // backoff + relanzamiento (comun a crash/hang)
    void handleCrash(bool isA);
    void handleHang(bool isA);

    // --- Recuperacion (UI) --------------------------------------------------
    RecoveryAction presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung);
    void forceRestart();

    // --- Vigilancia y cierre ------------------------------------------------
    void watchdogLoop();
    void orderedShutdown();
    void stopChild(bool isA);

    ChildProcess& child(bool isA) { return isA ? app_ : gateway_; }

    // --- Estado -------------------------------------------------------------
    Config cfg_;
    ChildProcess app_;
    ChildProcess gateway_;
    ba::ipc::HealthBlock health_;
    ControlChannel control_;
    ba::platform::SingleInstance instance_;
    bool shuttingDown_ = false;

    static std::atomic<bool> stopRequested_;

    // Momento del ultimo lanzamiento de cada hijo (ms monotonicos): durante el
    // periodo de gracia de arranque no se evalua el heartbeat (libobs tarda en
    // cargar modulos y el proceso todavia no late).
    std::uint64_t launchedAtMs_[2] = {0, 0};
    bool inStartupGrace(bool isA) const;

    // Politica de reinicio (spec 3.1.8).
    int appConsecutive_ = 0; // reinicios consecutivos de A (para backoff)
    int bConsecutive_ = 0;
    bool appNeedsRestore_ = false;             // A relanzado tras crash arranca con --restore
    bool safeMode_ = false;                    // circuit breaker: >5 crashes de A en 2 min
    std::vector<std::uint64_t> appCrashTimes_; // ms monotonicos, para el circuit breaker

    // Cache del reporte de integridad para la UI de recuperacion.
    std::uint64_t lastIntegrityCheckMs_ = 0;
    IntegrityReport lastIntegrityReport_;
    bool hasIntegrityReport_ = false;
};

} // namespace ba::launcher
