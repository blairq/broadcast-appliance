# Broadcast Appliance Portable v2

Aplicativo de emisión en vivo (broadcast) embebido sobre **obs-studio / libobs**,
con un **gateway WebRTC** para invitados remotos y un **supervisor** (launcher)
que lanza, vigila y recupera los procesos. El corazón del sistema —`ba_core`— es
**C++20 puro** (sin Qt, sin libobs), por lo que compila y se prueba por unidad en
**Linux** y en **Windows**; las piezas que dependen de la plataforma (libobs, Qt,
WASAPI/D3D, libwebrtc, Win32) se escriben con `#ifdef _WIN32` y se validan de
compilación en Linux, verificándose su comportamiento real en Windows. El host de
plugins **LSP/VST3** es un componente **CORE** del producto, no una opción.

[![CI](https://github.com/actions/workflows/ci.yml/badge.svg)](#tests)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Licencia](https://img.shields.io/badge/licencia-GPL--3.0-blue.svg)](#licencia)

> La documentación completa (módulos, clases y namespaces reales) está en
> [`docs/`](docs/) — generada con Sphinx y publicada en ReadTheDocs.

---

## Características

- **Supervisor / launcher** (`BroadcastAppliance`, Proceso 0): instancia única,
  verificación de integridad (SHA-256), lanzamiento ordenado de B y A, watchdog
  (crash por handle + cuelgue por heartbeat), reinicio con backoff y circuit
  breaker, shadow-copy y cierre ordenado.
- **Gateway WebRTC** (`ba-gateway`, Proceso B): señalización segura (tokens HMAC,
  rate-limit, expiración), túnel cloudflared, motor de conversación y app web del
  invitado. *(plataforma-gated: libwebrtc — Windows.)*
- **DSP nativo** (canal de audio): **channel strip** HPF → EQ 4 bandas →
  de-esser → gate → compresor → limitador, **automixer** tipo Dugan (mesa
  redonda) y **resampling adaptativo** (`ba::dsp::Resampler` sobre
  libsamplerate). Presets DSP como sets de parámetros.
- **Host de plugins VST3/LSP** con **editor gráfico completo** (`ba_plugin_host`):
  escaneo de bundles `.vst3` (C++ puro, testeado en Linux) y carga real +
  embebido del editor en Windows. LSP se empaqueta como asset en
  [`assets/audio-plugins/`](assets/audio-plugins/).
- **Secretos cifrados**: `Config/secrets.json` con **AES-256-GCM** (mbedTLS) +
  KDF **Argon2id** (referencia P-H-C).
- **Snapshots y restauración** (`ba::persist`): persistencia con debounce, doble
  generación, checksum, y flujos de recuperación.
- **Diagnóstico** (`ba::diag`): logs rotativos + exportación. *(Minidumps:
  plataforma-gated — Windows.)*
- **Inyección de fallas** (`ba::fault::FaultInjector`) para endurecimiento y
  pruebas de recuperación.

---

## Arquitectura

El sistema se compone de un launcher (Proceso 0), dos procesos hijos y un módulo
de fuentes, más el host de plugins. Todo el núcleo es una lib estática sin
dependencias de UI o de libobs.

- **Proceso 0 — `BroadcastAppliance`** (launcher/supervisor, Win32 puro, CRT
  estática `/MT`): instancia única, verificación de integridad, lanzamiento de B
  y luego A, watchdog (crash por handle + cuelgue por heartbeat), reinicio con
  backoff y circuit breaker, y cierre ordenado. En Linux compila como stub; el
  código Win32 real está completo.
- **Proceso A — `ba-app`** (motor de emisión + UI de operación): Qt 6 sobre
  libobs, escenas, encoding y UI del operador. *(plataforma-gated: Qt + libobs —
  Windows.)*
- **Proceso B — `ba-gateway`** (gateway WebRTC + conversación + túnel):
  señalización, túnel (cloudflared) y motor de conversación para invitados.
  *(plataforma-gated: libwebrtc — Windows.)*
- **`ba-sources`** (módulo de fuentes libobs, DLL en `obs-plugins/64bit/`):
  filtros de channel strip, fuentes IPC y automixer. *(plataforma-gated: libobs
  — Windows.)*
- **`ba_plugin_host`** (host VST3/LSP, en [`src/plugin-host`](src/plugin-host/)):
  carga los bundles `.vst3` de LSP y embebe su editor gráfico completo. El
  escaneo (`scanDirectory`) es C++ puro (`std::filesystem`, cross-platform,
  testeado en Linux); la carga real del `.vst3` y el embebido del editor viven en
  `#ifdef _WIN32`.
- **`ba_core`** (núcleo, lib estática, en [`src/core`](src/core/)): **sin Qt, sin
  libobs**, testeable por unidad. Submódulos:
  - `ipc/` — rings de audio compartidos, salud, canal de control, transporte Spout.
  - `dsp/` — channel strip, automixer, resampler, biquads, presets.
  - `persist/` — snapshot, restauración, documentos de estado.
  - `security/` — secrets (AES-GCM + Argon2id), tokens de señalización.
  - `diag/` — logs rotativos y exportación.
  - `fault/` — inyección de fallas.
  - `crypto/` — SHA-256, HMAC.
  - `hw/` — identidad de dispositivos de audio.

Los bundles de **LSP** (asset de despliegue, no se compila) viven en
[`assets/audio-plugins/`](assets/audio-plugins/) y son poblados por el paso de deploy
(`ba::plugin::PluginDeployer::deploy`), que además reescribe `manifest.json`.

Reglas de la arquitectura (spec §14, §3, §9):

- **C++20** obligatorio en todo el árbol.
- **`ba_core` es el núcleo**: SIN Qt, SIN libobs, testeable por unidad.
- El **launcher** es Win32 puro con CRT estática (`/MT`).
- **A**, **B** y el **módulo** usan `/MD` (CRT dinámica, por consistencia con
  libobs/Qt).
- Los **PDBs** se emiten siempre (debug info) incluso en Release.

---

## Estado

Los procesos están **implementados** y compilan (cross MinGW a Windows; el
núcleo además nativo en Linux). Ya no son esqueletos:

- **`ba-app`** (Proceso A): `MainWindow` (QMainWindow del operador) +
  `OperatorEngine` (integra `ba_core::dsp` ChannelStrip/Automixer + presets,
  IPC SharedAudioRing/HealthBlock/ControlEndpoint y persistencia
  SnapshotStore). Feature-flag `BA_APP_HAVE_QT_OBS` (headless si no hay
  Qt6/libobs).
- **`ba-gateway`** (Proceso B): `Webrtc` (sesiones + hub), `SignalingServer`
  (HTTP + WebSocket), `Cloudflared` (túnel), `MediaPump` (IPC ↔ WebRTC) y
  `TokenAuth` (sobre `ba_core::security::TokenService`). Sirve `Web/guest/`
  (index.html, app.js, style.css). Feature-flag `BA_GATEWAY_HAVE_WEBRTC`.
- **`ba-sources`** (módulo libobs): `ba_sources.cpp` con `obs_module_register`,
  una fuente y un filtro basados en `ba_core::dsp` y presets
  podium/lavalier/handheld. Stubs locales en `src/obs-module/obs/`.
  Feature-flag `BA_SOURCES_HAVE_OBS`.
- **`ba_plugin_host`** (VST3/LSP): `Vst3Host` (load + attachEditor),
  `PluginDeployer` (deploy + manifest) y `scanDirectory`. Feature-flag
  `BA_PLUGIN_HOST_HAVE_VST3`.
- **`BroadcastAppliance`** (launcher): `Supervisor` + `RecoveryUi` (ventana
  Win32 con `Incident` + `RecoveryAction`).

Queda por hacer:

- **Tests de integración** con libobs/libwebrtc reales (hoy el núcleo y el
  host de plugins tienen unit tests en Linux; el comportamiento real de
  libobs/libwebrtc se verifica en Windows).
- **Packaging** final del bundle portable (runtime de terceros vía
  `tools/deploy`).

---

## Requisitos de build

Todo lo que hay que tener instalado para compilar, probar y linterizar el
repo. Las **dependencias de código** (mbedTLS, nlohmann, libsamplerate, Argon2,
VST3 SDK, libobs, Qt, CEF, libwebrtc, cloudflared) NO se instalan a mano: el
build las trae por `FetchContent` (self-contained) en la versión fijada en
[`versions.cmake`](versions.cmake). Lo de esta sección es la **toolchain** del
host.

Los **assets de despliegue** de Windows (los del bloque "plataforma-gated" de
[Dependencias](#dependencias): OBS 31.0.0, libwebrtc M123, Spout2, cloudflared,
VC++ redist y LSP) no se instalan a mano ni por `FetchContent`: `make deps` los
descarga a `extern/` (gitignored, idempotente, verificado por SHA-256). Hay
targets individuales `make deps-obs`, `deps-webrtc`, `deps-spout`,
`deps-cloudflared`, `deps-vcredist` y `deps-lsp` (y `deps-status`/`deps-clean`).

### Herramientas obligatorias

| Herramienta | Mínimo | Notas |
|---|---|---|
| **CMake** | 3.21 | `cmake_minimum_required(VERSION 3.21)` en el CMakeLists raíz. |
| **Compilador C++20** | gcc ≥ 13 / clang ≥ 15 / MSVC 2022 | C++20 obligatorio en todo el árbol. En Linux nativo: `g++` (gcc). |
| **GNU Make** | 3.81 | El [`Makefile`](Makefile) envuelve a CMake. |
| **git** | — | `FetchContent` baja las deps por git; se necesita en el PATH. |

> **Referencia de este host** (Ubuntu 24.04 / noble, donde el verde está
> verificado): gcc/g++ 13.3.0, CMake 3.28.3, GNU Make 4.3, git 2.43.0.

### Herramientas por tarea (opcionales según lo que hagas)

| Tarea | Herramienta | Cómo se obtiene |
|---|---|---|
| **Lint** (`make lint`) | `clang-format` | `sudo apt install clang-format` (noble → 18.1.3). El Makefile usa el binario de la distro y **no descarga nada**; si no está, `make lint` falla con un mensaje claro. La versión esperada queda fijada en `versions.cmake` (`CLANG_FORMAT_VERSION`) y CI la aserta. |
| **Suite Python** (`pytest`) | Python ≥ 3.10 + `venv` | `.venv` local (nunca global) con [`requirements-dev.txt`](requirements-dev.txt) (pytest). |
| **Docs** (`make docs`) | Python + Sphinx + sphinx-rtd-theme | Se instala en el `.venv` desde `docs/requirements.txt`. |
| **Caché de build** (acelera) | `ccache` | Opcional: si está en el PATH, CMake lo usa como launcher de C/C++ automáticamente. |
| **Cross Windows** (`make win`) | MinGW-w64 (`x86_64-w64-mingw32-g++`) | Opcional: solo para cross-compilar a Windows desde Linux. |

### Reglas del entorno de desarrollo

- **Sin instalaciones globales**: las deps de Python van en un venv local
  (`.venv/`); las de C++ las trae `FetchContent` dentro del árbol de build.
  `clang-format` y `ccache` son las únicas herramientas del sistema que se
  asumen instaladas por apt.
- **C++20** obligatorio; el build falla si el compilador no lo soporta.
- El build es **self-contained**: no hay `Dockerfile` ni `.devcontainer`;
  basta el toolchain de la tabla anterior.

---

## Compilación

El build es **CMake** (C++20, self-contained vía `FetchContent`); el
[`Makefile`](Makefile) lo envuelve para que el flujo sea simple. `ccache` se
activa solo si está en el PATH.

### (a) Linux nativo — desarrollo

```bash
make dev      # build de desarrollo (Debug) en build/
# o
make build    # configura (idempotente) y compila (build type por defecto: Release)
```

### (b) Producción — bundle portable

```bash
make prod     # Release + instala el layout §5.1 en release/
```

### (c) Windows cross con MinGW-w64

```bash
make win      # cross-compile a build/win/ si existe x86_64-w64-mingw32-g++
```

Solo si el cross-compiler está disponible; si no, avisa y termina en verde. El
cross-build va a `build/win/` (dentro del root de build, aislado por subdirectorio). En CI se usa
`CMAKE_SYSTEM_NAME=Windows` con los binarios `x86_64-w64-mingw32-*` y verifica
que se generen `.exe`/`.dll`.

### (d) Documentación

```bash
make docs     # Sphinx -> docs/_build/html (usa un venv local .venv/)
```

Genera el sitio HTML con Sphinx + sphinx-rtd-theme desde `docs/`. La integración
con Doxygen (breathe) es opcional y no es requerida por el build.

---

## Tests

Dos suites:

- **C++ (ctest)** — unit tests de `ba_core` (y del host de plugins) en Linux:

  ```bash
  make test             # o: ctest --test-dir build --output-on-failure
  ```

  **18/18** tests: `test_shared_audio_ring`, `test_health_block`,
  `test_control_protocol`, `test_spout_transport`, `test_shadow_copy`,
  `test_channel_strip`, `test_token_service`, `test_snapshot_store`,
  `test_device_match`, `test_automixer`, `test_vst3_host`, `test_plugin_deploy`,
  `test_resampler`, `test_secrets`, `test_restore`, `test_diag`, `test_fault`,
  `test_ring_drift`.

- **Python (pytest)** — suite de invariantes del repositorio en
  [`tests/`](tests/) (docs, packaging, bundle LSP, tests C++):

  ```bash
  python3 -m venv .venv
  .venv/bin/pip install -r requirements-dev.txt
  .venv/bin/pytest -q
  ```

El flujo completo (build nativo + ctest + pytest + cross Windows) corre en CI
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)).

---

## Empaquetado

- **Bundle portable** (layout §5.1, espeja el portable de OBS):

  ```
  <prefix>/
  ├── bin/64bit/            # ejecutables y DLLs propias
  ├── obs-plugins/64bit/    # plugins de libobs (incluye ba-sources)
  ├── data/libobs/          # shaders (*.effect) de libobs
  ├── Config/               # configuración del usuario
  ├── Logs/                 # logs rotativos y dumps
  ├── Recordings/           # grabación de respaldo
  └── Web/guest/            # app web del invitado (la sirve B)
  ```

  Se produce con `make prod` / `make release` (instala el esqueleto en
  `release/`). Los runtime de terceros (OBS, CEF, Qt, VC++, cloudflared)
  los copia `tools/deploy` sobre este esqueleto.

- **Flatpak** (Linux): manifiesto en
  [`packaging/flatpak/org.legis.broadcastappliance.json`](packaging/flatpak/org.legis.broadcastappliance.json)
  (runtime `org.freedesktop.Platform` 23.08, C++20, módulos mbedtls/nlohmann/
  libsamplerate/argon2) y `.desktop` en
  [`packaging/BroadcastAppliance.desktop`](packaging/BroadcastAppliance.desktop).

---

## Estructura del proyecto

```
.
├── CMakeLists.txt            # build raíz (C++20, FetchContent, layout §5.1)
├── versions.cmake            # versiones FIJADAS de dependencias de terceros
├── Makefile                  # envuelve a CMake (dev/build/prod/docs/test/release/win)
├── pyproject.toml            # metadatos + config de pytest (suite de invariantes)
├── requirements-dev.txt      # pytest (para la suite Python)
├── readthedocs.yaml          # config de ReadTheDocs (Sphinx)
├── Doxyfile                  # extracción de docstrings C++ (opcional)
├── src/
│   ├── core/                 # ba_core — núcleo C++20 (sin Qt/libobs)
│   │   ├── ipc/              #   rings de audio, salud, control, Spout
│   │   ├── dsp/              #   channel strip, automixer, resampler, presets
│   │   ├── persist/          #   snapshot, restauración, estado
│   │   ├── security/         #   secrets (AES-GCM+Argon2id), tokens
│   │   ├── diag/             #   logs rotativos, export
│   │   ├── fault/            #   inyección de fallas
│   │   ├── crypto/           #   SHA-256, HMAC
│   │   ├── hw/               #   identidad de dispositivos de audio
│   │   └── tests/            #   unit tests (18)
│   ├── launcher/             # BroadcastAppliance — supervisor + RecoveryUi (Win32, /MT)
│   ├── app/                  # ba-app — Proceso A (Qt + libobs, flag BA_APP_HAVE_QT_OBS)
│   ├── gateway/              # ba-gateway — Proceso B (WebRTC, flag BA_GATEWAY_HAVE_WEBRTC)
│   ├── obs-module/           # ba-sources — módulo libobs (flag BA_SOURCES_HAVE_OBS)
│   └── plugin-host/          # ba_plugin_host — host VST3/LSP + PluginDeployer
├── assets/                   # assets de despliegue (no se compilan)
│   ├── web/guest/            #   app web del invitado (la sirve B)
│   └── audio-plugins/        #   bundles .vst3 de LSP 1.2.35
├── packaging/                # flatpak (manifiesto) + .desktop
├── docs/                     # documentación Sphinx (módulos, compilación, tests)
├── tests/                    # suite Python de invariantes del repo (pytest)
└── .github/workflows/ci.yml  # CI: linux (ctest+pytest) + windows-cross + release
```

---

## Dependencias

Versiones **fijadas** en [`versions.cmake`](versions.cmake) (único lugar donde se
pinnan; el build las consume para ser reproducible y auditable). "Núcleo" = se
compila en Linux y Windows; "plataforma-gated (Windows)" = solo se trae/usa bajo
`if(WIN32)`.

| Dependencia | Versión fijada | Licencia | Alcance |
|---|---|---|---|
| mbedTLS | `v3.6.2` | Apache-2.0 | Núcleo (cripto: AES-GCM, SHA-256, HMAC) |
| nlohmann/json | `v3.11.3` | MIT | Núcleo (JSON header-only) |
| libsamplerate (Secret Rabbit Code) | `0.2.2` | MIT | Núcleo (resampling, spec 7.4) |
| Argon2 (referencia P-H-C) | `20190702` | CC0 / Apache-2.0 | Núcleo (KDF Argon2id de `secrets.json`) |
| VST3 SDK (Steinberg) | `v3.7.9_build_61` | MIT | Host de plugins (solo Windows) |
| LSP Plugins | `1.2.35` | LGPL | Asset de despliegue en `assets/audio-plugins/` |
| obs-studio (libobs) | `31.0.0` | GPL-2.0 | Plataforma-gated (Windows) |
| Qt 6 | `6.6.3` | LGPL-3.0 (dual) | Plataforma-gated (Windows) |
| CEF (Chromium Embedded) | `119.0.11+chromium-119.0.6045.105` | BSD-3-Clause | Plataforma-gated (Windows) |
| Spout2 (SDK) | `2.0.0.0` | — | Plataforma-gated (Windows) |
| libwebrtc | `M123` (`123.0.6312.5`) | BSD-3-Clause | Plataforma-gated (Windows) |
| cloudflared | `2024.11.0` | — | Plataforma-gated (Windows) |

> Las 6 primeras son las que el núcleo consume de forma incondicional (o como
> asset). Las del bloque "plataforma-gated (Windows)" solo se enlazan bajo
> `if(WIN32)` y su comportamiento real se verifica en Windows.

---

## Licencia

El código propio del proyecto se distribuye bajo **GNU GPL-3.0** (ver
[`LICENSE`](LICENSE)); el titular es **Legis Studio**. Las dependencias de
terceros conservan sus licencias propias (ver tabla de
[Dependencias](#dependencias)): mbedTLS (Apache-2.0), nlohmann/json (MIT),
libsamplerate (MIT), Argon2 (CC0/Apache-2.0), VST3 SDK (MIT) y LSP (LGPL), entre
otras.

---

## Contribución

El flujo de desarrollo se basa en CMake + el `Makefile`:

```bash
make dev      # build de desarrollo (Debug)
make test     # corre ctest (18 tests de ba_core)
make lint     # clang-format sobre src/ (requiere clang-format en el sistema)
```

Para la suite de invariantes del repositorio (Python):

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/pytest -q
```

Antes de enviar cambios:

1. `make build` debe compilar sin errores (C++20).
2. `make test` debe pasar **18/18**.
3. `pytest` debe pasar la suite de `tests/`.
4. Si tocas `versions.cmake`, documenta el cambio en el changelog y re-valida el
   preflight contra el tag resultante.

Las piezas plataforma-gated (libobs, Qt, WASAPI/D3D, libwebrtc, Win32) se
validan de compilación en Linux y su comportamiento real en Windows/MSVC.
