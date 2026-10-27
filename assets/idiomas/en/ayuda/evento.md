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

With the rundown running for real the system enters **event mode**: the app warns the operator before any risky action, and `settings_set` is blocked (a gateway-scope setting would restart the conference and kick out all the guests at the worst moment).

## Teleprompter
The `teleprompter` cue loads a text from `Discursos/` and leaves it READY but stopped. Everything else (writing the speeches, sending them, handling the text on air) is its own category: `help {"topic": "prompter"}`.
