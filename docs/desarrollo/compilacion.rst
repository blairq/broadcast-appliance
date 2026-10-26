.. ============================================================================
   Compilación
   ============================================================================

..
   Página de compilación. Describe cómo configurar y compilar el proyecto
   (CMake, C++20) en Windows y Linux.

Compilación
===========

El proyecto se configura con **CMake** (mínimo 3.21) y exige **C++20** en todo
el árbol (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`,
extensiones off). Las dependencias de terceros se descargan y compilan dentro
del árbol de build con **FetchContent** (self-contained, sin instalación
global): `mbedTLS`, `nlohmann/json`, `libsamplerate` y `Argon2` (con
``extern/`` poblado por ``make deps`` el configure es offline).

Plataformas
-----------

- **Windows/MSVC** (plataforma principal): flags `/W4 /permissive- /utf-8 /EHsc`
  y `/MD` por defecto; el launcher se pasa a `/MT` (CRT estática). Los PDBs se
  emiten siempre.
- **Linux** (gcc/clang): `-Wall -Wextra`, y `-g` en Debug. Sirve para correr
  las pruebas unitarias de `ba_core`.

Configurar y compilar
---------------------

.. code-block:: bash

   cmake -S . -B build
   cmake --build build -j4

El build por defecto va a **Release** (el artefacto portable es Release).
Si no se pide nada, `CMAKE_BUILD_TYPE` se fija a `Release`.

Componentes
-----------

El `CMakeLists.txt` raíz añade estos subdirectorios, cada uno con su target:

- `src/core` → `ba_core` (lib estática, la base).
- `src/launcher` → `BroadcastAppliance` (ejecutable, solo real en Windows).
- `src/app` → `ba-app` (ejecutable, implementado; flag `BA_APP_HAVE_GUI`).
- `src/obs-module` → `ba-sources` (DLL libobs, implementado; flag
  `BA_HAVE_LIBOBS`).
- `src/gateway` → `ba-gateway` (ejecutable, implementado; flag
  `BA_GATEWAY_HAVE_WEBRTC`).

Versiones fijadas
-----------------

Las versiones de todas las dependencias de terceros se concentran en
`versions.cmake` (único lugar, spec §14 "Versiones fijadas") para que el build
sea reproducible y auditable. No deben cambiarse sin una decisión registrada.

Instalación (layout portable)
------------------------------

El layout espeja el portable de OBS (spec §5.1). `cmake --install` crea los
directorios del layout e instala los targets propios; los runtimes de terceros
(OBS, CEF, Qt, VC++, cloudflared) los copia `tools/deploy` sobre ese
esqueleto::

  <prefix>/
  ├── bin/64bit/            # ejecutables y DLLs propias
  ├── obs-plugins/64bit/    # plugins de libobs (incluye ba-sources)
  ├── data/libobs/          # shaders (*.effect) de libobs
  ├── Config/               # configuración del usuario
  ├── Logs/                 # logs rotativos y dumps
  ├── Recordings/           # grabación de respaldo
  └── Web/guest/            # app web del invitado (la sirve B)

Builds sin cambios (atajos)
---------------------------

Un build de Linux o Windows en Docker (``tools/ci/compilar-en-docker.sh``) sin
nada que compilar casi no rehace nada, y cada paso dice qué salteó y por qué
(líneas ``[atajo]``, ``[instalar]``, ``[tests-cache]``). Todo por **contenido**
(``tools/ci/huellas.py``), nunca por fechas:

- ``deps-win`` / ``deps-linux``: no se verifican si no cambiaron ``Makefile``,
  ``versions.cmake``, ``tools/deps/``, ``cmake/toolchains/`` ni lo que hay en
  ``extern/`` (qué dependencias y sus ``.version``). La clave queda en
  ``extern/.atajos/``.
- Tests: ver :doc:`tests` (caché de tests).
- ``release-win`` / ``release``: instalación incremental
  (``tools/ci/instalar-incremental.sh``: ``cmake --install`` sin ``rm -rf``; lo
  que ya no se instala sale comparando con el ``install_manifest`` anterior). El
  chequeo de portabilidad y el ``manifest.json``, solo si cambió el contenido del
  bundle.
- El ``.zip`` / ``.tar.gz``: solo si el bundle cambió desde el último (la clave,
  al lado: ``<archivo>.clave``); la copia a ``dist/`` de la Mac, solo si cambió su
  sha256; los símbolos de Windows, solo si cambió el bundle. El ``.commit`` se
  escribe siempre.

``SIN_CACHE=1`` hace todo de cero (``make publicar`` lo pone solo, y llega a Docker).
Si algo raro pasa con un atajo, ``SIN_CACHE=1`` y listo.
