[English](../en/architecture.md) · **Español** · [← README](../../README.es.md)

# Arquitectura

Cómo está armado por dentro: los procesos, el núcleo, el estado de cada parte, la estructura del repositorio y las dependencias.

## Arquitectura

El sistema se compone de un launcher (Proceso 0), dos procesos hijos, el motor
de video (GStreamer, con plugins propios) y el host de plugins de audio. Todo el
núcleo es una lib estática sin dependencias de UI ni del motor.

- **Proceso 0 — `BroadcastAppliance`** (launcher/supervisor, Win32 puro, CRT
  estática `/MT`): instancia única, verificación de integridad, lanzamiento de B
  y luego A, watchdog (crash por handle + cuelgue por heartbeat), reinicio con
  backoff y circuit breaker, y cierre ordenado. Funciona en Windows y en Linux
  sobre `ba::platform`.
- **Proceso A — `ba-app`** (motor de emisión + UI de operación): Qt 6 sobre el
  motor de GStreamer (`src/motor`), escenas, encoding y UI del operador
  (Windows, Linux y macOS).
- **Proceso B — `ba-gateway`** (gateway WebRTC + conversación + túnel):
  señalización, túnel (cloudflared) y motor de conversación para invitados.
- **Plugins de GStreamer propios** (`src/gst-plugins`, en `gst-plugins/` del
  bundle): filtros, PTZ digital, superresolución, el rack de audio y los
  plugins de Python.
- **Rack de audio** (en [`src/app/audio`](../../src/app/audio/), dentro de `ba-app`):
  host CLAP (`ClapHost`, editor X11/Win32 en `ClapEditor`), grafo de módulos
  (`AudioRack`, un rack por fuente vía `RackManager`),
  catálogo con nombres reales de parámetros (`Modules`) y análisis/generador
  (`Analyzer`).
- **`ba_core`** (núcleo, lib estática, en [`src/core`](../../src/core/)): **sin Qt, sin
  motor de video**, testeable por unidad. Submódulos:
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
- **`ba_core` es el núcleo**: SIN Qt, SIN motor de video, testeable por unidad.
- El **launcher** es Win32 puro con CRT estática (`/MT`).
- **A** usa `/MD` (CRT dinámica, por consistencia con Qt y GStreamer); el
  gateway va `/MT` con el launcher (lo exige libwebrtc).
- Los **PDBs** se emiten siempre (debug info) incluso en Release.

---

## Estado

Los procesos están **implementados** y corren en Linux (nativo) y en Windows
(cross desde Linux con clang-cl, ABI de MSVC):

- **`ba-app`** (Proceso A): `MainWindow` (QMainWindow del operador) +
  `OperatorEngine` (integra `ba_core::dsp` ChannelStrip/Automixer + presets,
  IPC SharedAudioRing/HealthBlock/ControlEndpoint y persistencia
  SnapshotStore). Sin GStreamer, `ba-app` se compila sin motor (solo
  herramientas).
- **`ba-gateway`** (Proceso B): la conferencia (videoconferencia con invitados). `Room` (lobby,
  participantes, chat, archivos, rutas de medios), `SignalingServer` (HTTP +
  WebSocket de invitados) y `AdminApi` (API de control REST + SSE, solo en
  127.0.0.1, la usa la app), `Webrtc` (libwebrtc real,
  también en Linux), `MediaPump` (IPC ↔ WebRTC por ruta), `TlsTerminator`
  (HTTPS), `WgDevice` + `IpStack` (WireGuard + lwIP en espacio de usuario) y
  `Cloudflared`. Ver [`docs/funciones/CONFERENCIA.md`](../../docs/funciones/CONFERENCIA.md).
- **Motor de video** (`src/motor`, GStreamer): escenas, transiciones, DSK,
  clean feed, grabación y salidas; los invitados remotos entran a la escena
  desde el IPC del Proceso B. Sus plugins propios viven en `src/gst-plugins`.
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
  monitor, escenas y fuentes, configuración completa y la página **Conferencia**
  (lobby privado, invitados con su video, chat, archivos, acceso directo).
  Sin pantalla, `ba-app --no-ui` corre en modo consola.

Queda por hacer (ver [`docs/historia/PLAN_MULTIPLATAFORMA.md`](../../docs/historia/PLAN_MULTIPLATAFORMA.md)):

- **Compilar y probar en Windows (MSVC) y macOS** la interfaz, el motor y el
  gateway real (hoy se validan en Linux), y **CI** con runners nativos.
- **libwebrtc para macOS** (el SDK de Shiguredo existe; falta el build) y
  **PDF** como archivo compartido (hoy: imagen, video y audio).
- **Packaging** final del bundle portable (runtime de terceros vía
  `tools/deploy`).

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
│   ├── core/                 # ba_core — núcleo C++20 (sin Qt ni motor)
│   │   ├── ipc/              #   rings de audio, salud, control, Spout
│   │   ├── dsp/              #   channel strip, automixer, resampler, presets
│   │   ├── persist/          #   snapshot, restauración, estado
│   │   ├── security/         #   secrets (AES-GCM+Argon2id), tokens
│   │   ├── diag/             #   logs rotativos, export
│   │   ├── fault/            #   inyección de fallas
│   │   ├── crypto/           #   SHA-256, HMAC
│   │   ├── hw/               #   identidad de dispositivos de audio
│   │   └── tests/            #   unit tests
│   ├── launcher/             # BroadcastAppliance — supervisor + RecoveryUi (Win32, /MT)
│   ├── app/                  # ba-app — Proceso A (Qt + motor); main.cpp en la raíz
│   │   ├── arranque/         #   modos (GUI, operar, registrar), ajustes, inicio con el sistema
│   │   ├── emision/          #   BroadcastEngine sobre el motor, fuentes, NDI, autotest
│   │   ├── comun/            #   utilidades chicas (hora monotónica, uso de CPU, señales JSON)
│   │   ├── ui/ qml/          #   view-models y QML
│   │   └── …                 #   una carpeta por función: audio, video, mcp, evento, ptz…
│   ├── motor/                # el motor de emisión sobre GStreamer
│   ├── gst-plugins/          # plugins de GStreamer propios
│   └── gateway/              # ba-gateway — Proceso B (WebRTC, flag BA_GATEWAY_HAVE_WEBRTC)
├── assets/                   # assets de despliegue (no se compilan)
│   └── web/                  #   apps web: invitado, control, pizarra (las sirve B)
├── packaging/                # flatpak (manifiesto) + .desktop
├── docs/                     # documentación (mapa en docs/README.md)
├── tests/                    # suite Python de invariantes del repo (pytest)
├── tools/                    # deps/, ci/, entrenar/, docs/, capturas/, generar/, test/ + deploy y publicar
├── datos/                    # datos versionados (catálogos, semillas, OUI)
├── cmake/                    # módulos de CMake (bundle de macOS, Python, dependencias)
├── third_party/              # código de terceros copiado al repo
└── .github/workflows/ci.yml  # CI/CD: lint + linux + windows (clang-cl + Wine) + sanitizers + release
```

---

## Dependencias

Versiones **fijadas** en [`versions.cmake`](../../versions.cmake) (único lugar donde se
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
| GStreamer | `1.28.7` (Linux desde fuente; macOS framework oficial) / `1.26.11` (Windows, oficial MSVC) | LGPL-2.1 (con plugins GPL) | Motor de emisión |
| Qt 6 | `6.6.3` | LGPL-3.0 (dual) | Interfaz (vendoreado; Windows y Linux) |
| Spout2 (SDK) | `2.0.0.0` | — | Se descarga (Windows) pero el código no lo usa: el video entre procesos va por memoria compartida (`ipc/SpoutTransport`) |
| libwebrtc | `M123` (`123.0.6312.5`) | BSD-3-Clause | Gateway (precompilado de Shiguredo; Windows y Linux) |
| cloudflared | `2024.11.0` | — | Túnel (binario por plataforma) |

> Las 6 primeras son las que el núcleo consume de forma incondicional (o como
> asset). Las del bloque "plataforma-gated (Windows)" solo se enlazan bajo
> `if(WIN32)` y su comportamiento real se verifica en Windows.
