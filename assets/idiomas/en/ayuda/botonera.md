The control surface is a grid of controls on a **tablet** (paired by QR), a **Stream Deck**, a **MIDI** or **OSC** controller (through a plugin). Each cell triggers an action and shows live state. The app decides everything: the device only says “they pressed (x, y)”.

## Building it
1. `botonera_plantillas` and `botonera_perfil_crear {desde_plantilla: "director"}` (or audio, asamblea, prompter): it is built with the scenes, sources and seats there are. A profile is a STATION with its `herramientas_permitidas`.
2. `botonera_celdas {perfil, pagina}` to look; `botonera_celda_set` to put or change a cell:
   - `tipo`: boton, fader, perilla, medidor, texto, reloj; `estilo`: {texto, icono, color, fondo};
   - `accion`: {tipo: herramienta, herramienta, args} (args with "{{valor}}" for faders, "{{delta}}" for relative knobs, "{{no_activo}}" to toggle), {tipo: pagina, ir_a}, {tipo: secuencia, pasos, espera_ms}, {tipo: receta, id}; `accion_mantener` (0.7 s) and `accion_soltar`;
   - `estado`: bindings to the bus [{tema, ruta, op, valor, efecto, dato}] — effect fondo, color, texto (“{{v}}” is the value), titilar, valor, nivel, activo.
3. Test it with `botonera_presionar` (it goes through the permissions; on-air actions, while on air, are approved by a person).

Binding examples: tally of a scene `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}`; armed in green with `ruta: "preview"`; recording `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}`; loudness `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}`; mute `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}` with the action `audio_set {name, muted: "{{no_activo}}"}`. The topics: `eventos_temas`.

## What you CAN'T do (on purpose)
- **Add tools to a profile** or enable editing from the device: a person does that on the Control Surface page. If a cell uses a tool the profile doesn't have, `botonera_celda_set` warns you: ask the operator for it.
- **Approve a pairing**: `botonera_emparejar` gives the QR URL; the tablet asks and a person approves on the Control Surface → Devices page.

## Hardware (Stream Deck, MIDI, OSC, X-keys…)
A plugin registers the device with `plugin.superficie.registrar(...)` and it becomes one more device (`botonera_dispositivos`, `botonera_dispositivo_asignar`). Ready-made examples: `superficie-streamdeck`, `superficie-midi`, `superficie-osc` (`plugin_desde_ejemplo`); for tally to monitors or cameras, `tally-tsl-umd`. The libraries (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) come with the program. For a new device: `dispositivos_listar`, and write the plugin on top of `ba_plugin.superficie` (`render_tecla` draws a key with PIL).

## Events for plugins
`@plugin.escuchar("aire/tally")` (or "salida/#", "audio/nivel/+", "alarma/#") receives the app's topics; what a plugin publishes with `plugin.evento("canal", datos)` shows up as `plugin/<id>/<canal>`. `eventos_esperar {temas, timeout_s}` waits for something from the assistant.
