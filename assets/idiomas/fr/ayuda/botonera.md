La surface de contrôle est une grille de commandes sur une **tablette** (appairée par QR), un **Stream Deck**, un contrôleur **MIDI** ou **OSC** (par plugin). Chaque case déclenche une action et affiche un état en direct. C'est l'application qui décide tout : l'appareil dit seulement « on a appuyé sur (x, y) ».

## Préparer
1. `botonera_plantillas` et `botonera_perfil_crear {desde_plantilla: "director"}` (ou audio, asamblea, prompter) : elle se construit avec les scènes, sources et sièges existants. Un profil est un POSTE avec ses `herramientas_permitidas`.
2. `botonera_celdas {perfil, pagina}` pour voir ; `botonera_celda_set` pour mettre ou changer une case :
   - `tipo` : boton, fader, perilla, medidor, texto, reloj ; `estilo` : {texto, icono, color, fondo} ;
   - `accion` : {tipo: herramienta, herramienta, args} (args avec "{{valor}}" pour les faders, "{{delta}}" pour les potentiomètres relatifs, "{{no_activo}}" pour basculer), {tipo: pagina, ir_a}, {tipo: secuencia, pasos, espera_ms}, {tipo: receta, id} ; `accion_mantener` (0,7 s) et `accion_soltar` ;
   - `estado` : liaisons au bus [{tema, ruta, op, valor, efecto, dato}] — effet fondo, color, texto (« {{v}} » est la valeur), titilar, valor, nivel, activo.
3. Testez-la avec `botonera_presionar` (passe par les autorisations ; ce qui touche à l'antenne, à l'antenne, est approuvé par une personne).

Exemples de liaisons : tally d'une scène `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}` ; prête en vert avec `ruta: "preview"` ; en enregistrement `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}` ; loudness `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}` ; muet `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}` avec l'action `audio_set {name, muted: "{{no_activo}}"}`. Les sujets : `eventos_temas`.

## Ce que vous NE pouvez PAS faire (exprès)
- **Ajouter des outils à un profil** ni activer l'édition depuis l'appareil : c'est une personne qui le fait dans la page Surface de contrôle. Si une case utilise un outil que le profil n'a pas, `botonera_celda_set` le signale : demandez-le à l'opérateur.
- **Approuver un appairage** : `botonera_emparejar` donne l'URL du QR ; la tablette demande et une personne approuve dans la page Surface de contrôle → Appareils.

## Matériel (Stream Deck, MIDI, OSC, X-keys…)
Un plugin enregistre l'appareil avec `plugin.superficie.registrar(...)` et il devient un appareil comme les autres (`botonera_dispositivos`, `botonera_dispositivo_asignar`). Exemples prêts : `superficie-streamdeck`, `superficie-midi`, `superficie-osc` (`plugin_desde_ejemplo`) ; pour le tally vers des moniteurs ou des caméras, `tally-tsl-umd`. Les bibliothèques (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) sont fournies avec le programme. Pour un nouvel appareil : `dispositivos_listar`, et écrivez le plugin sur `ba_plugin.superficie` (`render_tecla` dessine une touche avec PIL).

## Événements pour les plugins
`@plugin.escuchar("aire/tally")` (ou "salida/#", "audio/nivel/+", "alarma/#") reçoit les sujets de l'application ; ce que publie un plugin avec `plugin.evento("canal", datos)` apparaît sous `plugin/<id>/<canal>`. `eventos_esperar {temas, timeout_s}` attend quelque chose depuis l'assistant.
