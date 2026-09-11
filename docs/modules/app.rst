.. ============================================================================
   ba-app — Proceso A (motor de emisión + UI de operación)
   ============================================================================

..
   Página del componente app (src/app). IMPLEMENTADO: MainWindow (QMainWindow
   del operador) + OperatorEngine (integra ba_core::dsp + IPC + persistencia).
   Feature-flag BA_APP_HAVE_QT_OBS (headless si no hay Qt6/libobs).

ba-app (Proceso A)
==================

`ba-app` es el **Proceso A**: motor de emisión + UI de operación (spec §6). En
Windows enlaza **Qt 6** + **libobs** cuando están presentes (feature-flag
`BA_APP_HAVE_QT_OBS`); si no, `main.cpp` arranca en **modo headless** (sin
ventana) sobre el mismo motor, para que el árbol compile y corra en
cross-compile/Linux. Usa `/MD` (CRT dinámica) por consistencia con libobs y
Qt.

Estado actual: **IMPLEMENTADO**. La lógica vive en dos capas:

- **`OperatorEngine`** (sin Qt, sin libobs — la pieza testeable y portable):
  integra `ba_core` y es la que la UI o el headless invocan.
  - **DSP por canal**: un `ba::dsp::ChannelStrip` por canal de micrófono + el
    `ba::dsp::Automixer` compartido de mesa redonda, con los tres presets de
    fábrica (`presetPodium`/`presetLavalier`/`presetHandheld`) aplicables en
    caliente por canal (`applyPreset`).
  - **IPC hacia invitados** (spec §4): `ba::ipc::SharedAudioRing` como lector
    del audio de invitados, `ba::ipc::HealthBlock` (beat + lectura del slot
    observado) y `ba::ipc::ControlEndpoint` (sendRequest/poll).
  - **Persistencia** (spec §11): `ba::persist::SnapshotStore` + `StateDoc`,
    escrito en el cierre ordenado (`shutdown`).
  - `init`, `applyPreset`, `processAudio`, `processGuestBlock`, `tick`,
    `watchedIsStale`, `buildStateDoc`, `shutdown`. No lanza; move-only por los
    objetos IPC/persistencia de `ba_core`.
- **`MainWindow`** (`QMainWindow` del operador, solo con Qt6/libobs): área de
  **monitor program** + **panel de canales** (un `QComboBox` por canal con los
  tres presets, medidor de nivel y ganancia del automixer). Refleja y opera el
  `OperatorEngine` desde el thread de la UI (un `QTimer` de 50 ms invoca
  `tick()`); `closeEvent` llama a `OperatorEngine::shutdown()`.

Enlaza `ba_core`.

Rol (spec §6)
-------------

- **Motor de emisión** sobre libobs: escenas, fuentes, encoding y salida en
  vivo (RTMPS).
- **UI de operación** (spec §5): monitor program + panel de canales (Fase 5,
  GATED:Qt/libobs).
- **Fuentes IPC** en `ba-sources.dll` que consumen los rings de audio, el
  transporte Spout y el canal de control del núcleo (ver :doc:`modules/core`).
- **Host de plugins** (VST3/LSP): embebe los editores de los bundles `.vst3`
  vía `ba::plugin::Vst3Host` (ver :doc:`modules/plugin-host`).
- **DSP nativo**: channel strip, automixer y presets del núcleo (`ba::dsp`,
  ver :doc:`modules/core`) como filtros libobs RT-safe.

Instalación
-----------

Proceso A va a `bin/64bit/` (spec §5.1).
