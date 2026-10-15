# Plan: «Tiempos», el análisis de demoras del pipeline

Una página nueva que muestra **cuánto tarda cada cosa**, de cada entrada a cada salida,
en video y en audio, módulo por módulo y plugin por plugin. Y, en lo más extremo, el
retardo **real** hasta lo que ve la gente: el televisor de la sala, YouTube.

## Qué hay hoy (relevamiento del 2026-10-15)

Ya se mide más de lo que se ve, pero cada cosa en un lugar distinto:

| Tramo | Qué se mide | Dónde |
|---|---|---|
| Entrada | edad de cada cuadro al llegar al estudio (P50/P95), latencia declarada, lo que pide al reloj, «llega tarde» | `Entrada::alSalir` / `medirLatencia` (src/motor/Entrada.cpp) |
| Tarjetas propias | `ms-por-cuadro` (superres, onnx/coreml/vt, mejora, rostros) | `Entrada::estadoFiltros`, `VideoGrafos::demorasDe` |
| Grafo de la entrada | `latencia_grafo_ms` (suma estimada) | VideoGrafos.cpp:1615 |
| Composición / mezcla | latencias **fijadas** (`min-upstream-latency`) y de salida, no medidas | Composicion/Mezcla `estado()` |
| Racks de audio | ms por bloque de cada plugin, latencia CLAP acumulada | AudioRack.cpp:1158, 1240 |
| Sincronía | aplauso (programa), huellas de luma (entrada contra entrada) | SincroLabios, Sincronia |
| Salidas | grabación `atraso_s`; transmisión kbps/perdidos; salud de YouTube por la API | Salidas.cpp, `stream_salud` |
| Televisor (cast) | buffer, proceso, RTT, jitter… **como texto suelto** cada 30 s | assets/web/cast/app.js, gateway.log |
| Topología real | todos los pipelines, elementos, enlaces y memoria CPU/GPU | `motor_mapa` (src/motor/Mapa.cpp) |

Lo que **falta**:

1. **Edad en cada tramo.** Hoy se mide en un solo punto (el intersink de la entrada).
2. **Video y audio por separado.** `edadesA_` existe pero no se publica.
3. **Tiempo de elementos de GStreamer comunes** (decodificador, videoconvert, gl*).
4. **La latencia del rack de audio en GStreamer.** `barack` no contesta la consulta de latencia, así que el audio procesado no se compensa contra el video.
5. **Codificador.** No se sabe cuánto tarda.
6. **Televisor estructurado.** Hoy sus datos llegan como texto.
7. **Punta a punta.** No hay nada que mida hasta YouTube ni hasta la pantalla.
8. **Historia.** Solo existe el momento actual.

## La idea: un mapa que late

### Modelo: un grafo de tiempos (`TiemposModel`)

Un solo modelo C++ arma el grafo de lo que está andando, con **nodos** y **tramos**:

- **Columna vertebral:** `Mapa` del motor (pipelines, elementos, quién lee y quién escribe a quién). Encima se pegan las tarjetas de VideoGrafos (las que ve el operador) y los módulos de los racks de audio.
- **Nodo:** una entrada, una tarjeta o plugin, la composición de una escena, la mezcla, un codificador o una salida (transmisión, grabación, televisor, invitado, NDI). Cada nodo tiene:
  - qué es;
  - cuánto **agrega** (P50, P95, máximo);
  - cuánto **acumula** desde la fuente;
  - si es **medido** o **declarado** (se dibujan distinto);
  - su historia (60 s a 10 Hz, más 10 min a 1 Hz);
  - su detalle (caps, memoria CPU/GPU, motor usado, errores).
- **Tramo:** de un nodo a otro, con su demora y si es de video, de audio o de los dos.
- **Por MCP:** `tiempos_mapa` y `tiempos_nodo` (el asistente ve lo mismo que el operador) y `tiempos_medir` (correr un análisis).

Sigue la regla de MVC de siempre: el modelo avisa y la vista se engancha a propiedades y roles, nunca a Q_INVOKABLE que se leen una vez.

### Mediciones, de la más barata a la más extrema

**Nivel 1: siempre prendido, casi gratis.** Se publica lo que ya hay, más:
- edad de video y de audio por separado en cada entrada;
- `ms-por-cuadro` de cada tarjeta;
- latencias declaradas;
- `atraso_s` de grabaciones y colas;
- los datos del televisor, ahora como JSON y no como texto (app.js manda un objeto; el gateway lo pasa a la app).

**Nivel 2: sondas por tramo, a pedido.** Al apretar «Analizar», el motor pone sondas de buffer (`gst_pad_add_probe`) a la entrada y a la salida de cada elemento de interés:
- fuente y decodificador;
- cada `ba_f_*`;
- entrada y salida del compositor;
- entrada y salida del codificador;
- muxer y sink.

Cada sonda anota el *running time* del reloj del estudio contra el PTS. Como todos los pipelines comparten reloj y base_time, las restas valen entre pipelines distintos. Se miden N segundos y se sacan. Así se obtiene cuánto tarda cada elemento, también los comunes de GStreamer, sin dejar costo prendido.

Además:
- `barack` aprende a contestar la consulta de latencia (la suma de su rack); queda mejor sincronizado aunque nadie abra la página;
- el codificador se mide con la sonda antes y después (cuadro entra, paquete sale).

**Nivel 3: punta a punta con marca de tiempo (lo extremo).** Cada entrada estampa en sus propios cuadros el instante en que se capturaron, **lo antes posible en el pipeline**, de la forma más rápida:

- **Dónde:** en el pad de salida de la fuente (la cámara, el RTSP o el archivo ya decodificado), antes de `capssetter`, de la subida a la GPU y de cualquier tarjeta. Es el primer punto donde hay píxeles.
- **Cómo:** con una **sonda de pad**, no con un elemento nuevo: escribe las celdas sobre el mismo buffer (`gst_buffer_make_writable` + map), sin copiar la imagen ni agregar un hilo. Si el decodificador ya entrega memoria de GPU (GL/D3D12), la marca va en el primer paso de GL, con un shader mínimo que pinta las celdas.
- **Qué tiempo:** el PTS del cuadro en el reloj del estudio, que para una fuente en vivo es el **instante de captura** según GStreamer. Así lo que mide cada lector va desde la captura, no desde algún punto intermedio. La sonda también anota cuándo llegó el cuadro, y la diferencia es lo que tardaron la fuente y el decodificador.
- **Audio:** igual, en el pad de salida de la fuente de audio. Mientras dura el análisis, reemplaza un bloque corto (20 ms, uno por segundo) por un clic FSK con los mismos datos.
- La marca lleva también el **número de la entrada**, así una escena con varias cámaras muestra varias marcas y cada lector sabe de cuál es cada una.
- Mientras no hay análisis, la sonda no está puesta: no cuesta nada.

La marca:
- **Puntos en binario:** una grilla de 11×8 celdas cuadradas en una esquina, cada una blanca (1) o negra (0).
  - **4 celdas de referencia** en las esquinas: dos siempre blancas y dos siempre negras. De ahí sale el umbral, así no importa cuánto cambien el brillo o el contraste en el camino.
  - **84 celdas de datos:**
    - **64 bits: el tiempo del cuadro en nanosegundos**, con toda la resolución del reloj del estudio: el running time (PTS) de ESE cuadro en el reloj de GStreamer, entero, sin redondear y sin vuelta (584 años);
    - 8 bits del número de la entrada;
    - 4 bits de contador de cuadro;
    - 8 bits de CRC. Una lectura que no cierra se descarta: nunca da un número equivocado.
  - Leer es mirar el brillo en el **centro de cada celda** (88 píxeles por cuadro) y comparar contra el umbral. Como la marca la pone el sistema, se sabe dónde está cada celda, en posición relativa al cuadro: sobrevive a que lo escalen.
  - Celdas de unos 16 px a 1080p (176×128 px en total), para que sigan teniendo al menos 3 px aun si YouTube baja a 360p, y para que la compresión no las mezcle.
  - El lector anota la hora de llegada en ns del mismo reloj y guarda los pares crudos (marca, llegada). El resultado sale de muchas lecturas: la mediana y la dispersión. La marca es exacta; la precisión de la medición la pone cada cuánto se puede tomar un cuadro en ese punto (en YouTube, uno por segundo).
- **Dígitos** al lado, solo para mirarlos a ojo. No hace falta OCR.
- Va solo mientras dura el análisis, chica y en una esquina de la imagen de la entrada. En el evento real se activa unos segundos, para medir.

El lector (el mismo código en C++ para el motor y en JS para el navegador) compara la hora pintada con la hora del estudio cuando el cuadro llega. Puntos donde lee:
- **Programa** (vista del motor): sirve para validar la medición contra sí misma; tiene que dar unos pocos ms.
- **Televisor de la sala** (cast): la página de la TV ya recibe el video. El lector va en el navegador (canvas, cada 500 ms) y le contesta al gateway «leí la hora X a la hora Y». La hora de la TV se alinea con un ping (RTT/2), con el mismo esquema que ya usa el operador. Da el retardo **hasta lo que muestra esa pantalla**.
- **YouTube:** se lee el video público del propio stream (HLS por yt-dlp, que ya va en el bundle; ffmpeg saca un cuadro por segundo, solo el recorte del cuadrito). Da el retardo real hasta el espectador, incluido el modo de latencia elegido (normal, baja o ultra baja). Para que no gaste:
  - es un muestreo de 20-30 s, a pedido;
  - lo que se baja es nuestro propio video, y se trata como dato no confiable, igual que cualquier otra descarga.
- **Invitados WebRTC:** el lector va en la página del invitado, igual que en la TV. Así el presentador remoto sabe con cuánto atraso nos ve.
- **Audio:** la misma idea con un «clic» codificado (un FSK corto de la hora), que se detecta en la salida que corresponda. Va en una segunda etapa.

Mientras hay **algo al aire**, la marca solo se pone con confirmación y queda chica. Es la misma regla de «al aire» (`BroadcastEngine::haySalida`) que ya usa todo lo demás.

## La página

Se llama **«Tiempos»**, en el grupo «Señales y equipos» del riel.

```
┌──────┬──────────────────────────────────────────────────────────────┬─────────┐
│ ▣ Cám│   [Cámara 1]━━━[PTZ]━━━[Superres]━━┓                         │ Total   │
│ ▣ Cám│     12 ms       0.4     18 ms      ┣━━[Escena «Orador»]━━┳━━[YouTube]
│ □ Mic│   [Cámara 2]━━━━━━━━━━━━━━━━━━━━━━━┛      33 ms          ┣━━[TV sala] │
│ ▣ Inv│   [Mic 1]━━[Rack: EQ·Comp]━━━━━━━━━━[Mezcla]━━━━━━━━━━━━━┛  140 ms  │
│ ─────│                                                              │  6.2 s  │
│ ▶ Ana│                                                              │         │
│ ⏺ Mar│                                                              │         │
├──────┴──────────────────────────────────────────────────────────────┴─────────┤
│ Análisis: ● Programa 4 ms ✓   ● TV sala 138 ms ✓   ◌ YouTube midiendo… 18/30 s │
└───────────────────────────────────────────────────────────────────────────────┘
```

- **Lienzo central.** Columnas de izquierda a derecha:
  - entradas;
  - tarjetas y módulos;
  - composición y mezcla;
  - codificación;
  - salidas.

  Se puede hacer zoom y arrastrar; el orden lo calcula el modelo, por capas y sin cruces.
- **Tarjetas de nodo** de vidrio (el mismo lenguaje que Theme/GlassButton):
  - ícono, nombre y la demora grande (P50) con su color: verde si está bien, ámbar si supera lo que pide el reloj, rojo si llega tarde;
  - una **chispa** (sparkline) de los últimos 60 s y la marca de «medido» o «declarado»;
  - **efecto 3D:** perspectiva con `Matrix4x4` y una inclinación que sigue al puntero (paralaje), sombra suave y profundidad por columna. Se hace con MultiEffect y ShaderEffect, sin Qt Quick 3D, que no está en el bundle.
  - Al tocarla **se da vuelta**: el dorso muestra histograma P50/P95/máx, caps, memoria CPU/GPU, motor usado, errores y «qué hacer» (por ejemplo, «este superres agrega 18 ms: probá el modelo chico»).
- **Líneas de luz:**
  - Shapes con un trazo brillante (glow) y un **pulso que viaja** por la línea, cuya velocidad sigue la demora real del tramo. Una línea que tarda más late más lento.
  - Grosor según el bitrate; cian para el video y ámbar para el audio.
  - Una línea apagada indica que no llega nada.
- **Botones laterales (izquierda):**
  - una casilla por entrada para mostrarla u ocultarla, con «solo esta» al hacer doble click;
  - filtros: video, audio, solo lo medido;
  - **▶ Analizar**, que corre el nivel 2 en todo lo visible;
  - **⏺ Marca de tiempo**, que corre el nivel 3 y deja elegir dónde leer (programa, TV, YouTube, invitados).
- **Columna derecha:** el total de cada salida, de punta a punta, con su desglose al tocarlo («de los 6,2 s hasta YouTube, 5,9 s son de YouTube»).
- **Abajo, el estado de los análisis:** una fila por análisis con estado (esperando, midiendo con su progreso, listo, falló y por qué), su resultado, cuándo se hizo y «repetir». Queda en la historia del evento, para comparar antes y después de un cambio.
- **Rendimiento:** la página dibuja solo lo visible y anima con `NumberAnimation`, sin timers de JS. Cerrada no cuesta nada: el modelo deja de pedir datos.

## Fases

| Fase | Qué | Cómo se prueba |
|---|---|---|
| T1 | `TiemposModel` con el nivel 1 (Mapa + tarjetas + racks + lo que ya se mide), `tiempos_mapa`/`tiempos_nodo`, edad de video y audio por separado, limpiar los campos viejos que ya nadie produce (`fps_recibidos`, `tarde_10s`, `compensacion_ms`) | test del modelo con un motor de prueba (barras + una tarjeta) |
| T2 | La página: lienzo, tarjetas con su dorso, líneas que laten, botones laterales, estado abajo | capturas en macOS y Windows, a 60 fps con 8 entradas |
| T3 | Nivel 2: sondas por elemento a pedido, `barack` con consulta de latencia, codificador | test_motor_app: un `identity sleep-time` de 20 ms tiene que medir ≈20 ms en su tarjeta |
| T4 | Televisor estructurado (app.js da JSON), la marca por entrada (sonda en la salida de la fuente; puntos en binario + dígitos) con su lector en el motor (programa) | el programa contra sí mismo da menos de un cuadro; el lector lee la marca recomprimida a 500 kbps y escalada a 360p, y el CRC descarta las lecturas rotas |
| T5 | Lector en la TV y en los invitados (canvas en el navegador) | Chrome con la página cast contra el gateway local |
| T6 | YouTube: muestreo HLS con yt-dlp y ffmpeg, recorte y decodificación del código | un stream de prueba propio; en el evento, solo a pedido |
| T7 | Audio punta a punta (clic FSK codificado) | señal sintética, sin sonar por los parlantes |

Decidido (2026-10-15): la marca va en cada entrada, lo antes posible (la salida de la fuente), con una sonda de pad; el historial de análisis se guarda por evento.
