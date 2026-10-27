**English** · [Español](../es/arquitectura.md) · [← README](../../README.md)

# Architecture

How it is built inside: the processes, the core, the status of each part, the repository layout and the dependencies.

## Architecture

The system consists of a launcher (Process 0), two child processes, the video
engine (GStreamer, with our own plugins) and the audio plugin host. The whole
core is a static lib with no UI or engine dependencies.

- **Process 0 — `BroadcastAppliance`** (launcher/supervisor, pure Win32, static
  CRT `/MT`): single instance, integrity check, launching B
  and then A, watchdog (crash via handle + hang via heartbeat), restart with
  backoff and circuit breaker, and orderly shutdown. Works on Windows and on Linux
  on top of `ba::platform`.
- **Process A — `ba-app`** (broadcast engine + operator UI): Qt 6 on top of the
  GStreamer engine (`src/motor`), scenes, encoding and the operator UI
  (Windows, Linux and macOS).
- **Process B — `ba-gateway`** (WebRTC gateway + conversation + tunnel):
  signaling, tunnel (cloudflared) and the conversation engine for guests.
- **Our own GStreamer plugins** (`src/gst-plugins`, in the bundle's `gst-plugins/`):
  filters, digital PTZ, super-resolution, the audio rack and the
  Python plugins.
- **Audio rack** (in [`src/app/audio`](../../src/app/audio/), inside `ba-app`):
  CLAP host (`ClapHost`, X11/Win32 editor in `ClapEditor`), module graph
  (`AudioRack`, one rack per source via `RackManager`),
  catalog with real parameter names (`Modules`) and analysis/generator
  (`Analyzer`).
- **`ba_core`** (core, static lib, in [`src/core`](../../src/core/)): **no Qt, no
  video engine**, unit-testable. Submodules:
  - `ipc/` — shared audio rings, health, control channel, Spout transport.
  - `dsp/` — channel strip, automixer, resampler, biquads, presets.
  - `persist/` — snapshot, restore, state documents.
  - `security/` — secrets (AES-GCM + Argon2id), signaling tokens.
  - `diag/` — rotating logs and export.
  - `fault/` — fault injection.
  - `crypto/` — SHA-256, HMAC.
  - `hw/` — audio device identity.

The **LSP** modules (CLAP) go in the bundle's `plugins/lsp/`: on Linux the
official binary (`make deps-lsp`, with cairo/libsndfile vendored), on Windows
built from the official source (`make deps-lsp-win`).

Architecture rules (spec §14, §3, §9):

- **C++20** is required across the whole tree.
- **`ba_core` is the core**: NO Qt, NO video engine, unit-testable.
- The **launcher** is pure Win32 with a static CRT (`/MT`).
- **A** uses `/MD` (dynamic CRT, for consistency with Qt and GStreamer); the
  gateway goes `/MT` along with the launcher (required by libwebrtc).
- **PDBs** are always emitted (debug info), even in Release.

---

## Status

The processes are **implemented** and run on Linux (native) and on Windows
(cross-built from Linux with clang-cl, MSVC ABI):

- **`ba-app`** (Process A): `MainWindow` (the operator's QMainWindow) +
  `OperatorEngine` (integrates `ba_core::dsp` ChannelStrip/Automixer + presets,
  IPC SharedAudioRing/HealthBlock/ControlEndpoint and SnapshotStore
  persistence). Without GStreamer, `ba-app` is built without the engine (tools
  only).
- **`ba-gateway`** (Process B): the Conference (videoconference with guests). `Room` (lobby,
  participants, chat, files, media routes), `SignalingServer` (HTTP +
  guest WebSocket) and `AdminApi` (REST + SSE control API, only on
  127.0.0.1, used by the app), `Webrtc` (real libwebrtc,
  on Linux too), `MediaPump` (IPC ↔ WebRTC per route), `TlsTerminator`
  (HTTPS), `WgDevice` + `IpStack` (userspace WireGuard + lwIP) and
  `Cloudflared`. See [`docs/funciones/CONFERENCIA.md`](../../docs/funciones/CONFERENCIA.md).
- **Video engine** (`src/motor`, GStreamer): scenes, transitions, DSK,
  clean feed, recording and outputs; remote guests enter the scene
  through Process B's IPC. Its own plugins live in `src/gst-plugins`.
- **Audio rack** (CLAP/LSP, `src/app/audio`): host, per-source graph,
  analyzer and generator. Flag `BA_HAVE_CLAP` (CLAP headers from `make deps-clap`).
- **`BroadcastAppliance`** (launcher): `Supervisor` + `RecoveryUi`. Works
  on Windows and on Linux (and is written for macOS) on top of the
  `ba::platform` platform layer (`src/core/platform/`): launches B and A, detects crashes
  and hangs, restarts with backoff, adopts A and B if the launcher crashes and
  comes back (the broadcast is not interrupted), and performs the orderly shutdown over the control
  channel. The recovery window is still Win32; on Linux the
  incident is logged and retried.
- **`ba-app` Qt/QML UI** (vendored Qt, portable): a dashboard of
  glass cards over the live video, output windows for another
  monitor, scenes and sources, full configuration and the **Conference (Conferencia)** page
  (private lobby, guests with their video, chat, files, direct access).
  Without a display, `ba-app --no-ui` runs in console mode.

Still to do (see [`docs/historia/PLAN_MULTIPLATAFORMA.md`](../../docs/historia/PLAN_MULTIPLATAFORMA.md)):

- **Build and test on Windows (MSVC) and macOS** the UI, the engine and the
  real gateway (today they are validated on Linux), and **CI** with native runners.
- **libwebrtc for macOS** (the Shiguredo SDK exists; the build is missing) and
  **PDF** as a shared file (today: image, video and audio).
- Final **packaging** of the portable bundle (third-party runtime via
  `tools/deploy`).

## Project structure

```
.
├── CMakeLists.txt            # root build (C++20, FetchContent, §5.1 layout)
├── versions.cmake            # PINNED versions of third-party dependencies
├── Makefile                  # wraps CMake (dev/build/prod/docs/test/release/win)
├── pyproject.toml            # metadata + pytest config (invariants suite)
├── requirements-dev.txt      # pytest (for the Python suite)
├── readthedocs.yaml          # ReadTheDocs config (Sphinx)
├── Doxyfile                  # C++ docstring extraction (optional)
├── src/
│   ├── core/                 # ba_core — C++20 core (no Qt, no engine)
│   │   ├── ipc/              #   audio rings, health, control, Spout
│   │   ├── dsp/              #   channel strip, automixer, resampler, presets
│   │   ├── persist/          #   snapshot, restore, state
│   │   ├── security/         #   secrets (AES-GCM+Argon2id), tokens
│   │   ├── diag/             #   rotating logs, export
│   │   ├── fault/            #   fault injection
│   │   ├── crypto/           #   SHA-256, HMAC
│   │   ├── hw/               #   audio device identity
│   │   └── tests/            #   unit tests
│   ├── launcher/             # BroadcastAppliance — supervisor + RecoveryUi (Win32, /MT)
│   ├── app/                  # ba-app — Process A (Qt + engine); main.cpp at the root
│   │   ├── arranque/         #   modes (GUI, operate, register), settings, start with the system
│   │   ├── emision/          #   BroadcastEngine on top of the engine, sources, NDI, self-test
│   │   ├── comun/            #   small utilities (monotonic time, CPU usage, JSON signals)
│   │   ├── ui/ qml/          #   view-models and QML
│   │   └── …                 #   one folder per feature: audio, video, mcp, evento, ptz…
│   ├── motor/                # the broadcast engine on top of GStreamer
│   ├── gst-plugins/          # our own GStreamer plugins
│   └── gateway/              # ba-gateway — Process B (WebRTC, flag BA_GATEWAY_HAVE_WEBRTC)
├── assets/                   # deployment assets (not compiled)
│   └── web/                  #   web apps: guest, control, whiteboard (served by B)
├── packaging/                # flatpak (manifest) + .desktop
├── docs/                     # documentation (map in docs/README.md)
├── tests/                    # Python suite of repo invariants (pytest)
├── tools/                    # deps/, ci/, entrenar/, docs/, capturas/, generar/, test/ + deploy and publish
├── datos/                    # versioned data (catalogs, seeds, OUI)
├── cmake/                    # CMake modules (macOS bundle, Python, dependencies)
├── third_party/              # third-party code copied into the repo
└── .github/workflows/ci.yml  # CI/CD: lint + linux + windows (clang-cl + Wine) + sanitizers + release
```

---

## Dependencies

Versions **pinned** in [`versions.cmake`](../../versions.cmake) (the only place where they are
pinned; the build consumes them to be reproducible and auditable). "Core" = built
on Linux and Windows; "platform-gated (Windows)" = only fetched/used under
`if(WIN32)`.

| Dependency | Pinned version | License | Scope |
|---|---|---|---|
| mbedTLS | `v3.6.2` | Apache-2.0 | Core (crypto: AES-GCM, SHA-256, HMAC) |
| nlohmann/json | `v3.11.3` | MIT | Core (header-only JSON) |
| libsamplerate (Secret Rabbit Code) | `0.2.2` | MIT | Core (resampling, spec 7.4) |
| Argon2 (P-H-C reference) | `20190702` | CC0 / Apache-2.0 | Core (Argon2id KDF for `secrets.json`) |
| CLAP (headers) | `1.2.6` | MIT | Audio module host |
| LSP Plugins | `1.2.35` | LGPL-3.0 | Audio rack modules (`plugins/lsp/`; Windows: built from source) |
| GStreamer | `1.28.7` (Linux from source; macOS official framework) / `1.26.11` (Windows, official MSVC) | LGPL-2.1 (with GPL plugins) | Broadcast engine |
| Qt 6 | `6.6.3` | LGPL-3.0 (dual) | UI (vendored; Windows and Linux) |
| Spout2 (SDK) | `2.0.0.0` | — | Platform-gated (Windows) |
| libwebrtc | `M123` (`123.0.6312.5`) | BSD-3-Clause | Gateway (Shiguredo prebuilt; Windows and Linux) |
| cloudflared | `2024.11.0` | — | Tunnel (per-platform binary) |

> The first 6 are the ones the core consumes unconditionally (or as an
> asset). Those in the "platform-gated (Windows)" block are only linked under
> `if(WIN32)` and their real behavior is verified on Windows.
