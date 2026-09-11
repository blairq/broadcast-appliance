// Supervisor.hpp
//
// Proposito:
//   Corazon del launcher (Proceso 0, spec 3.1): instancia unica, verificacion
//   de integridad, modo de error, lanzamiento de B y luego A, watchdog
//   (crash por handle + cuelgue por heartbeat), politica de reinicio con
//   backoff y circuit breaker, readopt de hijos vivos, y cierre ordenado.
//   No lleva Qt ni DLLs propias.
//
// Modelo de threading:
//   run() se ejecuta en el thread principal. El loop de vigilancia
//   (watchdogLoop) vive en ese mismo thread: alterna entre esperar sobre los
//   handles de proceso y leer los heartbeats de ba_core (HealthBlock). No hay
//   threads adicionales en esta fase; el polling de 250 ms es suficiente para
//   la deteccion de cuelgue (umbral de 5 s, spec 3.1.7).
//
// Estrategia de errores:
//   Nunca lanza. Cada fallo (integridad, lanzamiento, dump) se loguea de forma
//   estructurada (Log) y se traduce en una accion degradada: advertencia,
//   reinicio con backoff, o modo seguro. Un crash de A con codigo 0 se trata
//   como cierre ordenado del operador, no como fallo.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChildProcess.hpp"
#include "ControlChannel.hpp"
#include "Integrity.hpp"
#include "ipc/HealthBlock.hpp"
#include "RecoveryUi.hpp"

namespace ba::launcher {

/// Supervisor de los procesos A (app) y B (gateway).
class Supervisor {
   public:
    /// Configuracion del supervisor.
    struct Config {
        std::wstring root;       // raiz del disco (GetModuleFileNameW).
        std::wstring appExe;     // bin/64bit/ba-app.exe.
        std::wstring gatewayExe; // bin/64bit/ba-gateway.exe.
        std::wstring logsDir;    // Logs/ (para dumps).
        std::uint32_t slotA = 1; // slot de A en BA.Health.
        std::uint32_t slotB = 0; // slot de B en BA.Health.
    };

    explicit Supervisor(Config cfg);
    ~Supervisor();

    /// Punto de entrada: ejecuta todo el ciclo de vida del launcher.
    ///
    /// :returns: codigo de salida del proceso.
    int run();

   private:
    // --- Ciclo de vida ------------------------------------------------------
    bool acquireInstance(); // mutex Local\BroadcastAppliance.Instance.
    void releaseInstance();
    void setupErrorMode();             // SetErrorMode (spec 3.1.4).
    IntegrityReport verifyIntegrity(); // manifest.json SHA-256 (spec 3.1.2).

    // --- Hijos --------------------------------------------------------------
    void launchChildren(bool safeMode);
    void launchOne(std::uint32_t slot, ChildProcess& child, const std::wstring& exe, const std::wstring& name,
                   bool safeMode);
    void relaunch(bool isA);
    void restartChild(bool isA); // backoff + relaunch (comun a crash/hang).
    void handleCrash(bool isA);
    void handleHang(bool isA); // minidump + terminate + restart.

    // --- Recuperacion (UI) --------------------------------------------------
    // Muestra la UI de recuperacion con el estado actual de los hijos y la
    // integridad del disco; devuelve la accion elegida por el operador.
    RecoveryAction presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung);
    // Forzar reinicio: termina todos los hijos, limpia la politica de reinicio
    // y relanza B y A desde 0.
    void forceRestart();

    // --- Vigilancia y cierre ------------------------------------------------
    void watchdogLoop();
    void orderedShutdown();

    // --- Estado -------------------------------------------------------------
    Config cfg_;
    ChildProcess app_;
    ChildProcess gateway_;
    ba::ipc::HealthBlock health_;
    ControlChannel control_;

    void* hInstance_ = nullptr; // HANDLE del mutex de instancia (Win32).
    bool shuttingDown_ = false;

    // Politica de reinicio (spec 3.1.8).
    int appConsecutive_ = 0; // reinicios consecutivos de A (para backoff).
    int bConsecutive_ = 0;
    bool appNeedsRestore_ = false;             // A relanzado tras crash arranca con --restore.
    bool safeMode_ = false;                    // circuit breaker: >5 crashes de A en 2 min.
    std::vector<std::uint64_t> appCrashTimes_; // epoch ms, para el circuit breaker.

    // Cache del reporte de integridad para la UI de recuperacion (evita
    // re-hashear todo el disco en un loop de crashes).
    std::uint64_t lastIntegrityCheckMs_ = 0;
    IntegrityReport lastIntegrityReport_;
    bool hasIntegrityReport_ = false;
};

} // namespace ba::launcher
