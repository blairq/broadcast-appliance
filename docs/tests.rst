.. ============================================================================
   Tests
   ============================================================================

..
   Página de tests. Describe el esquema de pruebas unitarias (CTest) de
   ba_core y ba_plugin_host.

Tests
=====

Las pruebas unitarias usan **CTest** sin framework externo: cada `test_*.cpp`
es un ejecutable con un `main()` que devuelve `0` si pasa y `!= 0` si falla.
Se enlazan contra `ba_core` (o `ba_plugin_host`) y se registran con
`add_test`. Solo el núcleo (`ba_core`) y el host de plugins (`ba_plugin_host`)
tienen tests que corren en Linux; el resto es GATED (Windows).

Ejecutar
--------

.. code-block:: bash

   cmake -S . -B build
   cmake --build build -j4
   ctest --test-dir build

Estado verificado (último build conocido bueno): ``18/18 Passed``.

Tests de `ba_core`
------------------

Los tests viven en `src/core/tests/test_*.cpp`. Cada uno se compila como un
ejecutable que enlaza `ba_core` y se registra con CTest:

- `test_shared_audio_ring` — concurrencia/wrap/reinicio del ring de audio.
- `test_health_block` — bloque de salud (seqlock, stale).
- `test_control_protocol` — framing y request/response del canal de control.
- `test_spout_transport` — contrato del transporte Spout (backend NULL).
- `test_channel_strip` — cadena DSP nativa.
- `test_automixer` — automixer Dugan de mesa redonda.
- `test_resampler` — re-muestreo adaptativo (`ba::dsp::Resampler`).
- `test_token_service` — tokens HMAC + rate-limit de señalización.
- `test_snapshot_store` — snapshot con doble generación + debounce.
- `test_restore` — restauración y recuperación del snapshot.
- `test_diag` — logs rotativos y export de diagnóstico.
- `test_fault` — inyección de fallas (`ba::fault::FaultInjector`).
- `test_secrets` — bóveda de secretos cifrada (`ba::security::SecretsStore`).
- `test_device_match` — matching de identidad de dispositivos.
- `test_vst3_host` — host VST3 (`scanDirectory`).
- `test_plugin_deploy` — despliegue de bundles LSP (`PluginDeployer`).
- `test_ring_drift` — proxy acotado de deriva de rings (muchos wraps, sin
  pérdida/deriva).

Tests de `ba_plugin_host`
-------------------------

Los tests viven en `src/plugin-host/tests/test_*.cpp` y se registran con el
mismo patrón (enlazan `ba_plugin_host`):

- `test_vst3_host` — `scanDirectory()` (puro C++, cross-platform).
- `test_plugin_deploy` — `PluginDeployer::deploy()` + `manifest.json`.

Tests del launcher
------------------

- `test_shadow_copy` — la copia sombra es portable (`std::filesystem`), por lo
  que compila y corre en Linux y Windows.

Nota de plataforma
------------------

Todo lo que toca libobs, Qt, WASAPI/D3D, libwebrtc o Win32 se escribe con
`#ifdef _WIN32` y se valida de compilación en Linux, pero su comportamiento
real se verifica en Windows/MSVC. Por eso la estrategia es sacar adelante todo
el núcleo testeable en Linux mientras las piezas plataforma-gated esperan el
entorno correcto.
