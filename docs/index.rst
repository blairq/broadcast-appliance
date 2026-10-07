.. ============================================================================
   Broadcast Appliance Portable v2 — Documentación
   ============================================================================

..
   Página de entrada. El toctree referencia SOLO páginas que existen en este
   árbol (docs/modules/*.rst, docs/desarrollo/compilacion.rst, docs/desarrollo/tests.rst).

Broadcast Appliance Portable v2
===============================

Appliance de emisión en vivo embebido sobre **obs-studio / libobs**, con un
gateway **WebRTC** para invitados remotos y un **supervisor** (launcher) que
lanza, vigila y recupera los procesos. Todo el núcleo (`ba_core`) es **C++20
puro** (sin Qt, sin libobs) y testeable por unidad en Linux y Windows.

Esta documentación describe los módulos reales del árbol `src/` y sus clases/
namespaces, y cómo compilar y probar el proyecto. Los docstrings en español
que viven en los headers (comentarios de bloque) son extraíbles con Doxygen
(véase :ref:`doxygen`); el build de Sphinx no depende de ello.

.. toctree::
   :maxdepth: 1
   :caption: Contenido

   modules/core
   modules/launcher
   modules/gateway
   modules/app
   modules/obs-module
   desarrollo/compilacion
   desarrollo/tests

Visión general
--------------

El sistema se compone de un launcher (Proceso 0) y dos procesos hijos, más un
módulo de fuentes y un host de plugins:

- **Proceso 0 — `BroadcastAppliance`** (launcher/supervisor, Win32 puro, CRT
  estática `/MT`): instancia única, verificación de integridad, lanzamiento de
  B y luego A, watchdog (crash por handle + cuelgue por heartbeat), reinicio con
  backoff y circuit breaker, y cierre ordenado.
- **Proceso A — `ba-app`** (motor de emisión + UI de operación): Qt 6 sobre
  libobs, escenas, encoding y UI del operador.
- **Proceso B — `ba-gateway`** (gateway WebRTC + conversación + túnel):
  señalización, túnel (cloudflared) y motor de conversación para invitados.
- **`ba-sources`** (módulo de fuentes libobs, DLL en `obs-plugins/64bit/`):
  filtros de channel strip, fuentes IPC y automixer.
- **Rack de audio** (dentro de `ba-app`, `src/app/audio`): host CLAP para los
  módulos LSP, patchbay por fuente, analizador y generador.
- **`ba_core`** (núcleo, lib estática): IPC (rings de audio, salud, control,
  Spout), DSP (channel strip, automixer, resampler, presets), persistencia
  (snapshot, restore), seguridad (secrets, tokens), diagnóstico (logs, export),
  inyección de fallas, cripto (SHA-256, HMAC) y hardware (identidad de
  dispositivos).

Arquitectura
------------

Reglas de la arquitectura (spec §14, §3, §9):

- **C++20** obligatorio en todo el árbol.
- **`ba_core` es el núcleo**: SIN Qt, SIN libobs, testeable por unidad.
- El **launcher** (`BroadcastAppliance`) es Win32 puro con CRT estática (`/MT`).
- **A** (`ba-app`), **B** (`ba-gateway`) y el **módulo** (`ba-sources`) usan
  `/MD` (CRT dinámica, por consistencia con libobs/Qt).
- Los **PDBs** se emiten siempre (debug info) incluso en Release.

El árbol compila en **Windows/MSVC** (plataforma principal) y en **Linux**
(gcc/clang) para poder correr las pruebas unitarias de `ba_core`. Lo que toca
libobs, Qt, WASAPI/D3D, libwebrtc o Win32 se escribe con `#ifdef _WIN32` y se
valida de compilación en Linux, pero su comportamiento real se verifica en
Windows.

Layout de instalación (portable, espeja el de OBS — spec §5.1)::

  <prefix>/
  ├── bin/64bit/            # ejecutables y DLLs propias
  ├── obs-plugins/64bit/    # plugins de libobs (incluye ba-sources)
  ├── data/libobs/          # shaders (*.effect) de libobs
  ├── Config/               # configuración del usuario
  ├── Logs/                 # logs rotativos y dumps
  ├── Recordings/           # grabación de respaldo
  └── Web/guest/            # app web del invitado (la sirve B)

Módulos
-------

Las páginas de la sección :ref:`Contenido` documentan cada componente con sus
clases y namespaces reales:

- :doc:`modules/core` — `ba_core` (dsp, ipc, persist, security, diag, fault,
  crypto, hw).
- :doc:`modules/launcher` — `BroadcastAppliance` (supervisor + `RecoveryUi`).
- :doc:`modules/gateway` — `ba-gateway` (Proceso B, implementado; flag
  `BA_GATEWAY_HAVE_WEBRTC`).
- :doc:`modules/app` — `ba-app` (Proceso A, implementado; flag
  `BA_APP_HAVE_GUI`).
- :doc:`modules/obs-module` — `ba-sources` (módulo libobs, implementado; flag
  `BA_HAVE_LIBOBS`).

Estado
------

Los procesos están **implementados** y compilan (cross MinGW a Windows; el
núcleo además nativo en Linux). Ya no son esqueletos: `ba-app` (MainWindow +
OperatorEngine), `ba-gateway` (Webrtc/SignalingServer/Cloudflared/MediaPump/
TokenAuth), `ba-sources` (módulo libobs) y el rack de audio (host CLAP + módulos
LSP) están completos, y el launcher suma `RecoveryUi` (ventana
Win32 con `Incident` + `RecoveryAction`). Quedan por hacer los **tests de
integración** con libobs/libwebrtc reales (hoy el núcleo y el rack de audio
tienen unit tests en Linux) y el **packaging** final del bundle portable.

.. _doxygen:

Docstrings C++ (Doxygen)
------------------------

Los headers de `src/` llevan docstrings en español (comentarios de bloque) y
comentarios doxgen (`///`) en los símbolos. El archivo `Doxyfile` de la raíz
está configurado para extraerlos de `src/` a `docs/doxygen/` (`GENERATE_HTML=YES`,
`GENERATE_LATEX=NO`, `EXTRACT_ALL`). Para verlos:

1. Generar el XML/HTML: ``doxygen Doxyfile``.
2. (Opcional) Instalar `breathe` + `sphinxcontrib-breathe` para integrar los
   símbolos en esta documentación.

El build de Sphinx **no** depende de que Doxygen haya corrido: si falta el XML
o breathe, se construye solo con las páginas RST manuales.
