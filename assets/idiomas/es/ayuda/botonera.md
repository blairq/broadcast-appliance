La botonera es una grilla de controles en una **tablet** (emparejada por QR), un **Stream Deck**, un controlador **MIDI** u **OSC** (por plugin). Cada celda dispara una accion y muestra estado vivo. La app decide todo: el aparato solo dice «apretaron (x, y)».

## Armar
1. `botonera_plantillas` y `botonera_perfil_crear {desde_plantilla: "director"}` (o audio, asamblea, prompter): se arma con las escenas, fuentes y bancas que hay. Un perfil es un PUESTO con sus `herramientas_permitidas`.
2. `botonera_celdas {perfil, pagina}` para ver; `botonera_celda_set` para poner o cambiar una celda:
   - `tipo`: boton, fader, perilla, medidor, texto, reloj; `estilo`: {texto, icono, color, fondo};
   - `accion`: {tipo: herramienta, herramienta, args} (args con "{{valor}}" para faders, "{{delta}}" para perillas relativas, "{{no_activo}}" para alternar), {tipo: pagina, ir_a}, {tipo: secuencia, pasos, espera_ms}, {tipo: receta, id}; `accion_mantener` (0,7 s) y `accion_soltar`;
   - `estado`: ligaduras al bus [{tema, ruta, op, valor, efecto, dato}] — efecto fondo, color, texto («{{v}}» es el valor), titilar, valor, nivel, activo.
3. Probalo con `botonera_presionar` (pasa por los permisos; lo de aire, al aire, lo aprueba una persona).

Ejemplos de ligaduras: tally de una escena `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}`; armada en verde con `ruta: "preview"`; grabando `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}`; loudness `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}`; mudo `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}` con la accion `audio_set {name, muted: "{{no_activo}}"}`. Los temas: `eventos_temas`.

## Lo que NO podes (a proposito)
- **Sumar herramientas a un perfil** ni habilitar la edicion desde el aparato: lo hace una persona en la pagina Botonera. Si una celda usa una herramienta que el perfil no tiene, `botonera_celda_set` avisa: pediselo al operador.
- **Aprobar un emparejamiento**: `botonera_emparejar` da la URL del QR; la tablet pide y una persona aprueba en la pagina Botonera → Aparatos.

## Hardware (Stream Deck, MIDI, OSC, X-keys…)
Un plugin registra el aparato con `plugin.superficie.registrar(...)` y queda como un aparato mas (`botonera_dispositivos`, `botonera_dispositivo_asignar`). Ejemplos listos: `superficie-streamdeck`, `superficie-midi`, `superficie-osc` (`plugin_desde_ejemplo`); para tally a monitores o camaras, `tally-tsl-umd`. Las bibliotecas (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) vienen con el programa. Para un aparato nuevo: `dispositivos_listar`, y escribi el plugin sobre `ba_plugin.superficie` (`render_tecla` dibuja una tecla con PIL).

## Eventos para plugins
`@plugin.escuchar("aire/tally")` (o "salida/#", "audio/nivel/+", "alarma/#") recibe los temas de la app; lo que publica un plugin con `plugin.evento("canal", datos)` aparece como `plugin/<id>/<canal>`. `eventos_esperar {temas, timeout_s}` espera algo desde el asistente.