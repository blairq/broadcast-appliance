## How it's built
Every source with audio (microphones, captures, videos) can have a JACK-style **rack**: `in` (the raw source) -> LSP modules connected with cables -> `out` (what goes on to the mix). The person sees it in the **Audio** tab (patchbay); `audio_show {"source"}` opens it on that source.
- `audio_sources`: sources with audio and whether they have a rack. `audio_rack {"source"}`: nodes, cables and meters.
- `audio_plantillas`: save a rack with a name and apply it to another source. Marked `porDefecto` for `ba_guest_audio`, NEW guests pick it up on their own when they connect: it's the only way for them to sound good without building anything for them while the event is starting.
- Each module has its own meters: `entradaDb`, `salidaDb` and `cambioDb` (what it's lowering or raising RIGHT NOW: in a gate/compressor it's the gain reduction).
- An empty rack doesn't change the sound. `audio_module_remove {"node": "*"}` removes the whole rack.
- In the tab, each card sits on a grid (the "Arrange" button lines everything up) and its chevron opens the detail: the real name of each of the module's ports and whether it's mono or stereo.

## Ports and channels (L/R)
A module can have several inputs and outputs: `in` (main), `sc` (sidechain) and, if it has more, `in2`, `in3`... and `out`, `out2`... (`audio_rack` reports them in `entradas`/`salidas`, and in `puertosEntrada`/`puertosSalida` with their real name and how many channels each one has). A 16-input LSP mixer reports all 16.
- By default a cable carries the **whole port**: left to left and right to right.
- You can wire **a single channel** by adding `canalSalida` and `canalEntrada` ("L" or "R") in `audio_patch`: e.g. send one module's right to another's left, or use a single channel and leave the other silent.
- A whole-port cable and a channel cable can't coexist between the same two ports: the last one replaces the previous one (otherwise the audio would be summed twice).

## Listening through one of the machine's outputs (headphones)
Useful to check how a point in the rack sounds WITHOUT touching what goes on air or the program mix.
1. `audio_outputs`: this machine's outputs, grouped by sound card.
2. `audio_output_set {"source", "salida": <id>}`: adds a card with the output's name to that source's graph.
3. `audio_patch {"source", "conectar": [{"desde": "in", "hacia": "salida-audio"}]}`: only then is it heard (you can hook up the output of any module, not just `in`).
CAREFUL: the engine plays through ONE output at a time; choosing another one on another source changes it for all of them.

## The machine's microphones and audio inputs
They don't appear on their own: they have to be added. `audio_devices` lists them (`inputs`) and `audio_input_add {"device"}` adds one. It goes to a GLOBAL channel (like the guests' audio): it sounds in ALL scenes, it's not a scene item and it stays saved for the next startup. After that it's processed like any source with its rack.

## Golden rule: measure, change, measure again
1. `audio_analyze {"source", "punto": "in", "segundos": 5}`: peak, RMS, LUFS, `pisoDeRuidoDbfs`, `nivelActivoDbfs`, `rangoDinamicoDb`, spectrum by thirds and by zones (bass, body, mids, presence, sibilance, air), clipping and `pistas`. If it says no audio is arriving, the source is muted, inactive or nobody is talking: ask the person to speak (`operator_ask`).
2. Change with the named tools (values in real units: dB, ms, Hz).
3. Verify: `audio_analyze {"punto": "out"}` and/or `audio_measure` (offline copies, it doesn't touch the air).
Tell the person what you measured and what you changed, with the numbers.

## Typical calibrations (voice)
- **Gate** (`audio_gate`): measure TWICE, with the person silent (noise floor) and speaking (active level). `umbral_db` = floor + 6 to 10 dB, and always well below the active level. `reduccion_db` -20 to -40 (full mute isn't needed), `ataque_ms` 1-5, `sosten_ms` 50-100, `liberacion_ms` 100-250. Verify with `audio_measure {"tipo": "curva"}` (where it opens) and by listening that it doesn't cut word endings. If the floor and the voice are less than ~15 dB apart, the gate isn't enough: suggest moving the mic closer or lowering the input gain.
- **Compressor** (`audio_compressor`): preset `voz` and adjust `umbral_db` so `cambioDb` shows -3 to -6 dB on the loud parts (not all the time). `ratio` 2-4, `ataque_ms` 5-20, `liberacion_ms` 80-200, `ganancia_db` to recover the level.
- **De-esser** (`audio_deesser`): look at the `sibilancia` zone (5-9 kHz) of the analysis. `deteccion_hz` at the peak of the s sounds (5-7 kHz typical), `umbral_db` so it lowers 3-6 dB only on the s sounds. `escuchar: true` lets you hear what it detects (only for adjusting: remember to turn it off).
- **EQ** (`audio_eq`, by `bandas`): high-pass at 70-100 Hz for voice; if the `cuerpo` zone (200-400 Hz) sticks out, a -2 to -4 dB bell; if clarity is lacking, +1 to +3 dB at 3-5 kHz. Small changes. Verify with `audio_measure {"tipo": "respuesta"}`.
- **Multiband** (`audio_multiband`): when a single compressor pumps or the problem is in one zone (booming bass, harsh highs). `corte_hz` and thresholds per band.
- **Limiter** (`audio_limiter`, preset `seguridad`): at the end of the chain, ceiling -1 dBFS. It's a safety net, not volume.
- Usual order: high-pass EQ -> gate -> compressor -> de-esser -> fine EQ -> limiter. `audio_patch` reconnects; `audio_bypass` compares with/without.

## Loudness
For the broadcast, aim for -16 to -14 LUFS integrated on the mix (YouTube/Twitch normalize near -14). A single voice: -18 to -16 LUFS at `out`. Peaks that hit 0 dBFS (`recortes`) are fixed at the input gain, not with the limiter.

## Generator and other modules
- `audio_generator {"senal": "rosa"|"seno"|"barrido"|"blanco", "nivel_db", "conectar_a"}` creates a STANDALONE test signal (e.g. into an analyzer from `audio_module_add {"module": "analizador"}`). Connecting it to what goes on air requires `al_aire: true`: confirm with the person first.
- `audio_modules {"buscar"}` lists the ~200 LSP plugins; `audio_module_add` adds any of them and `audio_module_get`/`audio_module_set` read and change all their parameters.
- `audio_editor` opens the LSP module's original graphical interface (for the person).
