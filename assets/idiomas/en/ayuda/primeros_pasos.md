## What it is
A complete streaming studio in a single app:
- **Program**: what goes on air. It is the **on-air scene** (`scene_switch` changes it, with a transition).
- **Scenes**: compositions of **sources** (cameras, images, text, videos, guests, colors...). A source can be in several scenes (it is the same one).
- **Outputs**: RTMP streaming (`stream_start`) and recording (`record_start`).
- **Conference**: guests join from the browser (WebRTC). First they wait in a private **lobby** (they only see you); once admitted they take a **place** (slot 1..N) and their camera is the source `Invitado N`. With **speakers mode**, instead of joining directly they queue up and go on air in turn with a clock.

## First look (recommended)
1. `status` — what's on air, whether it's streaming or recording, health.
2. `foto_programa` — see the program.
3. `scene_list` / `foto_todas_escenas` — all the scenes.
4. `room_status` — who is in the conference.

## Conventions
- Positions in **fractions of the canvas** (0..1), or presets (`full`, `pip-br`, `grid-4-2`...). See topic `fuentes`.
- Volume in **dB** (0 = no change). Speaker times in seconds or `m:ss`.
- Tools save the configuration on their own.
- Errors explain what happened and how to continue: read them and fix it.