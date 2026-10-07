# Cablear GStreamer: de libobs a un motor propio

Cómo están armados hoy los pipelines de GStreamer del aparato, cómo conviene cablearlos para que anden
sincronizados y aguanten fallas, y cómo se reemplaza libobs del todo. Junta lo que se fue haciendo (las
migraciones de octubre de 2026), lo charlado con el usuario y lo que se aprendió de otros proyectos.

- El estado al día de cada pieza (qué ya pasó, qué falta) está en [MIGRACION-GSTREAMER.md](../desarrollo/MIGRACION-GSTREAMER.md).
- Está en dos partes:
  - **Parte I, todo en uno** (§1–§10): una sola máquina. Reglas de cableado, el pipeline de destino, la
    robustez, las mejoras a lo que ya anda, lo que pediría un operador de broadcast y su plan. **Va
    primero.**
  - **Parte II, sistema avanzado** (§11–§14): dos máquinas por un cable de 10 GbE, el Congreso a escala con
    videowall (simulcast bajo y alto), seguridad, archivo, monitoreo y su plan. Se arma después, encima del
    todo en uno.

**En una línea:** cada fuente es un pipeline propio que sella con la hora de captura y declara su latencia.
Todas comparten un reloj y se juntan en buses (compositor y audiomixer) que esperan a la más lenta. De los
buses salen ramas independientes a cada destino. Hoy las entradas ya son así; falta mudar la mezcla, la
composición y las salidas, que siguen en libobs. Para el operador, lo primero no es sacar libobs sino que
nada se caiga (§7) y lo de §9.1, todo en una sola máquina.

---

# Parte I — Todo en uno (una sola máquina)

El aparato completo en una máquina: entradas, procesamiento con GPU, mezcla, escenas y salidas. Primero se
termina esto; el sistema avanzado (Parte II) se arma encima, sin cambiar el diseño.

---

## 1. Dónde estamos

```
 ENTRADAS (GStreamer, una cadena por fuente)                  MOTOR (libobs, por ahora)           SALIDAS (libobs)
 ─────────────────────────────────────────                    ─────────────────────────           ───────────────
 cámara local ─┐                                              escenas, compositor, DSK,           RTMP (YouTube…), SRT,
 cámara de red ─┤                                             gráficos, transiciones,             grabación, replay,
 micrófono ────┤  ba_gst_fuente / ba_gst_medio ─► appsink ─►  mezclador, pistas, monitoreo  ──►   NDI, ventanas,
 archivo ──────┤  (racks barack, automixer, tarjetas,         multiview                           retorno a invitados
 invitado ─────┤   superres, filtros)                                                             (mix-minus por
 remoto ───────┘                                                                                   memoria compartida)
 voces, NDI ───┘
 audio del sistema ───────────────── fuentes de libobs ─────►
```

**Ya en GStreamer**:

| Pieza | Tipo y elemento |
|---|---|
| Cámaras locales | `ba_gst_fuente` + `camara` → avfvideosrc / mfvideosrc / v4l2src |
| Cámaras de red | `uridecodebin3` |
| Micrófonos | osxaudiosrc / wasapi2src / pulsesrc |
| Archivos y playlist | `ba_gst_medio` |
| Audio y video de los invitados y del remoto con la palabra | `baanillo`, `bavideoanillo` (plugin `baipc`) |
| Racks LSP/CLAP | `barack`, registrado por la app |
| Automixer | `baautomixer` (plugin `baaudio`) |
| Filtros de imagen | `bafiltros` |
| Superresolución | `basuperres` |
| Voces del sistema y del teleprompter | `bavoz`, registrado por la app (`audio/GstVoz.*`) |
| NDI | `bandisrc` |
| Vigía de dispositivos | `GstDeviceMonitor` (DEVICE_ADDED/REMOVED): un micrófono o una cámara que falta espera y se conecta al aparecer (§7) |

**Todavía en libobs**: audio del sistema, la escucha del lobby, **la mezcla de audio, la
composición de video y todas las salidas**.

Lo que entra por GStreamer llega a libobs por un `appsink`: cada cuadro y cada bloque de audio se le
entregan a `obs_source_output_video/audio` con la hora de ese momento. Ese salto es lo que más problemas de
sincronía da hoy (ver §4.6), y desaparece cuando la mezcla y la composición pasan a GStreamer.

---

## 2. Reglas de cableado

Valen para todo lo que se agregue de acá en adelante.

1. **Un pipeline por fuente, y buses que juntan.**
   - Cada entrada es su propio pipeline. Si una cámara se cae, se rearma sola y no arrastra al programa.
   - Hoy cada `ba_gst_fuente` ya es así. En el motor nuevo se conectan con `intersink`/`intersrc` (§5.1).
2. **Un solo reloj en todo el estudio.**
   - Todas las cadenas usan el mismo `GstClock`: el del sistema, o NTP/PTP/red (`reloj` en `ba_gst_fuente`;
     `ba::gst::publicarReloj` lo ofrece a otros aparatos).
   - Hoy, en modo estándar, las cadenas comparten además el base_time del estudio; en modo colchón cada
     cadena tiene el suyo y se compara en tiempo absoluto del reloj (base_time + hora de ejecución).
   - En el motor nuevo, un solo base_time para todo: así «la hora de captura» significa lo mismo en todas.
3. **La hora de cada cosa es la de cuando se capturó, no la de cuando la leímos.**
   - Una fuente en vivo sella con el reloj al capturar.
   - Lo que viene de otro proceso trae su sello (`qpc` del gateway) y se pasa al reloj del pipeline leyendo
     los dos relojes juntos (`src/gst-plugins/ipc/hora.hpp`).
   - Lo que viene por RTP trae la hora del que manda por RTCP (`ntp-sync`).
4. **La latencia se declara, no se adivina.**
   - Cada elemento que demora responde la consulta LATENCY (`baanillo` 10 ms, `bavideoanillo` 1 cuadro,
     los filtros lo que tardan).
   - El pipeline usa la mayor del estudio (`gst_pipeline_set_latency`).
   - Cuando algo cambia, se escucha el mensaje LATENCY y se llama a `gst_bin_recalculate_latency`.
5. **Eventos de GStreamer, no parches.**
   - Saltos con flush y segmentos para los medios.
   - QOS para tirar lo que llega tarde.
   - `ba/control` (evento propio, hacia abajo, fuera de banda) para avisarle cosas a las tarjetas. Hoy
     lleva el modo de la superresolución; la idea es sumar al aire, preview y tally (§4.4).
6. **Formatos fijos en los bordes.**
   - Audio: F32LE intercalado, 48 kHz, estéreo, antes de cualquier bus.
   - Video: lo que diga Ajustes → Video (de fábrica NV12, BT.709, rango limitado) al salir de la entrada
     (`normalizar`).
   - Adentro de cada cadena, que la negociación de caps elija.
7. **La GPU no se suelta.**
   - Si el primer filtro trabaja en la placa (CoreML, CUDA, D3D12, GL), se engancha pegado al decodificador.
   - Se baja a la CPU una sola vez, al final (`cadenaDe` en `ba_gst_fuente.cpp`).
   - En el motor nuevo, el compositor también va en la placa.
8. **Una entrada solo de audio lleva `"pipeline": ""`.**
   - Si no, `ba_gst_fuente` arma su video de prueba de fábrica.
   - Ese video gasta CPU y mete al audio en la compensación del estudio (~¾ s tarde).
9. **Nada de `ba_dec.` sueltos en archivos.**
   - Los pads de `uridecodebin3` se nombran (`video_0`, `audio_0`) y se arman solo las ramas que el archivo
     tiene (GstDiscoverer).
   - Una rama sin datos no deja llegar el EOS.

---

## 3. Recetas por tipo de entrada (cómo quedó cada cadena)

Lo que arma `armar()` en `src/obs-module/ba_gst_fuente.cpp`, simplificado.

**Cámara del equipo** (`camara` = id estable; al armar se busca su índice/ruta en el monitor de dispositivos):
```
avfvideosrc device-index=N ! [video/x-raw,width=…,height=…,framerate=…]
  ! queue name=ba_origen ! [tarjetas del grafo: bacolor, bacroma, baplugin…]
  ! deinterlace mode=auto ! videorate ! videoconvertscale ! video/x-raw,format=NV12,… ! appsink name=ba_video
```

**Cámara de red** (con superresolución y su audio):
```
uridecodebin3 name=ba_dec uri=rtsp://…
  ba_dec. ! queue name=ba_origen ! [placa primero: bavtsuperres/baonnxsr…] ! tarjetas ! normalizar ! appsink name=ba_video
  ba_dec. ! queue ! audioconvert ! audioresample ! F32/48k/2 ! barack fuente=… ! [filtros_audio] ! appsink name=ba_audio
```

**Micrófono** (sin video: `pipeline` vacío):
```
osxaudiosrc unique-id=… ! queue ! audioconvert ! audioresample ! F32/48k/2
  ! barack fuente="Mic Banca 3" ! audioconvert ! baautomixer grupo=bancas ! … ! appsink name=ba_audio
```

**Archivo** (`ba_gst_medio`, solo las ramas que tiene, con preroll):
```
uridecodebin3 name=ba_dec uri=file://…
  ba_dec.video_0 ! queue name=ba_origen ! normalizar ! appsink name=ba_video async=true
  ba_dec.audio_0 ! queue ! … ! barack ! appsink name=ba_audio async=true
```
- `looping`: saltos por segmentos (`SEGMENT` + `SEGMENT_DONE`), sin cortes.
- Ir a un tiempo: salto con flush (`ACCURATE`), en cualquier estado gracias al preroll.

**Invitado / remoto con la palabra** (fuera de la compensación del estudio, `estudio=false`):
```
bavideoanillo canal=BA.Guest.N ! queue name=ba_origen ! … ! appsink name=ba_video      (BGRA, sin normalizar)
baanillo canal=BA.Audio.Guest.N ! queue ! … ! barack ! appsink name=ba_audio
```
Los dos sellan con el `qpc` del gateway: el audio y el video del invitado van en cadenas distintas pero
quedan alineados por esa hora.

### Lista para agregar una entrada nueva

1. ¿Es en vivo? Que selle con la hora de captura y responda LATENCY con lo que tarda.
2. ¿Viene de otro proceso? Que traiga su sello (como el `qpc` del gateway) y se traduzca con `hora.hpp`.
3. ¿Solo audio? `"pipeline": ""`. ¿Solo video? `"audio": false`.
4. ¿Se alinea con las cámaras del estudio? `estudio: true` (de fábrica). ¿Su par va por otro lado (como el
   video de un invitado, que todavía va por libobs)? `estudio: false`.
5. Formato fijo al salir: F32/48 kHz/estéreo el audio; el del lienzo el video (`normalizar`), salvo que se
   necesite alfa (`alfa`) o sea un invitado (BGRA tal cual).
6. Que se rearme sola si se cae (hoy: el reintento de `ba_gst_fuente`; en el motor nuevo: `fallbackswitch`).
7. GUI y MCP: que aparezca en Entradas, en el grafo de video si tiene video y en Audio → Estudio si tiene
   audio, con su tipo lógico (`inputs::tipoLogico`) si reemplaza a una fuente de libobs.

---

## 4. Tiempo, latencia y eventos

### 4.1 Dos modos de sincronía en `ba_gst_fuente`

| Modo | Cómo espera | Quién lo usa |
|---|---|---|
| `gstreamer` (estándar) | Los sinks esperan al reloj (`sync=true`) con base_time y latencia | Medios (siempre: con preroll, `async=true`, y sin latencia forzada); entradas que lo pidan (con la latencia del estudio) |
| `colchon` (de fábrica en vivo) | Los sinks no esperan; un colchón propio presenta cada cuadro a su hora (captura + latencia + desfase + compensación) | Cámaras, micrófonos, red, invitados |

El objetivo es que todo vaya al modo estándar cuando la mezcla esté en GStreamer: el colchón existe porque
libobs presenta cada cuadro apenas lo recibe.

### 4.2 La compensación del estudio (T21)

- Cada entrada mide cuánto tardan sus filtros (lo viejo que llega cada cuadro) y declara lo que necesita.
- El estudio usa la mayor de todas, así lo capturado a la vez sale a la vez.
- Un micrófono también entra (cuenta bloques de audio en vez de cuadros), para quedar con su cámara.
- **`estudio: false`** deja una entrada con su propia demora:
  - lo usan los invitados, cuyo video y audio van por la hora del gateway;
  - lo usan los medios (un archivo no tiene que alinearse con las cámaras).
- Costo: con superresolución en una cámara de red, la compensación puede llegar a ~0,9 s **para todo el
  estudio**. Si molesta, la causa está en los filtros de esa cámara, no en las demás entradas.

### 4.3 Qué mensajes del bus se escuchan

`tic()` lee el bus **entero** en cada cuadro de libobs (antes leía uno solo y tiraba los demás):

| Mensaje | Qué se hace |
|---|---|
| `ERROR` | Se anota, se desarma y se reintenta a los 2 s (un archivo queda en estado de error) |
| `EOS` | Medios: termina (negro si `clear_on_media_end`) o, si quedó sin segmento, rearma el bucle. En vivo: se reintenta |
| `SEGMENT_DONE` | Medios con `looping`: otra vuelta sin flush |
| `LATENCY` | `gst_bin_recalculate_latency` (un elemento cambió lo que tarda) |
| `CLOCK_LOST` | Pausa y play (el pipeline elige otro reloj) |
| `WARNING` | Al registro, con el nombre del elemento que avisa |

### 4.4 Eventos que se mandan

| Evento | Para qué |
|---|---|
| Salto con `FLUSH` (`ACCURATE`) | Ir a un tiempo en un archivo |
| Salto con `SEGMENT` | Bucle sin cortes |
| `ba/control` (propio, hacia abajo, fuera de banda) | Hoy: el modo de la superresolución (reposo, barato, activo) y la duración de la transición, desde el programador. A sumar: al aire, preview y tally, para que una tarjeta cara trabaje solo cuando se ve. Cada elemento que lo entiende lo usa y todos lo dejan pasar. La entrada lo recuerda y lo reenvía si se rearma |
| QOS (lo generan los sinks) | Los filtros pesados tiran cuadros atrasados en vez de acumular demora |

### 4.5 Lo que viene de otro proceso

- El gateway sella cada cuadro y cada bloque con su reloj monotónico (`HealthBlock::nowQpc`).
- `hora.hpp` lee el reloj del pipeline y el del gateway **en el mismo instante** y traduce: no hace falta que
  tengan la misma base.
- Un sello sin sentido (0, del futuro, de hace más de 5 s) se reemplaza por la hora de lectura.
- `baanillo` además cuenta las muestras mientras siga al día, para no copiar el temblor de la escritura.

### 4.6 El salto a libobs (lo que se va con la migración)

- `presentarVideo`/`presentarAudio` le entregan a libobs con la hora de entrega (`os_gettime_ns`).
- libobs vuelve a suavizar el audio (umbral de 70 ms) y presenta el video al próximo cuadro del lienzo.
- Resultado: sobre lo que ordenó GStreamer, se suma hasta un cuadro de temblor y algún reacomodo de audio.
- No se arregla del todo mientras libobs mezcle: se arregla sacándolo.

---

## 5. El motor de destino (sin libobs)

Es el diseño: nada de esta sección está escrito todavía. Los elementos que nombra se revisaron en el bundle
de Mac (§10.1).

```
 RELOJ DEL ESTUDIO (sistema / NTP / PTP / red) + base_time común + latencia del estudio
 ════════════════════════════════════════════════════════════════════════════════════════

 ENTRADAS: un pipeline por fuente ─► [fallbackswitch/livesync] ─► intersink name=<fuente>
 ──────────────────────────────────────────────────────────────────────────────────────
 cámaras, red, micrófonos, archivos, invitados (webrtcbin), remoto, gráficos (con alfa), NDI (ndisrc), voces (appsrc)

                          │ intersrc (mismo proceso, mismo reloj, 1 productor → N consumidores)
                          ▼
 VIDEO: un compositor por bus, en la placa (glvideomixer · d3d12compositor · cudacompositor; compositor en CPU de respaldo)
 ─────────────────────────────────────────────────────────────────────────────────────────────────────
  PROGRAMA ── capas = la escena al aire (pad: xpos, ypos, width, height, alpha, zorder, crop)
           ── + DSK y gráficos arriba ── tee ─┬─► programa
                                              └─► clean feed (la escena SIN DSK/gráficos)
  PREVIEW  ── la escena armada
  MULTIVIEW── mosaico PGM/PVW/entradas + tally y nombres
  TAKE     ── GstController (InterpolationControlSource) anima los pads de la escena vieja a la nueva:
              corte, mezcla, empuje, cortina (glshader), stinger (ba_gst_medio con alfa por encima)

 AUDIO: audiomixer por bus (F32/48k, mismo reloj)
 ───────────────────────────────────────────────
  PROGRAMA ── la Matriz (qué pads a qué bus, nivel, mute) ─► limitador máster (T12) ─► tee
  PISTAS 2…6 ── grabación multipista
  MIX-MINUS ── uno por invitado y por remoto: programa SIN su propio audio
  ESCUCHA  ── PFL, auriculares, escucha de los racks ─► osxaudiosink / wasapi2sink / pulsesink
  level + ebur128level en cada bus ─► medidores y loudness (T11)

 SALIDAS (tee ─► queue leaky=downstream ─► una rama por destino)
 ──────────────────────────────────────────────────────────────
  programa ─┬─► codificador (vtenc_h264 · nvh264enc · qsvh264enc · d3d12h264enc · x264enc) + AAC
            │     ─► [cccombiner + h264ccinserter: subtítulos, §6] ─► flvmux ─► rtmp2sink ─► YouTube / Twitch / Facebook
            ├─► mpegtsmux ─► srtsink (también el clean feed)
            ├─► splitmuxsink (video + pistas de audio, por tramos)
            ├─► anillo de repetición (N s) ─► clips / replay
            ├─► ndisink · cámara virtual
            ├─► sink de pantalla ─► PANTALLA: preview, programa y multiview dentro de la interfaz (§5.4)
            └─► glimagesink ─► ventanas de salida en otros monitores

 WEBRTC (en el mismo proceso; adiós memoria compartida)
 ──────────────────────────────────────────────────────
  invitados de la sala ◄─► webrtcbin por invitado
        entra: decodebin ─► intersink «Invitado N»
        sale:  programa (video chico) + su mix-minus (Opus)
  asamblea (sesión remota) ◄─► webrtcbin por integrante (o webrtcsink/WHEP con SFU si son muchos)
        el que tiene la palabra ─► intersink «Remoto» ─► programa
        todos reciben: programa + audio de la sala (cada uno su mix-minus)
  audio del recinto: los micrófonos entran por la placa o la consola como micrófonos comunes
        ─► barack ─► baautomixer «bancas» ─► audiomixer PROGRAMA.
        El DCN de Bosch no lleva audio: abre y cierra micrófonos, dice quién tiene la palabra y elige la cámara.
```

### 5.1 Cómo se conectan las piezas

- **`intersink` / `intersrc`** (gst-plugins-rs, plugin `rsinter`):
  - pasa datos entre pipelines del mismo proceso, de 1 productor a N consumidores (programa, preview,
    multiview y el monitor de la entrada);
  - el productor y el consumidor **no pueden estar en el mismo pipeline**;
  - hay variantes que comparten hilos (`ts-intersink`/`ts-intersrc`) para cuando hay muchas entradas.
- **Escena = un conjunto de propiedades de pads.** Lo que hoy guarda `app.json` por escena (rectángulo,
  recorte, visibilidad, orden) se vuelve propiedades de los pads del compositor.
  - Armar en preview: poner esos valores en el compositor PREVIEW.
  - Take: animarlos en el PROGRAMA.
- **Pads dinámicos**: agregar una fuente a una escena es pedir un pad nuevo al compositor andando.
  - Siempre con un `queue` delante.
  - Siempre con la latencia mínima ya declarada (§5.2), para que no llegue tarde.

### 5.2 Tres cosas que hay que hacer bien desde el primer día

Salen de proyectos que ya armaron esto (ver §15).

1. **`min-upstream-latency` en los mezcladores.**
   - `compositor` y `audiomixer` fijan su latencia al arrancar. Una entrada más lenta que se enchufa después
     (una cámara RTSP, un invitado) llega tarde para siempre.
   - Se arranca con un piso que cubra la más lenta posible (la latencia del estudio + margen).
2. **Que la primera entrada que llega tarde no rebobine la mezcla.**
   - El caso: un mezclador con `force-live` y `start-time-selection=zero` sale con silencio o negro mientras
     sus entradas están vacías. Cuando llega el primer dato de verdad, elige la hora de inicio y vuelve la
     salida a 0.
   - El codificador recibe un PTS hacia atrás y la salida se cae: en Strom se cortaban los espectadores
     WebRTC; acá se cortaría YouTube.
   - Arreglo: al armar el mezclador, fijar su segmento inicial (`gst_aggregator_update_segment`) para que no
     espere al primer dato.
3. **Que una entrada caída no deje negro ni silencio al aire.**
   - Cada entrada termina en `fallbackswitch` (con una placa «sin señal» y silencio de respaldo) y, si
     tartamudea, `livesync`.
   - El cambio es sin corte y vuelve solo cuando la entrada regresa. Reemplaza buena parte del reintento
     escrito a mano en `ba_gst_fuente`. Más en §7.

### 5.3 Transiciones, DSK y gráficos

- **Corte**: cambiar los pads de un saque, en el borde de un cuadro.
- **Mezcla y empuje**: `GstController` con `InterpolationControlSource` sobre `alpha`/`xpos` de los pads,
  sincronizado con la hora del pipeline (frame-exacto).
- **Cortinas, iris, efectos**: shaders GLSL (`glshader` o un elemento propio con un catálogo de shaders).
  Strom tiene uno armado así; conviene copiar el diseño.
- **Stinger**: un `ba_gst_medio` con alfa en la capa de arriba; el corte se hace en el cuadro marcado del
  stinger.
- **DSK**: capas fijas arriba del PROGRAMA (hasta 4), fuera del preview.
- **Gráficos y rótulos**: los mismos renders de hoy (`graficos::reproducir`), entregados por `appsrc` con alfa
  a su `intersink`.

### 5.4 La interfaz

- Lo ideal es `qml6glsink`: muestra cualquier rama dentro de Qt Quick sin bajar a la CPU (preview, programa,
  multiview, el monitor de cada entrada) y reemplaza los `VideoItem` que hoy dibujan desde libobs.
- **No está en el bundle** (ni `qmlglsink`): el plugin `qt6` de gst-plugins-good hay que compilarlo contra
  nuestro Qt.
- Mientras tanto, un `appsink` por rama que le entrega los cuadros a nuestro `VideoItem` (como hoy, pero
  desde GStreamer). En Mac, si los cuadros vienen como IOSurface, sin copiar.

---

## 6. Subtítulos en vivo (YouTube y varios idiomas)

```
 Mic presidencia ─► barack ─► «Voz a texto» ─► flujo «Sala·ES» ─┐   (corrector T44 opcional antes de salir)
                                  ├─► Traductor ─► «Sala·EN» ─┐ │
                                  └─► Traductor ─► «Sala·PT» ┐│ │
                                                             ││ │
  appsrc (texto + hora de captura) ─► tttocea608 (roll-up, CC1) ◄────┘   ES
  appsrc ──────────────────────────► tttocea608 (CC3) ◄────────────┘     EN
  appsrc ──────────────────────────► tttocea708 (servicio 3) ◄──────┘    PT
              └─► ccconverter ─► cccombiner ◄── video del PROGRAMA
                                     │
                                     ▼
            codificador H.264 ─► h264ccinserter (SEI) ─► flvmux ─► rtmp2sink ─► YouTube
                                     ├─► mpegtsmux ─► srtsink (los mismos 608/708)
                                     ├─► HLS propio: webvttenc por idioma ─► hlssink3 (pistas elegibles)
                                     └─► quemados (un idioma, una salida): textoverlay / ba_subtitulos
```

**Cómo se inyectan**
- El texto sale de lo que ya existe:
  - la voz a texto (tarjetas del rack);
  - el traductor (una salida de texto por idioma);
  - el puesto de corrección (T44), si alguien revisa antes de que salga.
- `cccombiner` pega el texto al video como `GstVideoCaptionMeta`.
- `h264ccinserter` lo mete como SEI después de codificar, con cualquier codificador (algunos de placa no lo
  hacen solos).

**Varios idiomas en YouTube**
- Lo seguro:
  - los subtítulos dentro del video (608/708) son una forma de subir subtítulos en vivo que YouTube
    reconoce;
  - YouTube puede autotraducir desde la pista principal.
- **Hay que probarlo con una transmisión real**:
  - si YouTube muestra más de un canal 608 a la vez (CC1 y CC3), o servicios 708;
  - si su carga de subtítulos por HTTP acepta más de un idioma a la vez.
- Caminos que no dependen de eso:
  1. La pista principal (castellano) y la autotraducción de YouTube.
  2. Nuestro HLS (o la página de la sala) con una pista por idioma: control total.
  3. Una transmisión por idioma (`rtmp2sink` por idioma, cada una con su pista). Cuesta un codificador por
     idioma.

**Tiempos**
- La voz a texto tarda 1–3 s. Cada frase lleva la hora en que se dijo el audio, así `cccombiner` la pega al
  cuadro que corresponde y no al que estaba al aire cuando llegó.
- Para que esa rama no se apure, espera esa demora extra: un `queue` con la demora solo en la salida a
  YouTube. No frena la pantalla ni a los invitados.

**Detalles**
- 608 tiene los caracteres del castellano (á é í ó ú ñ ¿ ¡), pero son 32 columnas y Latin-1. 708 sirve para
  cualquier idioma.
- Elementos:
  - en gst-plugins-rs: `tttocea608`, `tttocea708`, `cea608tott`;
  - en -bad y en el núcleo: `ccconverter`, `cccombiner`, `h264ccinserter` (desde 1.24).
  - En el bundle de Mac están todos (§10.1). Falta confirmarlo en Windows y Linux.
- MCP y GUI: por salida, qué salidas de texto van y por qué canal (CC1, CC3, servicios 708), con un medidor
  que muestre qué sale y con cuánta demora.

---

## 7. Robustez: que una falla no tire el aire

Qué pasa si se desenchufa un dispositivo, se corta un cable USB un instante, se cae una cámara de red o se
corta la luz, y cómo hacer que levante rápido.

**La idea:** separar lo que se puede caer de lo que tiene que seguir andando.

```
                  SE PUEDE CAER                     NUNCA SE CAE (siempre andando)
 ┌──────────────────────────────┐        ┌──────────────────────────────────────────────────────────┐
 │ pipeline FUENTE (chico)      │        │ pipeline PROCESO de la entrada                           │
 │ avfvideosrc / osxaudiosrc /  │──────► │ intersrc ─► fallbackswitch ─► livesync ─► tarjetas,      │
 │ fallbacksrc (red) …          │  inter │   ▲  (respaldo: último cuadro / placa «sin señal» /     │──► buses / libobs
 │ ─► intersink                 │        │   │   silencio)      superres (modelo cargado), barack   │
 └──────────────────────────────┘        │   └─ si no llega nada en ~200 ms, pasa al respaldo       │
        ▲ se rearma solo este            └──────────────────────────────────────────────────────────┘
        │ (sin tocar lo demás)
 GstDeviceMonitor: DEVICE_REMOVED / DEVICE_ADDED ─► avisa al instante
```

### 7.1 Reglas

1. **Partir cada entrada en dos.**
   - Un pipeline de fuente, chico: solo el dispositivo y un `intersink`. Es lo único que se rearma.
   - Un pipeline de proceso que nunca para: tarjetas, superresolución, rack, automixer.
   - Al volver no se recargan los modelos de superresolución (CoreML/ONNX tardan segundos), ni los
     plugins LSP, ni el estado del automixer. El programa nunca ve un error ni un EOS de un dispositivo.
2. **Tapar el hueco sin corte.**
   - `fallbackswitch` después del `intersrc`: si no llega nada en ~200 ms, pasa al respaldo sin corte.
     - Video: el último cuadro unos segundos, después una placa «Cámara 2 sin señal».
     - Audio: silencio, con un fundido corto para que no haga clic.
   - `livesync` tapa los microcortes de un cable USB flojo (huecos de 100 ms) repitiendo o rellenando, sin
     que salte el switch.
   - Cuando el dispositivo vuelve, el switch regresa solo con el primer cuadro bueno.
   - Los buses (o libobs, hoy) siguen alimentados: la latencia y la compensación del estudio no saltan.
3. **Enterarse al instante.**
   - `GstDeviceMonitor` con DEVICE_REMOVED/DEVICE_ADDED: cuando la cámara con ese id estable reaparece, se
     rearma en ese momento.
   - Los ids estables ya existen: uniqueID de AVFoundation, la ruta en Windows, `/dev/v4l/by-id` en Linux,
     `unique-id` en los micrófonos. Se encuentran aunque el USB cambie la numeración.
   - Un vigía de datos en cada `intersink`: si no llegan buffers en N ms pero no hubo ERROR (un
     dispositivo colgado), se rearma igual.
   - Reintento rápido con espera creciente: 0,2 s, 0,5 s, 1 s, 2 s, después cada 2 s.
4. **El reloj nunca lo pone un dispositivo.**
   - Un `osxaudiosrc`/`wasapi2src` puede ser el reloj del pipeline (`provide-clock`). Si ese dispositivo se
     desenchufa, el pipeline pierde el reloj (`CLOCK_LOST`) y todo se traba.
   - Regla: `provide-clock=false` en todos los dispositivos y siempre el reloj del estudio. La deriva del
     dispositivo se corrige remuestreando (`slave-method`), no tomando su reloj.
   - **A revisar primero**: si los micrófonos ya lo tienen. Es barato.
5. **Cámaras de red e invitados.**
   - `fallbacksrc` en lugar de `uridecodebin3`: reinicia la conexión RTSP/SRT sin que el pipeline dé error
     (`timeout`, `restart-timeout`, `retry-timeout`).
   - RTSP por TCP con `tcp-timeout` corto (2 s): un cable cortado se nota rápido.
   - Invitados: si el gateway se reinicia, `baanillo`/`bavideoanillo` ya se resuscriben solos. Con
     `fallbackswitch` delante, en el lugar del invitado queda su último cuadro y después una placa con su
     nombre y foto, en vez de negro.
6. **Las salidas también.**
   - Si se cae el codificador de placa o la conexión a YouTube, se rearma **solo esa rama** (`tee` →
     `queue leaky` → codificador → `rtmp2sink`) con reconexión, sin tocar el programa ni la grabación.
7. **Corte de luz de la máquina entera.**
   - **Volver a prender sola:**
     - Mac: `sudo pmset autorestart 1`;
     - Windows y Linux: «restore on AC power loss» en el BIOS.
     - Después la app arranca con la sesión (opción ya hecha, ver la tabla), y el launcher relanza el
       gateway y la app si se caen.
   - **Volver al aire:** si la corrida anterior no se cerró bien, al arrancar se retoman la transmisión y
     la grabación (T47 decidido sin passphrase; ya hecho). Falta guardar y retomar también la escena al
     aire y la playlist con su posición.
   - **Perder casi nada:**
     - grabación con `splitmuxsink` en tramos (MP4 fragmentado o MKV): un corte pierde como mucho el último
       tramo;
     - configuración escrita de forma atómica (archivo temporal + rename): un corte a mitad de guardar no
       deja `app.json` roto.
   - **Hardware:** una UPS chica para la máquina y el switch de red evita casi todo esto en cortes cortos.

### 7.2 Qué hay y qué falta

| | Hoy | Falta |
|---|---|---|
| Id estable y volver a encontrar el dispositivo | Sí (cámaras, micrófonos) | — |
| Enterarse al instante | Sí: vigía con DEVICE_ADDED/REMOVED; lo que falta espera sin reintentar cada 2 s | Vigía de datos (dispositivo colgado sin error) |
| Seguir sin corte mientras vuelve | No: cuadro congelado o negro, silencio | `fallbackswitch` + `livesync` + placa |
| Rearmar solo la fuente | No: se rearma la cadena entera (recarga modelos) | Partir fuente y proceso con `intersink` |
| Reloj independiente de los dispositivos | Parcial | `provide-clock=false` en todos |
| Invitados si se cae el gateway | Se resuscriben solos | Placa con nombre y foto en vez de negro |
| Corte de luz | Arranque con el equipo (opcional), retoma transmisión y grabación, launcher relanza procesos | Escena y playlist al retomar; grabación en tramos; configuración atómica |

---

## 8. Mejoras a lo que ya tenemos

Lo que conviene cambiar en lo que ya anda, aprendido de otros proyectos (Strom, la documentación de
GStreamer). Ordenado por impacto y esfuerzo.

1. **`livesync` + `fallbackswitch` al final de cada entrada** (§7). Mucho impacto, poco código; los dos
   están en el bundle de Mac.
2. **Entregarle a libobs cada cuadro con su hora, no con la de entrega.**
   - Hoy `presentarVideo`/`presentarAudio` sellan con `os_gettime_ns()` al llamarlos: libobs recibe la hora
     con el temblor de nuestro hilo.
   - Pasar la hora de captura traducida a `os_gettime_ns` (reloj del pipeline → monotónico, el mismo
     truco de los dos relojes de `hora.hpp`).
   - libobs alinea audio y video por la hora real. Mejora la sincronía aunque libobs siga. Son pocas
     líneas.
3. **Pasar las entradas en vivo del modo «colchón» al estándar de GStreamer.**
   - Hoy hay código propio que presenta cada cuadro a su hora (`venceDe`, las colas, `tic()`).
   - `appsink sync=true` con reloj compartido y `gst_pipeline_set_latency` hace eso. Los medios ya andan
     así y salió bien.
   - Menos código, audio y video de una entrada esperados por el mismo sink, y se va el temblor de
     presentar desde `tic()` (16–33 ms).
   - De a una entrada por vez, midiendo con `video_sincronia` antes y después.
4. **Latencia declarada primero, medida después.**
   - La compensación estima la demora por «lo viejo que llega cada cuadro» (P95 + margen). Es frágil: un
     salto, una cámara con el reloj corrido o una pausa la suben, y con ella a todo el estudio.
   - La consulta LATENCY como fuente principal. La medición, solo para lo que no declara, con un tope por
     entrada e histéresis.
   - Un interruptor `estudio` en la GUI y el MCP, para dejar afuera una entrada con superresolución pesada.
5. **Un solo lector por canal del gateway, repartido con `intersink`.**
   - Hoy cada consumidor lee su copia de la memoria compartida. Se multiplica cuando el mismo invitado vaya
     a programa, preview, multiview y su monitor.
   - Un `bavideoanillo`/`baanillo` por canal → `intersink BA.Guest.N`; los demás, `intersrc`.
   - Es el primer paso del motor sin libobs y no cambia nada al aire.
6. **`ba/control` con al aire, preview y tally.** Las tarjetas caras (superresolución, rostros, plugins de
   Python) descansan cuando su entrada no se ve. Generaliza lo que el programador de superresolución hace a
   mano.
7. **Prolijidades que evitan bugs.**
   - Al arrancar, revisar con `gst_element_factory_find` que estén los elementos imprescindibles
     (`intersink`, `fallbackswitch`, `barack`, `baanillo`, `bavideoanillo`, `bavoz`, los de subtítulos) y
     avisar en Diagnóstico. Que un plugin que falta en una plataforma no aparezca como silencio o negro.
   - Un solo armador de ajustes de `ba_gst_fuente`. Hoy se arman en `InputSpecs`, `VideoGrafos` y
     `Playlist`: el bug del video de prueba (`"pipeline": ""`) pasó por eso.
   - Pruebas por tipo de entrada con `fakesink` y reloj falso (`GstTestClock`), como `test_baanillo`: los
     tiempos se prueban sin la app ni hardware.

---

## 9. Lo que pide un operador de broadcast

Todo lo charlado mirado por alguien que tiene que sacar una sesión al aire sin sorpresas.

### 9.1 Para arreglar primero (afecta el aire hoy)

1. **El casi segundo de latencia del estudio.**
   - Una cámara con superresolución atrasa *todo* ~0,9 s. El operador ve la sala atrasada en el monitor, y
     el presentador que habla con un invitado recibe la respuesta tarde.
   - Ponerle tope a la compensación: no más de 2–3 cuadros para lo que está en la sala. La cámara pesada
     queda afuera (`estudio: false`) con su propia demora y un aviso: «esta cámara llega tarde».
   - La demora extra, por salida y donde haga falta (YouTube, grabación), no en el monitoreo del operador.
   - Un medidor visible: «latencia del estudio: 120 ms», en verde, o en rojo con la entrada que la causa.
2. **Los auriculares del operador en sincronía con lo que ve.**
   - Hoy se oye el micrófono directo y se ve el video atrasado: desorienta.
   - Dos escuchas, una tecla para pasar de una a otra:
     - «directo»: la sala sin demora, para trabajar los micrófonos;
     - «programa»: exactamente lo que sale, audio y video juntos.
3. **El limitador del máster y el loudness (T12 + T11).**
   - Nada debería salir al aire sin limitador de picos reales (true peak) en el programa.
   - Medidor LUFS con el objetivo a la vista: −14 para YouTube, −23 para TV. Avisar cuando se aleja.
4. **Alarmas de silencio, negro y congelado** en el programa y en cada salida.
   - «10 s de silencio al aire», «programa en negro 3 s», «cuadro congelado», «YouTube dejó de recibir».
   - Visibles, audibles y también en el teléfono o la superficie de control.
5. **Robustez** (§7). Para un operador es lo más importante de toda la migración, más que sacar libobs.
6. **Lip-sync por entrada, medible y ajustable.**
   - Una prueba de «aplauso + flash» (o tono + flash) que mide el desfase audio/video de cada cámara y
     micrófono.
   - Ajuste en cuadros o ms, y que el resultado quede guardado. Hoy se ajusta a ojo.
7. **Modo «al aire» que congela lo riesgoso.**
   - Durante una transmisión, bloquear: editar la escena al aire, cambiar ajustes de una entrada (rearma y
     corta), sacar fuentes, cambiar el rack de un micrófono al aire. O pedir confirmación.
   - Las guardas de aire del MCP existen; falta lo mismo en la GUI.
   - `--test` nunca tendría que poder arrancar transmitiendo o grabando.
8. **Probar los otros dos sistemas y corridas largas.**
   - Casi todo se probó solo en Mac y solo minutos.
   - Antes de un Congreso real: una corrida de 8 horas con todas las entradas y transmisión a un canal de
     prueba, mirando temperatura, memoria, deriva A/V y cuadros perdidos.

### 9.2 Lo que me gustaría tener (estándar de la industria)

- **Multiview de verdad:**
  - borde de tally rojo/verde en cada recuadro;
  - medidores de audio por recuadro y alarmas en el recuadro (sin señal, silencio);
  - nombre, reloj y cuenta regresiva.
- **Tally real afuera:** luces en las cámaras, el presentador y los remotos (por Companion o GPIO), y «estás
  al aire» en el teléfono del invitado.
- **Superficie de control:**
  - T-bar y macros (secuencias: «abrir sesión» = escena + gráfico + micrófono + grabar);
  - snapshots del mezclador y de escenas;
  - mapa de Stream Deck coherente;
  - deshacer la última acción que fue al aire.
- **Cue de medios:**
  - armar el clip en PVW y verlo antes de tomarlo;
  - lo que le queda al programa (existe), en grande, con aviso a los 10 s;
  - volver solo a una escena al terminar un clip.
- **Consola de audio:**
  - PFL/AFL (solo) por canal;
  - medidores con balística elegible (VU/PPM, en parte existe);
  - medidor de fase en el máster;
  - talkback al presentador y al piso (el IFB existe para invitados);
  - grupos de mute.
- **Redundancia:**
  - transmisión de respaldo a la URL de respaldo de YouTube;
  - grabación en dos discos;
  - grabación ISO por cámara (cada entrada por separado) para posproducción y archivo.
- **Monitoreo de confianza:** ver lo que realmente emite YouTube (con su demora) desde la misma consola.
- **Hora y reloj:**
  - hora del día (NTP) en las grabaciones y timecode en los archivos;
  - marcas en la grabación («empieza el tema X»), desde la consola o el guion.
- **Escaleta:** el guion del evento existe; hacerlo el eje (próximo punto, tiempo que queda, quién habla,
  qué gráfico va).

### 9.3 Para un Congreso en particular

- **Automatismo atado al DCN:**
  - el que pide la palabra trae su cámara (preset), su zócalo y su micrófono abierto;
  - cronómetro del orador en pantalla y para la presidencia.
  - Mucho existe (seguir al micrófono, zócalos de bancas). Falta que el pase sea sin parpadeos y con
    override del operador.
- **Intérprete de señas:** un PiP fijo que no se va al cambiar de escena, incluido o no en el clean feed
  según se configure.
- **Subtítulos en vivo** (§6), con corrector antes del aire y archivo por idioma.
- **Votaciones:** gráfico de resultados desde la fuente de datos, sin tipear.
- **Acta oficial:**
  - grabación con hash por tramo (integridad);
  - registro de quién hizo qué al aire;
  - metadatos (sesión, fecha, temas) para el archivo.
- **Remotos:**
  - calidad de conexión a la vista antes de darles la palabra;
  - prueba automática previa (eco, red: existe, T33);
  - si se caen, placa digna con nombre y foto, no negro.

---

## 10. Plan del todo en uno

Todo en una sola máquina. Cada etapa deja el aire andando y se puede volver atrás. Las entradas (etapa 0)
ya están hechas.

**Antes de la primera sesión real** (con libobs todavía, porque no dependen del motor nuevo):
- de §9.1, los puntos 1–5 y 8: tope a la latencia del estudio, las dos escuchas, limitador y loudness,
  alarmas, robustez (§7) y la corrida de 8 horas;
- de §8, los puntos 1 y 2 (`fallbackswitch`/`livesync` y la hora a libobs), y `provide-clock=false`.

Después: lip-sync, modo «al aire», multiview estándar, tally real y transmisión de respaldo. Y las etapas
del motor:

| # | Etapa | Qué se hace | Cómo se sabe que anda |
|---|---|---|---|
| 0 | Entradas | Todo lo de §3 (hecho, ver MIGRACION-GSTREAMER.md) | Las pruebas de cada migración |
| 1 | Mezcla de audio | `audiomixer` para PROGRAMA, pistas, mix-minus y escucha, con `min-upstream-latency` y el arreglo de §5.2. libobs recibe el programa ya mezclado como una sola fuente | Mismo nivel y loudness que hoy; un invitado no se oye en su retorno; ningún bus se cae al entrar el primer invitado |
| 2 | Salidas | Codificadores, RTMP/SRT, grabación, replay y NDI desde GStreamer, alimentados por el programa que todavía compone libobs (`appsrc` del lienzo) | Transmisión de una hora sin cortes ni deriva A/V; grabación multipista igual a la de hoy |
| 3 | Composición | PROGRAMA, PREVIEW, MULTIVIEW (con tally y medidores, §9.2), take, transiciones, DSK y gráficos en `compositor`/`glvideomixer`; la interfaz por `appsink` (o `qml6glsink`, §5.4); `intersink`/`intersrc` entre entradas y buses (fuente y proceso separados, §7.1) | Take frame-exacto; cambiar de escena no corta ni el audio ni la transmisión; una entrada caída muestra la placa y vuelve sola |
| 4 | WebRTC en el proceso | `webrtcbin` en lugar de la memoria compartida para invitados y sesión remota, con su mix-minus (el Congreso a escala, con simulcast y SFU, es del sistema avanzado, §12) | Latencia de ida y vuelta igual o menor que hoy; la hora de captura por RTCP |
| 5 | Subtítulos | §6 | YouTube muestra los subtítulos a tiempo |
| 6 | Sacar libobs | El audio del sistema a GStreamer (las voces y NDI ya pasaron); se saca `libobs.framework` (y sus plugins) del bundle | La app arranca sin libobs; el bundle pesa menos |

**Riesgos**
- Las transiciones y los gráficos: hoy los resuelve OBS.
- El temblor de los compositores de placa en cada sistema (D3D12 en Windows, GL/CUDA en Linux): hay que
  medirlo con placas reales.
- Los elementos de gst-plugins-rs tienen que estar en el bundle de Windows y Linux también (en Mac están,
  ver §10.1).

### 10.1 Qué hay en el bundle (Mac, GStreamer 1.28.7)

Revisado con `gst-inspect-1.0` del bundle el 2026-10-05. Windows y Linux, sin revisar.

| Para | Está | No está (y qué se usa) |
|---|---|---|
| Conectar pipelines | `intersink`, `intersrc` | `interpipesink` (no hace falta) |
| Robustez | `fallbackswitch`, `fallbacksrc`, `livesync` | |
| Video | `compositor`, `glvideomixer`, `glshader`, `glimagesink`, `glsinkbin` | `vtcompositor` (no existe: en Mac va `glvideomixer`) |
| Interfaz | `gtk4paintablesink` (no sirve para Qt) | `qml6glsink`, `qmlglsink` (§5.4) |
| Audio | `audiomixer`, `level`, `ebur128level`, `audioloudnorm` | `ebur128` (se usa `ebur128level`) |
| Salidas | `rtmp2sink`, `flvmux`, `mpegtsmux`, `srtsink`, `splitmuxsink`, `hlssink3`, `ndisink`, `encodebin2`, `vtenc_h264`, `avenc_aac` | `fdkaacenc` (se usa `avenc_aac`) |
| WebRTC | `webrtcbin`, `webrtcsink`, `whipclientsink`, `whepclientsrc`, `whipserversrc` | `whepsink` (se usa `webrtcsink` o `whipclientsink`) |
| Subtítulos | `tttocea608`, `tttocea708`, `ccconverter`, `cccombiner`, `h264ccinserter`, `webvttenc` | |

Para repetir la revisión:
```sh
F=dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/GStreamer.framework/Versions/1.0
for e in intersink fallbackswitch glvideomixer qml6glsink ebur128level h264ccinserter; do
  "$F/bin/gst-inspect-1.0" $e >/dev/null 2>&1 && echo "$e sí" || echo "$e NO"; done
```

---

# Parte II — Sistema avanzado (servidor)

Cuando una sola máquina no alcanza o la instalación lo pide: varias máquinas por un cable dedicado,
cientos de remotos con videowall, archivo oficial, roles y monitoreo. Se arma sobre el todo en uno.

---

## 11. Dos máquinas: entradas con GPU por un lado, mezcla y salida por el otro

Separar físicamente lo que procesa (entradas, análisis, mejora de audio y video, que necesita GPU) de lo que
mezcla, arma escenas y sube. Las une un cable de red dedicado de 10 GbE.

```
 MÁQUINA A — «ENTRADAS» (con GPU)                                   MÁQUINA B — «MEZCLA Y SALIDA»
 ════════════════════════════════                                  ══════════════════════════════
 cámaras, red, micrófonos de la sala (y del DCN)                   receptor de red por fuente
   ─► ba_gst_fuente (superres, tarjetas, rostros,                     ─► compositor (escenas, take, DSK, gráficos)
      barack, automixer, voz a texto, traductor)                      ─► audiomixer (programa, pistas, mix-minus)
   ─► EMISOR por fuente ──────── cable 10 GbE dedicado ─────────►     ─► salidas: YouTube, SRT, grabación, pantallas
                                 (punto a punto, MTU 9000)            ─► gateway WebRTC, GUI del operador
   ◄──────────────── control (WebSocket con auth) y lo que A necesite de B ──┘
 RELOJ: A publica su reloj (PTP, o el «red» que ya tenemos) ─► B lo sigue: las horas de captura de A valen en B
```

### 11.1 Por dónde va cada cosa en el cable

| Forma | Elementos | Por cada 1080p60 | Latencia | En el bundle de Mac |
|---|---|---|---|---|
| **Video sin comprimir por RTP** (RFC 4175, la base de SMPTE ST 2110-20) | `rtpvrawpay` / `rtpvrawdepay` en `rtpbin` | ~2 Gbps (4:2:2, 8 bits); ~2,6 Gbps a 10 bits | Menos de 1 cuadro + 2–5 ms de jitter buffer | Sí |
| **Audio sin comprimir por RTP** (L24, la base de AES67 / ST 2110-30) | `rtpL24pay` / `rtpL24depay` | ~2,3 Mbps por par estéreo | 1–4 ms | Sí |
| **NDI** (compresión liviana, estándar de la industria) | `ndisink` / `bandisrc` | ~150–250 Mbps | ~1 cuadro | Sí |
| **SRT / RIST** (comprimido) | `srtsink`/`srtsrc`, `ristsink`/`ristsrc` | 10–50 Mbps | 50–200 ms + codificar | Sí (para sitios lejanos, no para el cable entre racks) |
| **JPEG XS** (mezzanine, ~10:1 casi sin demora) | `svtjpegxsenc`/`dec` + su RTP | ~200–400 Mbps | Menos de 1 ms | **No**: hay que compilar SVT-JPEG-XS |

**Qué entra en 10 GbE** (números aproximados):
- Sin comprimir, unas 3–4 fuentes 1080p60, o 7–8 a 1080p30. 4K60 sin comprimir (~8–10 Gbps) no deja lugar
  para nada más: pide 25 GbE, NDI o JPEG XS.
- Por NDI, decenas de fuentes 1080p.
- Recomendación: RTP sin comprimir para las cámaras del programa que importan, NDI para lo demás, L24 para
  todo el audio.

### 11.2 La sincronía entre las dos máquinas

- Un solo reloj para las dos: A publica el suyo y B lo sigue.
  - El reloj «red» (`GstNetTimeProvider` / `GstNetClientClock`) ya lo soporta `ba_gst_fuente`.
  - O PTP (`GstPtpClock`; necesita `gst-ptp-helper` con permisos).
- La hora de captura cruza la red: RTP con la referencia del reloj (RFC 7273: `a=ts-refclk` en el SDP y
  `rtpjitterbuffer rfc7273-sync=true`). La mezcla en B alinea por cuándo se capturó en A, no por cuándo
  llegó.
- El mezclador de B declara la latencia de la red (`min-upstream-latency`) como la de cualquier entrada.

### 11.3 Cómo encaja con el diseño

- Es la misma separación «un pipeline por fuente + buses» de §5: el todo en uno ya queda armado así, y
  partirlo en dos máquinas no cambia el diseño. En una máquina, la unión es
  `intersink`/`intersrc`; en dos, ese mismo punto pasa a ser un emisor/receptor RTP. El grafo es el mismo:
  el mismo programa corre en una o en dos máquinas según la instalación.
- **Control:** la GUI de B le habla a A por la red. El MCP hoy es local: hace falta un canal por red con
  autenticación (un WebSocket dedicado por el mismo cable). Los eventos `ba/control` (al aire, preview,
  tally) cruzan por ese canal o como extensiones de cabecera RTP.
- **Cable:** punto a punto o switch con PTP; jumbo frames (MTU 9000); buffers UDP grandes (`udpsrc
  buffer-size` y los límites del sistema: `net.core.rmem_max`, `kern.ipc.maxsockbuf`); Energy Efficient
  Ethernet apagado. En Mac, 10 GbE por adaptadores Thunderbolt.
- **Prueba:** 4 cámaras sin comprimir + NDI + audio durante una hora, mirando pérdida de paquetes, temblor y
  deriva entre las dos máquinas.

### 11.4 Dónde van WebRTC (servidor y clientes)

En **B**, no en A. Solo el video de un invitado que necesite GPU hace ida y vuelta a A.

```
                        INTERNET (invitados, integrantes, presidencia, botonera, Companion)
                            ▼
 MÁQUINA B ═════════════════════════════════════════════════════════════════════════════════════
  ┌─ GATEWAY: señalización, páginas (/, /sesion/, /presidencia/…), auth, TURN, proxy SNI/ACME
  ├─ webrtcbin por invitado / por remoto ── audio ─► barack (AEC, rack) ─► audiomixer PROGRAMA
  │        ▲                              └ video ─► decodificador de placa ─► compositor
  │        └── retorno: programa (video chico) + SU mix-minus ◄── generados acá mismo
  │
  │      solo si un invitado necesita GPU (superres, rostros, igualar):
  └──────── video crudo ─► cable ─► MÁQUINA A (procesa) ─► vuelve por el cable ─► compositor
 MÁQUINA A: sin internet, solo el cable dedicado.
```

Por qué en B:
1. **La conversación no sale de una máquina.** El invitado habla, B mezcla y le devuelve su mix-minus. Con el
   gateway en A, cada palabra cruzaría el cable dos veces y se arriesga a que el retorno pase por la
   compensación del estudio: arruina una charla.
2. **El retorno se genera en B** (programa y mix-minus): se codifica para WebRTC ahí mismo.
3. **Seguridad:** B ya mira a internet (sube a YouTube). A queda aislada; la máquina con la GPU cara y el
   procesamiento de la sala no queda expuesta.
4. **El gateway comparte memoria con la mezcla** (`BA.Guest.N`, `BA.Audio.Return.N`): eso no cruza dos
   máquinas. Al lado de la mezcla (o con `webrtcbin` en el proceso, etapa 4 del plan) sigue siendo local.
5. **Un invitado pide poca GPU:** decodificar alcanza con el decodificador de placa de B; el audio (AEC,
   rack, automixer) es CPU. Si se quiere superresolución o rostros en un invitado, solo su video va a A y
   vuelve, fuera del camino del audio: el video se alinea con el audio por la hora de captura.

| Pieza | Dónde |
|---|---|
| Señalización, páginas, auth, TURN, proxy | B (o una tercera máquina en la DMZ, cuando son muchos) |
| `webrtcbin` de invitados y del que tiene la palabra | B |
| Mix-minus y retorno de programa a invitados | B |
| Procesamiento con GPU del video de un invitado (opcional) | A, ida y vuelta por el cable |
| Micrófonos de la sala y del DCN | A, a B como L24 |
| Remotos que solo miran y escuchan | SFU (§12) |

---

## 12. Congreso a escala y videowall con todos los rostros (simulcast)

Con cientos de integrantes conectados (una cámara grande tiene más de 250 bancas), la mezcla no puede
decodificar a todos en alta calidad. La solución es **simulcast con dos calidades**: cada remoto manda a la
vez un video **bajo** y uno **alto**.

- El **bajo** es para armar videowalls al instante: todos los rostros, siempre disponibles, baratos de
  decodificar.
- El **alto** es solo para quien va al aire: el que tiene la palabra (y el siguiente en la lista, para que el
  pase sea inmediato).

```
 INTEGRANTES (navegador / teléfono) — simulcast: BAJO (~320×180, 15 cps, ~150 kbps) + ALTO (720p, ~1,5 Mbps)
                                      + audio Opus con su nivel en la cabecera RTP (RFC 6464)
        │
        ▼
 SFU (gateway a escala; máquina C o DMZ) ── no decodifica: reenvía la calidad que cada destino necesita
        │
        ├── BAJO de TODOS ──────────────► MÁQUINA DEL MURO
        │                                   N × decodificador liviano ─► un compositor por pantalla
        │                                   grilla con nombre, foto si apagó la cámara, borde de quien
        │                                   habla (por el nivel RFC 6464, sin decodificar audio), quórum
        │                                   ─► glimagesink por pantalla (o NDI/RTP a los controladores)
        │                                   ─► opcional: el muro como una entrada más del programa
        │
        ├── ALTO + audio del que tiene la palabra (y del siguiente) ─► MÁQUINA B (programa)
        │
        └── a todos: programa (una codificación) + audio de la sala, repartido por el SFU
```

### 12.1 Por qué escala

- **Dos calidades, nada más:** el navegador manda las dos a la vez (sube ~10–20 % más que con una sola). El
  SFU elige cuál va a cada lado sin transcodificar. Pasar a alguien del muro al aire es pedir la otra
  calidad, sin renegociar.
- **Decodificar el bajo es barato:** 257 × 320×180 × 15 cps ≈ 220 Mpix/s, menos que dos 1080p60. Una CPU
  decente lo decodifica (VP8/H.264 en baja resolución); en Mac, VideoToolbox maneja muchas sesiones.
- **Quién habla, sin decodificar audio:** el nivel viaja en la cabecera RTP de cada remoto
  (`ssrc-audio-level`). El muro marca al que habla sin decodificar 257 audios.
- **Ancho de banda al muro:** 257 × ~150 kbps ≈ 40 Mbps.
- **Sin un lienzo gigante:** un compositor por pantalla con su parte de los recuadros, cada uno a su salida.
  Un único lienzo de 8K partido después cuesta mucha más GPU.

### 12.2 El muro (ejemplo: 257 en 4K, 4 pantallas)

- Grilla de 16×16 con recuadros de 240×135: el video bajo alcanza.
- **El que tiene la palabra**, agrandado en el medio (o en una pantalla propia) con el video alto.
- **Cámara apagada:** foto y nombre del Cuerpo (los datos ya están), no negro.
- **Estado en cada recuadro:** presente/ausente (quórum), micrófono silenciado, pidió la palabra, votó.
- **Orden:** por banca o por bloque, como en el recinto (la disposición del Recinto ya existe).
- **Si alguien se corta** (teléfono con mala red): el recuadro sostiene el último cuadro y después su foto,
  sin parpadear.

### 12.3 Dónde va

- **No en B** (el programa): un muro con cientos de decodificaciones no tiene que competir con el aire.
- **Al lado del SFU** (máquina C): ya tiene todos los videos bajos, sin saltos de más.
- Si el muro tiene que salir al aire, entra al programa **como una entrada** (NDI o RTP por el cable).
- El mismo video bajo sirve para el multiview del operador y para la página de presidencia (ver quién está
  conectado de un vistazo).

### 12.4 Qué hay que hacer (es R5 y R6 de la sesión remota)

1. **Simulcast en la página de la sesión** (`assets/web/sesion/`): `sendEncodings` con dos `rid` (bajo y
   alto).
2. **SFU:** hoy el gateway mezcla y decodifica a mano. A escala hace falta uno que reenvíe calidades sin
   decodificar (`webrtcsink`/`webrtcsrc` de GStreamer, o un SFU dedicado), con TURN.
3. **Compositor «MURO»:** otro bus del motor (§5), con su disposición por pantalla y los datos del Cuerpo en
   cada recuadro.
4. **Prueba de carga (R6):** 257 Chrome simulados mandando simulcast, midiendo CPU de la máquina del muro,
   latencia y recuadros perdidos.

**Límite:** si además del que habla se quiere oír a todos en el muro, habría que decodificar y mezclar 257
audios. No es lo usual: se oye solo al que tiene la palabra.

---

## 13. Más para el servidor avanzado

Lo que hace falta cuando el aparato pasa de una consola a una instalación (varias máquinas, varios
operadores, un archivo oficial).

### 13.1 Seguridad y acceso
- Cable entre máquinas cifrado (SRTP o una VLAN dedicada) si alguna vez pasa por un switch compartido.
- **Roles** en la GUI y el MCP: operador, audio, director, solo lectura. Hoy cualquiera en la consola puede
  hacer todo.
- **Registro de quién hizo qué al aire**: sirve también como parte del acta oficial.

### 13.2 Grabación y archivo
- **Grabación ISO** (cada entrada por separado) en la máquina de entradas, para no cargar la del programa.
- **Proxy**: una copia liviana para buscar y hacer clips durante la sesión, mientras sigue grabando la
  calidad completa.
- **Timecode de hora del día** embebido en cada archivo: las ISO y el programa se alinean en posproducción.
- **Subida automática al archivo** al terminar, con los metadatos de la sesión (fecha, temas, quién habló y
  cuándo, sacados del DCN).

### 13.3 Monitoreo del propio sistema
- **Tablero de salud**: CPU/GPU, temperatura, disco que queda para grabar, paquetes perdidos en el cable,
  deriva entre máquinas, latencia de cada entrada, estado de cada salida.
- **Alertas remotas** (teléfono, Telegram) si algo falla y nadie está mirando.

### 13.4 Configuración y versiones
- **Perfiles exportables**: toda la configuración de una «sesión» (escenas, racks, mesas, salidas, el muro),
  que se lleva a otra máquina o se restaura en un clic.
- **Nada de actualizaciones durante una transmisión**, y volver a la versión anterior rápido si una nueva
  falla.

### 13.5 Accesibilidad y público
- **Audiodescripción o segundo audio** (por ejemplo, sin música o con la voz del intérprete) como pista
  aparte a YouTube o al HLS.
- **Segunda pantalla para el público**: una página con el orden del día, quién habla y la votación en vivo,
  con los mismos datos que los gráficos.

### 13.6 Pruebas
- **Simulador de sesión completa**: cámaras y micrófonos falsos, 257 remotos simulados, DCN en simulación (ya
  existe) y transmisión a un canal de prueba. Es la única forma segura de probar todo el diseño antes de una
  sesión real.

---

## 14. Plan del sistema avanzado

Recién cuando el todo en uno (§10) esté terminado y probado en sesiones reales.

| # | Etapa | Qué se hace | Cómo se sabe que anda |
|---|---|---|---|
| A1 | Partir en dos máquinas | Emisor/receptor RTP (video sin comprimir, L24) y NDI en el punto donde hoy van `intersink`/`intersrc`; reloj compartido (red o PTP) con RFC 7273; canal de control por red con autenticación (§11) | Una hora con 4 cámaras sin comprimir + NDI + audio sin pérdidas ni deriva entre máquinas |
| A2 | WebRTC en la máquina de mezcla | Gateway y `webrtcbin` en B; el video de un invitado a A solo si necesita GPU (§11.4) | La conversación con un invitado igual de rápida que con todo en una máquina |
| A3 | Congreso a escala | Simulcast bajo/alto en la página de la sesión, SFU con TURN, compositor «MURO» por pantalla (§12) | Prueba de carga con 257 remotos simulados: muro completo, pase al aire inmediato |
| A4 | Archivo y seguridad | Roles, registro de acciones, grabación ISO, proxy, timecode, subida al archivo (§13.1, §13.2) | Una sesión completa queda archivada con sus metadatos y el registro de quién hizo qué |
| A5 | Operación de una instalación | Tablero de salud, alertas remotas, perfiles, control de versiones, simulador de sesión (§13.3, §13.4, §13.6) | Una sesión simulada completa corre sin intervención y avisa las fallas provocadas |
| A6 | Público | Segundo audio y segunda pantalla (§13.5) | El público elige la pista de audio y sigue la sesión desde su teléfono |

---

# Apéndice

---

## 15. Referencias

- [Eyevinn/strom](https://github.com/Eyevinn/strom): motor de pipelines de GStreamer con editor de nodos.
  - Bloques: mezclador de video PVW/PGM con shaders, consola de audio, WebRTC, RTMP, SRT, NDI, AES67,
    DeckLink.
  - Lo usa [Open Live](https://github.com/Eyevinn/open-live) como motor
    ([artículo](https://eyevinntechnology.medium.com/open-live-building-a-software-broadcast-mixer-on-commodity-gpu-hardware-3b015ce9d1b5)).
  - Es lo más parecido a lo que queremos: conviene leer su código antes de escribir la mezcla.
  - Está en Rust: copiar ideas, no código, salvo que se revise la licencia.
- [Strom PR #895](https://github.com/Eyevinn/strom/pull/895): la primera entrada tarde que rebobina un
  mezclador en vivo (§5.2.2).
- [Guía del vision mixer de Strom](https://raw.githubusercontent.com/Eyevinn/strom/main/docs/VISION_MIXER_OPERATOR_GUIDE.md):
  take atómico PVW↔PGM, transiciones, DSK, latencia de fábrica de 20 ms.
- [GstAggregator](https://gstreamer.freedesktop.org/documentation/base/gstaggregator.html): `min-upstream-latency`,
  `start-time-selection`, `force-live`.
- [intersink](https://gstreamer.freedesktop.org/documentation/rsinter/intersink.html) /
  [intersrc](https://gstreamer.freedesktop.org/documentation/rsinter/intersrc.html).
- [fallbacksrc y fallbackswitch](https://coaxion.net/blog/2020/07/automatic-retry-on-error-and-fallback-stream-handling-for-gstreamer-sources/);
  `livesync` en [gst-plugins-rs](https://github.com/GStreamer/gst-plugins-rs).
- [Diseño de latencia](https://gstreamer.freedesktop.org/documentation/additional/design/latency.html),
  [fuentes en vivo](https://gstreamer.freedesktop.org/documentation/additional/design/live-source.html) y
  [relojes](https://gstreamer.freedesktop.org/documentation/application-development/advanced/clocks.html).
- [videomixer](https://gstreamer.freedesktop.org/documentation/videomixer/index.html): obsoleto, usar `compositor`.
- [gst-switch](https://github.com/timvideos/gst-switch): un switcher viejo, sirve como referencia de estructura.
- [GStreamer Conference 2025](https://gstreamer.freedesktop.org/conference/2025/) ([videos](https://discourse.gstreamer.org/t/gstreamer-conference-2025-video-recordings-now-available/5528)),
  en particular [ST 2110 con placas y NICs de NVIDIA](https://indico.freedesktop.org/event/11/contributions/499/).
