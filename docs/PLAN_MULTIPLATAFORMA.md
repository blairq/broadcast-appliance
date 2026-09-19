# Plan — Broadcast Appliance multiplataforma (Windows · Linux · macOS)

> Estado de partida: 2026-09-18. Este plan cambia el alcance de la spec
> (`docs/prompts/primer_prompt.md` §1: "Windows 10/11 x64"). Cuando se apruebe,
> la spec y el README se actualizan para declarar tres plataformas de primera
> clase:
>
> | Plataforma | Arquitectura | Mínimo |
> |---|---|---|
> | Windows 10 22H2 / 11 | x86-64 | spec actual |
> | Linux (glibc) | x86-64 (arm64 como extra barato, ver D9) | glibc 2.35 (Ubuntu 22.04) |
> | macOS | **arm64 (Apple Silicon, M1 en adelante)** | macOS 13 (ver D8) |

## Avance (actualizado 2026-09-18)

Verificado en Linux (Ubuntu 24.04, Ryzen 7 PRO 4750U con GPU AMD integrada,
sin NVIDIA). Windows: el código nuevo compila (chequeo con llvm-mingw); falta
ejecutarlo. macOS: escrito, sin compilar todavía.

**Fase 1**
- [x] `ba::platform`: `Paths` (+ `dataRoot`), `Process` (+ `attach`),
      `LocalSocket`, `SingleInstance`, con `test_platform` (contrato, cruza
      procesos reales; 30 corridas sin fallos, limpio con ASan+UBSan).
- [x] Nombres de memoria compartida hasheados (límite de 31 de macOS; UID en
      el hash).
- [x] `make lint` en verde; mbedTLS ya no se cuela en el bundle.
- [x] CI de Windows (clang-cl desde Linux, ABI MSVC, tests en Wine).
- [~] macOS (rama `macos`, sin validar en una Mac todavía; ver docs/MACOS.md):
      `make deps-macos` (libobs con Xcode + obs-deps arm64, Qt, libwebrtc
      macos_arm64, LSP desde fuente), rama APPLE de `cmake/BaLibObs.cmake`,
      plugins `.plugin`, Qt en frameworks, gateway con libwebrtc real, editor
      CLAP Cocoa (QWindow), Info.plist embebido (TCC), firma ad-hoc,
      `check-portable-macos.sh`, diálogo de recuperación con osascript.
      Workflow `macos.yml` manual.

**Adelantado de otras fases (para que el sistema levante en Linux)**
- [x] Canal de control real (F2.1): `ControlService` en A y B sobre
      `LocalSocket`; el launcher cierra por ahí y cae a SIGTERM si no responde.
      Arreglado el contrato de `DuplexStream::read` (0 ≠ EOF) y la pérdida del
      último mensaje cuando el peer cierra enseguida.
- [x] Launcher multiplataforma (F4.1): lanza, vigila (crash + cuelgue con
      gracia de arranque), reinicia con backoff, readopt si el launcher se cae,
      cierre ordenado. Copia sombra con clave por contenido en la caché del
      usuario.
- [x] Codificador por plataforma (F5.5): `auto` / alias / id, con caída a x264
      si el elegido no arranca. VAAPI verificado en la GPU AMD integrada.
- [x] Modo de operación de `ba-app` sin UI: escena configurable
      (`Config/app.json`), grabación y RTMP verificados.

- [x] Video de invitados entre procesos (F2.2): memoria compartida con
      triple buffer + seqlock, resuscripcion si el escritor se reinicia.
- [x] UI Qt/QML en Linux (F5) con Qt vendoreado dentro del bundle.
- [x] **Gateway con media real en Linux (F7.1-7.2)**: libwebrtc M123 de
      Shiguredo contra la libc++ de Chromium (std::__Cr), compilado aparte con
      clang (`cmake/gateway-webrtc`) y enlazado estatico (solo libc/libm).
      `test_webrtc_loopback` prueba camara, microfono, pantalla compartida y
      retorno por RTP real; E2E con Chrome real.
- [x] Sala: lobby privado, API de control (web + Qt), archivos y pantallas
      compartidas, HTTPS propio, tunel WireGuard + lwIP en espacio de usuario
      (ver `docs/CONFERENCIA.md`).

**Siguiente**: CI de tres runners (cierra la fase 1), libwebrtc de macOS
(`deps-webrtc` macos_arm64) y ejecutar en Windows/MSVC todo lo nuevo.

## 0. Objetivo, principio y criterio de "hecho"

**Objetivo.** El producto corre completo en las tres plataformas: launcher →
gateway (B) → motor + UI (A), con invitados remotos por WebRTC, su audio y
video en la escena, emisión RTMP, grabación y recuperación ante crash.

**Principio rector: máxima reutilización, cero concesiones en el camino
caliente.**

- Todo lo que no toca al sistema ni al hardware es **un solo código**: lógica
  de supervisión, protocolos, DSP (en su forma de referencia), persistencia,
  seguridad, señalización, UI Qt, motor de emisión.
- Donde el sistema o la arquitectura ofrecen algo **mejor** (GPU compartida,
  codificador por hardware, SIMD, planificación de tiempo real), se escribe un
  backend específico detrás de una interfaz común. No se usa el mínimo común
  denominador si cuesta rendimiento.
- Todo backend específico tiene: (1) la misma interfaz, (2) un backend de
  referencia portable que sirve de fallback y de oráculo en los tests, y (3)
  un test que compara ambos.

**Criterio de "hecho" por plataforma.**

1. El bundle arranca en una máquina limpia sin instalar nada.
2. La suite E2E (sección 9) pasa en CI en esa plataforma.
3. Ningún camino de producto es un stub. Los `#ifdef` de plataforma viven
   solo dentro de `ba::platform` y de los backends especializados.
4. Los presupuestos de rendimiento (sección 5) se cumplen **en cada
   plataforma**, medidos, no supuestos.

## 1. Diagnóstico: qué es solo Windows hoy

| Área | Windows | Linux | macOS | Severidad |
|---|---|---|---|---|
| Launcher (`src/launcher`) | Win32 real | sale con "solo para Windows" | no compila probado | **Bloqueante** |
| Canal de control A↔B (`ControlEndpoint`) | **loopback en memoria** | igual | igual | **Bloqueante en todas** |
| Video A↔B (`SpoutTransport`) | Spout2/D3D11 (documentado, gated) | NULL **intra-proceso** | NULL intra-proceso | **Bloqueante** |
| Audio A↔B (`SharedAudioRing`), salud | memoria compartida | `shm_open` real, test cross-proceso | `shm_open` sirve, pero nombres ≤ 31 chars (ver 3.4) | Bajo |
| `ba-app` modo operación | Qt, gated a `_WIN32` | autotest headless y sale | igual | **Bloqueante** |
| Motor (`BroadcastEngine`) | compila | real, autotest OK | sin probar | Medio |
| Gateway HTTP/WS/tokens | real | real, testeado | sin probar (POSIX, debería andar) | Bajo |
| Gateway media WebRTC | libwebrtc solo MSVC, nunca ejecutado | NULL | NULL | **Bloqueante en todas** |
| cloudflared | spawn Win32 | no-op | no-op | Alto |
| Dispositivos de audio | cfgmgr32 (en curso), WASAPI (cola) | nada | nada | Alto |
| Host de plugins (CLAP) | hecho (Win32) | hecho (X11) | pendiente | Alto |
| Recovery UI | ventana Win32 | log | log | Medio |
| Crash dumps | minidump | nada | nada | Medio |
| DSP | escalar, `double` por muestra, sin SIMD | igual | igual | Rendimiento |
| CI | cross MinGW, nada se ejecuta en Windows | build + ctest **sin libobs** | no existe | Alto |

## 2. Capa de plataforma (`ba::platform`)

Una interfaz, tres implementaciones: `*_win.cpp`, `*_posix.cpp` (lo común a
Linux y macOS) y `*_linux.cpp` / `*_mac.cpp` / `*_mac.mm` solo donde difieren.
CMake elige los archivos; la lógica no tiene `#ifdef`.

| Interfaz | Windows | Linux | macOS |
|---|---|---|---|
| `Process` | `CreateProcessW` + Job Object | `fork`+`execve` (el `PR_SET_PDEATHSIG` se fija dentro del hijo) + process group | `posix_spawn` + process group; muerte del padre vía `kqueue` `EVFILT_PROC/NOTE_EXIT` en el hijo (no hay `PDEATHSIG`) |
| `LocalSocket` | named pipe con DACL del usuario | Unix socket en `$XDG_RUNTIME_DIR` (0700) + verificación de UID del otro extremo; el namespace abstracto se descartó: no tiene permisos | Unix socket en `$TMPDIR` (sin namespace abstracto; `sun_path` ≤ 104) |
| `SingleInstance` | mutex con nombre | `flock` en `$XDG_RUNTIME_DIR` | `flock` en `$TMPDIR` |
| `Paths` | `GetModuleFileNameW` | `/proc/self/exe` | `_NSGetExecutablePath` + raíz del `.app` |
| `SharedMemoryRegion` (ya existe) | `CreateFileMapping` | `shm_open` | `shm_open` con nombre **hasheado ≤ 31** |
| `RealtimeThread` | MMCSS "Pro Audio" + EcoQoS off | `SCHED_FIFO` vía rtkit, si no `nice` | `THREAD_TIME_CONSTRAINT_POLICY` + **audio workgroup** (`os_workgroup`) para que el planificador lo mantenga en P-cores |
| `DynLib` | `LoadLibraryW` | `dlopen` | `dlopen` / `CFBundle` |
| `CrashHandler` | Crashpad (D5) | Crashpad | Crashpad |
| `PowerAssertion` (evitar suspensión durante la emisión) | `SetThreadExecutionState` | `org.freedesktop.ScreenSaver.Inhibit` / systemd-inhibit | `IOPMAssertionCreateWithName` + desactivar App Nap |

Cada interfaz tiene un test de contrato idéntico en las tres plataformas.
`DuplexStream` se implementa sobre `LocalSocket`; `LoopbackDuplex` queda solo
para tests.

## 3. Especialización por plataforma y arquitectura (caminos calientes)

### 3.1 Video entre procesos (B→A invitados, A→B programa)

Interfaz común `VideoTransport` (hoy `SpoutTransport`) con el
`SpoutMetaBlock` que ya existe (contador de frame + timestamp en memoria
compartida, lock-free). Lo que cambia es **cómo viaja la imagen**:

| Plataforma | Backend primario (zero-copy GPU) | Fallback |
|---|---|---|
| Windows | Spout2 / textura D3D11 compartida (misma GPU por LUID) | CPU |
| Linux | **DMA-BUF** exportado por EGL (`EGL_MESA_image_dma_buf_export`), fd pasado por el `LocalSocket` con `SCM_RIGHTS` | CPU |
| macOS | **IOSurface** (`IOSurfaceCreateMachPort`/`IOSurfaceLookupFromMachPort`), textura GL/Metal sin copia; unified memory de Apple Silicon | CPU |

El backend CPU (ring de frames sobre `SharedMemoryRegion`) es común a las
tres, es el fallback de la spec §4.2 y es el oráculo de los tests.

### 3.2 Codificación de video

La lógica de selección ya existe (`BroadcastEngine`: hardware con caída a
x264). Se extiende la tabla de preferencias por plataforma, con prueba real
del encoder al arrancar (no solo "está registrado"):

| Plataforma | Orden de preferencia |
|---|---|
| Windows | NVENC → QSV → AMF → x264 |
| Linux | NVENC → VAAPI (Intel/AMD) → x264 |
| macOS | **VideoToolbox** (H.264/HEVC por hardware, plugin `mac-videotoolbox`) → x264 (en arm64 x264 usa NEON) |

### 3.3 Render de libobs

| Plataforma | Backend |
|---|---|
| Windows | D3D11 |
| Linux | OpenGL/EGL (X11 y Wayland; `obs_set_nix_platform()` antes de `obs_startup`) |
| macOS | OpenGL en OBS 31. **Verificar** en qué versión de OBS el renderer Metal queda estable. Si aporta rendimiento, se sube `OBS_STUDIO_TAG` para las tres plataformas a la vez (hay un solo pin; no se divide por plataforma). |

### 3.4 IPC de audio y salud

Código común (`SharedAudioRing`, `HealthBlock`). Único ajuste: en macOS
`shm_open` limita el nombre a 31 caracteres (`PSHMNAMLEN`). `toShmName` pasa a
usar un prefijo corto + hash del nombre lógico en todas las POSIX (hoy recorta
a 200, que en macOS fallaría y además podría colisionar).

### 3.5 DSP (channel strip, automixer, resampler)

Hoy es escalar y en `double` por muestra. Plan:

1. Mantener la versión escalar como **referencia** (y para tests).
2. Kernels vectorizados por arquitectura para los bucles calientes (biquads,
   detectores de envolvente, ganancias del automixer, mezcla):
   - x86-64: SSE2 base (garantizado) + **AVX2/FMA con dispatch en runtime**
     (no se puede exigir AVX2 en PCs de salón viejas).
   - arm64 (macOS y Linux arm64): **NEON**, garantizado, sin dispatch.
3. Procesamiento en `float` con estados de biquad en `double` donde la
   estabilidad lo pida (graves con HPF muy bajo).
4. Test de equivalencia kernel vs referencia (tolerancia en dB) en las tres
   plataformas + benchmark en CI (sección 5).

Se evalúa una capa fina propia (p. ej. `ba::simd` con `float4`/`float8`) o
xsimd/Highway; la elección se hace en la fase 3 con benchmarks, no antes.

### 3.6 Audio de dispositivos

| Plataforma | API | Identidad del dispositivo |
|---|---|---|
| Windows | WASAPI (exclusivo si hace falta latencia) | cfgmgr32 (en curso) |
| Linux | PipeWire (nativo), PulseAudio como fallback | udev/sysfs (vid:pid:serial, ruta USB) |
| macOS | CoreAudio (HAL) | IOKit (vendor/product/serial, `locationID`) |

`DeviceMatcher` (scoring) ya es portable y no cambia.

### 3.7 Host de plugins (CLAP)

Hecho con CLAP en vez de VST3 (`src/app/audio`): un solo host para las tres
plataformas; solo cambia la ventana del editor.

| Plataforma | Carga | Editor |
|---|---|---|
| Windows | `plugins/lsp/lsp-plugins.clap` (compilado desde fuente) | `CLAP_WINDOW_API_WIN32` — hecho |
| Linux | `plugins/lsp/lsp-plugins.clap` (release oficial) | `CLAP_WINDOW_API_X11` (XWayland) — hecho |
| macOS | bundle `.clap` | `CLAP_WINDOW_API_COCOA` — pendiente |

Riesgo: **LSP no publica builds de macOS**. Ver D10.

### 3.8 WebRTC

Un solo código (`Webrtc.cpp`) sobre libwebrtc M123, con el SDK por
plataforma: Windows (MSVC), Linux (resolver libc++ vs libstdc++, ver F5),
macOS arm64 (libc++ del sistema: sin conflicto). La captura de audio del
invitado usa el ADM de libwebrtc solo como fuente de red, nunca del
dispositivo local.

## 4. Decisiones

| # | Decisión | Recomendación |
|---|---|---|
| D1 | Stack WebRTC | **libwebrtc** en las tres: NetEQ, jitter buffer, AEC y control de congestión son justamente lo que importa para voz de invitados. |
| D2 | Toolchain | **MSVC** en Windows (libwebrtc solo enlaza así), **gcc o clang** en Linux (según F5), **Apple clang** en macOS. MinGW se retira del producto. |
| D3 | Video entre procesos | **GPU zero-copy por plataforma** (3.1) + CPU común como fallback. Cambia la recomendación anterior (CPU primero): con macOS y "sin ceder rendimiento", el CPU queda solo como red de seguridad. Se implementa CPU primero igual, porque desbloquea el resto y es el oráculo. |
| D4 | Recovery UI | Proceso `ba-recovery` en Qt (común). Respaldo si Qt está roto: Win32 nativo en Windows (ya existe), zenity/`notify-send` en Linux, `osascript display dialog` en macOS. |
| D5 | Crash dumps | **Crashpad** en las tres: mismo formato (minidump), análisis offline común y soporte nativo arm64. |
| D6 | Distribución | Windows: carpeta portable (§5.1). Linux: tarball portable + AppImage. macOS: `.app` firmado y notarizado dentro de un `.dmg`. |
| D7 | Línea base Linux | Ubuntu 22.04 (glibc 2.35), build en contenedor. |
| D8 | macOS: firma y distribución | **Developer ID + notarización** (cuenta Apple Developer, USD 99/año). Sin eso, Gatekeeper bloquea el `.app` en cualquier Mac ajena, y el caso "correr desde un disco externo" de la spec lo agrava (cuarentena). Mínimo macOS 13. |
| D9 | Arquitecturas | Windows x86-64; macOS **solo arm64** (Intel Mac fuera); Linux x86-64, y arm64 como extra una vez que existan los kernels NEON de macOS (es casi gratis). |
| D10 | Plugins de audio en macOS | LSP no tiene build de macOS: (a) compilarlo desde fuente para macOS (LGPL, CMake/Make; verificar viabilidad), (b) cubrir los presets con el DSP nativo propio (ya existe el channel strip) y dejar VST3 de terceros opcional. Recomendación: **(b) como base, (a) si la spike sale bien**. |

## 5. Presupuestos de rendimiento (medidos en CI y en hardware real)

| Métrica | Presupuesto | Dónde se mide |
|---|---|---|
| Latencia invitado → escena (audio) | ≤ 1 bloque de ring + jitter buffer de WebRTC | E2E |
| Copia de video B→A por frame 1080p | zero-copy en el backend GPU; ≤ 1 ms en fallback CPU | benchmark de `VideoTransport` |
| DSP por canal (bloque de 480 muestras, 48 kHz) | ≤ 2 % de un núcleo | benchmark de DSP, las tres plataformas |
| Encoder | hardware en toda máquina que lo tenga; 1080p60 sin frames perdidos | autotest del motor |
| Reenganche de A tras crash | < 1 s (spec) | test de 100 kills |
| Hilos de audio | sin underruns en 8 h con carga de CPU de fondo | soak |

Los benchmarks corren en CI y **fallan** si hay regresión > 10 % contra la
línea base guardada.

## 6. Fases

Tamaños: **S** ≈ días, **M** ≈ 1–2 semanas, **L** ≈ 3+ semanas (una persona).

### Fase 1 — Cimientos (M)
1. `ba::platform`: `Process`, `LocalSocket`, `SingleInstance`, `Paths` con
   tests de contrato.
2. CI con **tres runners nativos**:
   - Linux (contenedor 22.04): libobs cacheada, `make test`, autotest del motor
     bajo Xvfb + Mesa llvmpipe.
   - Windows (`windows-latest`, MSVC): build + `ctest` **ejecutado**.
   - macOS (`macos-14`, **Apple Silicon M1**): build + `ctest`. Es la forma de
     validar macOS sin tener la Mac a mano.
3. `make deps-obs-macos`: libobs 31 desde fuente para arm64 con los
   `obs-deps` prebuilt de macOS (Xcode requerido).
4. Nombres de memoria compartida hasheados (3.4).
5. Arreglar `make lint` (falla en `src/app/OperatorEngine.hpp`) y sacar
   `include/`/`lib/` de mbedTLS del bundle.

### Fase 2 — IPC real entre procesos (M)
1. Canal de control sobre `LocalSocket` en A, B y el launcher.
2. `VideoTransport` con backend CPU común (reemplaza el registro
   intra-proceso).
3. `test_ipc_cross_process` extendido a control y video, en las tres.

### Fase 3 — Rendimiento del núcleo (M)
1. Benchmarks de DSP y de `VideoTransport` en CI (línea base por plataforma).
2. Kernels SIMD (3.5): SSE2/AVX2 con dispatch, NEON.
3. `RealtimeThread` y `PowerAssertion` (MMCSS / rtkit / workgroups de audio).

### Fase 4 — Launcher multiplataforma (M)
1. `Supervisor`/`ChildProcess`/`ControlChannel` sobre `ba::platform`.
2. Entorno saneado por plataforma (`LD_LIBRARY_PATH`, `DYLD_*`, `QT_*`).
3. Recovery UI (D4).
4. Test de 100 kills en CI, en las tres.

### Fase 5 — `ba-app` con UI en las tres (M/L)
1. Un solo punto de entrada común; modo operación por defecto; autotest
   detrás de un flag.
2. Gate de `MainWindow` solo por `BA_APP_HAVE_QT_OBS` (sin `_WIN32`).
3. Qt 6 vendoreado por plataforma (`aqt`: `win64_msvc2019_64`, `linux
   gcc_64`, `mac clang_64` universal) con despliegue propio de cada una
   (`windeployqt`, rpath `$ORIGIN`, `macdeployqt`).
4. Preview de libobs: HWND / xcb o Wayland / `NSView`.
5. Encoders por hardware por plataforma (3.2) con prueba real al arrancar.

### Fase 6 — Video GPU zero-copy (M/L)
Backends de 3.1: Spout2 (Windows), DMA-BUF (Linux), IOSurface (macOS), con
caída automática a CPU y test de equivalencia contra el backend CPU.

### Fase 7 — Gateway con media real (L, mayor riesgo; arranca en paralelo tras F1)
1. Spike de 1 semana: libwebrtc M123 en Linux (libc++ vs libstdc++) y en
   macOS arm64.
2. `deps-webrtc-{win,linux,mac}` vendoreados; backend real en las tres.
3. `test_gateway_integration` sin skip en las tres.
4. cloudflared sobre `ba::platform::Process` (hay binarios para las tres) y
   resolución de los `VERIFY`.

### Fase 8 — Hardware y plugins (M/L)
Dispositivos (3.6), VST3 (3.7), D10, wizard/preflight en Qt común.

### Fase 9 — Diagnóstico y empaquetado (M)
1. Crashpad (D5).
2. Linux: ffmpeg y demás vendoreados en la línea base; verificación con `ldd`
   de que nada fuera de glibc/GL sale del sistema. Tarball + AppImage.
3. macOS: `.app` con frameworks, entitlements del hardened runtime
   (micrófono, cámara, `disable-library-validation` para plugins de OBS y
   VST3), firma, notarización, `.dmg` (D8).
4. Windows: layout §5.1 como hoy.

### Fase 10 — Verificación de producto (M)
Suite E2E (sección 9) + soak de 8 h en las tres + máquinas limpias. En macOS,
el paso final es en tu M1 con `make verify-mac`, que corre la suite y deja un
reporte en `Logs/` para traer de vuelta.

## 7. Orden y dependencias

```
F1 cimientos ─┬─► F2 IPC ─┬─► F4 launcher ─┬─► F10 verificación
              │           ├─► F5 UI ───────┤
              │           └─► F6 GPU ──────┤
              ├─► F3 rendimiento ──────────┤
              ├─► F7 WebRTC (paralelo) ────┤
              ├─► F8 hardware/plugins ─────┤
              └─► F9 diag/empaquetado ─────┘
```

## 8. Riesgos

| Riesgo | Mitigación |
|---|---|
| libwebrtc en Linux (libc++/libstdc++) | Spike al inicio de F7 antes de comprometer diseño. |
| Runners macOS de CI sin GPU real (el pipeline de video puede no inicializar) | El autotest devuelve SKIP (77) como en Linux headless; el camino GPU se valida en tu M1 con `make verify-mac`. |
| Gatekeeper / cuarentena en macOS | Firma + notarización (D8) desde la primera build que salga de CI. |
| LSP sin macOS | D10. |
| Wayland: preview y editores VST3 son X11 | Default `QT_QPA_PLATFORM=xcb` (XWayland), como OBS; Wayland nativo después. |
| AVX2 no disponible en PCs viejas | Dispatch en runtime con SSE2 de base. |
| Divergencia de comportamiento entre plataformas | Tests de contrato + backend de referencia como oráculo + misma suite E2E. |

## 9. Suite E2E (criterio de aceptación, las tres plataformas)

1. El launcher arranca B y A desde el bundle, sin variables de entorno.
2. Un invitado headless entra con sala + PIN; su tono y su patrón de video
   llegan a `ba_guest_audio` / `ba_guest_video`.
3. A emite a un RTMP local y graba; se verifica con `ffprobe`.
4. Retorno N−1: el invitado recibe el programa sin su propio audio.
5. 100 kills de A: reenganche < 1 s, sin perder el snapshot.
6. Kill de B: el launcher lo relanza y el invitado reconecta.
7. Soak de 8 h sin deriva de rings, sin underruns y sin crecimiento de memoria.
8. Benchmarks dentro del presupuesto (sección 5).

## 10. Qué no cambia

- `ba_core` sin Qt ni libobs.
- Layout §5.1 (en macOS vive dentro de `Contents/` del `.app`, con la misma
  estructura relativa).
- `versions.cmake` como único lugar de pines (se suman los de Linux y macOS).
- DSP (como referencia), persistencia, secretos, tokens, señalización.

## 11. Documentación a actualizar con cada fase

Spec §1 y reglas §14 (`/MT`/`/MD` pasan a ser reglas de Windows), README
(Estado, Arquitectura, Dependencias con columna por plataforma) y
`docs/TASKS.md` con columnas Windows / Linux / macOS.
