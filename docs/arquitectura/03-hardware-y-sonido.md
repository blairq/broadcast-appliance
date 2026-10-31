# Hardware y sonido: latencia y disponibilidad (03)

Opinión de ingeniería de hardware (video/audio en tiempo real, GPU, captura, redes,
energía, térmica) y de sonido para broadcast en vivo (latencia, relojes, mix-minus,
loudness, monitoreo, redundancia) sobre el sistema descripto en
[`01-relevamiento.md`](01-relevamiento.md). Objetivo: **latencia mínima y
disponibilidad máxima**, respetando las decisiones del dueño (todo portable y
vendoreado, todo en GStreamer, pruebas de audio con señales sintéticas y nunca por
parlantes, N salidas como operadores completos, corte de luz = todo vuelve solo).

- Base: el relevamiento (se cita «§sección») y el código en `master` a 2026-10-31
  (se cita `archivo:línea`). No se corrió la app: los números «configurados» salen
  del código; los «estimados» son míos, a partir de valores típicos de equipos y
  bibliotecas, y están para **medirse** (cada propuesta dice cómo).
- Supuestos de las tablas: lienzo 1920×1080 a 30 cps (un cuadro = 33 ms), 48 kHz,
  bloques de 10 ms. **L_e** = latencia del estudio (§5.9: la mayor de las entradas
  que no pasan el tope de 250 ms, `src/motor/Reloj.hpp:85-137`).

## Índice

1. [Resumen](#1-resumen)
2. [Presupuesto de latencia](#2-presupuesto-de-latencia)
3. [Audio](#3-audio)
4. [Video y GPU](#4-video-y-gpu)
5. [Disponibilidad](#5-disponibilidad)
6. [Equipo recomendado: qué soportar y qué detectar](#6-equipo-recomendado-qué-soportar-y-qué-detectar)
7. [Tabla de propuestas](#7-tabla-de-propuestas)
8. [Top 10 priorizado](#8-top-10-priorizado)
9. [Cómo medir sin parlantes](#9-cómo-medir-sin-parlantes)

---

## 1. Resumen

1. **El retorno de audio a los invitados probablemente corta y llega 150 ms tarde
   por diseño.** El hilo de B saca **un** bloque de 10 ms y después duerme
   `sleep_for(10 ms)` relativo (`src/gateway/MediaPump.cpp:516-579`, verificado): el
   período real es 10 ms + trabajo + lo que se pasa el sueño, siempre más de 10 ms.
   El escritor (`baanillosink sync=true`) produce exactamente 100 bloques/s; el
   lector saca menos. El anillo se llena (16 bloques = 160 ms, §3.1) y desde ahí el
   escritor **descarta** bloques (`SharedAudioRing.hpp:141-146`): latencia fija de
   ~150-160 ms y un corte de 10 ms cada vez que se desborda. En Windows, sin
   `timeBeginPeriod` ni temporizador de alta resolución en ningún lado de `src/`
   (grep), `sleep_for(10)` dura ~15,6 ms: se perdería **un tercio** de los bloques.
   El relevamiento lo deja abierto (Apéndice A, punto 5). Es lo primero a medir y
   arreglar.
2. **Cuando el mix-minus tiene un hueco, el invitado oye el programa entero, que
   contiene su propia voz** (`MediaPump.cpp:527-531`): eco de ~0,5 s para el que
   habla. Además los mix-minus existen solo en la salida por omisión
   (`Motor.hpp:50-53`, `MediaPump.cpp:470-472`): en las otras salidas el invitado se
   oye siempre a sí mismo.
3. **Una sola latencia del estudio gobierna todo**: una cámara RTSP a 200 ms sube
   L_e a ~250 ms y arrastra los auriculares del operador (L_e + 85 ms) y el retorno
   de los invitados (L_e + 45 a 75 ms), que no necesitan esperar al video.
4. **Relojes**: el audio de cada placa se reancla cuando se aparta 50 ms
   (`Piezas.cpp:711-790`) sin remuestreo adaptativo: diente de sierra de sincronía
   A/V de hasta 50 ms y un salto audible (hueco o recorte de 50 ms) cada 8-17 min
   por placa (deriva típica 50-100 ppm).
5. **Rack**: los plugins CLAP con latencia propia se compensan **entre ramas** del
   rack (`AudioRack.cpp:860-880`) pero la latencia total (`latenciaTotal_`) no se
   informa a GStreamer (`barack` no contesta LATENCY, `PLAN-TIEMPOS.md:27`): el audio
   procesado sale atrasado respecto de su sello.
6. **Disponibilidad**: la grabación caída no se rearranca (`Salidas.cpp:527-542`), el
   codificador caído no se reemplaza (`BroadcastEngine.cpp:1863-1865`), la copia usa
   un **segundo codificador** (`BroadcastEngine.cpp:1664-1676`), `al-aire.json` y
   `app.json` se escriben sin `fsync` y ocho configuraciones se truncan en el lugar
   (§7.4), no hay detección de UPS ni de batería (grep), y todo el aire vive en el
   mismo proceso que la interfaz y el MCP (§2.2).
7. **Video**: el camino al invitado hace ~67 MB de copias por cuadro para terminar en
   640×360 (§3.7, `Config.hpp:83-84`), y codifica el programa **una vez por sesión**.

---

## 2. Presupuesto de latencia

### 2.1 Cómo se arma la espera en el motor

Todas las ramas sellan con la hora de captura y esperan en el **sumidero o
agregador con sincronía** hasta `sello + latencia del pipeline`. Lo que importa:

| Pieza | Espera configurada (desde el sello) | Fuente |
|---|---|---|
| Entrada (lo que pide) | máx(declarada + 15 ms, p90 de edad + jitter acotado 3-40 + 5 ms) | §5.9, `Entrada.cpp:1247-1290`, `1361-1384` |
| **L_e** (estudio) | la mayor de las entradas bajo el tope; tope automático 250 ms | `Reloj.hpp:135`, `Reloj.cpp:445-490` |
| Composición | L_e + 5 ms, más alineación a la grilla de cuadros (0-33 ms, media 17) | `Composicion.cpp:215-230`, `395-412` |
| Bus de audio | L_e + 30 ms + limitador 5 + bloque de salida 10 = **L_e + 45** | `Mezcla.cpp:133-147`, `limitador.cpp:8`, `Mezcla.cpp:84` |
| Escucha (auriculares) | bus + 40 = **L_e + 85** | `Escucha.cpp:32`, `93` |
| Puente al gateway (programa) | bus + 30 = **L_e + 75** | `Motor.cpp:237-257`, `406-415` |
| Mix-minus `retorno<n>` | el bus termina en `baanillosink`: **L_e + 45** | `Mezcla.cpp:86-88` |
| Vistas (UI, `BA.Program`) | «productor + 40», pero `appsink sync=false` (`Vistas.cpp:136`): **no espera**; solo cuesta convertir | `Vistas.cpp:23`, `173` |
| Grabación / transmisión | máx(video, audio) + 300 = **L_e + 345** | `Salidas.cpp:56`, `454`, `674` |

Nota: el «+40 de las vistas» que figura en §5.9 es nominal. La vista no sincroniza;
el video del programa sale de la composición a L_e + 5 ms y la vista lo entrega en
cuanto lo convierte. Esto importa en 2.5 (desfase A/V del retorno).

### 2.2 Valores de L_e por escenario

| Escenario | Entrada que manda | L_e estimada |
|---|---|---|
| A. Solo cámaras USB y micrófonos locales | cámara UVC: declarada ~33 + 15 | **~50 ms** |
| B. A + invitados WebRTC | `bavideoanillo` declara 33 ms (`video.cpp:185`) → ~48; edad medida desde el sello de B, chica | **~50-60 ms** |
| C. Con una cámara RTSP migrada (`ffmpeg_source` → 200 ms, `BroadcastEngineScenes.cpp:581-582`) | 200 + decodificación + 15 | **~220-250 ms** (pega en el tope) |

### 2.3 Cámara / micrófono local → programa → monitor y auriculares

| # | Etapa | Configurado | Estimado A (L_e 50) | Estimado C (L_e 250) |
|---|---|---|---|---|
| 1 | Sensor + exposición + USB (UVC; MJPEG agrega decodificación) | — | 33-66 | 33-66 |
| 2 | Entrada: subida a GPU, filtros, `videorate`, `intersink sync=false` | cola de 1 cuadro, leaky (`Entrada.cpp:859`) | 2-5 (superres +10-30) | 2-5 |
| 3 | **Espera en la composición** | L_e + 5 + grilla | 50 + 5 + 17 = **72** | 250 + 5 + 17 = **272** |
| 4 | Composición GL/D3D12 | — | 1-3 | 1-3 |
| 5 | Vista `programa@ui`: escalar en GPU, bajar, BGRA | no espera | 2-5 | 2-5 |
| 6 | QML + vsync + monitor | — | 25-50 | 25-50 |
| | **Cámara → pantalla del operador** | | **~135-200 ms** | **~335-400 ms** |

| # | Etapa (audio) | Configurado | A (L_e 50) | C (L_e 250) |
|---|---|---|---|---|
| 1 | Buffer del dispositivo de captura | no se fija `latency-time`/`buffer-time` (§5.2); `wasapi2src low-latency=true` | 3-10 (Win/Mac), 10-25 (Pulse) | ídem |
| 2 | `queue ! barack ! filtros ! audioconvert ! audioresample` | rack sin latencia informada | 0-1 + **latencia de los plugins sin informar** | ídem |
| 3 | **Bus de programa** | L_e + 45 | **95** | **295** |
| 4 | **Escucha** | + 40 | **135** | **335** |
| 5 | Sink + DAC | `wasapi2sink` sin `low-latency` (`Escucha.cpp:20-30`) | 5-15 | 5-15 |
| | **Micrófono → auriculares del operador** | | **~145-160 ms** | **~345-360 ms** |
| | Bus `directo` (sala) + escucha | 40 + 40 fijos (`Motor.cpp:26`) | ~90-100 ms | ~90-100 ms |

Referencias de oficio: retorno de la propia voz al que habla < 10 ms; monitoreo del
operador < 20-30 ms para no molestar; refuerzo en sala < 10-15 ms (más de ~20 ms
produce filtro peine y «doble voz» con el sonido directo). **Ningún camino de
monitoreo de software cumple eso hoy**; el `directo` (~90-100 ms) tampoco sirve
para refuerzo. Para la sala y el talkback conviene el monitoreo directo de la placa
o de la consola; el software debería avisarlo (ver §6).

### 2.4 Programa → codificador → transmisión / grabación

| # | Etapa | Configurado | A (L_e 50) | C (L_e 250) |
|---|---|---|---|---|
| 1 | Hasta el programa (cámara + espera) | ver 2.3 | ~105 | ~305 |
| 2 | **Margen de salidas** | máx(v, a) + 300 → L_e + 345 (`Salidas.cpp:56`) | +300 sobre el programa | +300 |
| 3 | Bajada a NV12 (GL) / sin bajada (D3D12 que acepta `D3D12Memory`) | §4.3 | 1-3 | 1-3 |
| 4 | Codificador H.264 hw, `zerolatency`, sin reordenar (`Salidas.cpp:72-98`) | — | 5-20 (x264 `veryfast`: 33-66) | ídem |
| 5 | AAC (cuadro de 21 ms + cebado) | 160 kbps | ~25-45 | ídem |
| 6 | `flvmux` + `rtmp2sink` / `mpegtsmux` + `srtsink` | colas de 1 s (`Salidas.cpp:617-621`) | 1-5 en régimen | ídem |
| 7 | SRT: latencia del protocolo (de fábrica de `srtsink`, ~125) | no se fija | 120-125 | ídem |
| | **Cámara → salida por la red (RTMP)** | | **~440-480 ms** | **~640-680 ms** |
| | **Cámara → salida por la red (SRT)** | | **~560-600 ms** | **~760-800 ms** |

Para RTMP a una plataforma (que agrega 2-30 s) el margen de 300 ms es irrelevante.
Para **SRT de contribución** (a otro estudio, a una consola remota, a un monitor de
retorno) es el 40-50 % de la latencia propia y sobra: los codificadores con
`zerolatency` declaran su latencia y el pipeline ya la suma.

### 2.5 Invitado WebRTC → programa → retorno al invitado

**Audio (boca de un invitado → oído de otro, pasando por el estudio):**

| # | Etapa | Configurado | Estimado hoy | Con propuestas P01-P05 |
|---|---|---|---|---|
| 1 | Navegador: captura + AEC/NS + Opus 20 ms (+6,5 lookahead) | — | 30-40 | 30-40 |
| 2 | Red de subida | — | 10-80 | 10-80 |
| 3 | NetEq en B | adaptativo | 20-80 | 20-80 |
| 4 | `PullAdm` cada 10 ms con `sleep_until` absoluto (`Webrtc.cpp:213-230`, bien) + rebloqueo 10 ms | 10 + 10 | 5-15 | 5-15 |
| 5 | Anillo B→A, `baanillo` mira cada 4 ms (`anillo.cpp:77-82`) | 10 declarados | 10-20 | 10-20 |
| 6 | **Entrada + bus `retorno<n>`** | L_e + 45 (`Mezcla.cpp:86-88`) | **95** (A) / **295** (C) | **25** fijos (P05) |
| 7 | **Anillo A→B** | hasta 160 (`SharedAudioRing.hpp:61`) | **~150-160** (se llena, ver 3.1) | **15-25** (P01) |
| 8 | Hilo de retorno de B: 1 bloque y `sleep_for(10)` | 10 | 10-16 | 0-10 |
| 9 | Opus 20 ms + pacer | — | 25-30 | 25-30 |
| 10 | Red de bajada | — | 10-80 | 10-80 |
| 11 | Jitter buffer del navegador + salida | — | 30-100 | 30-100 |
| | **Total boca → oído** | | **~400-680 ms (A)**, **~600-880 (C)** | **~180-480 ms** |
| | **Aporte del estudio (filas 4-8)** | | **~270 ms (A)** / ~470 (C) | **~55-95 ms** |

ITU-T G.114: hasta 150 ms boca-oído es conversación natural; 150-400 aceptable; más
de 400 la gente se pisa. Hoy el estudio solo consume casi todo el presupuesto.

**Video (cámara del invitado → programa → retorno):**

| # | Etapa | Configurado | Estimado |
|---|---|---|---|
| 1 | Navegador: captura + codificación | — | 40-70 |
| 2 | Red + jitter buffer de video de B + decodificación | — | 40-150 |
| 3 | `I420ToARGB` + ajuste con barras al slot (1280×720 de fábrica, `Config.hpp:79`) en CPU + `memcpy` | — | 3-8 |
| 4 | `bavideoanillo` (mira cada 4 ms; 15 si quieto) | 33 declarados | 0-4 |
| 5 | Espera en la composición | L_e + 5 + grilla | 72 (A) / 272 (C) |
| 6 | Pasos 2-10 de §3.7 (bajar, `videoconvert`, 2 copias en `Vistas`, Spout, copia en B, escalar, I420) | — | 8-20 |
| 7 | Codificación por sesión (VP8/VP9 por software en Win/Linux; VideoToolbox en Mac) | — | 5-30 |
| 8 | Red + jitter buffer + decodificación + pantalla del navegador | — | 70-200 |
| | **Total** | | **~240-560 ms (A)**, **~440-760 (C)** |

**Sincronía de labios del retorno.** El video del programa sale hacia B a
L_e + 5 ms (la vista no espera, 2.1) y el audio a L_e + 75 ms más lo que tenga el
anillo (hoy ~150 ms): **el audio llega a B 70-230 ms después que el video**. B no
los realinea (cada uno se empuja por su lado: `MediaPump.cpp:576`, `651-702`). ITU-R
BT.1359: audio atrasado se nota desde ~125 ms y deja de ser aceptable a ~185 ms. Con
el anillo lleno, pantallas de sala, espectadores e integrantes remotos ven los labios
fuera de sincronía.

### 2.6 Dónde se va el tiempo (de mayor a menor)

| Causa | Cuánto | Camino | Propuesta |
|---|---|---|---|
| L_e única para todo, tope 250 | 50-250 ms | todos | P05, P06 |
| Anillo A→B que se llena (lector más lento que el escritor) | ~150 ms + cortes | retorno, IFB | P01, P02 |
| Margen de salidas +300 | 300 ms | SRT de contribución | P07 |
| Margen de mezcla +30, escucha +40, puente +30 | 70-100 ms | auriculares, retorno | P05 |
| Codificación por sesión + 11 pasos con conversiones | 15-50 ms y mucha CPU | video a invitados | V1, V2 |
| Grilla de cuadros | 0-33 ms | video | (inherente; ver V10) |
| Sondeos de 4 ms (y 15,6 ms en Windows sin alta resolución) | 2-8 ms por salto | IPC | V8, P02 |

---

## 3. Audio

### 3.1 El retorno de B: un lector que no da abasto (crítico)

Evidencia (código verificado):

- `retornoAudioThread` hace un `reader.pop()` por vuelta, un `pop()` por cada slot de
  mix-minus, mezcla el IFB y termina con `std::this_thread::sleep_for(10 ms)`
  (`MediaPump.cpp:516-579`). El sueño es **relativo** y se suma al trabajo.
- `baanillosink sync=true` escribe un bloque cada 10 ms exactos del reloj del estudio
  (`anillosink.cpp:1-20`). Ante anillo lleno, **descarta** (`SharedAudioRing.hpp:141-146`).
- El mismo patrón está en el audio del operador y las voces (`MediaPump.cpp:765`,
  `806`, `858`). `PullAdm`, en cambio, usa `sleep_until(next)` con `next += 10 ms`
  (`Webrtc.cpp:213-230`): ese está bien.

Consecuencia: con un período de 10,3 ms (macOS en carga) el lector saca 97 bloques/s
contra 100 escritos; en ~5 s el anillo está lleno y desde ahí se pierden ~3 bloques
por segundo (tres cortes de 10 ms por segundo en la voz del retorno) con 150-160 ms de
latencia fija. En Windows, con la resolución de 15,6 ms por defecto, el lector saca
~64 bloques/s: se pierde más de un tercio. Además en Windows 11 un proceso **sin
ventana** como B puede ver ignorado su pedido de resolución de temporizador por el
ahorro de energía, así que `timeBeginPeriod` solo no alcanza.

Agravante: los lectores de programa y de mix-minus se desbordan por separado y en
momentos distintos, así que entre el programa y el mix-minus queda un desfase
variable; y el `if (... !b2.isSilence)` (`MediaPump.cpp:527-529`) hace que en cada
hueco del mix-minus el invitado **oiga el programa entero con su voz**.

Propuestas:

- **P01** Retorno con ritmo absoluto y nivel objetivo: `next += 10 ms;
  sleep_until(next)` como `PullAdm`; si la ocupación supera el objetivo (2-3 bloques)
  descartar el excedente con fundido corto; si queda por debajo, no inventar (silencio
  con fundido). Leer programa y mix-minus del mismo **sello QPC** (ambos bloques lo
  traen, §3.1) en vez de «uno por vuelta». Exponer `overrunCount`/`underrunCount` y la
  ocupación en el registro cada 10 s y en `sistema_recursos`.
- **P02** Temporizador de alta resolución en Windows para B (y el hilo de la Matriz):
  `CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)` y
  `SetProcessInformation(ProcessPowerThrottling)` sin `IGNORE_TIMER_RESOLUTION`. Mejor
  aún (V8): que el escritor despierte al lector con un evento con nombre / semáforo.
- **P03** Hueco de mix-minus: nunca caer al programa entero. Repetir con fundido el
  último bloque de mix-minus o dar silencio; si el mix-minus no abre, ese invitado
  oye «apagado» y la interfaz lo marca.

### 3.2 Relojes y deriva

Evidencia: la hora de captura se cuenta por muestras y se **reancla** si se aparta
más de 50 ms del reloj del estudio (`Piezas.cpp:711-790`, verificado). No hay
remuestreo adaptativo: el `audioresample` de la cadena trabaja a razón fija (§5.8).
La Matriz (Qt Multimedia) es otro dominio con un anillo de 20 ms de colchón y recorte
a 80 ms (`RingEstereo.hpp:28-35`), con su hilo a `steady_clock` cada 10 ms
(`Matriz.cpp:809-830`).

Qué se oye:

- Una placa USB típica deriva 20-100 ppm respecto del reloj del sistema. A 50 ppm se
  acumulan 50 ms en ~17 min; a 100 ppm, en ~8 min. Entre reanclajes, el audio de esa
  placa se corre **en diente de sierra** respecto del video (hasta 50 ms: dentro de
  la tolerancia pero se suma a todo lo demás). En cada reanclaje, el `audiomixer` ve
  un salto de sello: **hueco de 50 ms** (placa más lenta) o **recorte de 50 ms**
  (placa más rápida). Es un corte audible en el programa, la grabación y el retorno.
- La Matriz por Qt corrige la deriva **tirando o rellenando** cuando el anillo pasa
  80 ms o se vacía: clicks periódicos, con un período que depende de la placa.
- Con dos placas (captura en una, escucha en otra) el mismo problema por duplicado.

Propuestas:

- **P10** (previa) Medir la deriva: el sondeo `ponerHoraAudio` ya tiene muestras
  contadas y hora del reloj; calcular ppm con una regresión de varios minutos, contar
  reanclajes y publicarlos por dispositivo (`sistema_recursos`, `tiempos_medir`). Cubre
  el Apéndice A, punto 10.
- **P09** Remuestreo adaptativo: un lazo de seguimiento (DLL de segundo orden,
  constante de tiempo de decenas de segundos) que estime la razón placa/estudio y
  maneje un remuestreador variable (libsamplerate ya está vendoreado y se usa en B,
  `MediaPump.cpp:1185-1203`) como elemento propio `baderiva` después de la fuente;
  el reanclaje queda solo como red de seguridad (> 50 ms = desconexión/reconexión).
  Alternativa sin remuestreo para la placa principal: **reloj del estudio = reloj de
  la placa principal** (nuevo tipo en `video-reloj.json`, junto a `sistema`, `ntp`,
  `ptp`, §6): el video tolera duplicar/descartar un cuadro, el audio no.
- **PTP / word clock**: con interfaces AES67/Dante, usar el tipo `ptp` del mismo
  dominio (ya soportado, `Reloj.cpp:325-370`) deja placa y estudio en el mismo reloj.
  Con placas USB con entrada de word clock, el software no lo ve: la medición de P10
  es la forma de confirmar que están enganchadas (deriva ≈ 0 ppm entre ellas).

### 3.3 Tamaños de bloque y latencia del rack

- `barack` procesa **en el lugar**, en trozos de hasta 1024 muestras
  (`GstRack.cpp:85`, `115-140`): el trozo no agrega espera, el buffer de GStreamer
  sigue siendo el que manda la fuente (~10 ms). Hasta acá bien.
- El rack sí calcula su latencia (CLAP_EXT_LATENCY por nodo, compensación entre
  ramas con tope de 2 s, `AudioRack.cpp:860-880`, `latenciaTotal_`), pero **no se la
  dice a GStreamer** (`barack` no contesta LATENCY, `PLAN-TIEMPOS.md:27`) ni corrige el
  sello. Un limitador con lookahead de 5 ms, un compresor de 10 ms o un EQ de fase
  lineal de 20-100 ms atrasan **ese** micrófono respecto de su video y de los demás
  micrófonos (filtro peine si el mismo sonido entra por dos micrófonos, uno con rack).
- El vigía que **puentea** un plugin lento (`AudioRack.cpp:1156-1170`) cambia la
  latencia del rack en caliente: salto de sello y, si el plugin era un limitador o un
  compresor, un salto de nivel.
- `barack` y el vigía registran desde el hilo de audio (`avisadoMs`, `GstRack.cpp`);
  `registrar()` toma un candado global y hace `fprintf`+`fflush` (§8.1, §8.2). Con
  prioridad de tiempo real (P12) eso es inversión de prioridad.

**P08**: `barack` contesta LATENCY con `latenciaTotal_` (y avisa
`gst_element_post_message(latency)` al cambiar el plan) y resta esa latencia del PTS
de salida, así el contenido queda sellado con su hora real de captura y el mezclador
espera lo justo. Al puentear un plugin, reemplazarlo por una línea de retardo de su
misma latencia. Registro desde el hilo de audio: a una cola sin candado.

### 3.4 Mix-minus, IFB y monitoreo

- **Mix-minus por salida (P04)**: hoy los `retorno<n>` siguen a la salida por omisión
  (`Motor.hpp:50-53`) y B solo abre slots de mix-minus para ella
  (`MediaPump.cpp:470-472`: «las otras bajan su retorno entero»). Con N salidas como
  operadores completos, un invitado de la salida B se oye a sí mismo. Propuesta: bus
  `retorno<n>:<salida>` por salida que tenga invitados, canal
  `BA.Audio.Return.<salida>.<n>`, y slots en el hilo de retorno de cada salida.
- **IFB**: el programa baja a ×0,4 (−8 dB) bajo la voz del operador
  (`MediaPump.cpp:533-556`), fijo y sin rampa: conviene un «ducking» con ataque/
  liberación (10/300 ms) y profundidad configurable (−12 a −20 dB es lo usual en IFB).
- **Monitoreo / PFL**: la escucha «selección» sigue a la salida por omisión
  (`Motor.hpp:323`) y va a bus + 40 ms. **P16**: PFL/AFL de baja latencia tomado de la
  entrada (antes del bus), con latencia fija corta como el `directo`, y por salida.
- **P05 — camino de monitoreo desacoplado de L_e**: los buses que alimentan oídos en
  vivo (retornos, IFB, escucha «en vivo», PFL) con latencia **fija** de ~20-25 ms,
  como ya hace `directo` (`Motor.cpp:26`, «lo que llega más tarde no entra»). Lo que
  no llega a tiempo (una cámara RTSP con audio) no entra al retorno: el invitado no
  necesita oír el audio de la cámara IP en sincronía con nada. Los buses de programa
  (lo que se graba y transmite) siguen con L_e, que es lo correcto para A/V.
- **P13** Escucha en Windows con `wasapi2sink low-latency=true` (hoy solo la fuente lo
  tiene, `EntradasAudio.cpp:22`) y `latency-time` explícito (5-10 ms) en fuentes y
  sinks de las tres plataformas, informado en `tiempos_pipeline`.

### 3.5 Loudness y limitador

- Bien: limitador true-peak con sobremuestreo ×4, techo −1 dBTP, lookahead declarado
  (`limitador.cpp:1-16`, `73-91`); `ebur128level` después del limitador (`Mezcla.cpp:81-82`).
- Falta: el objetivo de `loudness.json` (`OperateMode.cpp:630`) se **mide** pero no hay
  control lento de ganancia hacia el objetivo. **P15**: AGC de loudness opcional
  (ventana de corto plazo 3 s, velocidad ≤ 1 dB/s, rango ±6 dB, congelado en silencio,
  antes del limitador) y techo −2 dBTP cuando el AAC va por debajo de 128 kbps
  (recomendación AES TD1004 para códecs con pérdida). Las pistas 2-6 no tienen
  limitador (§5.4): si se publican como alternativa (otro idioma), necesitan el suyo.

### 3.6 Sincronía A/V

- En las salidas está bien resuelta por sellos y grilla (`Composicion.cpp:217-230`).
- En los invitados, libwebrtc sincroniza audio y video del mismo flujo antes de
  entregar, y B sella ambos con QPC: queda sincronizado.
- En el **retorno** no (2.5). **P14**: en B, demorar el video del programa según la
  diferencia de sellos QPC entre el cuadro y el bloque de audio que se está empujando
  (los dos traen sello), o pasar esos sellos como hora de captura a libwebrtc para que
  el navegador sincronice. Con P01 la diferencia baja a ~70 ms; P14 la lleva a < 20.
- 30 cps en un país de 50 Hz (ver V10): parpadeo de luces LED/fluorescentes en cámara.

### 3.7 Prioridad de los hilos de audio

Evidencia: sin MMCSS, `SCHED_FIFO` ni `THREAD_TIME_CONSTRAINT_POLICY` (§2.4,
`Prioridad.cpp`); en Mac solo QoS interactiva, que no es tiempo real; la medición
del propio código mostró que subir **todos** los hilos de streaming empeora (más
cambios de contexto, `Prioridad.cpp:280-286`).

**P12**: tiempo real solo para los pocos hilos que mueven audio con plazo: hilo de
salida de cada `audiomixer` de bus, `baanillosink`/`baanillo`, hilo de retorno de B,
`PullAdm`, mezcla de la Matriz. Windows: `AvSetMmThreadCharacteristicsW(L"Pro Audio")`
+ `AvSetMmThreadPriority(HIGH)`; macOS: `THREAD_TIME_CONSTRAINT_POLICY` (período
10 ms, cómputo 1-2 ms, plazo 5 ms) o unirse al `os_workgroup` del dispositivo; Linux:
RealtimeKit por D-Bus si está (es del sistema operativo, no una dependencia nuestra) y
si no, el `setpriority` actual. Condición previa: nada de candados globales ni E/S en
esos hilos (3.3). Esto importa más porque en la misma máquina corren modelos locales,
superresolución y ONNX (§1.1, §4.4), que compiten por CPU y GPU.

### 3.8 Matriz por Qt Multimedia vs motor

La Matriz abre placas con `QAudioSource`/`QAudioSink` (§5.2, §5.7), con su propio
hilo de 10 ms, anillos con candado (`RingEstereo.hpp:23-52`, tomado por un hilo de
GStreamer, el de mezcla y el de audio de Qt) y sin control de período ni modo
exclusivo. Es un segundo dominio de reloj sin seguimiento (3.2) y 20-30 ms extra
(§5.7). **P11**: llevar la Matriz al motor (fuentes/sinks multicanal de GStreamer con
`unique-id`/`device`, mismo tratamiento de hora y P09), coherente con «todo en
GStreamer». Mientras tanto, cambiar el candado de `RingEstereo` por un SPSC sin
bloqueo.

---

## 4. Video y GPU

### 4.1 Copias del programa hacia los invitados

Recorrido de §3.7 contado en bytes a 1080p BGRA (8,3 MB por cuadro): bajada GPU→CPU
(8,3), `videoconvert` RGBA→BGRA (lee y escribe 16,6), `assign` en `Vistas` (8,3),
`copia = c` (8,3), `memcpy` a Spout (8,3), `memcpy` en B (8,3), escalado box a
640×360 (lee 8,3) ≈ **67 MB por cuadro, ~2 GB/s a 30 cps**, para terminar en un
retorno de **640×360** (`Config.hpp:83-84`). Después, `ARGBToI420` y codificación
**por sesión** (`Webrtc.cpp:238-250`, `953-956`), por software en Windows y Linux
(libvpx/OpenH264, `Webrtc.cpp:1118-1131`): a 640×360 son ~5-10 % de un núcleo por
sesión en VP8; con 16 invitados y pantallas, varios núcleos y calor.

- **V2** (rápida): una vista `programa@invitados` al tamaño de la sesión, escalada y
  convertida a BGRA **en la GPU** (`glcolorconvert`/`d3d12convert` ya existen en
  `aSistemaEscalado`, `Piezas.cpp:1157-1170`) y una sola copia en `Vistas` (entregar
  el `GstBuffer` mapeado en vez de `assign` + `copia`). De ~67 a ~4 MB por cuadro
  (−95 %), −3-6 ms. La vista a tamaño completo solo cuando la pide una pantalla.
- **V1** (estructural): **codificar el programa una vez** en A con el codificador por
  hardware de GStreamer (H.264 baseline/constrained, sin B-frames, GOP largo con
  clave a pedido) y pasar por memoria compartida la unidad de acceso comprimida
  (~25 KB por cuadro) a B, que solo empaqueta RTP por sesión (B ya reenvía bytes
  codificados sin recodificar para los espectadores, `Webrtc.cpp:20-27`). Elimina los
  pasos 2-11 por sesión. Necesita pedido de clave (PLI/FIR) de B a A y, para
  adaptación, 2-3 capas (simulcast) codificadas una vez cada una.
- **V3** Invitados entrantes en su resolución: hoy B ajusta con barras al slot de
  1280×720 en CPU (`MediaPump.cpp:1063-1160`, `Config.hpp:79`): un invitado a 360p se
  agranda en CPU y se sube a la GPU 4× más grande. Publicar el tamaño real en la
  cabecera del canal y escalar en la GPU de A.

### 4.2 Texturas compartidas por plataforma

El transporte Spout es memoria de CPU con triple buffer en las tres plataformas (§13,
`SpoutTransport.cpp:55-77`). **V4** (después de V1/V2, porque con ellas lo que cruza
es chico): `IOSurface` en Mac (el `GLMemory` de `glvideomixer` ya vive sobre
IOSurface, `MAPA-GPU-CPU.md`; VideoToolbox en B lo toma sin copia), recursos D3D12
con `CreateSharedHandle` (NT handle) + fence compartida en Windows, DMA-BUF con
`EGL_EXT_image_dma_buf_import` en Linux. Útil sobre todo para las pantallas de sala a
resolución completa (`BA.Escena.<k>`, §3.2).

### 4.3 Linux en CPU

De fábrica Linux compone con `compositor` en CPU (`Piezas.cpp:1020-1021`). A 1080p30
con varias capas, más DSK, preview, escenas y multiview, es la plataforma que más
calienta y la que menos margen deja. **V5**: el backend `Gl` ya probado en la Mac
funciona sobre EGL (Mesa/NVIDIA) con `glupload` de DMA-BUF desde `v4l2src`/VA; activar
la autoprueba `glListo()` también en Linux y dejar CPU como respaldo, como hoy.

### 4.4 Codificadores, sesiones y salidas adicionales

- La copia de la grabación usa **otro codificador** con las mismas fuentes
  (`BroadcastEngine.cpp:1664-1676`), cada destino extra es otra `Transmision` con su
  codificador (`BroadcastEngine.cpp:1867-1879`), y la grabación y la transmisión de
  una salida codifican por separado. Por salida pueden ser 3-5 sesiones de
  codificador; con 8 ISO (`BroadcastEngine.hpp:734`), más. Las GeForce limitan las
  sesiones NVENC simultáneas (según el driver, 3 a 8); AMF y QSV tienen límites
  prácticos de ancho de banda. Pasar el límite = error del elemento = salida caída
  sin reemplazo (5.3).
- **V6**: un codificador por salida y perfil, con `tee` a grabación, copia,
  transmisión y destinos extra (misma calidad: la grabación suele querer más bitrate;
  dos perfiles como máximo). La copia protege contra **disco** (lo más frecuente);
  para proteger contra **codificador**, opción «copia con codificador distinto» (por
  ejemplo x264 `superfast` a menor bitrate) explícita y no de fábrica.

Costo estimado por salida adicional (1080p30, GPU):

| Recurso | Por salida | Fuente |
|---|---|---|
| Pipelines | composición programa (+DSK: 2.º mezclador), preview, bus, vista `BA.Salida.<id>`, salidas | §2.2, §4.6 |
| GPU | 2-6 ms por cuadro (composición + DSK + preview + escalados) | estimado |
| Bajadas GPU→CPU | 1 (NV12 al codificador en GL) + 1 (vista al gateway, 8 MB, hasta V2) | §4.3, §3.7 |
| Sesiones de codificador | 2-4 hoy; 1-2 con V6 | 4.10 |
| Hilos en B | par video+audio | `MediaPump.cpp:416-419` |
| CPU | 5-15 % de un núcleo (conversiones, mux, audio) | estimado |

### 4.5 Colas

| Cola | Tamaño | Opinión |
|---|---|---|
| Grabación, cruda antes del codificador | 3 s leaky (`Salidas.cpp:351`) | 90 cuadros: ~280 MB en NV12 de sistema o en memoria de GPU (D3D12). Si el codificador se traba, ocupa VRAM que la composición necesita. **V7**: 0,5-1 s; el colchón contra disco es el comprimido (60 s/512 MB), que está bien |
| Transmisión video / audio | 1 s leaky / 1 s **no** leaky (`Salidas.cpp:617-621`) | Política distinta entre video y audio: si la red se traba, el audio bloquea el mux mientras el video descarta; a la vuelta hay discontinuidad. Las dos leaky, o una sola cola después del mux |
| Escucha, puente y toma | 2 s (`audio.colaEscuchaS`) | No agregan en régimen (sink con sync). OK |
| Entrada de bus | `intersrc` 1,5 s (`Mezcla.cpp:168`) | OK |
| Capa de composición / vista | 3 / 2 buffers | OK |

### 4.6 Térmica y presupuesto de GPU

La memoria del proyecto lo registra: «la máquina calienta» y la CPU ociosa estaba en
~88 % de un núcleo (2026-10-20). Contribuyen: sondeos de 4 ms en ambos lados (cientos
de despertares por segundo por canal, §3.3), `FrameService::poll` copiando invitados
a 1280×720 en el hilo de la UI cada 16 ms (§2.2), codificación por sesión, 2 GB/s del
retorno (4.1), superresolución y modelos locales en la misma GPU.

- **V9** Térmica y degradación ordenada: leer el estado térmico
  (`NSProcessInfo.thermalState` en Mac; `/sys/class/thermal` y `hwmon` en Linux; en
  Windows, la temperatura y el throttling de GPU por la API del fabricante si está, y
  la frecuencia efectiva de CPU como indicio), publicarlo en `sistema_recursos` y
  aplicar la misma política que el disco en apuros («priorizar programa»,
  `BroadcastEngine.cpp:2010-2060`): primero se apagan superres, modelos locales,
  vistas sin mirar y la resolución de los invitados; nunca el programa ni la
  grabación.
- Presupuesto de GPU por cuadro (33 ms a 30 cps): composición programa + DSK + preview
  + multiview ~5-10 ms; superres ONNX/CoreML 10-30 ms por entrada; un modelo local
  puede tomar la GPU entera. Medida práctica: los mensajes QoS/«late» del agregador y
  la edad del programa (`MedidorEdad`, §6) como alarma de «GPU sin margen».
- **V8** Avisos por evento en vez de sondeo: semáforo con nombre (POSIX `sem_open`,
  disponible en Mac y Linux) o evento con nombre en Windows, señalado por el escritor
  de cada canal. −2 ms medios por salto y miles de despertares menos por segundo.
- **V10** Cuadros por segundo de fábrica según la red eléctrica: en Argentina (50 Hz),
  25 o 50 cps evitan el parpadeo de luces en cámara; y detectar cámaras a 29,97 en un
  lienzo de 30/1 (`videorate` duplica un cuadro cada ~33 s: salto visible en paneos).

---

## 5. Disponibilidad

### 5.1 Qué le pasa al aire ante cada falla

| Falla | Qué pasa hoy (según el código) | Aire | Propuesta |
|---|---|---|---|
| Cae A (motor + UI + MCP en el mismo proceso) | latido quieto 5 s o salida → relanzar con backoff 0/1/2/5/10 s y `--restore` (§1.3) | **corte**: detección (0-5 s) + arranque de GStreamer y motor (varios segundos, Apéndice A 12) + reconexión RTMP; grabación en archivo nuevo | D6, D12 |
| UI trabada (QML, MCP, E/S en el hilo de la interfaz, §8.2) | se marca a los 2 s; relanza si pasan 60 s **y no hay aire** (`LatidoAire.hpp:36-40`) | el aire sigue (bien), el operador queda sin mando | D6 |
| Cae B | relanzar sin preguntar; A se resuscribe (§1.5) | programa, grabación y transmisión siguen; invitados se cortan y reconectan | (bien) |
| GPU: TDR en Windows, contexto GL perdido, driver | no hay manejo de «device removed» en el relevamiento | probable caída de A o imagen congelada; `program_frozen` avisa a los 30 s (§7.2) | D7 |
| Codificador que falla | `atenderEncoderCaido` vacía; reintenta con la misma lista (§13) | transmisión en bucle de error; grabación muerta | D2 |
| Grabación con error (disco, muxer) | registra el error y **no rearranca** (`Salidas.cpp:527-542`); alarma `record_down` a 5 s | se pierde la grabación hasta que alguien actúe | D1 |
| Disco lento | colchón comprimido 60 s / 512 MB; «disco en apuros» pausa ISO, repetición y registro (§7.1) | bien | D10 |
| Disco lleno / desconectado | alarma a 5 GB (§7.2); la copia en otra carpeta sigue si es otro disco | depende de dónde esté la copia | D10 |
| Red de subida | reconexión 1-2-4-8-10 s; destinos extra (§7.1) | corte mientras dure | D9, D14 |
| Placa de audio desconectada | vigía de dispositivos cada 3 s, rearmado (§2.2, §4.5); el bus sigue con las demás | silencio de esa fuente; `audio_silence` avisa | D13 |
| Cámara desconectada | placa «sin señal» a 3 s; escena de respaldo opcional (§4.5, §7.1) | bien | — |
| Corte de luz | al volver: escena y salidas desde `al-aire.json` (§1.6) | depende de que el archivo sobreviva (sin `fsync`) y de que la máquina **arranque sola** | D3, D4, D5 |
| Cuelgue del sistema operativo | nada (el launcher también está colgado) | corte indefinido | D8 |

### 5.2 Puntos únicos de falla

Hardware: la máquina (fuente, placa madre, RAM), la GPU, el disco de grabación (salvo
copia en otro disco físico), la placa de audio principal, la interfaz de red de
subida, la energía. Software: el proceso A (aire + UI + MCP + agentes), el driver de
GPU, el codificador elegido, el `Registro` global (un `fflush` trabado traba a quien
registre, §8.2).

### 5.3 Grabación y codificador

- **D1** Grabación que se rearranca: en `Grabacion::tic`, ante `GST_MESSAGE_ERROR`,
  desarmar y volver a armar con backoff (0,5-1-2-5 s) en un archivo
  `…-parte<N>.mkv`; mantener la copia independiente; registrar el hueco en la bitácora
  del evento. Si el error es del disco, probar la carpeta de la copia como destino
  principal.
- **D2** Codificador caído: llenar `atenderEncoderCaido` (o hacerlo en el `tic` de cada
  salida): marcar la fábrica que falló como no disponible por N minutos y armar con la
  siguiente de la lista (`Salidas.cpp:40-70`), terminando en `x264enc` con
  `speed-preset` según la CPU libre. Aplicar también a la grabación.
- `EncoderConfig::rateControl = "CBR"` no llega al motor (§4.10, §13): para RTMP/SRT
  hace falta CBR (o VBR con tope) real; con VBR libre los picos de bitrate son la
  primera causa de «stream_health» en enlaces justos.

### 5.4 Escrituras y corte de luz

- **D3** `al-aire.json`, `app.json`, `gateway.json`, `launcher.json`: temporal +
  `rename` **sin `fsync`** (§7.4); `config::guardar` **trunca en el lugar** (8 sitios,
  `ArchivoConfig.cpp:16-22`). Tras un corte, en ext4/APFS/NTFS se puede encontrar el
  archivo vacío o viejo; en **exFAT/FAT** (un disco USB, natural en una app portable)
  sin journal, cualquiera de las dos cosas. Propuesta: una sola función de escritura
  atómica para todo, la de `SnapshotStore` (temporal + `fsync`/`FlushFileBuffers` +
  `rename` + `fsync` del directorio, `SnapshotStore.cpp:393-450`), con `F_FULLFSYNC`
  en Mac (el `fsync` de macOS no vacía la caché del disco). Avisar en Configuración si
  la carpeta de datos está en un sistema de archivos sin journal.
- **D4** Grabación ante corte: MKV de un solo archivo (`record.tramoMin` = 0) se puede
  abrir, pero sin `fsync` se pierde lo que tenga la caché del sistema (segundos a
  decenas de segundos) más el colchón en RAM. Propuesta: `fdatasync` periódico cada
  1-2 s desde un hilo aparte sobre el descriptor del archivo (sumidero propio
  envolviendo `filesink`, o propiedad en el elemento de escritura), y tramos de
  10-15 min de fábrica para que un archivo dañado no se lleve todo.

### 5.5 Energía, UPS y arranque solo

No hay código que lea la fuente de energía (grep de `battery`, `UPS`, `IOPS`,
`GetSystemPowerStatus` en `src/`: nada). **D5**:

1. Detectar UPS/batería: macOS `IOPSCopyPowerSourcesInfo` (los UPS USB HID aparecen
   solos), Windows `GetSystemPowerStatus` (ídem con UPS HID), Linux
   `/sys/class/power_supply` (UPS HID con `upower`; NUT queda como integración opcional).
2. Alarma «en batería» y política: a X % o Y minutos, finalizar grabaciones con EOS
   (archivos cerrados), seguir transmitiendo hasta el final y dejar `cerrado=false` para
   que al volver la luz la retoma haga lo que el dueño decidió (todo vuelve solo).
3. Verificar y avisar lo que el software no controla pero rompe «vuelve solo»:
   arranque automático al volver la energía (BIOS «Restore on AC power loss»; Mac
   `pmset autorestart 1`), inicio de sesión automático, la app en el inicio. Mostrarlo
   en una tarjeta de «preparación para corte de luz».

### 5.6 Proceso de aire separado

**D6**: el motor y las salidas en un proceso «de aire» mínimo, y la interfaz QML, el
MCP y los agentes en otro. Ya existe `OperatorEngine` (modo sin interfaz) con
`SnapshotStore` (§7.4); el launcher ya sabe vigilar procesos. Una falla de QML, de una
herramienta MCP (corren en el hilo de la interfaz, §3.5) o de un plugin deja de
cortar el aire. Es la mejora de disponibilidad más grande y la más cara.

**D12**: medir y bajar el tiempo de retoma tras una caída de A (registro de plugins de
GStreamer ya escaneado, motor que arranca las salidas antes que la interfaz, primera
clave del codificador inmediata). Meta: < 3 s de negro.

### 5.7 GPU, sistema operativo y red

- **D7** GPU perdida: en D3D12, consultar `GetDeviceRemovedReason` cuando un elemento
  falla; en GL, errores de contexto. Rearmar el motor sobre un dispositivo nuevo o
  caer a CPU a menor resolución, con la placa de seguridad mientras tanto.
- **D8** Watchdog de hardware opcional en Linux (`/dev/watchdog`, iTCO/sp5100): el
  launcher lo alimenta; si el sistema se cuelga, la placa reinicia. En Windows y Mac no
  hay uno genérico: documentar equipos con watchdog en BIOS o un enchufe inteligente
  que corta y repone si no ve el latido por la red.
- **D9** Respaldo de ingest: YouTube, Twitch y Facebook aceptan una URL de respaldo con
  la misma clave; con V6, una segunda `Transmision` con el mismo flujo codificado.
- **D14** Red: marcar DSCP (EF para audio, AF41 para video) en WebRTC y SRT;
  soportar una segunda interfaz de subida (cable + 4G/5G) eligiendo por destino.
- **D11** (Windows) Avisar plan de energía que no sea «alto rendimiento», suspensión
  selectiva de USB activa (causa clásica de cortes de placas de audio USB) y
  suspensión de la placa de red.
- **D13** Placa de audio que desaparece: probar con un dispositivo virtual que se
  quita (sin sonido real) que la escucha, la Matriz y la entrada se rearmen solas.

---

## 6. Equipo recomendado: qué soportar y qué detectar

| Equipo | Recomendación | Qué debería hacer el sistema |
|---|---|---|
| Placa de audio | Class-compliant USB de reloj estable (RME, MOTU, Focusrite, Audient) o AES67/Dante con PTP; 48 kHz nativos; monitoreo directo por hardware para talkback y sala | medir deriva (P10) y reanclajes; avisar si el dispositivo no está a 48 kHz (hoy remuestrea en silencio); si hay PTP, ofrecer el reloj `ptp` del mismo dominio |
| Word clock / PTP | Una sola fuente de reloj para todas las placas; PTP para AES67 | `gst-ptp-helper` necesita permisos en 319/320 (§6): detectar y explicar; mostrar «enganchado» por deriva ≈ 0 |
| Captura de video | Cámaras USB UVC en modo sin compresión o placas HDMI/SDI (Magewell, Blackmagic); para SDI, DeckLink con entrada de referencia | soportar `decklinkvideosrc`/`decklinkvideosink` (GStreamer, carga el driver del fabricante) para entrada SDI con audio embebido y salida de programa con genlock |
| Cámaras IP | RTSP/SRT con NTP del mismo servidor que el estudio | mostrar qué entrada fija L_e (P06); latencia por cámara |
| GPU | Codificador por hardware con sesiones suficientes (Intel QSV, AMD AMF/VCN, NVIDIA profesional o driver sin tope) | contar sesiones abiertas y avisar antes de chocar el límite; V6 baja la cuenta |
| Discos | NVMe interno para grabar; copia en **otro disco físico**; nada de discos SMR | avisar si grabación y copia están en el mismo dispositivo (`st_dev` / número de serie del volumen); mostrar horas restantes al bitrate actual (6 Mbps ≈ 2,7 GB/h por grabación, más la copia y las ISO); avisar FAT/exFAT |
| Red | Cable gigabit; VLAN aparte para cámaras; segunda salida (4G/5G) | D14; pérdida y RTT por destino en `stream_health` |
| Energía | UPS de línea interactiva o doble conversión con USB HID, autonomía ≥ 15 min | D5 |
| Gabinete y térmica | Equipo con ventilación activa; las Mac sin ventilador bajan frecuencia con carga sostenida | V9 |
| Iluminación | En 50 Hz, lienzo 25/50 cps | V10 |

---

## 7. Tabla de propuestas

Ganancias estimadas; costo S (horas a 2 días), M (una semana), L (varias semanas).

| Id | Problema | Evidencia | Propuesta | Ganancia | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|---|
| P01 | Lector del retorno más lento que el escritor: anillo lleno y bloques descartados | `MediaPump.cpp:516-579`; `SharedAudioRing.hpp:141-146`; Apéndice A 5 | `sleep_until` absoluto, drenar a nivel objetivo 2-3 bloques, leer programa y mix-minus por sello | −130 ms en retorno e IFB; sin cortes | S | bajo | test nuevo `test_retorno_ritmo` (escritor sintético 100 bloques/s, 60 s: overrun = 0, ocupación ≤ 3); `overrunCount` en el registro |
| P02 | `sleep_for` a 15,6 ms en Windows; B sin ventana | grep sin `timeBeginPeriod`; `MediaPump.cpp:593`, `765`, `806`, `858` | temporizador de alta resolución + sin throttling de temporizador | evita perder 1/3 del audio en Windows; −8 ms medios por salto de video | S | bajo | mismo test en la PC Windows; período medido del hilo |
| P03 | Hueco del mix-minus → programa entero con la voz del invitado | `MediaPump.cpp:527-531` | repetir con fundido o silencio; nunca el programa | sin eco del propio invitado | S | bajo | test con mix-minus sintético con huecos: correlación con la voz del invitado = 0 |
| P04 | Mix-minus solo en la salida por omisión | `Motor.hpp:50-53`; `MediaPump.cpp:470-472` | `retorno<n>:<salida>` y slots por salida | invitados de todas las salidas sin oírse | M | medio | `test_motor_retornos` con dos salidas |
| P05 | Monitoreo y retornos esperan L_e (hasta 250) + márgenes | §5.4, §5.7, 2.1 | buses de oído con latencia fija ~25 ms (como `directo`) | −(L_e + 20..60) ms: −70 a −290 ms en retorno y auriculares | M | medio (lo tardío no entra al retorno) | `test_lazo_latencia` (pulso sintético entrada→`baanillosink`) |
| P06 | Una entrada lenta sube todo el estudio | `Reloj.hpp:135`; §5.9 | mostrar qué entrada manda; tope configurable por evento («modo baja latencia» 100-120 ms) dejando a la cámara IP en retraso propio | −100..150 ms en escenario C | S | medio (la cámara IP queda corrida: compensar con `desfase`) | `test_latencia_estudio`; `tiempos_pipeline` |
| P07 | Margen +300 ms en todas las salidas | `Salidas.cpp:56` | margen por tipo: SRT 60-100, RTMP 300, grabación 300-1000 | −200..240 ms en SRT | S | bajo | edad del cuadro a la salida (`MedidorEdad`); cuadros tardíos en el sink |
| P08 | Latencia del rack no informada; puenteo cambia latencia | `PLAN-TIEMPOS.md:27`; `AudioRack.cpp:860-880`, `1156-1170` | LATENCY = `latenciaTotal_`, PTS corregido, retardo equivalente al puentear, registro sin candado desde el hilo de audio | sincronía correcta con plugins de 5-100 ms | S | bajo | `test_audio_rack` + plugin sintético con latencia 256 muestras |
| P09 | Reanclaje a 50 ms sin remuestreo adaptativo | `Piezas.cpp:711-790` | DLL + remuestreo variable (libsamplerate) o reloj del estudio = placa principal | sin saltos de 50 ms cada 8-17 min; A/V ±2 ms | M | medio (calidad del remuestreo, CPU) | fuente sintética a 48 000·(1+100 ppm): reanclajes = 0 en 1 h |
| P10 | Deriva desconocida | Apéndice A 10 | ppm y reanclajes por dispositivo en `sistema_recursos` | diagnóstico | S | nulo | el propio dato |
| P11 | Matriz en Qt Multimedia: otro reloj, candados, sin control de período | §5.7, §5.8; `RingEstereo.hpp:23-52` | Matriz en el motor | −20-30 ms; sin clicks por recorte | M-L | medio | `test_matrix_mixer` + deriva sintética |
| P12 | Audio sin prioridad de tiempo real | §2.4 | MMCSS «Pro Audio» / time constraint / RealtimeKit solo en hilos de audio | menos cortes bajo carga (superres, modelos) | S-M | medio (inversión de prioridad si hay candados) | contadores de underrun/overrun con carga sintética de CPU |
| P13 | Escucha en Windows sin `low-latency`; buffers no fijados | `Escucha.cpp:20-30`; §5.2 | `low-latency=true`, `latency-time` explícito e informado | −10..30 ms en auriculares | S | bajo | `tiempos_pipeline` |
| P14 | Retorno con audio 70-230 ms atrás del video | 2.5; `MediaPump.cpp:576`, `651-702` | alinear por sellos QPC en B o pasar sellos a libwebrtc | labios < 20 ms | M | bajo | `audio_sincro_medir` / `test_sincro_labios` sobre el retorno (destello + pulso sintéticos) |
| P15 | Loudness solo medido; techo fijo | `OperateMode.cpp:630`; `limitador.cpp` | AGC lento opcional; −2 dBTP con AAC < 128 kbps; limitador en pistas publicadas | entrega estable a −23/−16 LUFS | S | bajo | `test_loudness` con programa sintético de nivel variable |
| P16 | PFL/selección sigue a la salida por omisión y espera bus + 40 | `Motor.hpp:323`; `Escucha.cpp:32` | PFL/AFL por entrada, por salida, latencia fija | −60..290 ms en PFL | M | bajo | `test_lazo_latencia` |
| V1 | Programa codificado por sesión; 11 pasos | §3.7; `Webrtc.cpp:238-250`, `1118-1131` | codificar una vez en A (GStreamer, hw) y B solo empaqueta | −15..40 ms; −0,05..0,1 núcleo por sesión | L | alto (adaptación por sesión, claves) | `sistema_recursos` con 16 sesiones sintéticas; edad A→B (`MediaPump.cpp:663-700`) |
| V2 | ~67 MB de copias por cuadro para un retorno de 640×360 | §3.7; `Config.hpp:83-84`; `Vistas.cpp:50-90` | vista al tamaño de sesión, BGRA en GPU, una copia | −95 % de tráfico (−~1,9 GB/s), −3..6 ms | S | bajo | `motor_mapa`; perfil de CPU con `sample` |
| V3 | Invitados agrandados en CPU al slot 1280×720 | `MediaPump.cpp:1063-1160`; `Config.hpp:79` | tamaño real en la cabecera; escalar en GPU | −CPU en B y A; −subidas grandes | S-M | bajo | ídem |
| V4 | IPC de video por CPU | §13; `SpoutTransport.cpp:55-77` | IOSurface / D3D12 shared handle / DMA-BUF | −copias a resolución completa (pantallas) | L | alto (sincronización entre procesos y GPU) | `test_video_cross_process` |
| V5 | Linux compone en CPU | `Piezas.cpp:1020-1021` | backend GL sobre EGL + DMA-BUF | −CPU, −calor, más capas | L | medio (drivers) | `motor_mapa` en Linux |
| V6 | Un codificador por destino; copia con codificador propio | `BroadcastEngine.cpp:1664-1676`, `1867-1879` | un codificador por salida/perfil con `tee` | −1..3 sesiones por salida; evita el tope NVENC | M | medio (bitrates distintos) | sesiones abiertas por salida |
| V7 | Cola cruda de grabación de 3 s | `Salidas.cpp:351` | 0,5-1 s; colas de transmisión coherentes | −~200 MB de RAM/VRAM | S | bajo | memoria por proceso |
| V8 | Sondeo de 4 ms en ambos lados | §3.3 | aviso por semáforo/evento con nombre | −2 ms medios por salto; −miles de despertares/s | M | medio (portabilidad del aviso) | `sample` por hilo (memoria «CPU ociosa») |
| V9 | Sin dato térmico ni degradación | §7.3 | estado térmico + «priorizar programa» | evita bajones de frecuencia en el aire | M | bajo | `sistema_recursos` |
| V10 | 30 cps en red de 50 Hz; 29,97 vs 30 | §4.3 | lienzo según región; aviso de cámara a 29,97 | sin parpadeo ni saltos de `videorate` | S | bajo | contador de duplicados de `videorate` |
| D1 | Grabación caída no se rearranca | `Salidas.cpp:527-542` | rearmar en «parte N» con backoff | de «hasta que alguien vea» a 1-5 s de hueco | S-M | bajo | test con `filesink` que falla (sumidero sintético) |
| D2 | Codificador caído no se reemplaza | `BroadcastEngine.cpp:1863-1865`; `Salidas.cpp:52-70` | descartar la fábrica que falló y seguir con la lista | de bucle de error a segundos | S | bajo | test con un codificador falso que da error |
| D3 | Estado y configuración sin `fsync`; ocho que truncan | §7.4 | escritura atómica única (la de `SnapshotStore`), `F_FULLFSYNC` en Mac, aviso de FAT/exFAT | «vuelve solo» confiable tras corte | S | bajo | test que mata el proceso en mitad de la escritura en bucle: el archivo siempre es JSON válido; prueba real con corte en la PC de pruebas |
| D4 | Grabación sin `fsync`; un solo archivo | `BroadcastEngine.cpp:1647`; §13 | `fdatasync` cada 1-2 s; tramos de 10-15 min | pérdida ante corte: de decenas de s a ~2 s | S-M | bajo (E/S extra mínima) | test de corte simulado + lectura del MKV |
| D5 | Sin UPS ni arranque solo verificado | grep sin APIs de energía | detectar batería/UPS, cierre limpio, chequeo de arranque automático | sobrevive cortes largos sin archivos rotos | M | bajo | UPS simulado (estado de energía falso en test) |
| D6 | Aire, UI y MCP en un proceso | §2.2, §3.5 | proceso de aire aparte (`OperatorEngine`) | fallas de UI/MCP/plugins sin corte | L | alto | inyección de fallas: `abort()` en la UI con aire sintético corriendo |
| D7 | GPU perdida sin manejo | §4.2 | detectar y rearmar o caer a CPU | de caída a segundos de placa | M-L | medio | TDR forzado (`dxcap -forcetdr`) en la PC Windows |
| D8 | Cuelgue del sistema sin recuperación | — | watchdog de hardware opcional (Linux) | recupera cuelgues del SO | S | medio (reinicios no deseados) | prueba con `echo c > /proc/sysrq-trigger` en una máquina de prueba |
| D9 | Ingest único | §7.1 | URL de respaldo con el mismo flujo | supera caída de un punto de ingest | S | bajo | destino de respaldo local (servidor RTMP de prueba) |
| D10 | Copia en el mismo disco; sin horas restantes | §7.1, §7.2 | avisos de mismo dispositivo y horas restantes | evita perder ambas copias | S | nulo | test de rutas en el mismo volumen |
| D11 | Ajustes de Windows que cortan USB | — | chequeo de plan de energía y suspensión selectiva | menos cortes de placas USB | S | nulo | tarjeta de preparación |
| D12 | Tiempo de retoma sin medir | Apéndice A 12 | medir y bajar; salidas antes que la interfaz | meta < 3 s de negro | M | medio | `kill -9` de A con aire sintético; cronometrar hasta el primer cuadro en la salida |
| D13 | Placa de audio que desaparece | §2.2 | probar rearmado con dispositivo virtual | confianza | S | nulo | test con dispositivo virtual |
| D14 | Sin QoS de red ni segunda subida | §3.4 | DSCP, segunda interfaz por destino | menos pérdida en redes compartidas | S-M | bajo | `stream_health`, estadísticas WebRTC |

---

## 8. Top 10 priorizado

Orden por (daño en el aire hoy × probabilidad) / costo.

| # | Ids | Por qué primero | Costo | Ganancia |
|---|---|---|---|---|
| 1 | **P01 + P02** | Bug probable que **corta la voz** de todos los retornos e IFB y agrega ~150 ms; peor en Windows. Se confirma en minutos leyendo `overrunCount` | S | −130 ms, sin cortes |
| 2 | **P03** | Eco del propio invitado cada vez que el mix-minus tiene un hueco (y hoy los tiene por P01) | S | sin eco |
| 3 | **D3** | «Corte de luz = todo vuelve solo» depende de archivos que hoy pueden quedar vacíos; el arreglo ya está escrito en `SnapshotStore` | S | retoma confiable |
| 4 | **D1 + D2** | Una grabación o un codificador que fallan quedan muertos sin aviso de acción; el comentario de `OperateMode.cpp:780` promete lo contrario | S-M | segundos en vez de «hasta que alguien vea» |
| 5 | **P05** | La conversación con invitados es lo que más sufre la latencia; los retornos no necesitan esperar al video | M | −70..290 ms de retorno y auriculares |
| 6 | **P08** | Sincronía y filtro peine con cualquier plugin con lookahead; el dato ya existe (`latenciaTotal_`) | S | A/V correcto con rack |
| 7 | **P10 → P09** | Saltos de 50 ms periódicos en el programa y la grabación; primero medir (S), después corregir (M) | S + M | sin cortes por deriva |
| 8 | **P04** | N salidas como operadores completos: hoy los invitados de las otras salidas se oyen a sí mismos | M | mix-minus en todas las salidas |
| 9 | **V6 + D9** | Menos sesiones de codificador (evita el tope NVENC y el calor) y respaldo de ingest casi gratis | M + S | −1..3 sesiones por salida; ingest redundante |
| 10 | **D5** | Sin UPS detectado ni arranque automático verificado, un corte largo rompe archivos o deja la máquina apagada | M | sobrevive cortes |

Siguientes en la cola: **V2** (barata, gran ahorro de memoria y calor), **P12** + **P13**
(tiempo real de audio), **P14** (labios del retorno), **D6** (proceso de aire aparte:
la mejora de disponibilidad más grande, pero L), **V1** (codificar el retorno una vez).

---

## 9. Cómo medir sin parlantes

Todas las pruebas con señales sintéticas y sumideros que no suenan (`fakesink`,
`appsink`, anillos leídos por el test), como pide el dueño.

| Medición | Cómo | Herramienta existente o nueva |
|---|---|---|
| Ritmo y ocupación del anillo A→B | escritor `baanillosink` con `audiotestsrc is-live=true`; lector = el hilo de retorno de B; 60 s; leer `overrunCount`, `underrunCount` y ocupación | nueva `test_retorno_ritmo` (gateway); además log cada 10 s en vivo |
| Latencia de lazo de audio por camino | `audiotestsrc wave=ticks` en una entrada → bus → `baanillosink`/`appsink`; correlación pulso a pulso contra la hora del sello | nueva `test_lazo_latencia` (motor) |
| Latencia del estudio | ya cubierta | `test_latencia_estudio`, `test_retraso_propio` |
| Deriva | fuente sintética a 48 000 × (1 ± 100 ppm) (remuestreada en el test); contar reanclajes y medir desfase | nueva `test_deriva_audio`; en vivo, P10 en `sistema_recursos` |
| Latencia del rack | plugin CLAP sintético que declara 256 muestras; LATENCY y PTS | `test_audio_rack` / `test_clap_host` ampliados |
| Sincronía A/V del retorno | entrada `tipo=color` que destella junto con un pulso; medir en lo que B empuja a la sesión | `audio_sincro_medir`, `test_sincro_labios`, `video_sincronia` |
| Copias y memoria por rama | mapa de memorias por enlace | `motor_mapa`; `sample` por hilo (memoria «CPU ociosa») |
| CPU, GPU, térmica, discos | 1 Hz, historial 10 min | `sistema_recursos` (ampliar con térmica, deriva, sesiones de codificador) |
| Tiempos por pipeline y plugin | latencias declaradas en vivo | `tiempos_pipeline`, `tiempos_plugin`, `tiempos_medir` |
| Retoma tras caída | `kill -9` de A con salidas a `fakesink`/servidor RTMP local; tiempo hasta el primer cuadro | nueva prueba de integración del launcher |
| Escritura atómica | matar el proceso en mitad de `al-aire.json`/`app.json` en bucle | nueva, junto a `test_config_persistida`, `test_snapshot_store` |
| Grabación y codificador que fallan | sumidero que falla a los N s; codificador falso que da error | nuevas, junto a `test_salidas`, `test_motor_salidas` |
