## What it is
An **event** is the rundown of what is going to happen: **blocks** (Opening, Words from the mayor, Closing) and inside each one the concrete **cues**. The event has EVERYTHING of its own (scenes, mixer, media, assembly, web): the whole event folder can be taken to another machine. The files of the music and video cues are the **event's media** (the Media page, `media_list`); the speeches go in the rundown's `Discursos/`.

## Cue types and what each one needs in `datos`
| Type | `datos` | Does it run today? |
|---|---|---|
| `escena` | `{escena}` | yes |
| `orador` | `{participante}` + `duracionSeg > 0` | yes |
| `musica` | `{archivo, accion, repetir}` — a file from the event's media (`media_list`) | yes |
| `teleprompter` | `{discurso, arrancar}` — text in `Discursos/` | yes |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | yes |
| `video` | `{archivo, alTerminar}` | not yet |
| `pantalla` | `{pantalla, contenido}` | not yet |
| `pausa` | waits for whatever `duracionSeg` says | yes |
| `espera` | stops until the operator confirms | yes |
| `nota` | does nothing: it's an instruction | yes |

The ones that don't run yet can still be written down: they count toward the timing and tell the operator what to do. When their turn comes, the system logs it instead of failing silently.

## Timing
From each cue's `duracionSeg` comes the block's and the event's; with `inicio` (HH:MM) comes the planned time of each thing, and with `fecha` (YYYY-MM-DD) plus `huso` (IANA name) comes `cuando`, the start as an absolute instant for whoever is in another time zone. While it runs, what matters is the **deviation**: a positive `desvioSeg` = we're running late. Paused time doesn't count as delay.

## Before the event
- `evento_revisar`: incomplete cues, scenes that don't exist, files that are missing or empty.
- `evento_ensayo_rapido`: goes through the WHOLE rundown at once, without touching anything, and says for each cue whether it could run. It's what you should run after building a long rundown.
- The **rehearsal** switch (`evento_correr {"ensayo": true}`) goes through the rundown in real time without going on air.

## While it runs
`evento_correr` with `arrancar`, `siguiente`, `atras`, `ir`, `pausar`, `reanudar`, `parar`. **It goes on air** except in rehearsal, so the operator decides.

Running the rundown is NOT being live: live means something is really going out (a stream or a recording), and `en_vivo` in `status` says so. While live, the app warns the operator before any risky action and `settings_set` does not apply what restarts the conference or the engine (it would kick out all the guests or cut the output at the worst moment). With the rundown running as a test, without streaming, nothing is blocked.

## Teleprompter
The `teleprompter` cue loads a text from `Discursos/` and leaves it READY but stopped. Everything else (writing the speeches, sending them, handling the text on air) is its own category: `help {"topic": "prompter"}`.
## The event's look on the web (theme)
Each event has its **theme**: accent, background, typeface, logo and name, on **every** web page of the system: the event's page, the guests' page, the remote session (where members vote), the audience questions, the registration kiosk and the room screens. The operator consoles (control, button panel, chair, captions, correction) take whatever the `alcance` says: by default, only the accent and the logo. Colors with a meaning never change: tally, on air and armed, meters, yes and no of a vote.

- `evento_tema {"accion": "ver"}`: the theme, the colors that will come out (`resuelto`) and the contrast `avisos`. If there are warnings, tell the operator and suggest another color.
- `evento_tema {"accion": "cambiar", "colores": {"acento": "#00a86b"}, "marca": {"nombre": "Forum 2026"}}`: whatever isn't set is calculated (the text on top of the accent comes from the contrast).
- `evento_tema {"accion": "logo", "archivo": "<path>", "cual": "logo"}` (SVG, PNG or WebP, up to 2 MB; `logoClaro` for dark backgrounds, `favicon` for the tab) and `{"accion": "fuente", "archivo": "<path>", "familia": "…"}` (WOFF2, WOFF, TTF or OTF, up to 4 MB). No internet: no Google Fonts, the typeface travels with the event. An SVG with scripts or event handlers is rejected.
- `alcance`: `{"publico": "todo", "control": "acento"}` (each one nada, acento or todo).
- A site generated before the theme: `evento_tema {"accion": "migrar_sitio"}` (it keeps what was changed by hand in `estilo.css`).

It's saved in the event's folder (`tema.json` and `Tema/`). Without a theme of its own, the accent and the background come from the site's (`evento_web sitio {color, fondo}`). In the app: Services → Look of the web pages → Event theme.

Outside the web it's also the default: **graphics** with an accent color (lower thirds, cards, tickers) come out with the event's unless given another one, and new **QR codes** take its colors (always dark on light, so they can be read) and its logo in the middle. With a light theme, the audience pages flip entirely (highlights, fields and shadows included).

