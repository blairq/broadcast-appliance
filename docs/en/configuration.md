**English** · [Español](../es/configuracion.md) · [← README](../../README.md)

# Configuration reference

What you can tweak by hand. In normal use you don't need to: the event assistant configures it for you (see the [README](../../README.md)). This is kept as a reference.

## Configuring the broadcast (`Config/app.json`)

On first run, A writes `release/Config/app.json` with default values
(background + title, 1080p30, two guests). Everything is editable:

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

- **`sources`** accepts the engine's sources with their *settings* (the ids are
  the usual ones: camera `v4l2_input`, `image_source`, `ffmpeg_source` for
  files or URLs, `pulse_input_capture`, etc.). `rect` is the position as
  fractions of the canvas `[x, y, width, height]`. A source that fails (an unplugged
  camera) is skipped with a warning: it does not bring down the broadcast.
- **`encoder.video`**: `auto` picks the hardware available on this machine and falls back
  to x264 if there is none or if it fails to start. Aliases: `vaapi` (AMD/Intel on Linux),
  `nvenc`, `qsv`, `amf`, `videotoolbox` (macOS) and `x264`. On an integrated AMD
  GPU (Ryzen) `auto` uses VAAPI (`vah264enc`).
- **The stream key does not go in the file**: you type it when going on air
  (it is stored encrypted in `Credenciales/`) or it is read from the environment variable
  named by `keyEnv` (default `BA_STREAM_KEY`).

`ba-app` command-line options (they override the file): `--encoder=`,
`--bitrate=`, `--resolution=WxH`, `--fps=`, `--record`/`--no-record`,
`--rec-dir=`, `--stream-server=`, `--guests=`, `--duration=<seg>`,
`--config=<archivo>`. To pass them through the launcher:
`--app-arg=--record`. `ba-app` also runs on its own, without the launcher:

```bash
./release/bin/64bit/ba-app --record --duration=10
```

To diagnose the engine without the full scene:
`./release/bin/64bit/ba-app --engine-selftest` (lists the available
encoders, records a few seconds and verifies the file; returns 77 if there is no
graphics context).

## The Conference (gateway)

The launcher already starts it. To run it on its own:

```bash
BA_GATEWAY_PIN=123456 ./release/bin/64bit/ba-gateway --port 8787
```

Guests join through the link on the app's **Conference (Conferencia)** page (or
`https://127.0.0.1:8789`): name, room and PIN → **lobby** (the operator sees them and
talks to them privately; they only see the operator) → admitted to the room, with
camera, microphone, **screen or window sharing**, files to show,
chat and raised hand. The operator runs everything from the Conference page or from the
**web dashboard** (`/control/`: Services → Web control, paired with a QR code
and approved in the app; works on a phone or tablet).

- **Real WebRTC** on Linux: `make deps-webrtc BA_WEBRTC_TARGET=linux` and
  `make build` build the gateway with libwebrtc (static, no system
  dependencies).
- **Built-in HTTPS** (camera/microphone require a secure page) with your own
  certificate (drop `cert.pem` + `key.pem` into `Credenciales/`: they are imported
  encrypted) or an automatic self-signed one.
- **Direct internet access** without Cloudflare: a **userspace WireGuard**
  tunnel to your own server with a public IP (`make deps-lwip`); the
  wizard on the Conference page generates the server configuration.

Full details (architecture, lobby, API, server, security):
[`docs/funciones/CONFERENCIA.md`](../../docs/funciones/CONFERENCIA.md).

## Everything in the portable folder

The appliance runs from any folder (a USB stick, for example) and **everything
it saves stays inside that folder**, never in the user profile
or on the machine:

| Folder | What it stores |
|---|---|
| `Config/` | configuration (scenes, settings, dashboard) |
| `Credenciales/` | tokens and keys (YouTube, Twitch, Facebook, MCP, WireGuard, HTTPS), **encrypted** with AES-256-GCM; an update never touches them |
| `Respaldos/` | a copy of `Config/` and `Credenciales/` before each update (the last 5 are kept) |
| `Runtime/` | session rendezvous files (room API, MCP port) |
| `Cache/` | Qt/QML, fontconfig and GPU shader caches (redirected at startup) |
| `Logs/`, `Recordings/`, `Shares/`, `Media/` | logs, recordings and files |

On Linux the control channel and the single-instance lock use sockets in the kernel's
**abstract** namespace: they write nothing to any disk. (On macOS,
which lacks it, they go to `$TMPDIR`, temporary and per-user.)

**Credentials without a password.** The machine starts on its own; encryption prevents
them from being read by accident (a FAT/exFAT USB stick has no permissions), but the
machine key (`Credenciales/clave-equipo`) travels on the same USB stick:
whoever takes the whole stick has everything. To move the machine to another
USB stick, copy the whole `Credenciales/` folder. Earlier versions stored
everything in plain text in `Config/`: on first startup it is imported and deleted.

## Updating

Unpack the new version into another folder and pick it in **Configuration →
System and data → Update from a folder** (or `BroadcastAppliance
--update <carpeta> --start`, or the MCP tool `system_update`). The app
closes; the launcher backs up `Config/` and `Credenciales/` into `Respaldos/`,
replaces **only the program** (all or nothing: if anything fails, the previous
version stays intact) and reopens. The previous version is kept:
**Go back to the previous version** (`--update-rollback`).

## AI assistant (MCP)

A built-in **MCP** server lets a vision-capable LLM drive and see the whole
app: scenes, sources, audio, outputs, configuration, room and speakers, with
screenshots of the program output, of each person and of the UI. Only on
`127.0.0.1:8790` with a token (encrypted in `Credenciales/`; `bin/64bit/ba-mcp
--token` shows it); stdio bridge `ba-mcp`. See
[`docs/funciones/MCP.md`](../../docs/funciones/MCP.md).

## AI assistant inside the app

The **✦** tab (at the very bottom of the bar) is a chat with the LLM that drives the
app: it runs the official **Claude Code** interface (Pro/Max subscription or API
key), **Gemini CLI** (Google account or key) or **opencode** (OpenCode Go or
OpenRouter) in its own terminal, already connected to the MCP server and with a
system prompt that explains every part of the system and the best practices.
The agent is installed from the tab (pinned and verified version) inside
the portable folder. With **watches**, the server posts to that
chat on its own when something happens (a channel clips, a topic ends, a speaker
runs out of time, someone is waiting in the lobby...) and the assistant can act right away. See
[`docs/funciones/MCP.md`](../../docs/funciones/MCP.md).

**Safeguard.** What guests or the audience write never turns into
actions on its own: the MCP server marks in code all content that comes from
them, and an assistant session that has read it cannot modify anything unless
the operator approves it with one click (the card shows the exact action). It
does not depend on the model. The assistant can also post notices in a **bar at the bottom of
the window** (color by priority), ask questions and suggest replies to
guests that are sent with one click.

## Video inputs

The **Inputs (Entradas)** page (◉ in the bar) and via MCP: USB cameras and capture cards
(V4L2 / DirectShow / AVFoundation, with format and resolution), IP cameras and
network feeds over RTSP, RTMP(S), **SRT**, HLS/HTTPS and UDP (the bundle's FFmpeg,
with reconnection and low latency), and **NDI®** with an open implementation
(VideoLAN's libNoIDea, LGPL, experimental; receives full NDI/SpeedHQ, not
NDI|HX) — without the proprietary SDK, on Linux and on Windows. A live thumbnail of each input. NDI® is a
trademark of Vizrt NDI AB; this project is not affiliated with Vizrt.

## Audio: per-source rack (LSP)

**Audio** tab (🎚 in the bar): on the left, the sources with audio (microphones,
captures and also videos); when you pick one, its **patchbay** appears — `in` → modules →
`out` — where you add modules (gate, compressor, de-esser, EQ, multiband,
limiter, analyzer, generator or any of LSP's ~200), connect them by
dragging from the outputs and disconnect them with ✕. Each module shows its
input/output level and how much it is reducing; the detail panel has the
parameters in real units, presets and the button to open the original LSP
interface. The analyzer gives LUFS (momentary, short-term, integrated), a third-octave
spectrum, noise floor and improvement hints. Racks are saved to
`Config/audio-racks.json`. Via MCP the assistant measures, builds and calibrates (e.g. a
gate measured with the person silent and then speaking): `help {"topic": "audio_rack"}`.

## YouTube, Twitch and Facebook

One tab per platform in the left bar. When not configured, each one
explains step by step (with links) how to create your own app in Google Cloud,
dev.twitch.tv or developers.facebook.com; once connected, it manages videos,
live streams (create, stream from the app, end), channel/Page and chat. The
**Live chat** scene source merges the chats of all three plus the room's
over the video. Everything is also available via MCP. Credentials and tokens are stored encrypted
in `Credenciales/` inside the portable folder, including the key from the
last time you went on air (`stream-last`: saves retyping it and lets you resume
the broadcast after a crash). Facebook only allows streaming to Pages (not to profiles).

Secrets go through the environment, never the command line (they show up in the process
list): `BA_GATEWAY_PIN` (if missing, one is generated and shown in the app),
`BA_TUNNEL_TOKEN` (Cloudflare), `BA_WG_PRIVATE_KEY` / `BA_WG_PSK` (WireGuard;
if missing, the key lives encrypted in `Credenciales/`). The HMAC key
for the tokens is random per startup (`BA_GATEWAY_HMAC_SECRET` pins it).
