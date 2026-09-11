# TODO — Broadcast Appliance v2 (estado de corte: 2026-09-11)

Documento de arranque para sesiones nuevas. Contiene: qué se hizo, qué falta
(placeholders y paths sin validar) y cómo se construye en este entorno.

## 1. Estado actual (verde)

- **Commits base**: `ddf32fb` (implementación de procesos + reorden) y
  `de0ad80` (consolidar `assets/` + cross-build en `build/win`).
- **Tree profesional**: 7 dirs en top-level (`assets/ build/ docs/ packaging/
  release/ src/ tests/`), un solo root de build, un solo `README.md`,
  `LICENSE` GPL-3.0 (Legis Studio), `.clang-format`, `requirements-dev.lock`.
- **Builds verdes**:
  - Nativo Linux: `make build` 100%.
  - Cross MinGW: `cmake --build build/win` exit 0 (5 artefactos de producto).
  - `make test` → **20/20 ctest** · `pytest` → **22/22**.
  - `make release` → bundle portable en `release/` (layout §5.1).
   - Smoke test headless `ba-app.exe` bajo wine: exit 0.
   - **2026-09-11**: `tools/deploy` creado; `writeMinidump()` real (Windows);
     job `lint` en CI + 3 warnings limpios + formato de `src/` normalizado
     (`.clang-format` corregido para 18.1.3); `pyproject.toml` renombrado a
     `broadcast-appliance`; sección «Requisitos de build» en README.

## 2. Qué falta (placeholders y paths nunca compilados)

### 2.1 Paths reales OFF en este entorno (feature-flags)
| Flag | Qué no se valida | Dónde |
|---|---|---|
| `BA_APP_HAVE_QT_OBS` | `MainWindow` + `obs_startup` solo se compilan con Qt6+libobs presentes | `src/app/` |
| `BA_GATEWAY_HAVE_WEBRTC` | ~15 marcadores `VERIFY` en `Webrtc.cpp`: includes y firmas de libwebrtc M123 sin contrastar contra el SDK real | `src/gateway/Webrtc.cpp` |
| `BA_SOURCES_HAVE_OBS` | `obs-stub.cpp` con 10 símbolos no-op; solo se activa con libobs 31 real | `src/obs-module/` |

Acción: en un entorno con Qt6 + libobs 31 + libwebrtc M123 reales, encender los
flags, compilar, ejecutar y quitar los `VERIFY`.

### 2.2 Stubs explícitos
- `main` no-Win del launcher — stub por diseño (valida compilación en Linux).

(`writeMinidump()` de `src/core/diag/DiagExport.cpp` quedó **implementado de
real** el 2026-09-11: carga `dbghelp.dll` a runtime y llama a
`MiniDumpWriteDump` con `MiniDumpWithHeapSnapshot | MiniDumpWithThreadInfo |
MiniDumpWithStackWalk`; solo se compila en Windows. Hecho.)

### 2.3 Marcadores `VERIFY` externos pendientes
- `src/gateway/Cloudflared.cpp:153,222` — flags y endpoint de salud de
  cloudflared 2024.11.0 sin contrastar.
- `src/gateway/CMakeLists.txt:93` — nombre exacto del `.a` del webrtc-build.
- `src/core/ipc/SpoutTransport.cpp:43` — ruta/firma del header Spout2.

### 2.4 Tests de integración
- `test_gateway_integration` y `test_ba_sources_load` corren **SKIP**
  (`SKIP: no libwebrtc` / `SKIP: no libobs`) hasta que lleguen las libs
  reales. El código del path ON ya está escrito y registrado en ctest.

### 2.5 Packaging
- Flatpak: solo validado **estáticamente** (no hay `flatpak-builder` en el
  host). Pendiente: build real del manifiesto.

(`tools/deploy` quedó **creado** el 2026-09-11: copia runtimes de terceros
sobre el bundle portable vía env `BA_DEPLOY_*`, idempotente, respeta el layout
§5.1 y escribe `manifest.json` con SHA-256. Hecho.)

### 2.6 Calidad
- **Hecho (2026-09-11)**: job `lint` en CI (`.github/workflows/ci.yml`,
  `runs-on: ubuntu-24.04` fijado + `apt install clang-format` + asert del pin
  `CLANG_FORMAT_VERSION` de `versions.cmake`). `make lint` usa el clang-format
  de la distro (no descarga nada) y **falla** si no está. Los 3 warnings
  propios quedaron limpios (`-Wcast-function-type` en `Vst3Host.cpp`,
  `-Wparentheses` en `SignalingServer.cpp`, `-Wunused-function` en `main.cpp`
  vía `[[maybe_unused]]`). Se normalizó el formato de `src/` con el
  `.clang-format` corregido para 18.1.3 (excluye `src/obs-module/obs/`, que es
  VENDOR).

### 2.7 Trivia
- ~~`pyproject.toml` seguía nombrando `"legis-studio"`~~ — renombrado a
  `broadcast-appliance` (2026-09-11).

## 3. Orden sugerido de ataque

1. Entorno con Qt6 + libobs + libwebrtc reales → compilar y ejecutar los 3
   paths con flags ON + tests de integración (2.1, 2.3, 2.4). **Único grande
   pendiente** (requiere las libs reales).
2. ~~`tools/deploy` (2.5)~~ — hecho 2026-09-11.
3. ~~Minidump real (2.2)~~ — hecho 2026-09-11.
4. ~~Job de lint en CI + limpiar los 3 warnings (2.6)~~ — hecho 2026-09-11.
5. ~~Renombrar `legis-studio` → `broadcast-appliance` en `pyproject.toml`
   (2.7)~~ — hecho 2026-09-11.

## 4. Notas de build para este host (Linux)

- **Cross MinGW va envuelto por wine**: `export PATH="/tmp/opencode/mingw:/tmp/opencode/mingw/wrap:$PATH"`.
  Emite rutas `Z:/` que rompen GNU make → usar **Ninja**:
  ```
  cmake -S . -B build/win -G Ninja -DCMAKE_MAKE_PROGRAM=/tmp/opencode/mingw/ninja \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_BUILD_TYPE=Release
  ```
  Reutilizar deps ya descargadas: `-DFETCHCONTENT_SOURCE_DIR_{MBEDTLS,NLOHMANN_JSON,LIBSAMPLERATE,ARGON2}=$PWD/build/_deps/*-src`
  y `-DFETCHCONTENT_SOURCE_DIR_VST3SDK=/tmp/opencode/vst3sdk-inspect`.
- **Los samples del VST3 SDK** (`sdk`, `validator`, `moduleinfotool`) están
  `EXCLUDE_FROM_ALL` en el CMakeLists raíz: `std::aligned_alloc` no existe en
  el libstdc++ de MinGW. No quitar.
- **Ejecutar `ba-app.exe` bajo wine**: copiar `libgcc_s_seh-1.dll`,
  `libstdc++-6.dll`, `libwinpthread-1.dll` (de `/tmp/opencode/mingw/mingw64/bin`)
  junto al `.exe` (wine no busca en el PATH de Unix).
- **Layout runtime §5.1** (`Web/guest` en el bundle, `/app/Web/guest` en
  Flatpak) NO se renombra: solo la ruta de FUENTE cambió a `assets/web/guest`.
- Suite de invariantes: `.venv/bin/pytest -q` (22 tests; `test_lsp_bundle.py`
  mira `assets/audio-plugins/lsp-plugins.vst3`).
