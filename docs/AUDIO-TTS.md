# Hablarle al aparato (y que conteste)

**Sin empezar.** Esto es el diseño y las mediciones, para retomarlo cuando
toque. Nada de acá está construido todavía.

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

Todo local y vendoreado, como el resto del aparato: tiene que andar desde un
pendrive, sin internet.

### Escuchar: `transcribe.cpp`

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

- Qué tecla, y si se puede apretar desde el tablero de la tablet además del
  teclado.
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
