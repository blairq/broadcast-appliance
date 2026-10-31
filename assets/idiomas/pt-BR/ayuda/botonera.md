A superfície de controle é uma grade de controles num **tablet** (pareado por QR), um **Stream Deck**, um controlador **MIDI** ou **OSC** (por plugin). Cada célula dispara uma ação e mostra o estado ao vivo. O app decide tudo: o aparelho só diz “apertaram (x, y)”.

## Montar
1. `botonera_plantillas` e `botonera_perfil_crear {desde_plantilla: "director"}` (ou audio, asamblea, prompter): é montada com as cenas, fontes e cadeiras que existem. Um perfil é um POSTO com suas `herramientas_permitidas`.
2. `botonera_celdas {perfil, pagina}` para ver; `botonera_celda_set` para pôr ou mudar uma célula:
   - `tipo`: boton, fader, perilla, medidor, texto, reloj; `estilo`: {texto, icono, color, fondo};
   - `accion`: {tipo: herramienta, herramienta, args} (args com "{{valor}}" para faders, "{{delta}}" para knobs relativos, "{{no_activo}}" para alternar), {tipo: pagina, ir_a}, {tipo: secuencia, pasos, espera_ms}, {tipo: receta, id}; `accion_mantener` (0,7 s) e `accion_soltar`;
   - `estado`: ligações ao barramento [{tema, ruta, op, valor, efecto, dato}] — efeito fondo, color, texto (“{{v}}” é o valor), titilar, valor, nivel, activo.
3. Teste com `botonera_presionar` (passa pelas permissões; o que é de ar, no ar, uma pessoa aprova).

Exemplos de ligações: tally de uma cena `{tema: "aire/tally", ruta: "programa", op: "==", valor: "Camara 2", efecto: "fondo", dato: "#c62828"}`; carregada em verde com `ruta: "preview"`; gravando `{tema: "salida/grabacion", ruta: "activo", op: "==", valor: true, efecto: "titilar"}`; loudness `{tema: "audio/loudness", ruta: "i", efecto: "texto", dato: "I {{v}}"}`; mudo `{tema: "audio/nivel/Mic 1", ruta: "mudo", op: "==", valor: true, efecto: "activo"}` com a ação `audio_set {name, muted: "{{no_activo}}"}`. Os temas: `eventos_temas`.

## O que você NÃO pode (de propósito)
- **Adicionar ferramentas a um perfil** nem habilitar a edição pelo aparelho: isso uma pessoa faz na página Superfície de controle. Se uma célula usa uma ferramenta que o perfil não tem, `botonera_celda_set` avisa: peça ao operador.
- **Aprovar um pareamento**: `botonera_emparejar` dá a URL do QR; o tablet pede e uma pessoa aprova na página Superfície de controle → Aparelhos.

## Hardware (Stream Deck, MIDI, OSC, X-keys…)
Um plugin registra o aparelho com `plugin.superficie.registrar(...)` e ele fica como mais um aparelho (`botonera_dispositivos`, `botonera_dispositivo_asignar`). Exemplos prontos: `superficie-streamdeck`, `superficie-midi`, `superficie-osc` (`plugin_desde_ejemplo`); para tally em monitores ou câmeras, `tally-tsl-umd`. As bibliotecas (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) vêm com o programa. Para um aparelho novo: `dispositivos_listar`, e escreva o plugin sobre `ba_plugin.superficie` (`render_tecla` desenha uma tecla com PIL).

## Eventos para plugins
`@plugin.escuchar("aire/tally")` (ou "salida/#", "audio/nivel/+", "alarma/#") recebe os temas do app; o que um plugin publica com `plugin.evento("canal", datos)` aparece como `plugin/<id>/<canal>`. `eventos_esperar {temas, timeout_s}` espera algo a partir do assistente.