.. ============================================================================
   BroadcastAppliance — Launcher / Supervisor (Proceso 0)
   ============================================================================

..
   Página del componente launcher (src/launcher). Documenta el namespace
   ba::launcher y sus clases reales.

BroadcastAppliance (launcher / supervisor)
===========================================

`BroadcastAppliance` es el **Proceso 0**: launcher y supervisor. **Win32 puro,
CRT estática (`/MT`), subsistema GUI** (sin consola) — spec §3.1, §14. Es la
pieza más simple y robusta del sistema: no lleva Qt ni DLLs propias. Enlaza
`ba_core`. Fuera de Windows se compila como stub para validar la compilación
del árbol en Linux.

El launcher vive en la **raíz del disco** (spec §5.1), no en `bin/64bit/`.

.. code-block:: cpp

   namespace ba::launcher { ... }

`ba::launcher`
--------------

- `Supervisor` — Corazón del launcher (spec 3.1): instancia única,
  verificación de integridad, modo de error, lanzamiento de B y luego A,
  watchdog (crash por handle + cuelgue por heartbeat), política de reinicio con
  backoff y circuit breaker, readopt de hijos vivos y cierre ordenado.
  - `Config` (root, appExe, gatewayExe, logsDir, slotA, slotB).
  - `run()` — Punto de entrada: ejecuta todo el ciclo de vida. Devuelve el
    código de salida.
  - Internos: `acquireInstance`/`releaseInstance` (mutex
    `Local\BroadcastAppliance.Instance`), `setupErrorMode`, `verifyIntegrity`,
    `launchChildren`/`launchOne`/`relaunch`/`restartChild`/`handleCrash`/
    `handleHang`, `watchdogLoop`, `orderedShutdown`.
  - Política de reinicio (spec 3.1.8): `appConsecutive_`/`bConsecutive_`
    (backoff), `appNeedsRestore_` (A relanzado tras crash arranca con
    `--restore`), `safeMode_` (circuit breaker: >5 crashes de A en 2 min),
    `appCrashTimes_`.
- `ChildProcess` — Lanzamiento y seguimiento de un proceso hijo (spec 3.1.6):
  subsistema GUI, sin consola, con entorno saneado (sin `QT_*`/`PYTHON*`) y
  PATH mínimo. Intenta `CREATE_BREAKAWAY_FROM_JOB` y reintenta sin ese flag.
  Move-only.
  - `Config` (exePath, args, workDir, name).
  - `start(cfg)`, `alive()`, `pid()`, `handle()`, `attach(pid)` (readopt,
    spec 3.1.9), `exitCode()`, `close()`.
- `Integrity` — Verificación de integridad (spec 3.1.2): comprueba los archivos
  del disco contra `manifest.json` (SHA-256) con verificación paralela.
  - `IntegrityReport` (ok, checked, missing, mismatch, failures).
  - `verify(root, workers)`, `verifyOne(path, expectedHex)`.
- `ControlChannel` — Cliente del canal de control (named pipe, spec 4.1) para
  el cierre ordenado (3.1.10): pide a A y a B que terminen y espera.
  - `kProtocolVersion`, `requestShutdown(pipeName, target, timeoutMs)`.
- `ShadowCopy` — Interfaz de la copia sombra (spec 3.1.5, decisión D3): copia
  `bin/`, `obs-plugins/` y `data/` a `%TEMP%\BroadcastAppliance\<hash>\` y
  ejecuta desde allí, para que un movimiento del cable USB no mate los
  procesos.
  - `prepare(root, versionHash)`, `cleanup(shadowRoot)`.
- `RecoveryUi` — UI de recuperación mínima del launcher (Win32 puro, sin Qt,
  `/MT`; spec 3.1.11). Es la única ventana visible del Proceso 0: cuando el
  `Supervisor` detecta que un hijo (A o B) ha caído o colgado, presenta el
  incidente (qué proceso, códigos de salida, estado de integridad del disco) y
  deja al operador elegir una acción. `present()` es BLOQUEANTE: corre su propio
  bucle de mensajes hasta que el operador elige una acción (botón) o cierra la
  ventana (X). Nunca lanza: si no puede mostrar la UI, devuelve `Dismissed`
  (el Supervisor lo interpreta como "continuar con el flujo normal").
  - `RecoveryAction` (enum): decisión del operador. `Retry` (relanzar el hijo
    caído con el backoff normal), `ForceRestart` (terminar todos los hijos y
    relanzar desde 0), `Exit` (cierre ordenado del launcher), `Dismissed` (se
    cerró la ventana sin elegir; el Supervisor continúa).
  - `Incident` (struct): descripción del incidente a presentar. El Supervisor
    aporta los hechos; la UI solo los presenta, sin lógica de negocio.
    `title`, `childADown`, `childBDown`, `appExitCode` (+`appCodeValid`),
    `gatewayExitCode` (+`gwCodeValid`), `integrity` (`IntegrityReport`),
    `showIntegrity`.
  - `static RecoveryAction present(const Incident& inc)` — Muestra la ventana
    de recuperación (bloqueante) y devuelve la decisión del operador.
- `Log` — Logger estructurado mínimo del launcher (spec regla 8: "todo camino
  de error deja un log estructurado: componente, código, contexto"). Writer de
  archivo con mutex, sin dependencias; espeja a stderr. Thread-safe.
  - `Level` (Info, Warn, Error).
  - `init(path)`, `close()`, `write(level, component, code, context)`.
  - Macros de conveniencia `BA_LOG_INFO`/`BA_LOG_WARN`/`BA_LOG_ERROR`.
- `Sha256` — Implementación propia de SHA-256 del launcher (Win32, `/MT`),
  independiente de `ba::crypto::Sha256` (mbedTLS). Se usa para la
  verificación de integridad de `manifest.json`.
