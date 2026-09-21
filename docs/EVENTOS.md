# Eventos: storyboard, oradores y pantallas

Documento de diseño. Todavía no hay nada de esto implementado: es la lista
desglosada de lo que queremos, para discutirla y después partirla en tandas.

La idea de fondo: hoy el sistema sabe **emitir**. Lo que falta es que sepa
**cómo va a ser el evento** — qué pasa, en qué orden, cuánto dura, quién habla,
qué suena y qué se ve en cada pantalla — y que eso se pueda ensayar antes.

---

## 1. El storyboard

### 1.1 Qué es

Un **evento** es una carpeta con su guion y sus medios:

```
Eventos/<nombre>/
├── evento.json         # el storyboard (bloques, cues, tiempos)
├── Medios/             # música, videos, placas propias del evento
├── Discursos/          # los textos del teleprompter
└── Respaldos/
```

Que sea una carpeta y no un registro suelto permite **llevarlo a otra máquina**
(o al equipo de respaldo) con todo adentro, y versionarlo.

### 1.2 Bloques y cues

El guion es una lista de **bloques** (apertura, himno, palabras del intendente,
video institucional, panel, cierre). Cada bloque tiene una lista de **cues**, que
son las cosas concretas que pasan:

| Cue | Qué hace |
|---|---|
| `escena` | pone una escena al aire (con su transición) |
| `musica` | arranca o para un tema, con fundido y volumen objetivo |
| `video` | reproduce un video (y qué hacer al terminar) |
| `teleprompter` | carga un texto y lo deja listo o lo arranca |
| `orador` | pone a alguien al aire, con su tiempo asignado |
| `pantalla` | manda contenido a una pantalla/Chromecast |
| `audio` | cambia el rack, silencia, hace ducking |
| `pausa` | espera N segundos |
| `espera` | **espera a que el operador confirme** (lo más importante: nada crítico se dispara solo) |
| `nota` | no hace nada: es una indicación para el operador o el orador |

Cada cue tiene: **duración prevista**, si es **automático o manual**, y de qué
cue depende. Los automáticos encadenan; los manuales esperan el clic.

### 1.3 Tiempos

- Hora prevista de inicio del evento y de cada bloque.
- Duración prevista por cue y por bloque; el total se calcula solo.
- Durante el evento: **desvío acumulado** («vamos 3:20 tarde»), bien grande y a
  la vista, y cuánto falta para el próximo cue automático.
- Al terminar: comparación previsto contra real, para el evento siguiente.

### 1.4 Ensayo (dry-run)

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

### 1.5 Mientras corre

- Una vista de **conducción**: el cue actual grande, el siguiente, el reloj del
  bloque y el desvío.
- Saltar, repetir, adelantar, **pausar todo el guion**.
- **Modo evento**: mientras corre, las acciones peligrosas (borrar escenas,
  actualizar, cambiar configuración que reinicia el gateway) piden confirmación
  extra o quedan bloqueadas.

---

## 2. Teleprompter

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
- **Chromecast y similares**: sirven para pantallas secundarias (un hall, otra
  sala, un bar). **Tienen entre 1 y 5 segundos de retardo** y no se pueden
  sincronizar con el audio de sala. Hay que decirlo en la interfaz para que
  nadie espere lo que no puede dar.

### 4.2 Qué hay que armar

- **Servidor de baja latencia en la LAN**: publicar lo que generamos (programa,
  o una escena concreta) para que otras pantallas lo tomen. Candidatos: WebRTC
  (lo más bajo, ya tenemos la pieza), SRT con `latency` chico, o NDI (que ya
  usamos para entradas).
- **Sección de pantallas**: lista de dispositivos encontrados, qué manda cada
  uno (programa, una escena, una placa fija, un video), y un cue de storyboard
  para cambiarlo.
- **Chromecast**: descubrimiento por mDNS y control por el protocolo de Google.
  No hay SDK abierto oficial para emitir: se hace con una app receptora propia
  apuntando a una URL nuestra (HLS o WebRTC). Es un trabajo en sí mismo.

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

1. **Storyboard mínimo**: modelo de datos, lista de bloques y cues, tiempos
   calculados, ejecución manual (el operador aprieta «siguiente»).
2. **Revisión previa y ensayo**, que es lo que evita los problemas del día.
3. **Teleprompter** (app web + control desde el celular + texto en el sistema).
4. **Vista del orador** en la app del invitado (texto, orden, cara del remoto).
5. **Cámaras de público** con su audio y el **retardo medido solo** (5.1): es
   de lo que más se nota y hoy se hace a ojo.
6. **Cliente**: chat abierto, sin preguntas si está el asistente,
   personalización.
7. **Herramientas del asistente** (descarga de música y video).
8. **Pantallas**: primero baja latencia en la LAN; Chromecast después.
9. **Asistente de armado** del storyboard.

Lo de sacar pestañas a ventanas se puede hacer en cualquier momento: es chico y
se disfruta enseguida.
