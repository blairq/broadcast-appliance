# Broadcast Appliance Portable v2

Aplicativo de emisión en vivo (broadcast) embebido sobre **obs-studio / libobs**,
con un **gateway WebRTC** para invitados remotos y un **supervisor** (launcher)
que lanza, vigila y recupera los procesos. El corazón del sistema —`ba_core`— es
**C++20 puro** (sin Qt, sin libobs), por lo que compila y se prueba por unidad en
**Linux** y en **Windows**; las piezas que dependen de la plataforma (libobs, Qt,
WASAPI/D3D, libwebrtc, Win32) se escriben con `#ifdef _WIN32` y se validan de
compilación en Linux, verificándose su comportamiento real en Windows. El rack
de audio con los módulos **LSP** (host CLAP propio) es parte del núcleo del
producto, no una opción.

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
- **Rack de audio por fuente** (pestaña **Audio**, 🎚): patchbay tipo JACK con
  los ~200 módulos **LSP** (formato CLAP, host propio en
  [`src/app/audio`](src/app/audio/)) conectados con cables, medidores por
  módulo, **analizador** (LUFS BS.1770, espectro por tercios, piso de ruido),
  **generador** de señal de prueba y la interfaz gráfica original de cada
  módulo. Todo manejable por MCP (gate, compresor, de-esser, EQ, multibanda,
  limitador con parámetros en unidades reales). LSP va en `plugins/lsp/`.
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
- **Rack de audio** (en [`src/app/audio`](src/app/audio/), dentro de `ba-app`):
  host CLAP (`ClapHost`, editor X11/Win32 en `ClapEditor`), grafo de módulos
  (`AudioRack`, un filtro OBS `ba_audio_rack` por fuente vía `RackManager`),
  catálogo con nombres reales de parámetros (`Modules`) y análisis/generador
  (`Analyzer`).
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

Los módulos **LSP** (CLAP) van en `plugins/lsp/` del bundle: en Linux el
binario oficial (`make deps-lsp`, con cairo/libsndfile vendoreados), en Windows
compilado desde la fuente oficial (`make deps-lsp-win`).

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
- **`ba-gateway`** (Proceso B): la sala de videoconferencia. `Room` (lobby,
  participantes, chat, archivos, rutas de medios), `SignalingServer` (HTTP +
  WebSocket de invitados) y `AdminApi` (API de control REST + SSE, de la que
  cuelgan la consola web `Web/admin/` y la vista Qt), `Webrtc` (libwebrtc real,
  también en Linux), `MediaPump` (IPC ↔ WebRTC por ruta), `TlsTerminator`
  (HTTPS), `WgDevice` + `IpStack` (WireGuard + lwIP en espacio de usuario) y
  `Cloudflared`. Ver [`docs/CONFERENCIA.md`](docs/CONFERENCIA.md).
- **`ba-sources`** (módulo libobs): se compila contra libobs **real** en
  cualquier plataforma y registra cuatro cosas — `ba_guest_audio` y
  `ba_guest_video` (traen al invitado remoto a la escena desde el IPC del
  Proceso B), el filtro `ba_channel_strip` (el DSP nativo de `ba_core::dsp`
  con presets podium/lavalier/handheld) y `ba_channel_preset` (tono de
  referencia). Sin libobs se compila solo un esqueleto.
- **Rack de audio** (CLAP/LSP, `src/app/audio`): host, grafo por fuente,
  analizador y generador. Flag `BA_HAVE_CLAP` (headers CLAP de `make deps-clap`).
- **`BroadcastAppliance`** (launcher): `Supervisor` + `RecoveryUi`. Funciona
  en Windows y en Linux (y está escrito para macOS) sobre la capa de
  plataforma `ba::platform` (`src/core/platform/`): lanza B y A, detecta crash
  y cuelgue, reinicia con backoff, adopta a A y B si el launcher se cae y
  vuelve (la emisión no se corta), y hace el cierre ordenado por el canal de
  control. La ventana de recuperación sigue siendo Win32; en Linux el
  incidente se loguea y se reintenta.
- **Interfaz Qt/QML de `ba-app`** (Qt vendoreado, portable): dashboard de
  tarjetas de vidrio sobre el video en vivo, ventanas de salida para otro
  monitor, escenas y fuentes, configuración completa y la página **Sala**
  (lobby privado, invitados con su video, chat, archivos, acceso directo).
  Sin pantalla, `ba-app --no-ui` corre en modo consola.

Queda por hacer (ver [`docs/PLAN_MULTIPLATAFORMA.md`](docs/PLAN_MULTIPLATAFORMA.md)):

- **Compilar y probar en Windows (MSVC) y macOS** la interfaz, el motor y el
  gateway real (hoy se validan en Linux), y **CI** con runners nativos.
- **libwebrtc para macOS** (el SDK de Shiguredo existe; falta el build) y
  **PDF** como archivo compartido (hoy: imagen, video y audio).
- **Packaging** final del bundle portable (runtime de terceros vía
  `tools/deploy`).

---

## Requisitos de build

Todo lo que hay que tener instalado para compilar, probar y linterizar el
repo. Las **dependencias de código** (mbedTLS, nlohmann, libsamplerate, Argon2,
CLAP, LSP, libobs, Qt, CEF, libwebrtc, cloudflared) NO se instalan a mano: el
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

### Motor de emisión: libobs (Linux)

El **motor de emisión** (`ba::app::BroadcastEngine`) se compila contra libobs.
En Windows se usa el runtime portable que baja `make deps-obs`; en Linux no
existe un equivalente prebuilt, así que libobs se **compila desde fuente** en la
versión fijada en [`versions.cmake`](versions.cmake) (`OBS_STUDIO_TAG`) hacia
`extern/obs-linux/`:

```bash
make deps-obs-linux    # sus dependencias desde fuente + obs-studio 31.0.0, en extern/
```

Primero compila **desde fuente fijada** (URL + SHA-256, o commit de git) las
dependencias de libobs hacia `extern/obs-deps-linux/`
([`tools/deps/obs-deps-linux.sh`](tools/deps/obs-deps-linux.sh)): FFmpeg 7.1.1
(sin bibliotecas externas; con VAAPI y NVENC), x264, libva (el cargador: los
drivers de video son del sistema), libv4l2, jansson, libuuid, speexdsp,
libpci, libcurl (solo HTTP) y mbedTLS; también nasm y meson para compilarlas.
Después compila libobs contra ellas. El build del proyecto toma libobs **solo**
de `extern/` y el bundle de `make release` se lleva todo con rpath `$ORIGIN`:
corre en una máquina sin nada instalado. Si `extern/obs-linux` no está,
`ba-app` y `ba-sources` se compilan en modo headless y el árbol sigue verde.

`make release` termina con
[`tools/deps/check-portable.sh`](tools/deps/check-portable.sh): resuelve cada
dependencia de cada ELF del bundle como lo hace el cargador dinámico (con el
RUNPATH del propio archivo) y **falla** si algo sale de la base del sistema
([`tools/deps/linux-baseline.txt`](tools/deps/linux-baseline.txt): glibc,
drivers de video, X11/Wayland, ALSA/Pulse, udev, fuentes y lo que exige Qt
oficial). El runtime de C++ viaja en `bin/64bit/cxx/` y el launcher usa el más
nuevo entre ese y el del sistema; el launcher y `ba-mcp` solo dependen de la
libc. La glibc mínima la fija la máquina donde se compila (la informa el
chequeo): para máquinas más viejas, compilar el release en una distro vieja.

Compilar requiere los **headers** de la base del sistema (toolchain, no
dependencias del producto). En Ubuntu 24.04:

```bash
sudo apt install   libgl1-mesa-dev libegl1-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev   libxcb-randr0-dev libxcb-shm0-dev libxcb-xfixes0-dev libxcb-xinerama0-dev   libxcb-composite0-dev libxcb-damage0-dev libxcb-present-dev libxcb-xkb-dev   libxcomposite-dev libxinerama-dev libxkbcommon-dev libxss-dev   libpulse-dev libasound2-dev libudev-dev   libwayland-dev libdrm-dev libfreetype-dev libfontconfig1-dev   patchelf
```

`uthash` y `nv-codec-headers` (ambos header-only) **no** se instalan: los
vendorea `make deps-obs-linux` dentro de `extern/`.

> **Contexto gráfico.** `obs_reset_video()` necesita GL (Linux) o D3D11
> (Windows). En un servidor headless sin sesión gráfica el pipeline de vídeo no
> puede inicializarse: `ba-app --engine-selftest` lo detecta y devuelve 77
> (*saltado*) en vez de error. El resto —carga de módulos, codificadores,
> salidas, escena, IPC— sí se ejercita headless.

### Herramientas por tarea (opcionales según lo que hagas)

| Tarea | Herramienta | Cómo se obtiene |
|---|---|---|
| **Lint** (`make lint`) | `clang-format` | `sudo apt install clang-format` (noble → 18.1.3). El Makefile usa el binario de la distro y **no descarga nada**; si no está, `make lint` falla con un mensaje claro. La versión esperada queda fijada en `versions.cmake` (`CLANG_FORMAT_VERSION`) y CI la aserta. |
| **Suite Python** (`pytest`) | Python ≥ 3.10 + `venv` | `.venv` local (nunca global) con [`requirements-dev.txt`](requirements-dev.txt) (pytest). |
| **Docs** (`make docs`) | Python + Sphinx + sphinx-rtd-theme | Se instala en el `.venv` desde `docs/requirements.txt`. |
| **Caché de build** (acelera) | `ccache` | Opcional: si está en el PATH, CMake lo usa como launcher de C/C++ automáticamente. |
| **Windows desde Linux** (`make win`) | nada instalado: `make win` baja LLVM, el CRT/SDK de MSVC (xwin), Qt y OBS a `extern/` | Requiere aceptar la licencia de Microsoft (`BA_ACCEPT_MSVC_LICENSE=1`). `ninja` y `7z` (`p7zip-full`) para `make dist-win`. |

### Reglas del entorno de desarrollo

- **Sin instalaciones globales**: las deps de Python van en un venv local
  (`.venv/`); las de C++ las trae `FetchContent` dentro del árbol de build.
  `clang-format` y `ccache` son las únicas herramientas del sistema que se
  asumen instaladas por apt.
- **C++20** obligatorio; el build falla si el compilador no lo soporta.
- El build es **self-contained**: no hay `Dockerfile` ni `.devcontainer`;
  basta el toolchain de la tabla anterior.

---

## Puesta en marcha

Secuencia completa desde cero, en Linux. Los pasos 1 y 2 se hacen una sola vez.

### 1. Clonar e instalar las dependencias de build

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
```

Después instalá los paquetes de desarrollo de la sección
[Requisitos de build](#motor-de-emisión-libobs-linux). Son para compilar
**libobs**, no dependencias del producto: el bundle final no las necesita.

### 2. Compilar libobs

```bash
make deps-obs-linux
```

Clona obs-studio en la versión fijada en [`versions.cmake`](versions.cmake),
vendorea `uthash` y `nv-codec-headers`, y compila hacia `extern/obs-linux/`.
**Tarda** (del orden de 20 minutos en una máquina de escritorio) y no instala
nada en el sistema.

### 3. Compilar y probar el proyecto

```bash
make build
make test
```

El configure imprime `ba-libobs: ENCONTRADA` cuando el motor de emisión se va a
compilar; si dice `ausente`, el paso 2 no terminó bien y `ba-app`/`ba-sources`
quedan en modo headless.

### 4. Levantar el sistema completo

```bash
make release
BA_GATEWAY_PIN=123456 ./release/BroadcastAppliance --no-recovery-ui
```

El launcher lanza el gateway (B) y el motor de emisión (A), los vigila y los
reinicia si se caen o se cuelgan. `Ctrl+C` (o `SIGTERM`) hace el cierre
ordenado: le pide a A y a B que terminen por el canal de control y, si no
responden, los termina. Se puede lanzar desde cualquier directorio: las rutas
salen de la ubicación del ejecutable. El log queda en `release/Logs/launcher.log`.

Detalles que conviene saber:

- **Copia sombra**: A y B corren desde una copia de `bin/`, `obs-plugins/` y
  `data/` en `~/.cache/BroadcastAppliance/<versión>` (así el bundle se puede
  actualizar en disco con el sistema corriendo). La versión sale del contenido:
  tras un `make release` se usa una copia nueva. `--no-shadow` la desactiva.
- **Los datos** (`Config/`, `Logs/`, `Recordings/`, `Web/`) siempre se leen y
  escriben en el bundle real.
- **Si el launcher se cae**, A y B siguen al aire; al volver a lanzarlo los
  adopta sin cortar nada.

> Corrélo desde una **sesión gráfica**: `obs_reset_video()` necesita un
> contexto GL. Por SSH sin display, A no arranca el motor y el launcher lo
> reintenta.

### 5. Configurar la emisión (`Config/app.json`)

La primera vez, A escribe `release/Config/app.json` con valores por defecto
(fondo + título, 1080p30, dos invitados). Todo es editable:

```json
{
  "video":   {"width": 1920, "height": 1080, "fps": 30},
  "encoder": {"video": "auto", "bitrateKbps": 6000},
  "record":  {"enabled": true, "dir": "Recordings", "container": "mkv"},
  "stream":  {"enabled": true, "server": "rtmp://a.rtmp.youtube.com/live2", "keyEnv": "BA_STREAM_KEY"},
  "guests":  {"count": 2},
  "sources": [
    {"id": "v4l2_input", "name": "camara", "settings": {"device_id": "/dev/video0"}},
    {"id": "text_ft2_source_v2", "name": "titulo", "settings": {"text": "En vivo"}, "rect": [0.05, 0.05, 0.6, 0.12]}
  ]
}
```

- **`sources`** acepta cualquier fuente de libobs con sus *settings* tal cual
  los usa OBS (cámara `v4l2_input`, `image_source`, `ffmpeg_source` para
  archivos o URLs, `pulse_input_capture`, etc.). `rect` es la ubicación en
  fracciones del lienzo `[x, y, ancho, alto]`. Una fuente que falla (cámara
  desenchufada) se omite con un aviso: no tumba la emisión.
- **`encoder.video`**: `auto` elige el hardware disponible en esta máquina y cae
  a x264 si no hay o si falla al arrancar. Alias: `vaapi` (AMD/Intel en Linux),
  `nvenc`, `qsv`, `amf`, `videotoolbox` (macOS) y `x264`, o un id de libobs
  literal. En una GPU AMD integrada (Ryzen) `auto` usa `ffmpeg_vaapi_tex`.
- **La clave de transmisión no va en el archivo**: se lee de la variable de
  entorno que indique `keyEnv` (por defecto `BA_STREAM_KEY`).

Opciones de `ba-app` por línea de comandos (pisan el archivo): `--encoder=`,
`--bitrate=`, `--resolution=WxH`, `--fps=`, `--record`/`--no-record`,
`--rec-dir=`, `--stream-server=`, `--guests=`, `--duration=<seg>`,
`--config=<archivo>`. Para pasarlas desde el launcher:
`--app-arg=--record`. `ba-app` también corre solo, sin launcher:

```bash
./release/bin/64bit/ba-app --record --duration=10
```

Para diagnosticar libobs sin la escena completa:
`./release/bin/64bit/ba-app --engine-selftest` (lista los codificadores
disponibles, graba unos segundos y verifica el archivo; devuelve 77 si no hay
contexto gráfico).

### 6. La sala de invitados (gateway)

El launcher ya lo levanta. Para correrlo solo:

```bash
BA_GATEWAY_PIN=123456 ./release/bin/64bit/ba-gateway --port 8787
```

Los invitados entran por el enlace de la página **Sala** de la app (o
`https://127.0.0.1:8789`): nombre, sala y PIN → **lobby** (el operador los ve y
les habla en privado; ellos solo ven al operador) → admitidos a la sala, con
cámara, micrófono, **pantalla o ventana compartida**, archivos para mostrar,
chat y mano levantada. El operador maneja todo desde la página Sala o desde la
**consola web** (`http://127.0.0.1:8788/admin/`, botón "Abrir consola web"):
las dos son clientes de la misma API de control.

- **WebRTC real** en Linux: `make deps-webrtc BA_WEBRTC_TARGET=linux` y
  `make build` compilan el gateway con libwebrtc (estático, sin dependencias
  del sistema).
- **HTTPS propio** (cámara/micrófono exigen página segura) con certificado
  propio (dejar `cert.pem` + `key.pem` en `Credenciales/`: se importan
  cifrados) o autofirmado automático.
- **Acceso directo por internet** sin Cloudflare: túnel **WireGuard en espacio
  de usuario** hacia un servidor propio con IP pública (`make deps-lwip`); el
  asistente de la página Sala genera la configuración del servidor.

Detalle completo (arquitectura, lobby, API, servidor, seguridad):
[`docs/CONFERENCIA.md`](docs/CONFERENCIA.md).

### Todo en la carpeta portable

El appliance corre desde cualquier carpeta (un pen drive, por ejemplo) y **todo
lo que guarda queda adentro de esa carpeta**, nunca en el perfil del usuario
ni en la maquina:

| Carpeta | Que guarda |
|---|---|
| `Config/` | configuracion (escenas, ajustes, tablero) |
| `Credenciales/` | tokens y claves (YouTube, Twitch, Facebook, MCP, WireGuard, HTTPS), **cifrados** con AES-256-GCM; una actualizacion nunca los toca |
| `Respaldos/` | copia de `Config/` y `Credenciales/` antes de cada actualizacion (quedan 5) |
| `Runtime/` | archivos de encuentro de la sesion (API de la sala, puerto MCP) |
| `Cache/` | caches de Qt/QML, fontconfig y shaders de la GPU (se redirigen al arrancar) |
| `Logs/`, `Recordings/`, `Shares/`, `Media/` | registros, grabaciones y archivos |

En Linux el canal de control y el lock de instancia unica usan sockets del
espacio **abstracto** del kernel: no escriben nada en ningun disco. (En macOS,
que no lo tiene, van a `$TMPDIR`, temporal y por usuario.)

**Credenciales sin contraseña.** El equipo arranca solo; el cifrado evita que
se lean por accidente (un pen drive FAT/exFAT no tiene permisos), pero la
clave del equipo (`Credenciales/clave-equipo`) viaja en el mismo pen drive:
quien se lleve el pen drive entero tiene todo. Para mudar el equipo a otro
pen drive, copiar `Credenciales/` completa. Las versiones anteriores guardaban
todo en claro en `Config/`: al primer arranque se importa y se borra.

### Actualizar

Descomprimir la version nueva en otra carpeta y elegirla en **Configuracion →
Sistema y datos → Actualizar desde una carpeta** (o `BroadcastAppliance
--update <carpeta> --start`, o la herramienta MCP `system_update`). La app se
cierra; el launcher respalda `Config/` y `Credenciales/` en `Respaldos/`,
reemplaza **solo el programa** (todo o nada: si algo falla, queda la version
anterior entera) y vuelve a abrir. La version anterior queda guardada:
**Volver a la version anterior** (`--update-rollback`).

### Asistente de IA (MCP)

Un servidor **MCP** integrado deja que un LLM con visión maneje y vea toda la
app: escenas, fuentes, audio, salidas, configuración, sala y oradores, con
capturas del programa, de cada persona y de la interfaz. Solo en
`127.0.0.1:8790` con token (cifrado en `Credenciales/`; `bin/64bit/ba-mcp
--token` lo muestra); puente stdio `ba-mcp`. Ver
[`docs/MCP.md`](docs/MCP.md).

### Asistente de IA dentro de la app

La pestaña **✦** (abajo de todo en la barra) es un chat con el LLM que maneja la
app: corre la interfaz oficial de **Claude Code** (suscripción Pro/Max o clave
de API), **Gemini CLI** (cuenta de Google o clave) u **opencode** (OpenCode Go u
OpenRouter) en una terminal propia, ya conectada al servidor MCP y con un
prompt del sistema que explica cada parte del sistema y las buenas prácticas.
El agente se instala desde la pestaña (versión fijada y verificada) dentro de
la carpeta portable. Con las **vigilancias**, el servidor avisa solo en ese
chat cuando algo pasa (un canal satura, termina un tema, a un orador se le
acaba el tiempo, alguien espera en el lobby...) y puede actuar en el acto. Ver
[`docs/MCP.md`](docs/MCP.md).

**Salvaguarda.** Lo que escriben los invitados o el público nunca se convierte
en acciones solas: el servidor MCP marca en código todo contenido que viene de
ellos, y una sesión del asistente que lo leyó no puede modificar nada sin que
el operador lo apruebe con un click (la tarjeta muestra la acción exacta). No
depende del modelo. El asistente además puede avisar en una **barra abajo de
la ventana** (color según prioridad), hacer preguntas y sugerir respuestas a
los invitados que se mandan con un click.

### Entradas de video

Página **Entradas** (◉ en la barra) y por MCP: cámaras USB y capturadoras
(V4L2 / DirectShow / AVFoundation, con formato y resolución), cámaras IP y
señales de red por RTSP, RTMP(S), **SRT**, HLS/HTTPS y UDP (FFmpeg del bundle,
con reconexión y latencia baja), y **NDI®** con una implementación abierta
(libNoIDea de VideoLAN, LGPL, experimental; recibe NDI completo/SpeedHQ, no
NDI|HX) — sin el SDK propietario, en Linux y en Windows. Miniatura viva de cada entrada. NDI® es una
marca de Vizrt NDI AB; este proyecto no está afiliado a Vizrt.

### Audio: rack por fuente (LSP)

Pestaña **Audio** (🎚 en la barra): a la izquierda las fuentes con audio (micrófonos,
capturas y también videos); al elegir una aparece su **patchbay** — `in` → módulos →
`out` — donde se agregan módulos (gate, compresor, de-esser, EQ, multibanda,
limitador, analizador, generador o cualquiera de los ~200 de LSP), se conectan
arrastrando desde las salidas y se desconectan con ✕. Cada módulo muestra su
nivel de entrada/salida y cuánto está bajando; el panel de detalle tiene los
parámetros en unidades reales, presets y el botón para abrir la interfaz
original de LSP. El analizador da LUFS (momentáneo, corto, integrado), espectro
por tercios, piso de ruido y pistas de mejora. Los racks se guardan en
`Config/audio-racks.json`. Por MCP el asistente mide, arma y calibra (p. ej. un
gate midiendo con la persona callada y hablando): `help {"topic": "audio_rack"}`.

### YouTube, Twitch y Facebook

Una pestaña por plataforma en la barra izquierda. Sin configurar, cada una
explica paso a paso (con enlaces) cómo crear la app propia en Google Cloud,
dev.twitch.tv o developers.facebook.com; ya conectada, administra videos,
vivos (crear, transmitir desde la app, terminar), canal/Página y chat. La
fuente de escena **Chat en vivo** mezcla los chats de las tres y el de la sala
sobre el video. Todo también por MCP. Credenciales y tokens quedan cifrados
en `Credenciales/` dentro de la carpeta portable; la clave de transmisión,
solo en memoria. Facebook solo permite transmitir a Páginas (no a perfiles).

Secretos por entorno, nunca por línea de comandos (se ven en la lista de
procesos): `BA_GATEWAY_PIN` (si falta, se genera uno y se muestra en la app),
`BA_TUNNEL_TOKEN` (Cloudflare), `BA_WG_PRIVATE_KEY` / `BA_WG_PSK` (WireGuard;
si falta, la clave vive cifrada en `Credenciales/`). La clave HMAC
de los tokens es aleatoria por arranque (`BA_GATEWAY_HMAC_SECRET` la fija).

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

### (c) Windows x64 desde Linux (clang-cl, ABI de MSVC)

```bash
make win BA_ACCEPT_MSVC_LICENSE=1   # baja todo y compila en build/win/
make test-win                       # corre los tests de Windows bajo Wine portable
make dist-win                       # dist/BroadcastAppliance-<ver>-windows-x64.zip
```

Todo el build de Windows usa el **ABI de MSVC**: el libwebrtc precompilado para
Windows (Shiguredo M123) está compilado con la STL de MSVC y su CRT estático, y
OBS y Qt (`msvc2019_64`) también son MSVC. `make win` descarga (con hash) y deja
en `extern/`, sin instalar nada en el sistema:

- **LLVM** (llvm-mingw): `clang-cl`, `lld-link`, `llvm-rc`, `llvm-dlltool`;
- el **CRT de MSVC y el Windows SDK** con [xwin](https://github.com/Jake-Shadle/xwin)
  (versiones exactas en `tools/deps/win-toolchain.sh`), más las DLL
  redistribuibles del CRT que viajan junto a los `.exe`. **Bajarlos implica
  aceptar la licencia de Visual Studio Build Tools**: por eso hace falta
  `BA_ACCEPT_MSVC_LICENSE=1` la primera vez;
- **Qt 6.6.3** `msvc2019_64` (más el Qt del host para `moc`/`rcc`), el runtime de
  **OBS 31** (sin su interfaz, su Qt ni CEF) con los headers del tag, y
  **libwebrtc** de Windows en `extern/webrtc-win`;
- para **NDI**: libNoIDea y libmicrodns (por commit, `tools/deps/ndi-src.sh`) y
  los headers + `.lib` de FFmpeg del `obs-deps` oficial que fija OBS 31 (FFmpeg
  n7.0.2, el mismo de las DLL de OBS que viajan: se decodifica SpeedHQ con
  ellas). Lo que libNoIDea espera de MinGW (`pthread.h`, `unistd.h`) lo cubre
  `cmake/ndi-win/`;
- para el **rack de audio**: los headers de **CLAP** y los módulos **LSP
  1.2.35**, que no tienen binarios oficiales de Windows: `tools/deps/lsp-win.sh`
  los compila desde la fuente oficial con el mismo llvm-mingw (plugin con ABI de
  C: lo carga el host de la app sin importar el compilador) y un PHP estático
  que usan sus generadores. Las herramientas que LSP compila para el host
  (Linux) usan libsndfile/cairo/X11 de `extern/obs-deps-linux` o, si no está,
  los paquetes `-dev` del sistema (solo para compilar: no viajan).

Requisito de Windows: **10 versión 1809** o posterior (la terminal de la pestaña
Asistente usa ConPTY; los agentes se descomprimen con el `tar.exe` del sistema).

El launcher y el gateway se compilan con el CRT estático (`/MT`, lo exige
libwebrtc) en un sub-build propio (`cmake/BaWinStaticPart.cmake`); la interfaz y
el módulo de OBS con `/MD`, como Qt y OBS. `make release-win` termina con
`tools/deps/check-portable-win.sh`, que falla si algún `.exe`/`.dll` del bundle
importa una DLL que no viaja adentro ni es parte de Windows 10+
(`tools/deps/windows-baseline.txt`).

`make test-win` corre los tests de los dos árboles de Windows con un Wine
portable (fijado por hash, en `extern/toolchain/wine`): los de la parte `/MT`
usan el CRT de Microsoft enlazado estático, así que detectan diferencias del CRT
real que el de Wine taparía. Lo único que Wine no emula (la exclusividad de
`FILE_FLAG_FIRST_PIPE_INSTANCE`) se saltea solo bajo Wine.

### (d) Documentación

```bash
make docs     # Sphinx -> docs/_build/html (usa un venv local .venv/)
```

Genera el sitio HTML con Sphinx + sphinx-rtd-theme desde `docs/`. La integración
con Doxygen (breathe) es opcional y no es requerida por el build.

---

## Tests

Dos suites:

- **C++ (ctest)** — unit tests de `ba_core`, del rack de audio, del gateway y
  del módulo OBS, en Linux:

  ```bash
  make test             # o: ctest --test-dir build --output-on-failure
  ```

  **43** tests (42 pasan + 1 saltado): núcleo (`test_platform`,
  `test_shared_audio_ring`, `test_health_block`, `test_ipc_cross_process`,
  `test_control_protocol`, `test_spout_transport`, `test_video_cross_process`,
  `test_channel_strip`, `test_automixer`, `test_resampler`, `test_token_service`,
  `test_snapshot_store`, `test_restore`, `test_diag`, `test_fault`,
  `test_secrets`, `test_device_match`, `test_ring_drift`), launcher
  (`test_shadow_copy`), audio (`test_clap_host`, `test_audio_dsp`,
  `test_audio_rack`, `test_audio_modules`),
  módulo OBS (`test_ba_sources_real`), interfaz (`test_dashboard_model`,
  `test_qml_syntax`) y gateway (`test_room`, `test_gateway_http`,
  `test_wireguard`, `test_ipstack`, `test_webrtc_loopback`,
  `test_gateway_integration`).

  `test_gateway_integration` (el test histórico contra el SDK de Windows) se
  reporta **Skipped**; el WebRTC real de Linux lo prueba
  `test_webrtc_loopback`. `test_ba_sources_real` solo se registra si libobs
  está en `extern/obs-linux`. Detalle en [`docs/tests.rst`](docs/tests.rst).

- **Python (pytest)** — suite de invariantes del repositorio en
  [`tests/`](tests/) (docs, packaging, pins, tests C++):

  ```bash
  python3 -m venv .venv
  .venv/bin/pip install -r requirements-dev.txt
  .venv/bin/pytest -q
  ```

### CI/CD

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) usa los **mismos targets
de make** que el desarrollo local, así que todo lo de terceros se baja o se
compila desde fuente fijada (con `extern/` en la caché de Actions):

| Job | Qué hace |
|---|---|
| `lint` | clang-format (versión fijada) |
| `linux` | `make deps-linux` (libobs y sus deps desde fuente, Qt, libwebrtc…), build, ctest, pytest y `make dist-linux` |
| `windows` | `make win` (clang-cl desde Linux), `make test-win` (Wine) y `make dist-win` |
| `sanitizers` | ASan/UBSan y TSan sobre la suite |
| `release` | con un tag `v*`: publica en un GitHub Release el `.tar.gz` de Linux y el `.zip` de Windows con sus `.sha256` |

El job de Windows acepta la licencia de Microsoft del CRT/SDK
(`BA_ACCEPT_MSVC_LICENSE=1`) en nombre del dueño del repositorio.

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

  Linux: `make release` → `release/` (verificado por `check-portable.sh`);
  `make dist-linux` → `dist/BroadcastAppliance-<ver>-linux-x64.tar.gz`.
  Windows: `make release-win` → `release-win/` (verificado por
  `check-portable-win.sh`); `make dist-win` →
  `dist/BroadcastAppliance-<ver>-windows-x64.zip`. Cada bundle lleva todo su
  runtime (libobs y plugins, Qt, CRT de MSVC en Windows, cloudflared): se
  descomprime y corre, o se aplica sobre una instalación con `--update`.

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
│   └── obs-module/           # ba-sources — módulo libobs (fuentes de invitado + DSP)
├── assets/                   # assets de despliegue (no se compilan)
│   └── web/                  #   apps web: invitado, admin, pizarra (las sirve B)
├── packaging/                # flatpak (manifiesto) + .desktop
├── docs/                     # documentación Sphinx (módulos, compilación, tests)
├── tests/                    # suite Python de invariantes del repo (pytest)
└── .github/workflows/ci.yml  # CI/CD: lint + linux + windows (clang-cl + Wine) + sanitizers + release
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
| CLAP (headers) | `1.2.6` | MIT | Host de módulos de audio |
| LSP Plugins | `1.2.35` | LGPL-3.0 | Módulos del rack de audio (`plugins/lsp/`; Windows: compilado desde fuente) |
| obs-studio (libobs) | `31.0.0` | GPL-2.0 | Motor de emisión (Linux: desde fuente en `extern/obs-linux`; Windows: runtime prebuilt) |
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
libsamplerate (MIT), Argon2 (CC0/Apache-2.0), CLAP (MIT) y LSP (LGPL), entre
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
2. `make test` debe pasar los **23** tests (22 + 1 saltado sin libwebrtc).
3. `pytest` debe pasar la suite de `tests/`.
4. Si tocas `versions.cmake`, documenta el cambio en el changelog y re-valida el
   preflight contra el tag resultante.

Las piezas plataforma-gated (libobs, Qt, WASAPI/D3D, libwebrtc, Win32) se
validan de compilación en Linux y su comportamiento real en Windows/MSVC.
