# Eventos: storyboard, oradores y pantallas

Documento de diseño y estado. Cada punto dice si ya está hecho o qué falta.

**Hecho al 2026-09-21:** el storyboard mínimo (1.1 a 1.5, menos el modo
evento), el ensayo (1.4) y el teleprompter (2), con todo expuesto al asistente
por MCP. Falta lo demás.

La idea de fondo: hoy el sistema sabe **emitir**. Lo que falta es que sepa
**cómo va a ser el evento** — qué pasa, en qué orden, cuánto dura, quién habla,
qué suena y qué se ve en cada pantalla — y que eso se pueda ensayar antes.

---

## 1. El storyboard — **hecho**, menos el modo evento

### 1.1 Qué es

Un **evento** es una carpeta con su guion y sus medios:

```
Eventos/<nombre>/
├── evento.json         # la línea de tiempo (`linea`) y su lectura como guion de cues (`bloques`)
├── recorrido.jsonl     # lo que salió al aire, renglón por renglón (ver 1.6)
├── Medios/             # música, videos, placas propias del evento
├── Discursos/          # los textos del teleprompter
└── Respaldos/          # evento-<fecha>.json al guardar, evento-v1.json, linea-<n>.json
```

Que sea una carpeta y no un registro suelto permite **llevarlo a otra máquina**
(o al equipo de respaldo) con todo adentro, y versionarlo.

### 1.2 Bloques y cues

El guion es una lista de **bloques** (apertura, himno, palabras del intendente,
video institucional, panel, cierre). Cada bloque tiene una lista de **cues**, que
son las cosas concretas que pasan:

| Cue | Qué hace |
|---|---|
| `escena` | pone una escena al aire por el PVW: la arma y hace TAKE (con su transición) |
| `musica` | arranca o para un tema, con fundido y volumen objetivo |
| `video` | reproduce un video (y qué hacer al terminar) |
| `teleprompter` | carga un texto y lo deja listo o lo arranca |
| `orador` | pone a alguien al aire, con su tiempo asignado |
| `pantalla` | manda contenido a una pantalla/Chromecast |
| `audio` | cambia el rack, silencia, hace ducking |
| `pausa` | espera N segundos y sigue sola |
| `espera` | **espera a que el operador confirme** (lo más importante: nada crítico se dispara solo) |
| `nota` | no hace nada: es una indicación para el operador o el orador |

Cada cue tiene: **duración prevista**, si es **automático o manual**, y de qué
cue depende. Los automáticos encadenan; los manuales esperan el clic.

**Pasar solo — hecho (L0, y desde L2 sobre la línea de tiempo).** Quién decide
cuándo toca es la línea (`linea::Corrida`, pura y probada en `test_linea`); cada
`EjecutorLinea` solo programa un disparo a esa hora y hace lo que le dicen. Ver 1.6:
desde la línea, el paso (lo que entra con un GO) agrupa los cues que lo siguen.

- Un cue **automático** pasa solo al siguiente cuando se cumple su duración. Sin
  duración, en el acto: así «cámara 2 + zócalo + bajar la música» entran juntos.
- Una **pausa** con duración espera y sigue sola (no hace falta marcarla
  automática).
- Una **espera** nunca pasa sola, aunque diga automática: existe para que
  confirme alguien. El **último** cue tampoco: terminar el evento lo decide una
  persona.
- Los últimos 5 s se avisan en la conducción: **«entra solo en 0:05»**
  (`entraSoloEnSeg` del view model; `reloj.entraSoloEnSeg` por MCP).
- **HOLD** (botón, tecla **H**, `evento_correr {"accion": "hold"}`, `linea_hold`):
  congela el avance automático del paso al aire. No es una pausa: la cuenta sigue
  en «+» y el atraso corre. Otro HOLD lo suelta (si ya se pasó, entra en el acto),
  y al cambiar de paso se suelta solo.
- La **pausa general** sigue igual: lo pausado no cuenta como desvío ni para el
  avance automático (la cuenta del cue se congela).

**El cue de escena pasa por el PVW.** No va directo al programa: se arma en el
PVW y entra con TAKE por el conmutador (`escenas::Conmutador`), con la
transición del cue (`datos.transicion` y `datos.ms`, opcionales) o la de entrada
de la escena. Así el tally, el AFV y la escena guardada se enteran como con
cualquier otro cambio del aire. **Lookahead:** cuando un cue de escena pasa a
ser el siguiente, su escena queda armada en el PVW de antemano (en ensayo no).
Si la escena ya está al aire, el cue no hace nada. El cue de video también
pone su escena por el conmutador (corte directo, como antes). Con varias
salidas, por ahora usa el conmutador de la salida por omisión.

### 1.3 Tiempos

- Hora prevista de inicio del evento y de cada bloque.
- Duración prevista por cue y por bloque; el total se calcula solo.
- Durante el evento: **desvío acumulado** («vamos 3:20 tarde»), bien grande y a
  la vista, y el aviso de los últimos 5 s antes de que un cue automático pase
  solo.
- Al terminar: comparación previsto contra real, para el evento siguiente.

### 1.4 Ensayo (dry-run) — **hecho** el ensayo sin aire y el acelerado; la
revisión previa todavía no mira archivos ni dispositivos

Un **modo ensayo** que corre el guion **sin salir al aire**, porque ya hubo
problemas y hay que verlos antes:

- **Revisión previa**: que existan los archivos, las escenas, las fuentes, los
  dispositivos de audio y video; espacio en disco; que los invitados citados
  estén conectados; que el teleprompter tenga su texto.
- **Ensayo real**: corre el guion contra el motor pero con la salida al aire
  apagada, para ver transiciones, niveles y tiempos de verdad.
- **Ensayo acelerado**: recorre los cues sin esperar las duraciones, solo para
  validar que cada uno puede ejecutarse.
- Informe final con lo que falló y un botón para ir al cue problemático.

### 1.5 Mientras corre — la vista de conducción está; el **modo evento** no

- Una vista de **conducción**: el cue actual grande, el siguiente, el reloj del
  bloque y el desvío.
- Saltar, repetir, adelantar, **pausar todo el guion**.
- **Modo evento**: mientras corre, las acciones peligrosas (borrar escenas,
  actualizar, cambiar configuración que reinicia el gateway) piden confirmación
  extra o quedan bloqueadas.

### 1.6 La línea de tiempo — **hecho L0-L3** (modelo, ejecutor de una salida y MCP)

El plan entero está en `docs/planes/PLAN-LINEA-DE-TIEMPO.md`. Lo hecho:

- **La línea manda.** El plan del evento es su línea (`evento/Linea.hpp`):
  bloques → **pasos** (lo que entra con un GO o solo: `tras`, `con`, a una
  `hora` con política avisar/esperar/cortar, `ancla` a otra salida) → **piezas**
  en pistas, con su tiempo relativo al paso, su `vida` (paso, bloque, hasta que se
  saque, evento) y qué hacer si se perdieron. `evento.json` la lleva en `linea`
  (versión 2).
- **Migración.** Un evento de antes (sin `linea`) se migra al abrirlo: cada cue
  de escena, orador, video, pausa o espera abre un paso y los demás son piezas de
  ese paso. La copia del v1 queda en `Respaldos/evento-v1.json` y el archivo pasa
  a v2. Una espera migrada es un paso abierto que nunca se va solo (como en el v1).
- **El guion de cues es una lectura de la línea** (`linea::proyectar`): la página
  Guion, `guion_*` y `evento_correr` siguen andando igual. Editar un cue edita la
  línea (`linea::reconciliar`): lo que no se tocó queda exacto (hard starts,
  salidas, PTZ, anclas), lo tocado se migra de nuevo sin perder lo propio de la
  línea. Las piezas que el v1 no conoce (PTZ, asistente…) se ven como notas y no
  pierden su tipo. Desde la línea, `bloques` en el archivo es solo lectura (se
  regenera al guardar).
- **Un solo ejecutor** (`linea::Corrida` + un `EjecutorLinea` por salida; el
  `Guion` v1 ya no existe). Timer exacto al próximo evento (no un sondeo), piezas
  a su hora y fuera al terminar su vida, lookahead al PVW, el aviso de 5 s, GO,
  HOLD, pausa general, mover el próximo, saltar, ir a con «load to time» (repone
  la música y los gráficos de vida larga y saca lo que sobra), repetir, adlibs del
  paso y del estante, e insertar un paso del estante (una copia; lo planeado no se
  toca). «Siguiente» en el guion de cues dispara la pieza siguiente del paso al
  aire si no salió todavía (lo que antes era el cue siguiente) y si no, hace GO.
  Por ahora sale de verdad la salida por omisión; las otras se anotan.
- **Lo emitido**: `Eventos/<x>/recorrido.jsonl` (qué entró, cuándo y cómo, las
  piezas, los HOLD, los saltos, lo insertado, los cortes, lo que se contó y lo que
  se decidió; lo del ensayo no) y `Runtime/linea-aire.json` (el estado vivo,
  atómico, cada segundo y en cada cambio). El informe y los capítulos salen de lo
  emitido, un renglón por paso.
- **Corte de luz** (T47 + 3.8 del plan). Si la app arranca después de un corte
  con `linea-aire.json` vivo, la línea vuelve con lo que estaba al aire (repone
  música y gráficos de vida larga) pero **en HOLD en todas las salidas**, y la
  conducción pregunta **«¿Qué está pasando?»** (texto, o dictado por la barra del
  asistente). Lo contado va al asistente con el resumen de la retoma para que
  **proponga** cómo adecuar la línea (`linea_editar`/`linea_ajustar` con probar);
  la persona confirma y recién ahí se suelta (`linea_soltar`). Sin respuesta queda
  en HOLD; «Seguir como estaba» o un GO lo sueltan tal cual. Todo queda en el
  recorrido. Para probarlo sin cortar nada: `linea_retomar {"simular": true}`.
- **MCP** (`linea_*`, categoría «Línea de tiempo del evento»): ver, estado,
  editar en lote con prueba y deshacer, revisar, ajustar a una hora (simple:
  recorta lo «recortable» y propone saltear lo «opcional»), las acciones de vivo y
  el flujo del corte. Ver `docs/funciones/MCP.md`.

Falta (L4 en adelante): la escaleta y la conducción nuevas en QML, la vista
multipista, varias salidas de verdad, disparadores y condiciones (`cuando`,
`mientras`), las superficies web y el productor automático.

---

## 2. Teleprompter — **hecho**

Una **app web propia**, servida por el gateway, con tres vistas:

- **Tablet del orador** (`/prompter`): solo el texto, grande, alto contraste,
  con opción de espejo para vidrio, márgenes y guías de lectura.
- **Control** (`/prompter/control`, en un celular): arrancar, parar, subir y
  bajar, velocidad, tamaño, ir a una marca, y un botón de pánico («pará ahí»).
- **Automático**: baja solo a velocidad configurable, con marcas en el texto
  para ajustar el ritmo por párrafo.

Detalles que importan:

- El texto **se carga en el sistema** (en `Discursos/`), no se pega en la
  tablet: así queda con el evento y el asistente puede ayudar a escribirlo o
  acortarlo.
- **Estimación de duración** del discurso por cantidad de palabras y velocidad
  (unas 130-150 palabras por minuto en castellano), para que el storyboard
  calcule tiempos reales.
- **Cuenta regresiva y aviso** de «te quedan 30 segundos», igual que el reloj
  del orador que ya existe.
- Que ande **sin internet**, en la LAN, y que aguante que la tablet se
  desconecte y vuelva sin perder la posición.

---

## 3. La vista del orador (y del orador remoto)

Hoy el invitado remoto ve el programa. Falta una vista pensada para **el que
está hablando**, que el operador pueda cambiar a distancia:

- **Texto para leer**: el teleprompter, cuando se le manda.
- **El orden de oradores**: quién habla ahora, quién sigue, cuánto le queda.
- **Cara a cara con el remoto**: cuando el orador de sala le habla a alguien
  remoto, hoy termina hablándole a la proyección que tiene a la espalda. Si el
  orador tiene su propia pantalla (tablet o monitor de retorno) con **la cara
  del remoto de frente**, mira al frente y no de costado.
- **Avisos del operador**: «redondeá», «te escuchamos bajo», «pasá al siguiente».

La forma más limpia es **ampliar la app web del invitado**: cuando a alguien se
lo promueve a orador, su propia interfaz pasa a mostrar esta información. Ya
tenemos el canal (WebRTC + el chat de la sala) y el rol de orador.

---

## 4. Pantallas: sala y Chromecast

### 4.1 Dos cosas distintas

Conviene separarlas desde el diseño, porque tienen latencias muy diferentes:

- **Pantallas del escenario** (proyector, TV detrás del orador): tienen que ir
  **sincronizadas** con lo que pasa en la sala. Lo bueno acá es salida directa
  (una ventana de salida en otro monitor, que ya existe), NDI o SRT con latencia
  baja en la LAN.
- **Chromecast y similares**: con el reproductor que trae de fábrica (HLS o
  DASH) el retardo es de 1 a 5 segundos. Pero el receptor de Chromecast **es un
  navegador**: se le puede pedir que abra una URL nuestra con un reproductor de
  baja latencia adentro, y ahí el retardo baja a menos de un segundo (ver 4.3).

### 4.2 Qué hay que armar

- **Servidor de baja latencia en la LAN**: publicar lo que generamos (programa,
  o una escena concreta) para que otras pantallas lo tomen. Candidatos: WebRTC
  (lo más bajo, ya tenemos la pieza), SRT con `latency` chico, o NDI (que ya
  usamos para entradas).
- **Sección de pantallas**: lista de dispositivos encontrados, qué manda cada
  uno (programa, una escena, una placa fija, un video), y un cue de storyboard
  para cambiarlo.
- **Chromecast**: descubrimiento por mDNS y control por el protocolo de Google.

### 4.3 Chromecast con baja latencia (receptor propio)

El receptor de un Chromecast es una **página web** corriendo en su Chrome: no
hay que mandarle video, hay que **decirle qué URL abrir**. Y esa URL la
servimos nosotros, que para eso ya tenemos servidor web y WebRTC.

**Cómo queda la cadena:**

1. El gateway sirve una página receptora (`/cast`) con un reproductor WebRTC.
2. Esa página se engancha como **espectador** del relay que ya existe (el mismo
   que alimenta las vistas web), pidiendo el programa o una escena.
3. Al Chromecast se le manda esa URL por su protocolo; el resto pasa en la LAN.

Latencia esperada: **menos de un segundo**, contra 1 a 5 segundos del
reproductor de fábrica. Sigue sin servir para sincronizar labios con el audio
de sala, pero para una pantalla secundaria cambia todo.

**Lo que hay que resolver, sin sorpresas el día del evento:**

- **Cómo se le dice qué URL abrir.** Un receptor propio necesita un *app id*
  registrado en la consola de Google Cast (cuenta de desarrollador, y el
  dispositivo dado de alta mientras se prueba). Hay receptores genéricos que
  aceptan una URL, pero dependen de que sigan publicados: como plan principal
  conviene el propio.
- **HTTPS con certificado de verdad.** El Chromecast exige HTTPS para el
  receptor y **no acepta certificados autofirmados**, que es justo lo que
  usamos en la LAN. Salidas posibles: servir esa página por el túnel de
  Cloudflare (certificado válido) aunque el video siga yendo por la red local,
  o un dominio propio con certificado. Hay que probarlo temprano: es el punto
  que más tiene para fallar.
- **Qué tan viejo es el aparato.** Los Chromecast con Google TV traen un Chrome
  completo y hacen WebRTC bien. Los más viejos (los dongles) tienen un Chrome
  recortado: si WebRTC no va, el mismo receptor puede caer a HLS de baja
  latencia, con más retardo pero funcionando.
- **Reintento**: si la pantalla se apaga o el aparato se reinicia, que vuelva
  solo a la URL. Eso lo maneja la sección de pantallas.

### 4.4 Qué transporte usar (y qué cuesta cada uno)

**Para la presentación en vivo, WebRTC**, que además es lo que ya tenemos.

El costo no es el que uno supone: en este sistema **el relay reenvía los bytes
ya codificados**. Cuando un invitado manda video, el `TapTransformer` copia el
cuadro codificado al `Feed` de esa sesión y cada espectador es una
PeerConnection cuyo sender **no tiene encoder** sino un `ViewerTransformer` al
que se le inyectan esos mismos bytes (ver el encabezado de `Webrtc.cpp`). O
sea: **una codificación, muchos espectadores**. Cada pantalla suma cifrado
SRTP, pacing y RTCP, que es poco.

Para mandar el **programa** (lo que compone libobs en A) a las pantallas hace
falta el mismo truco al revés: A ya codifica para transmitir, así que se le
cuelga una segunda salida del **mismo codificador** y B reenvía esos cuadros a
los espectadores. Una sola codificación para todo.

| Camino | CPU | Retardo | ¿Lo abre el navegador del Chromecast? |
|---|---|---|---|
| **WebRTC con relay** (lo nuestro) | 1 codificación + SRTP por pantalla | 100-300 ms | sí (en los nuevos) |
| fMP4 por WebSocket + MSE | 1 codificación + copiar bytes | 200-600 ms | sí |
| LL-HLS | 1 codificación + servir archivos | 1-3 s | sí |
| RTP multicast / SRT | 1 codificación, casi nada por receptor | 50-200 ms | **no** (hace falta reproductor nativo) |
| MoQ (Media over QUIC) | 1 codificación | bajo | todavía no; el estándar está en borrador |

**Decidido: una pantalla es un espectador más.** Como el video ya se codifica
una vez, sumar una pantalla en la LAN es abrirle una PeerConnection que recibe
esos mismos bytes. No se codifica de nuevo: lo que paga es el cifrado SRTP y el
pacing, que es poco. Con eso alcanza.

**¿Hay algo todavía más rápido?** Sí, pero con condiciones, y conviene medir
antes de escribirlo.

Primero, lo que no se puede: **un navegador no recibe UDP crudo**. Lo más
parecido es **WebTransport** (datagramas QUIC), que sí llega a una página.

Y lo que agrega retardo en Chrome no suele ser el transporte sino **el
reproductor**: `<video>` con MSE guarda buffer, y WebRTC mete su jitter buffer
(se achica con `playoutDelayHint: 0`, pero no desaparece). El camino sin
ninguno de los dos es:

> **WebTransport (o WebSocket) + WebCodecs pintando en un canvas.** El servidor
> manda los cuadros ya codificados, la página los mete en un `VideoDecoder` y
> dibuja cada uno apenas sale: red más un cuadro, decenas de milisegundos.

A cambio hay que resolver a mano lo que WebRTC te da: retransmisiones, control
de congestión y el audio con su sincronía (nulo si la pantalla va muda, que es
lo normal cuando el sonido sale por el PA). Y no anda en los Chromecast viejos.

Si la pantalla **no** es un Chromecast sino una mini PC, lo más barato de todo
es saltear el navegador: **RTP multicast o SRT con un reproductor nativo**
(mpv, ffplay), una sola codificación y un solo envío para todas.

**Dónde sigue sirviendo MSE:** para **videos estáticos** que tienen que ir
clavados entre varias pantallas. Ahí la página puede corregir su desfase
moviendo `playbackRate` unas milésimas contra un reloj común por WebSocket, que
es el actuador que necesita un PID. WebRTC no deja hacer eso: se puede pedir
más o menos buffer (`playoutDelayHint`), pero no ajustar la velocidad.

**Entonces:**

- **Vivo** (programa, cámaras, oradores, retorno): WebRTC, que ya está y sirve
  adentro y afuera de la LAN. Cada pantalla es un espectador más.
- **Solo si medimos y hace falta menos retardo** en pantallas mudas de la LAN:
  WebTransport + WebCodecs, con WebRTC de respaldo.
- **Video pregrabado que tiene que ir sincronizado entre pantallas**: fMP4 por
  WebSocket + MSE, con el PID y el reloj por WebSocket.
- **Pantalla que no pueda con ninguno de los dos**: LL-HLS, con más retardo.
- **MoQ**: seguirlo de lejos; hoy es borrador y depende de WebTransport en el
  aparato.

---

## 5. Cámaras de público (y su audio)

Hoy una cámara IP sin audio y un micrófono de sala son dos cosas sueltas que
hay que atar a mano. Proponemos una **sección propia**, con la misma idea que
el rack de audio:

- Una **cámara de público** es una entrada de video **más** una entrada de
  audio asociada (el micrófono ambiente que le corresponde).
- **Retardo de audio configurable** para que labios y sonido coincidan: las
  cámaras IP suelen llegar con retardo y hoy no hay forma de compensarlo.
- Varias, con nombre («tribuna», «fondo», «palco»).
- **Ducking automático**: cuando habla el orador, el ambiente baja solo; cuando
  termina, sube. Es lo que se hace a mano y siempre sale tarde.
- Cue de storyboard: «pasá a cámara de público 2 durante los aplausos».

---

### 5.1 Calibrar el retardo solo (auto-tuning de labios y voz)

Poner el retardo a mano es adivinar. Lo que queremos es **medirlo**, y hacerlo
antes del evento (entra natural en la revisión previa del punto 1.4).

**El problema.** Una cámara IP llega con 200 ms a 2 s de retardo (codificación
+ red + buffer), mientras que el micrófono por la placa de audio llega en 10 a
50 ms. Resultado: se escucha antes de que se vean los labios. Hay que **atrasar
el audio** hasta que coincida — y el número depende de cada cámara, de la red y
hasta del día.

**Cómo medirlo: el aplauso.** Es el método del claquetazo de toda la vida, pero
automático:

1. El sistema le pide a alguien que se pare frente a la cámara y aplauda una
   vez (el asistente puede guiarlo por chat, o sale en pantalla).
2. En el audio se detecta el **transitorio**: un salto de nivel muy corto y
   muy por encima del piso de ruido. Eso ya lo puede ver el analizador del rack.
3. En el video se detecta el **cambio brusco**: diferencia entre cuadros
   consecutivos, que salta cuando las manos se juntan.
4. El retardo es la diferencia entre los dos instantes.
5. Se repite 5 veces y se toma la **mediana**, que es lo que aguanta un
   aplauso mal detectado.

Después, un ajuste fino a mano con vista previa en vivo, porque el ojo humano
es el juez final.

**Cómo aplicarlo.** Hay dos caminos y conviene tener los dos:

- `obs_source_set_sync_offset()` de libobs: retardo por fuente, en nanosegundos
  y con signo. Es el mecanismo estándar y sirve también cuando el audio llega
  **después** del video.
- Un módulo de retardo en el rack (LSP trae `comp_delay_stereo`, pensado justo
  para esto): útil cuando además querés procesar ese audio.

**Dónde vive el número.** Con la cámara de público y su micrófono, en la
configuración del evento: cada par guarda su retardo medido, cuándo se midió y
con qué dispersión. Si la dispersión entre aplausos es alta, el sistema lo
avisa en vez de dar un número que no es.

**Cuándo se corre:**

- En la revisión previa del ensayo, para cada par cámara + micrófono.
- A pedido, con un botón «Medir de nuevo» (cámara reiniciada, red cambiada).
- Aviso si el retardo medido se aleja mucho del guardado.

**Lo que hay que mirar con honestidad:**

- Las cámaras IP tienen **jitter**: el retardo no es una constante. La mediana
  sirve, pero si la dispersión es grande hay que decirlo y no fingir precisión.
- Con RTSP existe un camino más exacto (los informes RTCP traen marcas de
  tiempo NTP), pero depende de que la cámara tenga la hora sincronizada. Vale
  como mejora posterior, no como base.
- Medir sin aplauso, correlacionando el movimiento de la boca con la envolvente
  de la voz, es posible pero mucho más ruidoso. Primero el aplauso, que es
  simple y se entiende.

## 6. Música y videos del evento

- **Biblioteca del evento**: los temas y videos quedan en `Medios/` de la
  carpeta del evento, con su duración, su punto de entrada y de salida y el
  volumen objetivo.
- **Fundidos** de entrada y salida por cue, y bajada automática cuando alguien
  habla.
- **Buscar y traer música con el asistente** (ver 8): que busque, te la muestre,
  y recién si te gusta la descargue y la deje lista **offline**. Nada de
  depender de internet el día del evento.
- Aviso honesto: bajar contenido de terceros tiene sus condiciones de uso y sus
  derechos. La herramienta está; el criterio es del operador.

---

## 7. Cliente del invitado

- **No pedirle los datos al entrar** cuando está prendida la atención por el
  asistente: que entre con el nombre y listo, y el resto se lo pregunte el
  asistente por chat.
- **El chat abierto al conectarse**, no minimizado: hoy queda escondido y la
  persona no ve que le están hablando.
- **Personalización**: fondo del cliente con imagen o video propio, colores y
  logo del evento. Que se configure por evento y viaje en su carpeta.
- Aprovechar que ya está: aviso de «te quedan 30 segundos», mano levantada,
  archivos.

---

## 8. Herramientas para el asistente

Para que la IA pueda hacer trabajo de verdad, el bundle tiene que traer, por
plataforma, herramientas propias (nunca del sistema, como todo lo demás):

- **Descargar y buscar**: `yt-dlp` (YouTube y otros), `curl` (ya viaja
  `cloudflared`, así que el patrón está), y un conversor de audio y video
  (`ffmpeg` ya viene con libobs).
- **Flujo pensado**: el asistente busca, te muestra **candidatos con título,
  duración y una vista previa**, vos elegís, y recién ahí descarga, normaliza el
  volumen y lo deja en `Medios/` listo para el storyboard.
- **Con permiso**: descargar y ejecutar cosas es exactamente lo que la
  salvaguarda de aprobaciones tiene que cubrir. Igual que la atención del
  lobby, se habilita por tarea y se ve qué se está por hacer.

---

## 9. Interfaz

- **Sacar cada pestaña a su propia ventana**, para mandarla a otra pantalla.
  Ya existe para el video («Sacar a ventana»); falta generalizarlo a Escenas,
  Audio, Sala, Storyboard y Asistente.
- **Vista de conducción** del evento (punto 1.5), pensada para mirarla de lejos.

---

## 10. Asistente de armado

Cuando el storyboard exista, que el asistente ayude a **llenarlo**:

- A partir de una descripción del evento («acto por el 25 de mayo, 40 minutos,
  himno, dos discursos, video institucional, cierre»), que proponga los bloques
  con sus tiempos.
- Que arme las **escenas propias del evento** (placas con el nombre del acto,
  fondo, zócalos con el cargo de cada orador).
- Que revise el guion y avise lo que falta: un discurso sin texto, un cue sin
  archivo, un orador sin tiempo, bloques que no cierran con la hora prevista.
- Que prepare el **informe posterior**: duración real por bloque, desvíos.

---

## 11. Orden sugerido

Cada punto de acá es entregable por sí solo:

1. ~~**Storyboard mínimo**: modelo de datos, lista de bloques y cues, tiempos
   calculados, ejecución manual (el operador aprieta «siguiente»).~~ **Hecho.**
   Lo que sigue de este punto es el **modo evento** (1.5) y que los cues de
   música, video, pantalla y audio se ejecuten de verdad: hoy quedan escritos
   en el guion, cuentan para los tiempos, y al tocarles el turno el sistema
   avisa que todavía no los sabe ejecutar en vez de fallar callado.
2. ~~**Revisión previa y ensayo**~~ **Hecho** lo que se puede saber sin tocar
   nada (cues incompletos, escenas que no existen) más el ensayo sin aire y el
   acelerado. **Falta** lo que necesita mirar el disco y los aparatos: que los
   archivos estén, que los dispositivos respondan, espacio libre, y que los
   invitados citados estén conectados.
3. ~~**Teleprompter** (app web + control desde el celular + texto en el
   sistema).~~ **Hecho.** Falta el modo automático con marcas de ritmo por
   párrafo (hoy la velocidad es una sola para todo el discurso).
4. **Vista del orador** en la app del invitado (texto, orden, cara del remoto).
5. **Cámaras de público** con su audio y el **retardo medido solo** (5.1): es
   de lo que más se nota y hoy se hace a ojo.
6. **Cliente**: chat abierto, sin preguntas si está el asistente,
   personalización.
7. **Herramientas del asistente** (descarga de música y video).
8. **Pantallas**: primero el reproductor de baja latencia en la LAN (que sirve
   para todo), después el receptor de Chromecast que lo abre. Probar temprano
   lo del certificado, que es lo que puede trabar todo.
9. **Asistente de armado** del storyboard.

Lo de sacar pestañas a ventanas se puede hacer en cualquier momento: es chico y
se disfruta enseguida.
