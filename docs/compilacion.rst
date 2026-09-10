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
global): `mbedTLS`, `nlohmann/json`, `libsamplerate`, `Argon2` y —solo en
Windows— el VST3 SDK.

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
- `src/app` → `ba-app` (ejecutable, skeleton).
- `src/obs-module` → `ba-sources` (DLL libobs, skeleton).
- `src/gateway` → `ba-gateway` (ejecutable, skeleton).
- `src/plugin-host` → `ba_plugin_host` (lib estática).

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
