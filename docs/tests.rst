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
`add_test`. En Linux corren los tests del núcleo (`ba_core`), del host de
plugins, del gateway (con backend WebRTC NULL) y del módulo OBS contra libobs
real; lo que depende de libwebrtc o Win32 queda GATED (Windows).

Ejecutar
--------

.. code-block:: bash

   cmake -S . -B build
   cmake --build build -j4
   ctest --test-dir build

Estado verificado (último build conocido bueno, 2026-09-18): **30 tests,
29 Passed + 1 Skipped** (``test_gateway_integration``: el test viejo del SDK de
Windows; el WebRTC real de Linux lo cubre ``test_webrtc_loopback``).
``test_ba_sources_real`` solo se registra si libobs está en ``extern/obs-linux``.

Tests de `ba_core`
------------------

Los tests viven en `src/core/tests/test_*.cpp`. Cada uno se compila como un
ejecutable que enlaza `ba_core` y se registra con CTest:

- `test_platform` — test de CONTRATO de la capa de plataforma
  (``ba::platform``): el mismo archivo tiene que pasar en Windows, Linux y
  macOS. Se relanza a sí mismo como hijo para probar, cruzando procesos
  reales: rutas del bundle, lanzamiento/espera/terminación de procesos
  (entorno, cwd, argumentos con comillas, nietos, muerte del padre), sockets
  locales (eco, escrituras grandes, EOF, nombre ocupado, socket huérfano,
  timeout, ``ControlEndpoint`` de punta a punta) e instancia única.
- `test_shared_audio_ring` — concurrencia/wrap/reinicio del ring de audio.
- `test_health_block` — bloque de salud (seqlock, stale).
- `test_ipc_cross_process` — ring de audio y bloque de salud cruzando un
  límite de proceso real (``fork()`` + ``shm_open``/``MAP_SHARED``).
- `test_control_protocol` — framing y request/response del canal de control,
  incluidos un frame partido entre dos lecturas y un peer que escribe y
  cierra enseguida (el mensaje se entrega antes de informar la desconexión).
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
- `test_ring_drift` — proxy acotado de deriva de rings (muchos wraps, sin
  pérdida/deriva).

Tests de `ba_plugin_host`
-------------------------

Los tests viven en `src/plugin-host/tests/test_*.cpp` y se registran con el
mismo patrón (enlazan `ba_plugin_host`):

- `test_vst3_host` — `scanDirectory()` (puro C++, cross-platform).
- `test_plugin_deploy` — `PluginDeployer::deploy()` + `manifest.json`.

Tests del gateway (Proceso B)
-----------------------------

Viven en `src/gateway/tests/`:

- `test_room` — modelo de la sala sin red: lobby FIFO, admitir/rechazar/
  expulsar, lobby apagado, reenganche con clave y plazo de gracia, chat
  (visibilidad lobby/adentro/privado), sala cerrada, pedidos, rutas de medios
  del lobby privado (preview, "hablar", admision sin reconexion) y
  `ShareStore` (tipo por contenido, streaming a disco, topes).
- `test_gateway_http` — la superficie de red de punta a punta con el backend
  WebRTC NULL: estaticos, join/PIN, upgrade WS (RFC 6455, anti-replay), API de
  control (Bearer, cookie + CSRF), lobby por SSE, publicar desde el lobby,
  offer fragmentado, chat, subida de archivos (415/401/413), sala llena,
  expulsion, reenganche, sala cerrada, PIN y login de la consola web.
- `test_wireguard` — primitivas contra vectores de terceros (RFC 7693/7748/
  8439, draft XChaCha, OpenSSL, libsodium), dos `WgDevice` en loopback (PSK,
  rafaga, PSK equivocada) e **interoperabilidad** con un par de referencia
  independiente en Python (`wg_reference_peer.py`) en los dos roles.
- `test_ipstack` — lwIP sobre WireGuard con DOS procesos: TCP con cabecera
  PROXY (IP real), 4 MB integros (~500 Mbit/s en loopback) y UDP con NAT por
  flujo. Tiene un modo ``--probe`` para probar un gateway real por el tunel.
- `test_webrtc_loopback` — libwebrtc REAL (lo compila `cmake/gateway-webrtc`):
  una PeerConnection hace de navegador; camara, microfono, pantalla compartida
  (re-offer) y retorno viajan por RTP/SRTP de verdad. SKIP (77) sin red.
- `test_gateway_integration` — el test historico contra el SDK de Windows;
  sin ese SDK se reporta *Skipped*.

Tests de la interfaz (Proceso A)
--------------------------------

- `test_dashboard_model` — disposicion del dashboard (encaje magnetico,
  ventanas de salida, persistencia).
- `test_qml_syntax` — pasa `qmllint` (del Qt vendoreado) por todos los QML y
  falla ante errores de sintaxis (un QML roto no rompe el build: rompe el
  arranque).

Tests del módulo OBS (`ba-sources`)
-----------------------------------

- `test_ba_sources_real` — carga `ba-sources` en la libobs **real** y comprueba
  el ABI (``obs_module_load``) y el registro de las fuentes. No necesita
  contexto gráfico. Solo se compila si libobs está en ``extern/obs-linux``.

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
