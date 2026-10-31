**English** · [Español](../es/compilar.md) · [← README](../../README.md)

# Building, testing and packaging

For developers: what you need to build, how to bring the system up from source, the tests, and how the portable bundle is put together.

## Build requirements

Everything you need installed to build, test and lint the
repo. The **code dependencies** (mbedTLS, nlohmann, libsamplerate, Argon2,
CLAP, LSP, GStreamer, Qt, libwebrtc, cloudflared) are NOT installed by hand: the
build pulls them in via `FetchContent` (self-contained) at the version pinned in
[`versions.cmake`](../../versions.cmake). What this section covers is the host
**toolchain**.

The Windows **deployment assets** (the ones in the "platform-gated" block of
[Dependencies](architecture.md#dependencies): Qt, GStreamer, libwebrtc M123, Spout2,
cloudflared, VC++ redist and LSP) are not installed by hand nor via `FetchContent`:
`make deps-win` downloads them into `extern/` (gitignored, idempotent, verified
by SHA-256). There are individual targets `make deps-qt`, `deps-gstreamer-win`, `deps-webrtc`, `deps-spout`,
`deps-cloudflared`, `deps-vcredist` and `deps-lsp` (plus `deps-status`/`deps-clean`).

### Required tools

| Tool | Minimum | Notes |
|---|---|---|
| **CMake** | 3.21 | `cmake_minimum_required(VERSION 3.21)` in the root CMakeLists. |
| **C++20 compiler** | gcc ≥ 13 / clang ≥ 15 / MSVC 2022 | C++20 is required across the whole tree. On native Linux: `g++` (gcc). |
| **GNU Make** | 3.81 | The [`Makefile`](../../Makefile) wraps CMake. |
| **git** | — | `FetchContent` fetches the deps through git; it must be on the PATH. |

> **Reference for this host** (Ubuntu 24.04 / noble, where green is
> verified): gcc/g++ 13.3.0, CMake 3.28.3, GNU Make 4.3, git 2.43.0.

### Broadcast engine: GStreamer (Linux)

The **broadcast engine** (`src/motor`) runs on GStreamer. On Windows it uses
the official MSVC GStreamer (`make deps-gstreamer-win`) and on macOS the
official framework (`make deps-gstreamer-macos`); on Linux there are no official
binaries, so it is **built from source** (`make deps-gstreamer-linux`,
[`tools/deps/gstreamer-linux.sh`](../../tools/deps/gstreamer-linux.sh)) into
`extern/gstreamer-linux/`:

```bash
make deps-terceros-linux   # FFmpeg, x264, SRT, cairo... from source, into extern/
make deps-gstreamer-linux  # GStreamer against them, into extern/gstreamer-linux
```

First it builds the third-party libraries **from pinned source** (URL + SHA-256, or git commit)
into `extern/obs-deps-linux/` (the name dates from when they
were the libobs dependencies)
([`tools/deps/terceros-linux.sh`](../../tools/deps/terceros-linux.sh)): FFmpeg 7.1.1
(no external libraries; with VAAPI and NVENC), x264, libva (the loader: the
video drivers come from the system), libv4l2, SRT and mbedTLS, cairo, pixman,
libpng and libsndfile (required by the LSP plugins) and libmicrodns; also nasm and
meson to build them. Then it builds GStreamer against them. The project
build takes everything **only** from `extern/`, and the `make release` bundle carries
everything with an `$ORIGIN` rpath: it runs on a machine with nothing installed. If
`extern/gstreamer-linux` is missing, `ba-app` is built without the engine and the tree
stays green.

`make release` finishes with
[`tools/deps/check-portable.sh`](../../tools/deps/check-portable.sh): it resolves every
dependency of every ELF in the bundle the way the dynamic loader does (using the
file's own RUNPATH) and **fails** if anything falls outside the system baseline
([`tools/deps/linux-baseline.txt`](../../tools/deps/linux-baseline.txt): glibc,
video drivers, X11/Wayland, ALSA/Pulse, udev, fonts and whatever the official Qt
requires). The C++ runtime ships in `bin/64bit/cxx/` and the launcher uses the
newer of that one and the system's; the launcher and `ba-mcp` depend only on
libc. The minimum glibc is set by the machine you build on (the check
reports it): for older machines, build the release on an older distro.

Building requires the **headers** of the system baseline (toolchain, not
product dependencies). On Ubuntu 24.04:

```bash
sudo apt install   libgl1-mesa-dev libegl1-mesa-dev libx11-dev libx11-xcb-dev libxcb1-dev   libxcb-randr0-dev libxcb-shm0-dev libxcb-xfixes0-dev libxcb-xinerama0-dev   libxcb-composite0-dev libxcb-damage0-dev libxcb-present-dev libxcb-xkb-dev   libxcomposite-dev libxinerama-dev libxkbcommon-dev libxss-dev   libpulse-dev libasound2-dev libudev-dev   libwayland-dev libdrm-dev libfreetype-dev libfontconfig1-dev   patchelf
```

`nv-codec-headers` (header-only) is **not** installed: it is vendored by
`make deps-terceros-linux` inside `extern/`.

> **Graphics context.** The video compositor uses GL. On a headless server
> with no graphical session the video pipeline cannot initialize:
> `ba-app --engine-selftest` detects this and returns 77 (*skipped*) instead of
> an error. Everything else (encoders, outputs, scene, IPC) is exercised
> headless.

### Tools per task (optional, depending on what you do)

| Task | Tool | How to get it |
|---|---|---|
| **Lint** (`make lint`) | `clang-format` | `sudo apt install clang-format` (noble → 18.1.3). The Makefile uses the distro binary and **downloads nothing**; if it is missing, `make lint` fails with a clear message. The expected version is pinned in `versions.cmake` (`CLANG_FORMAT_VERSION`) and CI asserts it. |
| **Python suite** (`pytest`) | Python ≥ 3.10 + `venv` | Local `.venv` (never global) with [`requirements-dev.txt`](../../requirements-dev.txt) (pytest). |
| **Docs** (`make docs`) | Python + Sphinx + sphinx-rtd-theme | Installed into the `.venv` from `docs/requirements.txt`. |
| **Build cache** (speeds things up) | `ccache` | Optional: if it is on the PATH, CMake uses it as the C/C++ launcher automatically. |
| **Windows from Linux** (`make win`) | nothing installed: `make win` downloads LLVM, the MSVC CRT/SDK (xwin), Qt and GStreamer into `extern/` | Requires accepting the Microsoft license (`BA_ACCEPT_MSVC_LICENSE=1`). `ninja` and `7z` (`p7zip-full`) for `make dist-win`. |

### Development environment rules

- **No global installs**: Python deps go in a local venv
  (`.venv/`); C++ deps are pulled in by `FetchContent` inside the build tree.
  `clang-format` and `ccache` are the only system tools assumed to be
  installed via apt.
- **C++20** is required; the build fails if the compiler does not support it.
- The build is **self-contained**: there is no `Dockerfile` or `.devcontainer`;
  the toolchain in the table above is enough.

---

## Getting started

The full sequence from scratch, on Linux. Steps 1 and 2 are done only once.

### 1. Clone and install the build dependencies

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
```

Then install the development packages from the
[Build requirements](#broadcast-engine-gstreamer-linux) section. They are for building
**GStreamer and its dependencies**, not product dependencies: the final bundle
does not need them.

### 2. Build the dependencies

```bash
make deps-linux
```

Builds the third-party libraries and GStreamer from source, and downloads Qt,
libwebrtc and the rest (all into `extern/`). **It takes a while** (the first time, on the order
of an hour on a desktop machine) and installs nothing on the system.

### 3. Build and test the project

```bash
make build
make test
```

Configure prints `GStreamer: <path>` when the broadcast engine is going to be
built; if it says `no está` (not found), step 2 did not finish properly and `ba-app` is left without
the engine.

### 4. Bring up the whole system

```bash
make release
BA_GATEWAY_PIN=123456 ./release/BroadcastAppliance --no-recovery-ui
```

The launcher starts the gateway (B) and the broadcast engine (A), watches them and
restarts them if they crash or hang. `Ctrl+C` (or `SIGTERM`) performs the orderly
shutdown: it asks A and B to exit over the control channel and, if they don't
respond, terminates them. It can be launched from any directory: paths
are derived from the executable's location. The log goes to `release/Logs/launcher.log`.

Details worth knowing:

- **Shadow copy**: A and B run from a copy of `bin/`, `gst-plugins/` and
  `data/` in `~/.cache/BroadcastAppliance/<version>` (so the bundle can be
  updated on disk while the system is running). The version is derived from the content:
  after a `make release` a new copy is used. `--no-shadow` disables it.
- **Data** (`Config/`, `Logs/`, `Recordings/`, `Web/`) is always read from and
  written to the real bundle.
- **If the launcher crashes**, A and B stay on air; when you launch it again it
  adopts them without interrupting anything.

> Run it from a **graphical session**: the video compositor needs a
> GL context. Over SSH with no display, A does not start the engine and the launcher
> keeps retrying.

## Building

The build is **CMake** (C++20, self-contained via `FetchContent`); the
[`Makefile`](../../Makefile) wraps it to keep the workflow simple. `ccache` is
enabled automatically if it is on the PATH.

**The commands.** A verb and, when needed, the platform at the end (`mac`,
`linux`, `windows`); without a platform it is the one of the machine you run it
on. Plain `make` shows the help.

| Command | What it does |
|---|---|
| `make deps` | the third-party pieces to build here, into `extern/` (`deps-mac`, `deps-linux`, `deps-windows`) |
| `make compilar` | builds into `build/`, no tests |
| `make probar` | the tests, cached (`probar-rapido` without the slow ones, `probar-todo` without cache) |
| `make app` | the program assembled to use here (Mac: the `.app` in ~25 s; `app-mac-de-cero` rebuilds it) |
| `make paquete-mac` / `paquete-linux` / `paquete-windows` | one package in `dist/` with its `.sha256`; Linux and Windows in Docker on a Mac |
| `make paquetes` | all three |
| `make publicar` | **everything**: the three packages from scratch, with all tests, uploaded to a GitHub release |

The old names (`build`, `test`, `release`, `win`, `test-win`, `dist-win`,
`macos`, `dmg-macos`…) still work: they are the individual steps the commands
above, Docker and CI use. `make pasos` lists them.

### (a) Native Linux

```bash
make deps        # once
make compilar    # configures (idempotent) and builds into build/
make probar
make app         # installs the portable bundle into release/
```

### (b) Packages and release

```bash
make paquetes    # dist/: the .tar.gz, the .zip and (on a Mac) the .dmg
make publicar    # the same from scratch, with all tests, uploaded to GitHub
```

### (c) Windows x64 from Linux (clang-cl, MSVC ABI)

```bash
make deps-windows BA_ACCEPT_MSVC_LICENSE=1   # LLVM, MSVC CRT/SDK, Qt, GStreamer... into extern/
make compilar-windows                        # builds into build/win/
make paquete-windows                         # tests under portable Wine and dist/…-windows-x64.zip
```

The whole Windows build uses the **MSVC ABI**: the prebuilt libwebrtc for
Windows (Shiguredo M123) is built with the MSVC STL and its static CRT, and
GStreamer and Qt (`msvc2019_64`) are MSVC too. `make win` downloads (with hash checks) and places
into `extern/`, without installing anything on the system:

- **LLVM** (llvm-mingw): `clang-cl`, `lld-link`, `llvm-rc`, `llvm-dlltool`;
- the **MSVC CRT and the Windows SDK** via [xwin](https://github.com/Jake-Shadle/xwin)
  (exact versions in `tools/deps/win-toolchain.sh`), plus the redistributable
  CRT DLLs that ship next to the `.exe` files. **Downloading them means
  accepting the Visual Studio Build Tools license**: that is why
  `BA_ACCEPT_MSVC_LICENSE=1` is needed the first time;
- **Qt 6.6.3** `msvc2019_64` (plus the host Qt for `moc`/`rcc`; the DLLs that
  ship come from that same SDK), the official MSVC **GStreamer** (with its FFmpeg)
  and Windows **libwebrtc** in `extern/webrtc-win`;
- for **NDI**: libNoIDea and libmicrodns (by commit, `tools/deps/ndi-src.sh`;
  NDI video is decoded by GStreamer). What libNoIDea expects from MinGW
  (`pthread.h`, `unistd.h`) is covered by `cmake/ndi-win/`;
- for the **audio rack**: the **CLAP** headers and the **LSP
  1.2.35** modules, which have no official Windows binaries: `tools/deps/lsp-win.sh`
  builds them from the official source with the same llvm-mingw (a plugin with a
  C ABI: the app's host loads it regardless of the compiler) and a static PHP
  used by their generators. The tools LSP builds for the host
  (Linux) use libsndfile/cairo/X11 from `extern/obs-deps-linux`
  (`make deps-terceros-linux`) or, if that is missing,
  the system `-dev` packages (only for building: they don't ship).

Windows requirement: **10 version 1809** or later (the terminal in the
Assistant tab uses ConPTY; the agents are unpacked with the system `tar.exe`).

The launcher and the gateway are built with the static CRT (`/MT`, required by
libwebrtc) in their own sub-build (`cmake/BaWinStaticPart.cmake`); the UI
with `/MD`, like Qt and GStreamer. `make release-win` finishes with
`tools/deps/check-portable-win.sh`, which fails if any `.exe`/`.dll` in the bundle
imports a DLL that neither ships inside nor is part of Windows 10+
(`tools/deps/windows-baseline.txt`).

`make test-win` runs the tests of both Windows trees with a portable
Wine (pinned by hash, in `extern/toolchain/wine`): the ones in the `/MT` part
use Microsoft's statically linked CRT, so they catch differences in the real CRT
that Wine's would hide. The only thing Wine does not emulate (the exclusivity of
`FILE_FLAG_FIRST_PIPE_INSTANCE`) is skipped automatically under Wine.

### (c2) macOS Apple Silicon (native, in development)

Branch `macos`. Built on the Mac (full Xcode + `brew install cmake
ninja meson pkg-config`):

```bash
make deps        # GStreamer, Qt, libwebrtc macOS, LSP... -> extern/
make compilar    # builds into build/
make probar
make app         # dist/BroadcastAppliance.app (verified with otool, ad-hoc signed)
make paquete-mac # dist/…-macos-arm64.dmg
```

Full guide, what to test and what's missing: [`docs/desarrollo/MACOS.md`](../../docs/desarrollo/MACOS.md).

### (d) Documentation

```bash
make docs     # Sphinx -> docs/_build/html (uses a local venv .venv/)
```

Generates the HTML site with Sphinx + sphinx-rtd-theme from `docs/`. Doxygen
integration (breathe) is optional and not required by the build.

---

## Tests

Two suites:

- **C++ (ctest)** — unit tests for `ba_core`, the audio rack, the gateway and
  the engine, on Linux:

  ```bash
  make test             # or: ctest --test-dir build --output-on-failure
  ```

  `make test` reports the exact count. It includes: core (`test_platform`,
  `test_shared_audio_ring`, `test_health_block`, `test_ipc_cross_process`,
  `test_control_protocol`, `test_spout_transport`, `test_video_cross_process`,
  `test_automixer`, `test_resampler`, `test_token_service`,
  `test_snapshot_store`, `test_restore`, `test_diag`, `test_fault`,
  `test_secrets`, `test_device_match`, `test_ring_drift`), launcher
  (`test_shadow_copy`), audio (`test_clap_host`, `test_audio_dsp`,
  `test_audio_rack`, `test_audio_modules`),
  UI (`test_dashboard_model`,
  `test_qml_syntax`) and gateway (`test_room`, `test_gateway_http`,
  `test_wireguard`, `test_ipstack`, `test_webrtc_loopback`,
  `test_gateway_integration`).

  `test_gateway_integration` (the legacy test against the Windows SDK) is
  reported as **Skipped**; real WebRTC on Linux is tested by
  `test_webrtc_loopback`. Details in [`docs/desarrollo/tests.rst`](../../docs/desarrollo/tests.rst).

- **Python (pytest)** — repository invariants suite in
  [`tests/`](../../tests/) (docs, packaging, pins, C++ tests):

  ```bash
  python3 -m venv .venv
  .venv/bin/pip install -r requirements-dev.txt
  .venv/bin/pytest -q
  ```

### CI/CD

[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) uses the **same make
targets** as local development, so everything third-party is downloaded or
built from pinned source (with `extern/` in the Actions cache):

| Job | What it does |
|---|---|
| `lint` | clang-format (pinned version) |
| `linux` | `make deps-linux` (GStreamer and its deps from source, Qt, libwebrtc…), build, ctest, pytest, `make dist-linux` and a smoke test of the published bundle (engine self-test under Xvfb, without `extern/`) |
| `windows` | `make win` (clang-cl from Linux), `make test-win` (Wine) and `make dist-win` |
| `sanitizers` | ASan/UBSan and TSan over the suite |
| `release` | on **every push to `master`** with everything green (automatic tag `v<version>-build.<n>`) or with a `v*` tag: publishes the Linux `.tar.gz` and the Windows `.zip` with their `.sha256` files to a GitHub Release |

The Windows job accepts the Microsoft CRT/SDK license
(`BA_ACCEPT_MSVC_LICENSE=1`) on behalf of the repository owner. A new push
to the same branch cancels the previous run.

**Test CI locally before pushing** with [act](https://github.com/nektos/act)
(pinned by hash, runs in Docker with the Ubuntu 24.04 runner image):

```bash
tools/ci/act.sh lint          # or sanitizers | linux | windows
BA_ACT_KEEP=1 tools/ci/act.sh linux   # reuses extern/ from the previous run
```

Each run starts from a clean copy of the repo (including uncommitted changes)
in `/tmp/ba-act` and from an empty `extern/`, as on GitHub; it only reuses the
already-verified tarballs in `extern/.downloads`.

---

## Packaging

- **Portable bundle** (§5.1 layout):

  ```
  <prefix>/
  ├── bin/64bit/            # executables and libraries (GStreamer, Qt, FFmpeg...)
  ├── gst-plugins/          # our own GStreamer plugins
  ├── Config/               # user configuration
  ├── Logs/                 # rotating logs and dumps
  ├── Recordings/           # backup recording
  ├── Web/guest/            # guest web app (served by B)
  ├── README.txt, LEEME.txt # instructions for whoever unpacks it (English, Spanish)
  ├── LICENSE
  └── Credenciales/, Respaldos/, Runtime/, Cache/, Shares/, Media/, plugins/
                            # created on use (see "Everything in the portable folder")
  ```

  Linux: `make release` → `release/` (verified by `check-portable.sh`);
  `make dist-linux` → `dist/BroadcastAppliance-<ver>-linux-x64.tar.gz`.
  Windows: `make release-win` → `release-win/` (verified by
  `check-portable-win.sh`); `make dist-win` →
  `dist/BroadcastAppliance-<ver>-windows-x64.zip`. Each bundle carries its whole
  runtime (GStreamer and its plugins, Qt, the MSVC CRT on Windows, cloudflared): you
  unpack it and run it, or apply it over an existing installation with `--update`.

- **Flatpak** (Linux): manifest at
  [`packaging/flatpak/io.github.blairq.broadcastappliance.json`](../../packaging/flatpak/io.github.blairq.broadcastappliance.json)
  (runtime `org.freedesktop.Platform` 23.08, C++20, modules mbedtls/nlohmann/
  libsamplerate/argon2) and `.desktop` at
  [`packaging/BroadcastAppliance.desktop`](../../packaging/BroadcastAppliance.desktop).
