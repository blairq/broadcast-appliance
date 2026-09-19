.. ============================================================================
   Tests
   ============================================================================

..
   Página de tests. Describe el esquema de pruebas unitarias (CTest) de
   ba_core, la app, el gateway y el módulo OBS.

Tests
=====

Las pruebas unitarias usan **CTest** sin framework externo: cada `test_*.cpp`
es un ejecutable con un `main()` que devuelve `0` si pasa y `!= 0` si falla.
Se enlazan contra `ba_core` (o las fuentes del componente) y se registran con
`add_test`. En Linux corren los tests del núcleo (`ba_core`), de la app (con
el rack de audio contra los módulos LSP reales), del gateway (con backend WebRTC NULL) y del módulo OBS contra libobs
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
  timeout, ``ControlEndpoint`` de punta a punta) e instancia única. En Linux
  comprueba además que el lock no deja nada en disco (espacio abstracto).
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
- `test_credentials` — carpeta `Credenciales/` (`ba::security::CredentialStore`):
  ida y vuelta, nada en claro en disco, permisos, integridad (byte alterado,
  archivo renombrado, clave de otro equipo), nombres inválidos, importación de
  lo que versiones anteriores guardaban en claro en `Config/` (y del
  certificado propio dejado en `Credenciales/`) y clave única con varios
  procesos arrancando a la vez.
- `test_device_match` — matching de identidad de dispositivos.
- `test_ring_drift` — proxy acotado de deriva de rings (muchos wraps, sin
  pérdida/deriva).

Tests del gateway (Proceso B)
-----------------------------

Viven en `src/gateway/tests/`:

- `test_room` — modelo de la sala sin red: lobby FIFO, admitir/rechazar/
  expulsar, lobby apagado, reenganche con clave y plazo de gracia, chat
  (visibilidad lobby/adentro/privado), sala cerrada, pedidos, rutas de medios
  del lobby privado (preview, "hablar", admision sin reconexion), modo
  oradores (fila sin slot, al aire uno/varios con slot por defecto,
  reordenar, tiempo propio, el reloj nunca saca a nadie, carrusel "chain" y
  "time" con margen, perfil, tablero sin claves) y `ShareStore` (tipo por
  contenido, streaming a disco, topes).
- `test_gateway_http` — la superficie de red de punta a punta con el backend
  WebRTC NULL: estaticos, join/PIN, upgrade WS (RFC 6455, anti-replay), API de
  control (Bearer, cookie + CSRF), lobby por SSE, publicar desde el lobby,
  offer fragmentado, chat, subida de archivos (415/401/413), sala llena,
  expulsion, reenganche, sala cerrada, PIN y login de la consola web, QR,
  modo oradores (info/contacto al entrar, aviso de turno, al aire), fotos
  (JPEG por contenido, con clave), tablero con su clave, permisos de las
  vistas en vivo y que un JSON con tipos inesperados no tumba al gateway.
- `test_wireguard` — primitivas contra vectores de terceros (RFC 7693/7748/
  8439, draft XChaCha, OpenSSL, libsodium), dos `WgDevice` en loopback (PSK,
  rafaga, PSK equivocada) e **interoperabilidad** con un par de referencia
  independiente en Python (`wg_reference_peer.py`) en los dos roles.
- `test_ipstack` — lwIP sobre WireGuard con DOS procesos: TCP con cabecera
  PROXY (IP real), 4 MB integros (~500 Mbit/s en loopback) y UDP con NAT por
  flujo. Tiene un modo ``--probe`` para probar un gateway real por el tunel.
- `test_webrtc_loopback` — libwebrtc REAL (lo compila `cmake/gateway-webrtc`):
  una PeerConnection hace de navegador; camara, microfono, pantalla compartida
  (re-offer) y retorno viajan por RTP/SRTP de verdad; y el **relay** a
  espectadores: una PeerConnection recvonly recibe la camara (y otra la
  pantalla) del invitado reenviadas codificadas, sin re-codificar. SKIP (77)
  sin red.
- `test_gateway_integration` — el test historico contra el SDK de Windows;
  sin ese SDK se reporta *Skipped*.

Tests de la interfaz (Proceso A)
--------------------------------

- `test_mcp` — servidor MCP (incluida la salvaguarda determinista: una sesión
  que leyó contenido de invitados no modifica nada sin aprobación humana): protocolo (negociacion de version, herramientas,
  validacion de argumentos, ayuda y busqueda, recursos, prompts, lotes),
  transporte HTTP (token, Origin, codigos, sesion) y el puente stdio `ba-mcp`.
- `test_youtube`, `test_twitch`, `test_facebook` — clientes de las plataformas
  contra servidores falsos locales (HTTPS propio con mbedTLS): login (loopback
  + PKCE en Google, codigo de dispositivo en Twitch, codigo + token largo +
  tokens de Pagina en Facebook), refresco de tokens, listados, crear vivos,
  ingesta (la clave nunca a disco), subidas por partes, chat (IRC en Twitch,
  comentarios en Facebook) y archivos guardados dentro de la carpeta portable.
- `test_terminal` — la terminal de la pestaña Asistente con un programa real
  en una pseudo-terminal: colores ANSI en la grilla, código de salida, tamaño
  visto por el programa y su cambio, teclas y pegado (con acentos), historial y
  caracteres anchos.
- `test_alerts` — vigilancias con entradas y reloj falsos: saturación (retención,
  enfriamiento por canal, ignora silenciados), silencio solo al aire, fin de un
  tema y "faltan N s", tiempo del orador una vez por turno, lobby/mano/caída
  sin avisar lo que ya estaba, chat por plataforma, texto y regex, cuadros
  perdidos y corte, disco, temporizadores (una vez / periódico), persistencia,
  `alert_wait` (con evento y al vencer) y validaciones.
- `test_operator_messages` — barra del asistente al operador: avisos que vencen
  (y los críticos que no), preguntas contestadas por la herramienta o, si ya no
  espera, al chat del asistente, respuestas sugeridas a invitados (en privado o
  al general), ignorar, que lo cerrado no se vuelva a contestar, y el
  formulario de credenciales: los valores van al almacén y **nunca** quedan en
  el item ni le llegan al asistente; con error el formulario sigue abierto.
- `test_ndi` — recepción NDI abierta: el descubrimiento mDNS vuelve a tiempo
  sin colgarse, y el FFmpeg que viaja en el bundle (en Windows, las DLL del
  runtime de OBS) tiene el decodificador SpeedHQ y lo abre. Bajo Wine el mDNS
  no arranca (Wine no implementa ``MCAST_JOIN_GROUP``; Windows sí): el test lo
  tolera.
- `test_account_guides` — guías para conectar YouTube, Twitch y Facebook (la
  misma fuente de las pestañas y del MCP): cada paso con su enlace, campos del
  formulario con el secreto marcado, la URI de redirección de Facebook de esta
  instalación, y la búsqueda del JSON de Google en Descargas (solo el cliente
  «App de escritorio», el más nuevo).
- `test_clap_host` — host CLAP contra el LSP real: lista los ~200 plugins,
  activa el compresor, convierte texto ↔ valor, comprime al cambiar umbral y
  ratio (la cola de parámetros llega al audio), aplica cambios sin procesar
  (flush) y guarda/restaura el estado.
- `test_audio_dsp` — analizador (pico, RMS, LUFS de BS.1770 con un seno de
  1 kHz, espectro por tercios, correlación, piso de ruido y nivel activo) y
  generador (ruido blanco y rosa calibrados en RMS y con la pendiente
  correcta por tercios).
- `test_audio_rack` — el grafo: módulos en cadena, medidores (``cambioDb``),
  analizadores en ``in``/``out``, ramas en paralelo que se suman, ciclos
  rechazados, guardar y cargar con el mismo sonido, medición fuera de línea
  (curva de un compresor, respuesta plana y campana de +12 dB) y quitar un
  módulo puenteando el cable.
- `test_audio_modules` — el catálogo (gate, compresor, de-esser, EQ,
  multibanda, limitador): cada parámetro existe en el plugin LSP, los presets
  se aplican sin errores, el EQ de voz da la respuesta esperada (pasa altos,
  -2 dB en 250 Hz, +2 dB en 4 kHz), el umbral del gate queda en -45 dB y una
  opción inválida devuelve las válidas.
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
- `test_updater` — el actualizador reemplaza el programa y nunca los datos
  (`Config/`, `Credenciales/`, `Recordings/`...), respalda antes, vuelve atrás
  (`--update-rollback`) y rechaza carpetas que no son una versión, carpetas
  dentro del sistema y versiones dañadas (manifest), sin tocar nada.

Chequeo del bundle
------------------

`make release` corre `tools/deps/check-portable.sh release`: cada biblioteca que
pide cada ELF del bundle tiene que resolverse dentro del bundle (con el RUNPATH
del propio archivo) o ser de la base del sistema
(`tools/deps/linux-baseline.txt`).

Nota de plataforma
------------------

Todo lo que toca libobs, Qt, WASAPI/D3D, libwebrtc o Win32 se escribe con
`#ifdef _WIN32` y se valida de compilación en Linux, pero su comportamiento
real se verifica en Windows/MSVC. Por eso la estrategia es sacar adelante todo
el núcleo testeable en Linux mientras las piezas plataforma-gated esperan el
entorno correcto.

Invariantes de los agentes (pytest)
-----------------------------------

``tests/test_agent_pins.py`` revisa ``src/app/agent/agentes.json``: cada agente
tiene Linux y Windows con URL https, SHA-256 y el ejecutable correcto (``.exe``
en Windows), de la versión fijada; opencode tiene además su variante
``baseline`` (procesadores sin AVX2, que la app elige sola).

Tests de Windows (Wine)
-----------------------

``make test-win`` compila el árbol de Windows (``make win``: clang-cl con el
CRT y la STL de MSVC) y corre **todos** sus ``test_*.exe`` —los de la parte
``/MD`` y los del sub-build ``/MT`` (launcher, gateway, núcleo)— bajo un Wine
portable fijado por hash (``tools/ci/test-win.sh``). Incluye
``test_webrtc_loopback`` con el libwebrtc **real** de Windows (RTP/SRTP sobre
UDP local) y los del rack de audio contra el LSP de Windows compilado desde
fuente (``make deps-lsp-win``; ``test-win.sh`` le pasa la ruta en
``BA_LSP_CLAP``). Resultado de referencia: 58 pasan y 4 se saltean (los que
necesitan Python/red o SDK externos). Los tests de la parte ``/MT`` usan el CRT
de Microsoft enlazado estático: así se encontró que ``fopen(..., "wbx")``
aborta en Windows (ahora ``_wopen`` con ``_O_EXCL``), algo que el ``ucrtbase``
de Wine tapaba. Lo único que Wine no emula —que un segundo servidor no pueda
tomar un pipe con nombre ocupado (``FILE_FLAG_FIRST_PIPE_INSTANCE``)— se
saltea solo bajo Wine.
