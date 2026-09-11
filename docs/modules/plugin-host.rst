.. ============================================================================
   ba_plugin_host — Host VST3 (LSP como componente CORE)
   ============================================================================

..
   Página del componente `ba_plugin_host` (src/plugin-host). Documenta el
   namespace ba::plugin y sus clases reales.

ba_plugin_host (host VST3)
==========================

`ba_plugin_host` es la librería estática que carga los plugins **VST3** (los
bundles `.vst3` de **LSP** que se despliegan en `assets/audio-plugins/`) y embebe su
**editor gráfico completo** en una ventana hija. Es componente **CORE** (spec
D6/§7.5): LSP ya no es opcional.

Regla de plataforma:

- **Linux**: build **OFFLINE**. No se trae ni compila el VST3 SDK; el módulo
  compila solo con `std::filesystem`. `scanDirectory()` es puro C++ y se testa
  en Linux; `load`/`attachEditor` son stubs que loguean.
- **Windows**: implementación REAL con los headers del VST3 SDK (MIT), traído
  por FetchContent **solo bajo `if(WIN32)`** en el `CMakeLists.txt` raíz.

Enlaza `nlohmann_json` (header-only) para el manifest; en Windows además
`base`/`pluginterfaces`/`sdk_hosting` del SDK.

.. code-block:: cpp

   namespace ba { namespace plugin { ... } }

`ba::plugin`
------------

- `BundleInfo` — Descripción de un bundle VST3 descubierto en disco. Un
  `.vst3` es un DIRECTORIO (bundle), no un archivo.
  - `name` (nombre corto, sin extensión), `bundlePath` (ruta absoluta del
    directorio), `dllPath` (ruta del binario con el entry point; vacío si no se
    pudo resolver).
- `scanDirectory(root)` — Escanea `root` UN NIVEL y devuelve los bundles VST3
  reales que contiene. Puro C++ (`std::filesystem`), cross-platform y
  testeable en Linux. Un bundle "real" es un directorio cuyo nombre termina en
  `.vst3` y que contiene un subdirectorio `Contents/`. No lanza; ante un
  directorio inexistible devuelve lista vacía.
- `Vst3Host` — Host de plugins VST3: ciclo de vida + embebido del editor
  gráfico. Move-only (no copiable).
  - `load(bundle)` — Abre el `.dll`, resuelve `ModuleEntry`, crea el
    `IAudioProcessor` y el `IEditControllerEx1` (el editor completo). Control
    path; en Windows es real (LoadLibraryW + QI), fuera es stub.
  - `unload()` — Descarga el plugin (Release de controller/processor,
    FreeLibrary). Idempotente.
  - `reset()` — Cierra el editor (si estaba abierto) y descarga el plugin.
  - `attachEditor(parentHwnd)` — Embebe el editor gráfico dentro de `parentHwnd`
    vía `IEditControllerEx1::createView` (requisito "toda la gráfica").
  - `isLoaded()` — true si hay un plugin cargado.
  - RT-safety: el `process()` del `IAudioProcessor` es el camino RT (lo invoca
    el Proceso A); `load`/`unload`/`reset`/`attachEditor` son control path y
    solo se llaman con el plugin sin procesar.
- `DeployResult` — Resultado de `PluginDeployer::deploy()`.
  - `copied` (bundles copiados), `manifestPath` (ruta del `manifest.json`).
- `PluginDeployer` — Paso de despliegue de los bundles LSP: copia los `.vst3`
  (versión fijada `LSP_VERSION`, ver `versions.cmake`) a `assets/audio-plugins/` y
  escribe el `manifest.json` que el host consume. Puro C++ (`std::filesystem` +
  `nlohmann/json`), cross-platform y testeable en Linux.
  - `deploy(sourceDir, installDir)` — Estática, idempotente. Control path (I/O
    de disco). No lanza: ante cualquier fallo de filesystem devuelve el conteo
    de lo copiado y la ruta del manifest.
