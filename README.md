<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/icons/logo.png">
    <img alt="Broadcast Appliance" src="assets/icons/logo-claro.png" width="480">
  </picture>
</p>

**English** · [Español](README.es.md)

# Broadcast Appliance

An all-in-one live production studio — switcher, audio mixer, video conference,
assemblies, graphics, recording and streaming — that you run **by talking to an
AI assistant**. Portable: everything it needs travels inside the folder, on
Windows, macOS and Linux.

[![CI](https://github.com/blairq/broadcast-appliance/actions/workflows/ci.yml/badge.svg)](https://github.com/blairq/broadcast-appliance/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](#license)

---

## Quick start

1. **Download** the latest release for your system from
   **[Releases](https://github.com/blairq/broadcast-appliance/releases/latest)**:
   `.zip` for Windows, `.tar.gz` for Linux, `.dmg` for macOS.
2. **Unpack it and open `BroadcastAppliance`.** Nothing gets installed on the
   system: the program, its data and its models live in that folder.
   On macOS the app is not notarized: the first time, right-click it → **Open**.
3. **Set up the assistant.** On the **Asistente** page (✦ in the side menu),
   pick the agent and the model, and start it. Then the **Agentes** page lets
   you turn on the automatic roles you want. (The interface is in Spanish; the
   assistant answers in your language.)
4. **Ask it for everything.** «Set up an event for tomorrow at 7 pm with two
   cameras and a remote guest», «stream it to YouTube», «why does guest 3 sound
   low?». The event assistant builds the event, configures it, runs it with you
   and explains whatever you ask.

<p align="center">
  <img alt="The Asistente page: choose the event assistant and its model, then start it" src="docs/img/quick-start-assistant.jpg" width="820">
</p>

> **There is no user manual, on purpose.** Everything is done through the event
> assistant: it knows every part of the program (more than 700 tools) and can
> see what is on screen and on air. If you don't know how to do something, ask
> it.

---

## What it does

- **Events, start to finish.** Each event carries its own scenes, mixer, media,
  assembly setup and web page. New events from scratch, from a copy or from a
  template; a history of everything that was recorded and streamed.
- **Video.** Scenes and transitions on its own GStreamer engine, cameras (USB,
  network, NDI, PTZ with face tracking), animated graphics, teleprompter, QR
  codes, multiview, AI super-resolution, panoramic stitching. Recording and
  streaming to YouTube, Twitch, Facebook or any RTMP/SRT server.
- **Audio.** A mixer with groups that compact and a **VCA per group**, a rack of
  LSP processors per source, a mixing matrix, EBU R128 loudness, room
  calibration, automixer, PFL, AFV and lip sync.
- **Conference.** Guests join by WebRTC from a link: a private lobby, chat,
  speaker mode with timer, up to 64 guests.
- **Assembly.** Conference systems (Bosch DCN), remote members, agenda, debates
  and voting.
- **Screens.** The machine's monitors and the TVs on the network (Chromecast),
  with what goes on each one.
- **People.** Face recognition, a guest list with check-in, and **registration
  totems**: a TV or a phone with a camera where people register by talking to
  the assistant, show their ID (barcode or the back via OCR) and get their
  photo taken.
- **Voice.** Dictation, live subtitles and translation (Parakeet or Nemotron,
  local), synthetic voices, and the assistant answering out loud.
- **Local AI.** Local language and vision models (llama.cpp / MLX) so the
  essentials work offline; online assistants (Claude, Gemini, opencode, pi)
  when you want them.
- **Reliable.** A supervisor that restarts whatever fails without cutting the
  stream, and after a power outage everything comes back as it was.

---

## Documentation

| | |
|---|---|
| [Building, testing and packaging](docs/en/building.md) | For developers: requirements, build, tests, CI and the portable bundle. |
| [Architecture](docs/en/architecture.md) | Processes, core, status of each part, repository layout, dependencies. |
| [Configuration reference](docs/en/configuration.md) | What can be tuned by hand (normally the assistant does it). |
| [How each feature works](docs/funciones/) | One file per feature *(Spanish)*. |
| [Operator manual](docs/en/manual/README.md) | For people who use it: first live show, guests, audio, troubleshooting. |
| [Guest guide](docs/en/manual/guest-guide.md) | For people joining the conference. |
| [`docs/`](docs/README.md) | Everything else: plans, local models, history *(Spanish)*. |

---

## Building from source

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
make deps      # pinned third-party dependencies for this machine (fetched, nothing system-wide)
make compilar  # build
make probar    # the test suite
```

Platform builds, the portable bundle and the details are in
[Building, testing and packaging](docs/en/building.md).

---

## License

The project's own code is distributed under the **GNU GPL-3.0** (see
[`LICENSE`](LICENSE)); the copyright holder is **Jorge Ortega**. Third-party
dependencies keep their own licenses (mbedTLS Apache-2.0, nlohmann/json MIT,
libsamplerate MIT, Argon2 CC0/Apache-2.0, CLAP MIT, LSP LGPL, among others; see
[Architecture → Dependencies](docs/en/architecture.md)).

## Contributing

Before sending changes: `make build` must compile without errors, `make test`
must pass, and the repository checks must pass (`pytest -q` with
`requirements-dev.txt`). Details in
[Building, testing and packaging](docs/en/building.md).
