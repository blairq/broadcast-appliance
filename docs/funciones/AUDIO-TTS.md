# Hablarle al aparato (y que conteste)

**Escuchar: hecho** (push-to-talk → texto → barra del asistente). **Contestar
hablando: falta.** Lo construido está en «Lo que hay» al final.

En una cabina las manos están ocupadas y los ojos están en el programa. Poder
decir «pasá a la escena 4» o «¿quién está esperando?» y que conteste al oído es
de las pocas cosas que de verdad cambian cómo se opera. Pero en este aparato el
audio no es un recurso neutro: **los micrófonos SON el programa**, y eso manda
sobre todo el diseño.

## La decisión que ordena todo: es para la cabina

El control por voz es para **quien está sentado en el aparato**. El invitado no
toca nada, ni por voz ni por ningún lado.

De ahí se cae solo un montón de trabajo que si no habría que hacer:

- **No hace falta un modelo de permisos para la voz.** Quien está físicamente
  en la cabina ya tiene teclado y mouse: hablar no le da ninguna autoridad que
  no tuviera. La voz es *otra forma de escribir en la barra del asistente* y
  hereda la política de aprobación que ya existe (`mcpApproval`).
- **No hace falta palabra de activación.** En una sala con micrófonos abiertos,
  «siempre escuchando» es una mala idea: el asistente reaccionaría a lo que
  dicen los oradores. Push-to-talk alcanza y es lo más seguro.

## La trampa del micrófono del operador

El candidato obvio para escuchar es `BA.Operator`, que es el feed del operador
y no va al aire. **Pero es lo que el invitado te oye en el lobby.** Usándolo
tal cual, el invitado escucharía las órdenes que le das al asistente —«sacá a
este tipo», «pasá a la 4»—. Sería peor que la charla privada mezclada en el
chat de la sala.

**La misma tecla hace las dos cosas.** Mientras está apretada, el feed del
operador hacia el lobby se corta y ese audio va al reconocedor; al soltarla,
vuelve. Una sola tecla, sin micrófono extra, y por construcción el invitado no
puede oír lo que se le pide al asistente.

Es el mismo principio que el cerrojo de las imágenes: **que no se pueda, no que
esté desaconsejado.** Un interruptor que alguien puede mover sin querer no es
una garantía.

## La salida no toca el programa

La voz del asistente va **solo al monitoreo**, a los auriculares. Nunca al bus
de programa. libobs ya distingue `MONITOR_ONLY` de `MONITOR_AND_OUTPUT`, y el
PFL de `audio_escuchar` ya hace exactamente esta clase de ruteo: hay de dónde
agarrarse.

Y otra vez estructural: que no exista el camino de la voz sintética al
programa, no que haya una casilla destildada.

## Las piezas

Todo local: corre en la máquina, sin internet. Pero **los modelos de voz no
viajan en el bundle**: se bajan a pedido, si la persona lo va a usar, como el
modelo de lenguaje (Configuración → Modelos locales), fijados por commit o por
sha256 y verificados. Lo único que viaja es el ONNX de rostros.

### Escuchar: sherpa-onnx (antes: `transcribe.cpp`)

**Lo que se eligió al construirlo:** sherpa-onnx compilado contra el MISMO ONNX
Runtime de los rostros (estático, `make deps-sherpa-onnx`), con
**Parakeet-TDT-0.6B-v3 int8** (CC-BY-4.0, error en castellano 3,1–3,5 % en
FLEURS) y **Silero VAD** para recortar el silencio. Con push-to-talk la toma ya
está delimitada, así que no hace falta streaming. Medido en una M1 Pro: 5,3 s
de habla en ~0,25 s de CPU, con puntuación y acentos. El texto de abajo es el
diseño original con transcribe.cpp, que queda como alternativa para los
subtítulos (streaming).

Una biblioteca de C/C++ sobre ggml, MIT, de handy-computer. **Se enlaza
directo, igual que ONNX Runtime**: no entra Node en el bundle.

A esto se llegó mirando `pi-voice` (la extensión de voz de pi). Su capa de
Node **no sirve acá**, y no por purismo:

- captura con `pvrecorder`, o sea un micrófono del sistema — nosotros
  necesitamos que entre por el camino de audio propio, que es el punto entero;
- baja los modelos de HuggingFace **en tiempo de ejecución** a una carpeta del
  usuario — acá van vendoreados con hash en tiempo de compilación;
- pide `ffmpeg` como ejecutable aparte;
- y no tiene TTS: es solo dictado.

Lo que sí sirve de `pi-voice` es su catálogo, que está fijado con revisión y
sha256 publicado — el mismo patrón que `make deps-rostros`.

| modelo | para qué | español | streaming | tamaño | licencia |
|---|---|---|---|---|---|
| Nemotron Streaming 3.5 (0.6B) | el bueno | `es-US`, `es-ES` | sí | 751 MB | other |
| Canary 180M Flash | equipos flojos | `es` | no | 180M par. | CC-BY-4.0 |

**El streaming importa más de lo que parece**: transcripción parcial mientras
se habla, que es lo que hace que no se sienta lento. La licencia de Canary
(CC-BY-4.0) es permisiva, lo que para distribuir no es un detalle.

### Dos reconocedores: Parakeet o Nemotron, y cada uno elige (2026-10-26)

Parakeet sigue (y es el de fábrica para todos). Al lado hay un segundo motor,
**Nemotron 3.5 ASR Streaming 0.6B** (NVIDIA, OpenMDW-1.1, uso comercial;
castellano `es-US`/`es-ES`, ~4,3 % en FLEURS con bloques de 560 ms), y cada
consumidor elige el suyo en la **pestaña Agentes** (arriba de los roles) o por
MCP (`voz_reconocedores`):

| consumidor | qué es | clave de sus corrientes |
|---|---|---|
| `dictado` | F9 del operador y el micrófono de la barra | `dictado` (toma suelta) |
| `transcripcion` | las «Voz a texto» en continuo: subtítulos, acta, disparadores y la **traducción en tiempo real** | el nombre de la salida de texto |
| `recepcion` | la atención hablada (lobby o sesión remota) | `atencion:<id>` |
| `registro` | el tótem (varias personas a la vez) | `registro:<conn>` |

**Cómo es cada uno.** Parakeet (`audio/Escucha`) es offline: el VAD corta la
frase en la primera pausa y recién ahí se transcribe entera; cada corriente de a
una, 1–3 s después. Nemotron (`audio/EscuchaStreaming` + `audio/EscuchaNemotron`)
es streaming: UN `OnlineRecognizer` de sherpa-onnx y un stream por corriente, que
se decodifican **en lote** (`SherpaOnnxDecodeMultipleOnlineStreams`: una corrida
del encoder para todas las voces que tienen un bloque listo). Da texto parcial
mientras se habla (`oido[].parcial`) y cierra el turno con el endpoint del modelo
(silencio después de lo dicho) o cuando se acaba el margen después de la voz.
Medido en la Mac (M1 Pro, 2 hilos): 13,8 s de habla x8 voces a la vez en 7,9 s;
una voz, 8 s en 0,8 s; carga en menos de 1 s (con el archivo en caché).

**El VAD manda qué le llega a Nemotron.** El modelo streaming procesa todo lo
que recibe, silencio incluido: cada corriente tiene su Silero y al modelo solo
le llega la voz, con 0,5 s antes (Silero marca la voz recién a los 0,25 s) y
0,8 s después (`escucha.margenAntesS` / `escucha.margenDespuesS`). Una frase que
pasa el máximo de los cortes se cierra en la primera pausa, no en seco (cortada
a mitad de palabra, el modelo pierde las palabras de los dos lados del corte: se
vio con «… doce, y [vengo por] la sesión…»).

**El idioma** va por stream (el «prompt» de idioma de Nemotron, la opción
`language` de sherpa-onnx): `es-US` de fábrica (`escucha.nemotronIdioma`:
`es-US`, `es-ES` o `auto`).

**Una copia de cada modelo en memoria** (`agent/Reconocedores`): cada corriente
abierta cuenta para el motor de su consumidor; el motor se carga con el primer
uso y se suelta cuando no queda ninguno, pasados 2 min (`escucha.descargarTrasS`),
para que un tótem que se reconecta no recargue 700 MB. El dictado cuenta siempre
(F9 está siempre a mano), así que su motor queda cargado como antes. Si los dos
motores están en uso por consumidores distintos, la pestaña Agentes muestra una
**alerta** (quién usa cada uno, ~1,7 GB entre los dos) con «Todos con Parakeet» /
«Todos con Nemotron»; lo mismo en `voz_reconocedores` (`alerta`) y en `ia_estado`.
Lo elegido va en `Config/reconocedores.json`.

**Interrumpir al asistente en el tótem.** La página del tótem pide el micrófono
con cancelación de eco y la voz del asistente baja por el mismo WebRTC. Con
Parakeet, mientras habla el asistente al oído le llegan ceros (si no, se
transcribiría a sí mismo). Con Nemotron y la opción «la persona puede
interrumpir» (pestaña Agentes; de fábrica prendida), el oído sigue oyendo: si el
VAD detecta la voz de la persona mientras el asistente habla, se corta la voz en
el acto (`VozAlLobby::callar`, y `Habla::callar` si no habla en otro tótem) y se
la escucha. Depende de que la cancelación de eco del navegador ande: si el eco
pasa, el asistente se cortaría solo (apagar la opción).

**El modelo** está en el catálogo como tipo `voz_streaming`:
`csukuangfj2/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-560ms-int8-2026-06-11`
(el export oficial de sherpa-onnx, revisión `ab43d89…` y el sha256 de cada
archivo fijos), 0,68 GB. sherpa-onnx v1.13.8 (la fijada) ya lo soporta: no hizo
falta actualizarla. Pruebas: `test_reconocedores` (sin modelos: la elección, la
carga por usos y la alerta; el lote, los márgenes y los cortes con un modelo
falso) y `test_nemotron` (con el modelo de verdad: `BA_NEMOTRON_MODELO`,
`BA_VOZ_VAD`, opcional `BA_VOZ_WAV` y `BA_NEMOTRON_VOCES`; sin eso da SKIP).

### Contestar: piper

TTS sobre **el ONNX Runtime que ya está vendoreado** para el reconocimiento de
rostros. No entra ningún runtime nuevo: solo un modelo de voz. Tiene voces en
español que suenan bien.

### La forma, que ya está escrita

Idéntica al `Reconocedor` (`src/app/ui/Reconocedor.hpp`): un hilo propio que es
el único dueño del modelo, pedido y callback, y **nada pesado en el hilo de la
interfaz**. Esa lección ya se pagó una vez: el reconocimiento de rostros corría
en el hilo de la interfaz, el latido se estancaba y el launcher daba a la app
por colgada. No repetirla.

El resto del camino de audio también está: `RackManager`, el mezclador,
`libsamplerate`, el monitoreo.

## Latencia esperada

Medido hoy, con las piezas que ya andan:

- el modelo propio contesta en **0,6 s** (medido, con imagen incluida);
- una frase de ~5 s en un modelo de 0.6B en CPU ronda **1 s**;
- piper es rápido.

Un ida y vuelta completo de **2 a 3 segundos**. Usable en cabina, sobre todo
con transcripción parcial mientras se habla.

## Lo que me preocupa de verdad

**Los nombres.** El reconocedor va a escribir «cero cuatro presentación
invitado uno» o «Ana Peres». Si el asistente toma la transcripción literal, no
va a encontrar ni la escena ni la persona, y va a contestar que no existen.

La solución no es un modelo más grande: es que **no confíe en el texto**. Que
compare contra los nombres que existen de verdad —`scene_list`,
`rostro_listar`, `room_status`— y elija el más parecido. Ahí se decide si esto
sirve en una cabina o es una demo bonita.

## Lo que queda por decidir

- La tecla quedó en **F9** (con la ventana de la app enfocada) y el botón del
  micrófono de la barra. Falta: configurable, y desde el tablero de la tablet.
- Si la respuesta hablada es siempre o solo cuando no hay nadie mirando la
  pantalla (hablar de más molesta).
- Si conviene cortar el reconocimiento mientras se está al aire y hay silencio
  esperado, para no gastar CPU que le hace falta al video.
- Qué pasa si se habla durante un take: probablemente ignorar y avisar.

## Nota aparte: dictado gratis si el asistente se muda a pi

Si alguna vez el asistente interactivo pasa de opencode a pi,
`pi install npm:@earendil-works/pi-voice` da dictado con push-to-talk sin
escribir una línea. Pero es **dictado en una TUI**: escribe texto en el chat,
no controla el aparato. Es otra cosa que lo de arriba y no lo reemplaza.

## Lo que hay

| | |
|---|---|
| `tools/deps/sherpa-onnx.sh` | sherpa-onnx v1.13.8 contra el ORT de `extern/rostros`, estático (Mac y Linux; Windows falta) |
| `src/app/audio/Escucha` | el hilo dueño del reconocedor y el VAD: empezar / alimentar / terminar |
| `src/app/OperatorFeed` | push-to-talk: con la tecla apretada, el micrófono va al dictado y el lobby recibe silencio |
| `src/app/agent/Voz` | F9 y el botón: carga los modelos bajados, junta todo, manda el texto a la barra |
| nodo «Voz a texto» del rack | en Audio, en cualquier fuente, como una salida: **dictado** (F9 al asistente; si hay varias, la que más habló) o **continuo** (subtítulos, acta; frase por frase con VAD, 1–3 s). Sin ninguno, F9 se lo pone al micrófono del operador |
| Configuración → Voz del operador | el micrófono del operador (antes en Sala), dónde hay voz a texto, la voz que contesta y lo transcripto |
| `audio/Habla` + `audio/MonitorVoz` | contesta hablando: piper frase por frase, solo al monitoreo (fuente privada MONITOR_ONLY) |
| MCP | `voz_microfono`, `voz_texto`, `voz_transcripcion`, `voz_decir`, `voz_ajustes`, `voz_reconocedores` |
| `datos/modelos-catalogo.json` | Parakeet (fijado por commit), Nemotron 3.5 streaming (commit y sha256 de cada archivo) y Silero VAD (por URL y sha256), tipos `voz`, `voz_streaming` y `vad` |
| `src/app/audio/EscuchaStreaming` + `EscuchaNemotron` | Nemotron: un reconocedor, un stream por corriente, en lote; el VAD decide qué le llega |
| `src/app/agent/Reconocedores` | qué motor usa cada consumidor, carga y descarga por usos, la alerta de los dos a la vez |
| Agentes → Oído | el motor de cada consumidor, la alerta y la opción de interrumpir del tótem (`controls/PanelOidos.qml`) |
| `test_escucha` | con modelos de verdad: wav a 22 kHz con silencio, el VAD lo recorta y el texto sale bien |
