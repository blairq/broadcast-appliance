# Broadcast Appliance v2 — Tablero de tareas (post-compaction)

> **Para qué sirve este archivo.** Es la fuente de verdad del PROGRESO. Si la
> sesión se compacta, leé esto primero: te dice qué está verificado, qué queda,
> y cuál es el próximo paso. La especificación completa está en
> `docs/prompts/primer_prompt.md` (secciones 1–17). Este tablero NO la
> repite: solo rastrea estado.

## Cómo leer los estados
- `[HECHO]` — implementado y verificado (compila + test verde en Linux).
- `[EN CURSO]` — delegado/abierto ahora mismo.
- `[COLA]` — pendiente, ordenado por riesgo.
- `[GATED:Win]` / `[GATED:libobs]` / `[GATED:Qt]` / `[GATED:webrtc]` — requiere
  plataforma/librería que NO está en este entorno Linux; se verifica en Windows.

## Restricción de plataforma (clave)
Este entorno es **Linux/gcc**. `ba_core` (C++ puro, sin Qt/libobs) compila y sus
tests corren aquí. Todo lo que toque **libobs, Qt, WASAPI/D3D, libwebrtc o
Win32** se escribe con `#ifdef _WIN32` y se valida de compilación en Linux, pero
su comportamiento real se verifica en Windows/MSVC. Por eso la estrategia es:
**sacar adelante todo el núcleo testeable en Linux** mientras las piezas
plataforma-gated esperan el entorno correcto.

## Estado verificado (último build conocido bueno)
```
cmake -S . -B build        -> exit 0
cmake --build build -j4    -> exit 0
ctest --test-dir build     -> 18/18 Passed
```
Tests verdes: `test_shared_audio_ring`, `test_health_block`,
`test_control_protocol`, `test_spout_transport`, `test_shadow_copy`,
`test_channel_strip`, `test_token_service`, `test_snapshot_store`,
`test_device_match`, `test_automixer`, `test_vst3_host`,
`test_plugin_deploy`, `test_resampler`, `test_secrets`,
`test_restore`, `test_diag`, `test_fault`, `test_ring_drift`.

## Tablero por fase

### Fase 0 — Spike de despliegue
| Tarea | Estado |
|---|---|
| libobs embebido en ventana Qt mínima + display + capturadora + RTMPS | `[COLA][GATED:libobs][GATED:Qt]` |
| Despliegue a disco + VM limpia usuario estándar | `[COLA][GATED:Win]` |

### Fase 1 — Núcleo IPC y supervisión
| Tarea | Estado |
|---|---|
| (a) Árbol + CMake (raíz, versions.cmake, esqueletos, install 5.1) | `[HECHO]` |
| (b) `ba_core/ipc/SharedAudioRing` (4.3) + tests concurrencia/wrap/reinicio | `[HECHO]` |
| (c) `ba_core/ipc/HealthBlock` (4.4) + tests | `[HECHO]` |
| (d) Launcher completo (3.1) salvo shadow/recovery | `[HECHO]` |
| `ShadowCopy` real (3.1.5) + test | `[HECHO]` |
| `RecoveryUi` real (3.1.11) | `[HECHO]` |
| Canal de control (4.1) `ControlProtocol` + test | `[HECHO]` |
| Transporte Spout (4.2) `SpoutTransport` + test | `[HECHO]` |
| `fake-gateway` (patrón de video + tono) | `[HECHO]` |
| Fuentes IPC en `ba-sources.dll` que consumen rings/Spout/control | `[COLA][GATED:libobs]` |
| 100 kills de A con reenganche < 1 s | `[COLA][GATED:libobs]` (integración) |
| Sin deriva de rings en 8 h | `[HECHO]` (proxy acotado `test_ring_drift`: muchos wraps, sin pérdida/deriva; el soak literal de 8 h queda como integración) |

### Fase 2 — Gateway mínimo
| Tarea | Estado |
|---|---|
| Señalización + túnel + TURN, 1 invitado, retorno N−1, motor conversación | `[COLA][GATED:webrtc]` |

### Fase 3 — Hardware y wizard
| Tarea | Estado |
|---|---|
| `DeviceMatcher` scoring (9.4) + tests | `[EN CURSO]` |
| `DeviceIdentityResolver` (9.2, walk cfgmgr32) + tests del algoritmo | `[EN CURSO]` |
| `AudioEndpointEnumerator`/`DeviceWatcher`/`TestToneGenerator` (9.1/9.6/9.7) | `[COLA][GATED:Win]` |
| Wizard/preflight UI (10) | `[COLA][GATED:Qt]` |

### Fase 4 — Audio del stream
| Tarea | Estado |
|---|---|
| Channel strip nativo (7.2): HPF→EQ→de-esser→gate→comp→limiter | `[HECHO]` |
| Presets DSP (7.3) como sets de parámetros | `[HECHO]` |
| Automixer Dugan (7.3 mesa redonda) | `[HECHO]` |
| Resampling adaptativo (7.4): `ba::dsp::Resampler` (libsamplerate) | `[HECHO]` |
| Host VST3/LSP (7.5, **AHORA CORE**): scanner de bundles + carga + editor GUI completo | `[HECHO]` (núcleo Linux: `ba_plugin_host` + `scanDirectory`) / `[GATED:Win]` (carga real del `.vst3` + embebido del editor) |
| Deploy de LSP a `AudioPlugins/` (7.5): `PluginDeployer` + `manifest.json` | `[HECHO]` |
| Patch P1 (bus DSP) + buses físicos | `[COLA][GATED:libobs]` |
| Patchbay (6.7) | `[COLA][GATED:Qt]` |

### Fase 5 — Operación
| Tarea | Estado |
|---|---|
| UI de operación, escenas, zócalos, proyectores, pánico, monitor | `[COLA][GATED:Qt][GATED:libobs]` |

### Fase 6 — Endurecimiento
| Tarea | Estado |
|---|---|
| Snapshot/persistencia (11.1/11.2): debounce, doble gen, checksum, fault-injection | `[HECHO]` |
| Restauración (11.3) + flujos de recuperación: `ba::persist::Restore` (restore/recover) | `[HECHO]` |
| Diagnóstico (11.4): logs rotativos + exportar (`ba::diag`); minidumps `[GATED:Win]` | `[HECHO]` |
| Inyección de fallas (13): `ba::fault::FaultInjector` | `[HECHO]` |

### Fase 7 — Gateway completo
| Tarea | Estado |
|---|---|
| N invitados, AEC afinado, estadísticas, app del invitado pulida | `[COLA][GATED:webrtc]` |

### Seguridad (12)
| Tarea | Estado |
|---|---|
| Señalización segura (8.3): token HMAC, rate-limit, expiración | `[HECHO]` |
| `secrets.json` (12) + endurecimiento: `ba::security::SecretsStore` (AES-256-GCM + Argon2id) | `[HECHO]` |

## Cola "next up" (riesgo primero, verificable en Linux)
1. Integración: cablear `Resampler`/`SecretsStore`/`Restore`/`FaultInjector` en los call sites reales del runtime (hoy son módulos unitariados; falta el uso en el flujo).
2. `src/core/hw/` — DeviceMatcher (9.4) + DeviceIdentityResolver (9.2): el algoritmo ya tiene test verde; queda el walk cfgmgr32 real (GATED:Win).
3. Todo lo demás restante es GATED (libobs/Qt/webrtc/Win): fuentes IPC en `ba-sources.dll`, patchbay, UI de operación, gateway webrtc.

## Lista de VERIFY acumulada (resolver contra el tag fijado en Windows)
- `CreateFileMappingW` `INVALID_HANDLE_VALUE` + `FILE_MAP_ALL_ACCESS` + DACL usuario en `Local\`.
- `MiniDumpWriteDump` flags + carga dinámica de `dbghelp.dll`.
- `std::atomic_ref<uint64_t>` lock-free en MSVC x64 + `msvcp140_atomic_wait.dll`.
- Unidades de `nowQpc()`: ticks QPC (Win) vs ns (Linux) — convertir antes de comparar.
- `spoutDX` API (init/sendTexture/receiveTexture) en la versión fijada; misma LUID.
- Sintaxis `cloudflared` + `SetProcessInformation(ProcessPowerThrottling)`.
- Callback de audio crudo por pista de libobs (nombre/firma en el tag) para buses físicos.

## Decisiones asumidas (regla 4 de la spec)
- Ring a nivel de **bloque** (potencia de 2 sobre `capacityBlocks`; `capacityFrames = blocks×480`).
- Launcher compila en Linux como stub; el código Win32 real está completo.
- `ba_core` no depende de launcher: la cripto de `ba_core` usa **mbedTLS** (FetchContent, v3.6.2); el launcher conserva su `Sha256` propio (Win32, /MT).
- **Preferencia de código abierto (regla del proyecto):** cripto → **mbedTLS** (Apache-2.0); JSON → **nlohmann/json** (MIT, header-only). El channel strip DSP se mantiene **nativo** (corazón del producto, spec 7.2); el resampling (7.4) irá a **speexdsp/libsamplerate**.
- **Cross-comp Linux/Windows:** el árbol compila en ambos; los includes POSIX (`<unistd.h>`, `<sys/mman.h>`) van guardados con `#if defined(_WIN32)` (p. ej. `getpid()` en tests usa `random_device` en Windows). Los caminos `_WIN32` se validan de compilación en MSVC.
- **Resampling (7.4) y secrets (12):** resampling → **libsamplerate** (Secret Rabbit Code, MIT; el repo canónico es `libsndfile/libsamplerate`, tag real `0.2.2` — `v2.2.0` no existe) vía `ba::dsp::Resampler`; `secrets.json` → **AES-256-GCM** (mbedTLS) + KDF **Argon2id** (referencia P-H-C, tag `20190702`) vía `ba::security::SecretsStore`. Ambos incondicionales (compilan en Linux y Windows).
- **LSP/VST3 = CORE (el usuario SOBRESCRIBE la decisión D6 / §7.5):** LSP ya no es opcional ni "fuera de la v1"; es parte del corazón y se usa **con su editor gráfico completo** (no solo parámetros/presets), compilado para Windows. Fundamento: `src/plugin-host/` (`ba_plugin_host`, namespace `ba::plugin`): `scanDirectory()` es C++ puro (`std::filesystem`, cross-platform, testeado en Linux) y la carga real + embebido del editor viven en `#ifdef _WIN32` (LoadLibraryW → `ModuleEntry` → QI de `IAudioProcessor`/controller → `createView`). El **VST3 SDK** (MIT) es la base del host (no se re-implementa el ABI); **LSP** (LGPL) es el asset de despliegue que se empaqueta en `AudioPlugins/`. Pins: `VST3_SDK_VERSION=v3.7.9_build_61` (FetchContent, **solo bajo `if(WIN32)`** → Linux queda offline) y `LSP_VERSION=1.2.35` (asset, no se compila).
