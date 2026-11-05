[English](../en/building.md) · **Español** · [← README](../../README.es.md)

# Compilar, probar y empaquetar

Para quien desarrolla: lo que hace falta para compilar, cómo levantar el sistema desde el código, los tests y cómo se arma el paquete portable.

## Requisitos de build

Todo lo que hay que tener instalado para compilar, probar y linterizar el
repo. Las **dependencias de código** (mbedTLS, nlohmann, libsamplerate, Argon2,
CLAP, LSP, GStreamer, Qt, libwebrtc, cloudflared) NO se instalan a mano: `make deps`
las baja y las compila en `extern/`, con la versión y el SHA-256 fijados en
[`versions.cmake`](../../versions.cmake) y [`mk/versiones.mk`](../../mk/versiones.mk). Lo de esta sección es la **toolchain** del
host.

Los **assets de despliegue** de Windows (los del bloque "plataforma-gated" de
[Dependencias](arquitectura.md#dependencias): Qt, GStreamer, libwebrtc M123,
cloudflared y LSP) no se instalan a mano ni por `FetchContent`:
`make deps-win` los descarga a `extern/` (gitignored, idempotente, verificado
por SHA-256). Hay targets individuales `make deps-qt`, `deps-gstreamer-win`, `deps-webrtc`,
`deps-cloudflared` y `deps-lsp-win` (y `deps-status`/`deps-clean`; `make pasos` los lista).

### Herramientas obligatorias

| Herramienta | Mínimo | Notas |
|---|---|---|
| **CMake** | 3.21 | `cmake_minimum_required(VERSION 3.21)` en el CMakeLists raíz. |
| **Compilador C++20** | gcc ≥ 13 / clang ≥ 15 / MSVC 2022 | C++20 obligatorio en todo el árbol. En Linux nativo: `g++` (gcc). |
| **GNU Make** | 3.81 | El [`Makefile`](../../Makefile) envuelve a CMake. |
| **git** | — | `FetchContent` baja las deps por git; se necesita en el PATH. |

> **Referencia de este host** (Ubuntu 24.04 / noble, donde el verde está
> verificado): gcc/g++ 13.3.0, CMake 3.28.3, GNU Make 4.3, git 2.43.0.

### Motor de emisión: GStreamer (Linux)

El **motor de emisión** (`src/motor`) corre sobre GStreamer. En Windows se usa
el GStreamer oficial de MSVC (`make deps-gstreamer-win`) y en macOS el
framework oficial (`make deps-gstreamer-macos`); en Linux no hay binarios
oficiales, así que se **compila desde fuente** (`make deps-gstreamer-linux`,
[`tools/deps/gstreamer-linux.sh`](../../tools/deps/gstreamer-linux.sh)) hacia
`extern/gstreamer-linux/`:

```bash
make deps-terceros-linux   # FFmpeg, x264, SRT, cairo... desde fuente, en extern/
make deps-gstreamer-linux  # GStreamer contra ellas, en extern/gstreamer-linux
```

Primero compila **desde fuente fijada** (URL + SHA-256, o commit de git) las
bibliotecas de terceros hacia `extern/obs-deps-linux/` (el nombre es de cuando
eran las dependencias de libobs)
([`tools/deps/terceros-linux.sh`](../../tools/deps/terceros-linux.sh)): FFmpeg 7.1.1
(sin bibliotecas externas; con VAAPI y NVENC), x264, libva (el cargador: los
drivers de video son del sistema), libv4l2, SRT y mbedTLS, cairo, pixman,
libpng y libsndfile (los piden los plugins LSP) y libmicrodns; también nasm y
meson para compilarlas. Después compila GStreamer contra ellas. El build del
proyecto toma todo **solo** de `extern/` y el bundle de `make release` se lleva
todo con rpath `$ORIGIN`: corre en una máquina sin nada instalado. Si
`extern/gstreamer-linux` no está, `ba-app` se compila sin motor y el árbol
sigue verde.

`make release` termina con
[`tools/deps/check-portable.sh`](../../tools/deps/check-portable.sh): resuelve cada
dependencia de cada ELF del bundle como lo hace el cargador dinámico (con el
RUNPATH del propio archivo) y **falla** si algo sale de la base del sistema
([`tools/deps/linux-baseline.txt`](../../tools/deps/linux-baseline.txt): glibc,
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

`nv-codec-headers` (header-only) **no** se instala: lo vendorea
`make deps-terceros-linux` dentro de `extern/`.

> **Contexto gráfico.** El compositor de video usa GL. En un servidor headless
> sin sesión gráfica el pipeline de vídeo no puede inicializarse:
> `ba-app --engine-selftest` lo detecta y devuelve 77 (*saltado*) en vez de
> error. El resto —codificadores, salidas, escena, IPC— sí se ejercita
> headless.

### Herramientas por tarea (opcionales según lo que hagas)

| Tarea | Herramienta | Cómo se obtiene |
|---|---|---|
| **Lint** (`make lint`) | `clang-format` | `sudo apt install clang-format` (noble → 18.1.3). El Makefile usa el binario de la distro y **no descarga nada**; si no está, `make lint` falla con un mensaje claro. La versión esperada queda fijada en `versions.cmake` (`CLANG_FORMAT_VERSION`) y CI la aserta. |
| **Suite Python** (`pytest`) | Python ≥ 3.10 + `venv` | `.venv` local (nunca global) con [`requirements-dev.txt`](../../requirements-dev.txt) (pytest). |
| **Docs** (`make docs`) | Python + Sphinx + sphinx-rtd-theme | Se instala en el `.venv` desde `docs/requirements.txt`. |
| **Caché de build** (acelera) | `ccache` | Opcional: si está en el PATH, CMake lo usa como launcher de C/C++ automáticamente. |
| **Windows desde Linux** (`make win`) | nada instalado: `make win` baja LLVM, el CRT/SDK de MSVC (xwin), Qt y GStreamer a `extern/` | Requiere aceptar la licencia de Microsoft (`BA_ACCEPT_MSVC_LICENSE=1`). `ninja` y `7z` (`p7zip-full`) para `make dist-win`. |

### Reglas del entorno de desarrollo

- **Sin instalaciones globales**: las deps de Python van en un venv local
  (`.venv/`); las bibliotecas de terceros de C++ (mbedTLS, ZXing, libsamplerate, argon2,
  libdeflate) las compila una vez `make deps` en `extern/estaticas/<plataforma>-<huella>` y se
  usan como objetivos IMPORTED (`-DBA_ESTATICAS_DESDE_FUENTE=ON` las compila dentro del build,
  p. ej. para los sanitizers). `clang-format` y `ccache` son las únicas herramientas del sistema
  que se asumen instaladas por apt.
- **C++20** obligatorio; el build falla si el compilador no lo soporta.
- El build es **self-contained**: basta el toolchain de la tabla anterior. La imagen de Docker de
  [`tools/ci/compilar.Dockerfile`](../../tools/ci/compilar.Dockerfile) solo se usa para compilar
  Linux y Windows desde una Mac y en el CI.

---

## Puesta en marcha

Secuencia completa desde cero, en Linux. Los pasos 1 y 2 se hacen una sola vez.

### 1. Clonar e instalar las dependencias de build

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
```

Después instalá los paquetes de desarrollo de la sección
[Requisitos de build](#motor-de-emisión-gstreamer-linux). Son para compilar
**GStreamer y sus dependencias**, no dependencias del producto: el bundle final
no las necesita.

### 2. Compilar las dependencias

```bash
make deps-linux
```

Compila desde fuente las bibliotecas de terceros y GStreamer, y baja Qt,
libwebrtc y el resto (todo en `extern/`). **Tarda** (la primera vez, del orden
de una hora en una máquina de escritorio) y no instala nada en el sistema.

### 3. Compilar y probar el proyecto

```bash
make build
make test
```

El configure imprime `GStreamer: <ruta>` cuando el motor de emisión se va a
compilar; si dice `no está`, el paso 2 no terminó bien y `ba-app` queda sin
motor.

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

- **Copia sombra**: A y B corren desde una copia de `bin/`, `gst-plugins/` y
  `data/` en `~/.cache/BroadcastAppliance/<versión>` (así el bundle se puede
  actualizar en disco con el sistema corriendo). La versión sale del contenido:
  tras un `make release` se usa una copia nueva. `--no-shadow` la desactiva.
- **Los datos** (`Config/`, `Logs/`, `Recordings/`, `Web/`) siempre se leen y
  escriben en el bundle real.
- **Si el launcher se cae**, A y B siguen al aire; al volver a lanzarlo los
  adopta sin cortar nada.

> Corrélo desde una **sesión gráfica**: el compositor de video necesita un
> contexto GL. Por SSH sin display, A no arranca el motor y el launcher lo
> reintenta.

## Compilación

El build es **CMake** (C++20, Qt 6.8 LTS, lo de terceros ya compilado en `extern/` por
`make deps`); el
[`Makefile`](../../Makefile) lo envuelve para que el flujo sea simple. `ccache` se
activa solo si está en el PATH.

**Los comandos.** Un verbo y, si hace falta, la plataforma al final (`mac`,
`linux`, `windows`); sin plataforma es la de la máquina donde se corre. `make` a
secas muestra la ayuda.

| Comando | Qué hace |
|---|---|
| `make deps` | lo de terceros para compilar acá, en `extern/` (`deps-mac`, `deps-linux`, `deps-windows`) |
| `make compilar` | compila en `build/`, sin tests (configura CMake solo si hace falta) |
| `make test` / `make probar` | compila lo que cambió y corre solo los tests cuya huella cambió (`test-rapido` sin los lentos; `test-explicar` dice qué correría y por qué) |
| `make test-completo` / `make probar-todo` | todos los tests, sin caché (antes de publicar) |
| `make app` | el programa armado para usarlo acá (Mac: la `.app` en ~14 s sin cambios; `app-mac-de-cero` la rehace) |
| `make paquete-mac` / `paquete-linux` / `paquete-windows` | un paquete en `dist/` con su `.sha256`; Linux y Windows en Docker si la máquina es una Mac |
| `make paquetes` | los tres |
| `make publicar` | **todo**: los tres paquetes de cero, con todos los tests, y los sube a un release de GitHub |

Los nombres de antes (`build`, `test`, `release`, `win`, `test-win`,
`dist-win`, `macos`, `dmg-macos`…) siguen: son los pasos sueltos que usan los
comandos de arriba, Docker y el CI. `make pasos` los lista. El
[`Makefile`](../../Makefile) tiene los comandos del día; el resto está por tema en
[`mk/`](../../mk) (versiones y SHA-256 de lo de terceros, deps, desarrollo, linux,
mac, windows, tablero-web, modelos, publicar).

**Lo óptimo va solo, sin flags a mano:**

- `-j`: los núcleos de la máquina; si está el árbitro de agentes
  (`~/.claude/arbitro/bin/turno`), el que da `turno jobs`. `JOBS=n` lo fija.
- CMake se configura solo si hace falta
  ([`tools/ci/preparar-build.sh`](../../tools/ci/preparar-build.sh)): si `build/`
  no está configurado o cambió una opción pedida (`PERFILAR`, `DIAG_ACOPLES`…).
  Si no, `cmake --build` vuelve a configurar solo cuando cambia un `CMakeLists.txt`.
  `make app` y `make test` no cambian las opciones de un `build/` que ya existe.
- En la Mac: Ninja, `arm64` y macOS 13.3 en todo configure (así un worktree comparte
  la caché de ccache con el árbol principal) y `ZERO_AR_DATE=1` (enlaces
  reproducibles: reenlazar lo mismo da el mismo binario y la caché de tests no
  vuelve a correrlo).
- En un worktree de git, `extern/` y `build/modelo-mac` son enlaces a los del
  árbol principal (los crea el Makefile si faltan).
- ccache: lo usa el `CMakeLists.txt` si está en el PATH (también en Docker y el CI), con la
  `sloppiness` que el build necesita puesta en el repo, no en la configuración de cada uno.
- Headers precompilados ([`cmake/BaPch.cmake`](../../cmake/BaPch.cmake)): solo cabeceras de
  terceros (la estándar, nlohmann/json, QtCore, GStreamer), nunca nuestras, compartidos entre
  objetivos con las mismas banderas. Apagados con clang-cl. `-DBA_PCH=OFF` los apaga (para ver
  si a un `.cpp` le falta un include que el PCH le tapaba; las cabeceras las revisa sin PCH
  `test_headers_solos`).
- El QML va compilado dentro del binario (`qt_add_qml_module`, solo bytecode): al arrancar no
  se lee ninguna caché de QML del disco.
- Linux y Windows desde una Mac: `make paquete-linux` / `paquete-windows`, en
  Docker ([`tools/ci/compilar-en-docker.sh`](../../tools/ci/compilar-en-docker.sh)):
  - De a una corrida por máquina (un candado en `~/.cache/broadcast-appliance/docker.lock`):
    comparten el volumen `ba-trabajo` y colima. Colima la prende y la apaga la corrida.
  - Capas: después de integrar a master, `make docker-base` (desde el árbol principal, en
    master) compila el Linux y el Windows de master como base de solo lectura. Cualquier otra
    corrida compila en su propia capa copy-on-write encima, así que recompila solo lo que
    difiere de master; capas de worktrees distintos pueden correr a la vez. `make docker-capas`
    las lista y `make docker-capas-limpiar DIAS=7` borra las viejas.
  - Las validaciones van sin información de depuración (`DEPURACION=0`, la mitad del disco).
    Los símbolos (`.pdb`, `.debug`) se arman y se guardan solo con `SIMBOLOS=1`, que pone
    `make publicar`, en un almacén único (`dist/simbolos` del árbol principal).
  - `make validar-arm64` compila y prueba nativo en arm64 (sin Rosetta) para validar; los
    paquetes siguen saliendo de x86_64. Experimental: su primera compilación entera fue más
    lenta que la emulada, falta entender por qué.

### (a) Linux nativo

```bash
make deps        # una vez
make compilar    # configura (idempotente) y compila en build/
make probar
make app         # instala el bundle portable en release/
```

### (b) Paquetes y release

```bash
make paquetes    # dist/: el .tar.gz, el .zip y (en una Mac) el .dmg
make publicar    # lo mismo de cero, con todos los tests, y lo sube a GitHub
```

### (c) Windows x64 desde Linux (clang-cl, ABI de MSVC)

```bash
make deps-windows BA_ACCEPT_MSVC_LICENSE=1   # LLVM, CRT/SDK de MSVC, Qt, GStreamer... a extern/
make compilar-windows                        # compila en build/win/
make paquete-windows                         # tests bajo Wine portable y dist/…-windows-x64.zip
```

Todo el build de Windows usa el **ABI de MSVC**: el libwebrtc precompilado para
Windows (Shiguredo M123) está compilado con la STL de MSVC y su CRT estático, y
GStreamer y Qt (`msvc2022_64`) también son MSVC. `make win` descarga (con hash) y deja
en `extern/`, sin instalar nada en el sistema:

- **LLVM** (llvm-mingw): `clang-cl`, `lld-link`, `llvm-rc`, `llvm-dlltool`;
- el **CRT de MSVC y el Windows SDK** con [xwin](https://github.com/Jake-Shadle/xwin)
  (versiones exactas en `tools/deps/win-toolchain.sh`), más las DLL
  redistribuibles del CRT que viajan junto a los `.exe`. **Bajarlos implica
  aceptar la licencia de Visual Studio Build Tools**: por eso hace falta
  `BA_ACCEPT_MSVC_LICENSE=1` la primera vez;
- **Qt 6.8.3** `msvc2022_64` (más el Qt del host para `moc`/`rcc`; las DLL que
  viajan salen de ese mismo SDK), **GStreamer** oficial de MSVC (con su FFmpeg)
  y **libwebrtc** de Windows en `extern/webrtc-win`;
- para **NDI**: libNoIDea y libmicrodns (por commit, `tools/deps/ndi-src.sh`;
  el video NDI lo decodifica GStreamer). Lo que libNoIDea espera de MinGW
  (`pthread.h`, `unistd.h`) lo cubre `cmake/ndi-win/`;
- para el **rack de audio**: los headers de **CLAP** y los módulos **LSP
  1.2.35**, que no tienen binarios oficiales de Windows: `tools/deps/lsp-win.sh`
  los compila desde la fuente oficial con el mismo llvm-mingw (plugin con ABI de
  C: lo carga el host de la app sin importar el compilador) y un PHP estático
  que usan sus generadores. Las herramientas que LSP compila para el host
  (Linux) usan libsndfile/cairo/X11 de `extern/obs-deps-linux`
  (`make deps-terceros-linux`) o, si no está,
  los paquetes `-dev` del sistema (solo para compilar: no viajan).

Requisito de Windows: **10 versión 1809** o posterior (la terminal de la pestaña
Asistente usa ConPTY; los agentes se descomprimen con el `tar.exe` del sistema).

El launcher y el gateway se compilan con el CRT estático (`/MT`, lo exige
libwebrtc) en un sub-build propio (`cmake/BaWinStaticPart.cmake`); la interfaz
con `/MD`, como Qt y GStreamer. `make release-win` termina con
`tools/deps/check-portable-win.sh`, que falla si algún `.exe`/`.dll` del bundle
importa una DLL que no viaja adentro ni es parte de Windows 10+
(`tools/deps/windows-baseline.txt`).

`make test-win` corre los tests de los dos árboles de Windows con un Wine
portable (fijado por hash, en `extern/toolchain/wine`): los de la parte `/MT`
usan el CRT de Microsoft enlazado estático, así que detectan diferencias del CRT
real que el de Wine taparía. Lo único que Wine no emula (la exclusividad de
`FILE_FLAG_FIRST_PIPE_INSTANCE`) se saltea solo bajo Wine.

### (c2) macOS Apple Silicon (nativo, en desarrollo)

Rama `macos`. Se compila en la Mac (Xcode completo + `brew install cmake
ninja meson pkg-config`):

```bash
make deps        # GStreamer, Qt, libwebrtc macOS, LSP... -> extern/
make compilar    # compila en build/
make probar
make app         # dist/BroadcastAppliance.app (verificada con otool, firmada ad-hoc)
make paquete-mac # dist/…-macos-arm64.dmg
```

Guía completa, qué probar y qué falta: [`docs/desarrollo/MACOS.md`](../../docs/desarrollo/MACOS.md).

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
  del motor, en Linux:

  ```bash
  make test             # o: ctest --test-dir build --output-on-failure
  ```

  La cantidad exacta la informa `make test`. Incluye: núcleo (`test_platform`,
  `test_shared_audio_ring`, `test_health_block`, `test_ipc_cross_process`,
  `test_control_protocol`, `test_spout_transport`, `test_video_cross_process`,
  `test_automixer`, `test_resampler`, `test_token_service`,
  `test_snapshot_store`, `test_restore`, `test_diag`, `test_fault`,
  `test_secrets`, `test_device_match`, `test_ring_drift`), launcher
  (`test_shadow_copy`), audio (`test_clap_host`, `test_audio_dsp`,
  `test_audio_rack`, `test_audio_modules`),
  interfaz (`test_dashboard_model`,
  `test_qml_syntax_*`) y gateway (`test_room`, `test_gateway_http`,
  `test_wireguard`, `test_ipstack`, `test_webrtc_loopback`). El WebRTC real
  lo prueba `test_webrtc_loopback`. Detalle en [`docs/desarrollo/tests.rst`](../../docs/desarrollo/tests.rst).

- **Python (pytest)** — suite de invariantes del repositorio en
  [`tests/`](../../tests/) (docs, packaging, pins, tests C++):

  ```bash
  python3 -m venv .venv
  .venv/bin/pip install -r requirements-dev.txt
  .venv/bin/pytest -q
  ```

### CI/CD

[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) usa los **mismos targets
de make** que el desarrollo local, así que todo lo de terceros se baja o se
compila desde fuente fijada (con `extern/` en la caché de Actions):

| Job | Qué hace |
|---|---|
| `lint` | clang-format (versión fijada) |
| `linux` | `make deps-linux` (GStreamer y sus deps desde fuente, Qt, libwebrtc…), build, ctest, pytest, `make dist-linux` y humo del bundle publicado (autotest del motor bajo Xvfb, sin `extern/`) |
| `windows` | `make win` (clang-cl desde Linux), `make test-win` (Wine) y `make dist-win` |
| `sanitizers` | ASan/UBSan y TSan sobre la suite |
| `release` | en **cada push a `master`** con todo en verde (tag automático `v<versión>-build.<n>`) o con un tag `v*`: publica en un GitHub Release el `.tar.gz` de Linux y el `.zip` de Windows con sus `.sha256` |

El job de Windows acepta la licencia de Microsoft del CRT/SDK
(`BA_ACCEPT_MSVC_LICENSE=1`) en nombre del dueño del repositorio. Un push nuevo
a la misma rama cancela la corrida anterior.

**Probar el CI en local antes de subir** con [act](https://github.com/nektos/act)
(fijado por hash, corre en Docker con la imagen de runner de Ubuntu 24.04):

```bash
tools/ci/act.sh lint          # o sanitizers | linux | windows
BA_ACT_KEEP=1 tools/ci/act.sh linux   # reusa el extern/ de la corrida anterior
```

Cada corrida parte de una copia limpia del repo (con los cambios sin commitear)
en `/tmp/ba-act` y de un `extern/` vacío, como en GitHub; solo reusa los
tarballs ya verificados de `extern/.downloads`.

---

## Empaquetado

- **Bundle portable** (layout §5.1):

  ```
  <prefix>/
  ├── bin/64bit/            # ejecutables y bibliotecas (GStreamer, Qt, FFmpeg...)
  ├── gst-plugins/          # plugins de GStreamer propios
  ├── Config/               # configuración del usuario
  ├── Logs/                 # logs rotativos y dumps
  ├── Recordings/           # grabación de respaldo
  ├── Web/guest/            # app web del invitado (la sirve B)
  ├── LEEME.txt, README.txt # instrucciones para quien lo descomprime (español, inglés)
  ├── LICENSE
  └── Credenciales/, Respaldos/, Runtime/, Cache/, Shares/, Media/, plugins/
                            # se crean al usarlo (ver "Todo en la carpeta portable")
  ```

  Linux: `make release` → `release/` (verificado por `check-portable.sh`);
  `make dist-linux` → `dist/BroadcastAppliance-<ver>-linux-x64.tar.gz`.
  Windows: `make release-win` → `release-win/` (verificado por
  `check-portable-win.sh`); `make dist-win` →
  `dist/BroadcastAppliance-<ver>-windows-x64.zip`. Cada bundle lleva todo su
  runtime (GStreamer y sus plugins, Qt, CRT de MSVC en Windows, cloudflared): se
  descomprime y corre, o se aplica sobre una instalación con `--update`.

- **Flatpak** (Linux): manifiesto en
  [`packaging/flatpak/io.github.blairq.broadcastappliance.json`](../../packaging/flatpak/io.github.blairq.broadcastappliance.json)
  (runtime `org.freedesktop.Platform` 23.08, C++20, módulos mbedtls/nlohmann/
  libsamplerate/argon2) y `.desktop` en
  [`packaging/BroadcastAppliance.desktop`](../../packaging/BroadcastAppliance.desktop).
