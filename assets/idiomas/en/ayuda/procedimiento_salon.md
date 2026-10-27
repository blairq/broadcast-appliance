The standard procedure for an **in-room** event: an audience in the room, speakers, streaming and recording, and if requested remote guests and audience questions. Follow it in order; each step says which tool to use. **Propose before doing** anything that changes the event or goes on air, and don't make up data: whatever you weren't told, ask.

**0. What to ask** (before building anything): the event's name; date, start time and **time zone** (if someone joins from elsewhere); venue; duration and parts (blocks) with their times; who speaks in each part, with **name and title** exactly as they go on screen; whether there are **remote guests**; whether the **audience asks questions and votes**; **where it's streamed** (YouTube, Twitch, Facebook, another server) and whether it's recorded; waiting and closing music; videos to be played; whether there's **check-in** for attendees; whether a teleprompter or captions are needed.

**1. Configure** (everything that has to be set up):
| What | With |
|---|---|
| The event and its rundown: blocks with times and cues (scene, speaker with their time, video, music, graphic, teleprompter, note, wait) | `evento_armar {nombre, fecha, inicio, huso, invitados, preguntas, bloques}`; then `guion_bloque` / `guion_cue` |
| The scenes: each camera, wide shot, shared screen, guests | `scene_define`, `scene_list` (help escenas) |
| The event's music and videos (the cues name them) | `media_list`, `media_find` (help medios) |
| Each speaker's lower third (name and title spelled correctly) | the `zocalo` graphic in graphic-type cues; `grafico_listar`, `grafico_probar` |
| The room's microphones and levels | `audio_input_add`, `audio_measure`, `audio_loudness` (help audio) |
| Where it's streamed | accounts: `account_setup_guide`, `accounts_status`; own server: `stream_destino_agregar` |
| The recording (disk and quality) | `record_disco`, `record_ajustes` |
| The event's web page (one page per block, it updates itself) | `evento_web {accion: generar}`; `evento_web {accion: sitio, participacion: true}` if the audience participates; where to watch it live: `sitio {vivos}` |
| Remote guests (if any) | `room_link`, `room_pin`; lobby with `room_set`; who's there: `invitado_chequeo` |
| Audience questions (if any) | the audience page (QR); moderation with `preguntas_ver` / `pregunta_moderar` |
| Attendees and check-in (if any) | `padron_anotar`; at the door, `padron_puerta` |
| The beginning and the end: test signal, “Before we start” slate, end slate, Pre-show and Closing blocks | `evento_preparar {musica_espera, musica_cierre, bajada, cierre}` |

**2. Check** (the automatic part): `evento_revisar` (scenes, files, disk, destination, inputs with no signal) → `evento_ensayo_rapido` (walks through the whole rundown) → `evento_verificar` (the complete checklist for this type of event). Fix each “missing” item with what `como` says and run `evento_verificar` again until nothing is missing.

**3. Ask the person to check**: `evento_verificar` leaves in the event's checklist what only someone watching and listening can confirm (status “a_mano”). **Show that whole list** to whoever asked you for the event and ask them to confirm each item; as they say yes, mark it with `evento_chequeo {item, hecho: true, nota}`. Don't mark anything that hasn't been confirmed to you.

**4. Rehearse** (test each piece, without going on air):
- **Music:** `audio_escuchar {name}` sends it to the operator's headphones; have them listen and say whether the volume is right.
- **Videos:** `scene_preview` with the video's scene and `foto_fuente` to look at it; with its audio.
- **Graphics:** `grafico_probar` with the real data of each lower third and slate (look at the images; names and titles spelled correctly).
- **Web page:** `evento_web {accion: ver}` and the QR with `qr_mostrar` to open it on a phone.
- **Test recording:** `record_start`, one minute, `record_stop`, and have them play it back.
- **Test guest** (if any): `invitado_prueba`: they're seen, heard, and they hear us.
- **Test question** (if any): have someone send one from their phone and moderate it.
- **Test stream:** with the test signal on air, `stream_start` and `stream_salud`; have the person watch it on the platform (bars, running clock, tone, that it's the right event). If it's long before the start, `stream_stop`.
- **The whole run:** `evento_correr` with rehearsal on (the event's help) to see the real order of the scenes.

**5. Leave it ready to start:** `evento_verificar` with nothing missing and everything confirmed; then `evento_preparar {al_aire: true}` leaves the test signal on program. Tell the person it's ready and what's left to do on the day.

**6. On the day, from the test signal to the end:**
1. **30 to 15 minutes before:** `evento_iniciar`; the rundown's Pre-show starts with the test signal (`evento_correr`); `stream_start` and `record_start`. Confirm it's seen and heard on the platform (the wait cue asks for it).
2. **10 to 5 minutes before:** `evento_correr` → “Before we start” slate with the waiting music.
3. **It's time:** `evento_correr` to the first block.
4. **During:** `evento_correr` to advance; lower thirds on time; questions moderated; keep an eye on `stream_salud` and `audio_loudness`.
5. **Closing:** the Closing block plays the music and the end slate; when it's over, `stream_stop`, `record_stop`, `evento_terminar` and `evento_informe` (how long each part lasted, for next time). The web page switches to the closing on its own.
