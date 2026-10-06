# Plan del todo en uno (Parte I de obs-gstreamer-migration.md)

Tareas precisas para terminar el aparato en **una sola máquina**, sacadas de
[obs-gstreamer-migration.md](obs-gstreamer-migration.md) §1–§10. **No incluye** la Parte II (servidor avanzado,
dos máquinas, Congreso a escala): eso es otro bundle, después.

**A futuro (no ahora):** separar la toma de medios (Linux) del cliente/GUI (Linux o Mac). Se hace cuando el todo
en uno ande. Mientras tanto, regla para no cerrarse esa puerta: lo nuevo del motor no habla con la GUI por
punteros sino por el MCP / el bus de estado, así después puede ir por la red.

Estados: ⬜ falta · 🟡 en curso · ✅ hecho (con commit) · ⏸ necesita al usuario o hardware.

---

## Bloque P0 — Antes de la primera sesión real (hecho sobre la app de hoy)

Orden de ataque: primero lo barato que mejora el aire, después lo grande.

| ID | Tarea | Dónde | Cómo se sabe que anda | Estado |
|---|---|---|---|---|
| P0.1 | **El reloj nunca lo pone un dispositivo.** Confirmar que ninguna cadena toma el reloj de un `osxaudiosrc`/`wasapi2src`/`pulsesrc` (hoy `gst_pipeline_use_clock` fuerza el del estudio) y poner `provide-clock=false` igual, como cinturón | `ba_gst_fuente.cpp` (`armar`), `audio/EntradasAudio.cpp` | Desenchufar el micrófono no manda CLOCK_LOST a nadie; prueba con `gst-launch` | ✅ |
| P0.2 | **Entregarle a libobs cada cuadro y bloque con su hora de captura**, no con la de entrega: hora del pipeline (base + PTS en tiempo de ejecución) traducida a `os_gettime_ns` leyendo los dos relojes juntos | `presentarVideo`/`presentarAudio` en `ba_gst_fuente.cpp` | Con una cámara y su micrófono: el desfase medido no cambia y el temblor baja (video_estado / medición de sincronía) | ✅ |
| P0.3 | **Elementos imprescindibles al arrancar**: revisar con `gst_element_factory_find` (`intersink`, `intersrc`, `fallbackswitch`, `livesync`, `audiomixer`, `compositor`, `ebur128level`, `avdec_speedhq`, `baanillo`, `bavideoanillo`, `barack`, `bavoz`, `bandisrc`, `baautomixer`, los de subtítulos) y avisar en Diagnóstico y por MCP | nuevo `diag/ElementosGst.*`, tool `gst_elementos` | En Mac todos «sí»; si falta uno, aviso claro (no silencio o negro) | ✅ |
| P0.4 | **Tapar el hueco sin corte**: al final de cada entrada en vivo (cámaras, red, micrófonos, invitados, NDI), `livesync` y un respaldo cuando no llega nada: video = último cuadro unos segundos y después placa «<entrada> sin señal»; audio = silencio con fundido. Vuelve solo con el primer dato bueno | `ba_gst_fuente.cpp` (rama de video y de audio antes del appsink) | Desenchufar la cámara: placa con su nombre en vez de negro/congelado; enchufar: vuelve sola. Un microcorte de 100 ms no se nota | ✅ (placa; `livesync` queda para E3) |
| P0.5 | **Vigía de datos**: si una entrada en vivo no entrega nada en N s sin haber dado error (dispositivo colgado), se rearma; reintento con espera creciente 0,2 / 0,5 / 1 / 2 s y después cada 2 s (hoy fijo 2 s) | `ba_gst_fuente.cpp` (`tic`, `reintento`) | Simular un dispositivo que deja de mandar: se rearma solo; el registro lo dice una vez | ✅ |
| P0.6 | **Tope a la latencia del estudio**: la compensación no pasa de un tope (de fábrica 250 ms, ajustable); una entrada que pide más queda con su propia demora y un aviso «llega tarde»; interruptor `estudio` por entrada en GUI y MCP; **medidor** «latencia del estudio: N ms» (verde / rojo con la entrada que la causa) | `ba_gst_fuente.cpp` (`compensacionEstudio`, `latenciaDelEstudio`), `video_estado`, página Video (Sincronía), MCP `video_entrada_red`/nuevo `estudio_latencia` | Con la cámara con superres: el estudio queda en ≤ tope y esa cámara avisa; el medidor lo muestra | ✅ |
| P0.7 | **Dos escuchas para el operador**: «directo» (la sala sin demora) y «programa» (lo que sale, audio con su video); una tecla para pasar | escucha de la app (auriculares: `RackManager::setAuriculares`, monitoreo de libobs) | Con «programa», el audio que se oye coincide con el video del programa; con «directo», sin demora | → M1.5 |
| P0.8 | **Limitador del máster (true peak) y loudness (T12 + T11)**: limitador en el programa (de fábrica −1 dBTP, se puede apagar), medidor LUFS integrado/corto con objetivo (−14 YouTube / −23 TV) y aviso si se aleja | programa de audio (hoy libobs: filtro en la mezcla; mañana `audiomixer`), `audio/LoudnessPrograma.*` (ya mide) | Un tono a +6 dBFS sale a ≤ −1 dBTP; el medidor LUFS coincide con `ebur128level` | → M1.3 (el medidor LUFS y su aviso ya están) |
| P0.9 | **Alarmas** de silencio (programa N s), negro (N s), cuadro congelado y salida caída (YouTube/SRT/grabación dejaron de mandar), visibles, audibles, en el bus (superficie de control / teléfono) y por MCP | nuevo `diag/Alarmas.*` sobre los medidores del programa y el estado de las salidas | Silenciar el programa 10 s → alarma; cortar la red → «YouTube dejó de recibir» | ✅ 2026-10-06 |
| P0.10 | **Volver como estaba (lo que falta)**: guardar y retomar la escena al aire y la playlist con su posición tras un corte; configuración escrita atómica (temporal + rename); grabación en tramos (MKV) para perder como mucho el último | `OperateMode.cpp` (`al-aire.json`), `AppSettings.cpp` (`saveAppConfig`), salida de grabación | `kill -9` grabando con la playlist andando: vuelve la escena, la playlist sigue donde estaba, la grabación anterior se abre bien | ✅ 2026-10-06 (tramos → E2) |
| P0.11 | **`--test` no arranca al aire**: con `--test`, no se retoma transmisión/grabación al arrancar | `OperateMode.cpp` (`restoreOutputs`) | Arrancar con `--test` tras un corte: no transmite | ✅ |
| P0.12 | **Corrida larga y los otros sistemas**: script de prueba de 8 h (todas las entradas, transmisión a un canal de prueba, mide temperatura, memoria, deriva A/V, cuadros perdidos) y validación en Windows y Linux por SSH | `tools/prueba-larga/`, docs | El informe de la corrida | ⏸ (hardware y una cuenta de prueba) |

## Bloque M — El motor GStreamer (libobs no se usa más)

**Decisión del usuario (2026-10-06):** «libobs no se usa más, no pienses más en libobs, justamente lo estamos
sacando». El motor nuevo se diseña directo en GStreamer:
- sin puentes hacia libobs: nada de «libobs recibe el programa mezclado» ni `appsrc` del lienzo de OBS;
- sin modo de volver a la mezcla de libobs.

Reemplaza el viejo bloque E (que mezclaba de a poco con libobs en el medio) y el P1 «todavía con libobs». Sus
tareas quedan repartidas acá.

**Cómo se construye:**
- El motor vive en `src/motor/`: una biblioteca sin nada de OBS, con sus tests en ctest y una prueba sin GUI (`ba-app --motor-prueba`).
- La app de hoy queda congelada (no se le agrega nada) hasta el cambio (M6). En el cambio, el `BroadcastEngine` de libobs se reemplaza por el motor, detrás de la misma API de `AppRuntime`, el MCP y la GUI.
- **El motor habla con la GUI solo por estado JSON y comandos**, así la toma de medios puede ir después a otra máquina (Linux) y la GUI a otra (Linux o Mac).
- **Imágenes para la GUI:** por `FrameService` (claves `program`, `preview`, `entrada:X`, `multiview`), con `appsink` en vez de libobs.

**Pieza por pieza** (`§` = obs-gstreamer-migration.md):

### M0 — Núcleo y entradas sin OBS

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M0.1 | `motor::Reloj`: el reloj del estudio (sistema, monotónico), base_time común para todos los pipelines, latencia del estudio con tope (lo de P0.6) | test: dos pipelines con el mismo base_time dan el mismo tiempo de ejecución | ✅ 2026-10-06 |
| M0.2 | `motor::Entrada`: la capa de entrada de `ba_gst_fuente` llevada a una clase sin OBS. Arma el pipeline con los mismos ajustes JSON: pipeline, `camara`, `microfono`, rack, filtros, medios, invitados, NDI, voces. Mantiene vigía de dispositivos, placa «sin señal», vigía de datos, reintentos, compensación y bus completo. Termina en `intersink` `<id>:video` y `<id>:audio` (F32 48 kHz estéreo) | test con `audiotestsrc`/`videotestsrc` y `fakesink`; cada tipo arma | 🟡 (hecho y probado con fuentes de prueba; falta con cámaras, micrófonos y red reales) |
| M0.3 | Medios con controles (play/pausa/ir a/bucle/fin) y la playlist encima de `motor::Entrada` (lo de `ba_gst_medio`) | test: fin de archivo, bucle sin corte, salto | 🟡 (controles y bucle hechos; falta la playlist encima) |
| M0.4 | Un solo armador de ajustes por tipo (P1.6) y pruebas por tipo con `GstTestClock` (P1.7) | ctest | ⬜ |
| M0.5 | Fuentes que hoy da OBS, en GStreamer: color (`videotestsrc pattern=solid-color`), imagen (`imagefreeze`), texto (render con Qt a BGRA → `appsrc`), captura de pantalla/ventana (`avfvideosrc capture-screen` / `d3d12screencapturesrc` / `pipewiresrc`), audio del sistema (elemento propio por sistema) | cada una da imagen/sonido en la prueba | 🟡 (color, imagen y texto con alfa hechos y probados; pantalla armada sin probar (pide permiso); falta el audio del sistema) |
| M0.6 | Estado del motor como JSON (entradas, latencias, señales, medidores) y comandos; el MCP y la GUI leen eso | `motor_estado` por MCP | 🟡 (`motor::Motor` con estado JSON y comandos; falta el MCP) |

### M1 — Mezcla de audio (`audiomixer`)

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M1.1 | Bus PROGRAMA: `intersrc` por entrada → `volume` (nivel, mute, balance) → `audiomixer` con `force-live`, `min-upstream-latency` = latencia del estudio + margen, `start-time-selection=first` y segmento fijo (§5.2) | una entrada que entra tarde no rebobina ni corta (probado en Python el 2026-10-06: +3 dB, tiempo continuo) | ✅ 2026-10-06 |
| M1.1b | **El audio de la escena sigue al programa:** una entrada que está en escenas suena solo si su escena está al aire (con fundido en la transición). Las globales (micrófonos, invitados, remoto, voces) suenan siempre. Hoy lo hace el canal 0 de libobs | cambiar de escena corta el audio de la vieja y abre el de la nueva, con fundido | ✅ 2026-10-06 (en el motor: `escenaAlAire`; lo dispara la composición en M2/M6) |
| M1.2 | Buses PISTA 2–6 (grabación multipista) y la Matriz (`audio-matriz.json`): qué entrada va a qué bus con qué nivel; los pads se agregan y sacan andando | cambiar la matriz no corta | 🟡 (pistas 2–6 y el ruteo por entrada hechos; falta leer la Matriz en M6) |
| M1.3 | Limitador del máster true peak (P0.8) en el plugin `baaudio`: `balimitador`, con lookahead y sobremuestreo ×4, de fábrica −1 dBTP, se puede apagar | un tono a +6 dBFS sale a ≤ −1 dBTP medido con `ebur128level` | ✅ 2026-10-06 |
| M1.4 | Mix-minus por invitado: un `audiomixer` por retorno (el programa sin su propio audio, sin restar muestras) → `baanillosink` nuevo en `baipc` que escribe `BA.Audio.Return.<n>` y `BA.Audio.ContentForReturn` (el gateway no cambia). Modo por invitado `programa`/`mix_minus` (`retornos.json`) | el invitado no se oye en su retorno | ✅ 2026-10-06 |
| M1.4b | Buses virtuales de la Matriz (salidas a otra placa, entradas virtuales) y las entradas físicas por canal (`EquipoAudio`) como entradas del motor | la Matriz de hoy anda igual | ⬜ |
| M1.5 | ESCUCHA (P0.7): «directo» (la sala sin demora), «programa» (lo que sale) y PFL de una entrada; salida a `osxaudiosink`/`wasapi2sink`/`pulsesink`; una tecla para pasar. También la escucha de los racks (nodo «monitor» de `barack`), las del lobby y la voz del sistema (solo auriculares) | con «programa» el audio coincide con el video del programa | 🟡 (programa, directo y PFL en el motor; falta la tecla y la GUI en M6) |
| M1.6 | Medidores: `level` y `ebur128level` en cada bus. Alimentan los medidores de la GUI, `LoudnessPrograma` y las alarmas (silencio, saturación, loudness) | la GUI y las alarmas leen del motor | 🟡 (level y ebur128level en el bus; falta enchufarlos a la GUI y las alarmas, en M6) |

### M2 — Composición de video

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M2.1 | PROGRAMA y PREVIEW: `glvideomixer` en Mac (`compositor` de respaldo, d3d12/cuda en los otros sistemas). Escena = propiedades de pads (posición, tamaño, recorte, alfa, orden) tomadas de `app.json`; pads dinámicos con `queue` y latencia declarada | una escena de `app.json` se ve igual | 🟡 (PROGRAMA y PREVIEW con `compositor`; `glvideomixer` sin probar; las escenas de `app.json`, en M6) |
| M2.2 | Take y transiciones: corte, mezcla, empuje, cortina (`GstController` + `glshader`), stinger (un medio con alfa encima); take frame-exacto | el cambio de escena no corta audio ni salida | 🟡 (corte sin negro, mezcla, empuje y cortina hechos y probados; falta el stinger) |
| M2.3 | DSK y gráficos arriba del programa; clean feed (sin DSK) por `tee`; zócalos y títulos con las fuentes de M0.5; subtítulos en pantalla (`ba_subtitulos`) | el clean feed sale sin gráficos | 🟡 (DSK con fundido y clean feed hechos; faltan los zócalos y títulos armados y los subtítulos en pantalla) |
| M2.4 | MULTIVIEW estándar (P1.4): mosaico PGM/PVW/entradas con tally rojo/verde, nombre, medidores de audio, alarmas por recuadro y reloj | se ve el tally y los medidores | 🟡 (PVW/PGM + hasta 8 entradas con tally y nombre; faltan medidores de audio, alarmas por recuadro y reloj) |
| M2.5 | Lo que hacían las fuentes propias de OBS: video grafo/compositor (`ba_video_grafo`, `ba_video_compositor`), PTZ digital, mejora de imagen, placa (`ba_placeholder`). Pasan a elementos en la cadena de la entrada o a pads del compositor | cada tarjeta del grafo anda en el motor | ⬜ |
| M2.6 | `ba/control` con al aire / preview / tally (P1.3): las tarjetas caras descansan cuando su entrada no se ve | una entrada fuera del aire baja su CPU | ⬜ |

### M3 — La GUI con el motor

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M3.1 | `FrameService` lee del motor: `appsink` chico (escalado en la GPU) para programa, preview, multiview y cada entrada | las vistas de la GUI andan sin libobs | 🟡 (del lado del motor: `Vistas` por clave, BGRA chico, por pedido; falta que `FrameService` las lea, en M6) |
| M3.2 | Modo «al aire» en la GUI (P1.2): al aire, cambiar algo riesgoso pide confirmación | prueba en la GUI | ⬜ |

### M4 — Salidas

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M4.1 | Codificadores por placa (`vtenc_h264`, `nvh264enc`, `qsvh264enc`, `d3d12h264enc`, `x264enc` de respaldo) + `avenc_aac`, con la lista de candidatos que ya usa la app | arranca con el de la placa y cae al de respaldo | ✅ 2026-10-06 (probado con vtenc_h264_hw; los de Windows y Linux, sin placa) |
| M4.2 | Transmisión: `flvmux` → `rtmp2sink` con reconexión y estado (cuadros perdidos, kbps, reintentando); respaldo a la URL de respaldo de YouTube (P1.5); SRT (`mpegtsmux` → `srtsink`) | una hora sin cortes ni deriva; las alarmas de salida leen del motor | 🟡 (RTMP y SRT con reconexión y estado; falta el respaldo de YouTube y la hora de prueba) |
| M4.3 | Grabación con `splitmuxsink` por tramos (lo que quedó de P0.10): video y pistas 2–6, MKV, remux a MP4 al terminar, copia en otro disco | `kill -9`: se pierde a lo sumo el último tramo | 🟡 (MKV en tramos hecho; falta el remux a MP4 y la copia en otro disco) |
| M4.4 | Replay (anillo de N s → clip), grabación por entrada, NDI de salida (`ndisink`), cámara virtual | cada una da su archivo o su señal | ⬜ |

### M5 — Robustez del motor

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M5.1 | `fallbackswitch` + `livesync` al final de cada entrada (§5.2.3): una caída no deja negro ni silencio y vuelve sola | desenchufar y enchufar sin corte en el programa | ✅ 2026-10-06 (video: placa con fallbackswitch; audio: el mezclador ya pone silencio; `livesync` queda para cuando haga falta) |
| M5.2 | Lip-sync por entrada medible y ajustable (P1.1): tono + flash, ajuste en ms guardado | la prueba da el desfase y lo corrige | ⬜ |
| M5.3 | Una entrada en su propio proceso, opcional (§7.1), para las que pueden colgarse | matar el proceso de la entrada no tira el programa | ⬜ |

### M6 — El cambio y sacar libobs

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M6.1 | `AppRuntime` y `StudioViewModel` sobre el motor: misma API, mismas herramientas MCP, misma GUI | las pruebas por MCP de siempre pasan | ✅ 2026-10-06 (probado con `--test`: escenas, take con mezcla, multiview, grabación, subtítulos, cierre limpio) |
| M6.2 | Migración de `app.json`: los tipos de OBS (`color_source_v3`, `text_ft2_source_v2`, `image_source`, `ffmpeg_source`, captura) pasan a los tipos del motor | una configuración vieja abre igual | ✅ 2026-10-06 (los tipos se traducen al armar la entrada, `ajustesMotor`; el app.json no se reescribe) |
| M6.3 | Fuera `src/obs-module`, `libobs.framework` y sus plugins del bundle de los tres sistemas | la app arranca sin libobs; el bundle pesa menos | 🟡 (código y bundle de macOS sin libobs; Makefile, deps, CI y bundles de Linux/Windows: M6.4) |
| M6.4 | Sacar libobs de la infraestructura: Makefile, `tools/deps`, cmake, CI, deploy, packaging; Linux y Windows armados en Docker | `make dist-*-docker` sin libobs | 🟡 |
| M6.6 | Lo que corría en el filtro de grafo de libobs y todavía no corre en el motor: tarjetas traer (a la escena de otra entrada), salida en un punto intermedio (hoy publica la salida de la entrada), corrección de color de `normalizar`, y el «marcar» de rostros. El estado lo dice por tarjeta | cada tarjeta corre en el motor | 🟡 (retardo ✅: `desfase_video` de la entrada; rostros ✅: Personas lee una vista del motor; sincro ✅: la medición usa vistas 16×9 del motor; compositor y salida ✅: escenas internas del motor, toma = la salida de la entrada) |
| M6.7 | `bamejora` en la GPU (hoy CPU: ~28 ms por cuadro 1080p en 8 hilos; viene apagado de fábrica) | menos de 5 ms por cuadro | ⬜ |
| M6.8 | El PTZ digital vuelve al encuadre si la cadena de la entrada se rearma | rearmar no mueve el encuadre | ✅ 2026-10-06 (convención: la propiedad `estado` de un elemento sobrevive al rearmado; `test_motor_app`) |

### M8 — El video en la GPU de punta a punta (docs/MAPA-GPU-CPU.md)

Medido con `motor_mapa` (2026-10-06, Mac): 87 pipelines, 428 enlaces de video, **todos en memoria de sistema**; 101 trabajos de video en CPU.

| ID | Tarea | Cómo se sabe | Estado |
|---|---|---|---|
| M8.0 | `motor_mapa`: el mapa vivo de todos los pipelines con la memoria de cada enlace, transiciones y trabajo en CPU | la herramienta lo dice | ✅ 2026-10-06 |
| M8.1 | Cortar el desperdicio en CPU: placas «sin señal» dibujadas una vez; entradas estáticas (color, texto, imagen) sin normalización y congeladas | `motor_mapa`: sin textoverlay por cuadro en las placas | ✅ 2026-10-06 |
| M8.2 | Un contexto de GPU para todo el motor (GstGLDisplay/GstGLContext en Mac y Linux, GstD3D12Device en Windows), entregado a cada pipeline: que un buffer de GPU cruce los `inter` | un buffer GL pasa de la entrada a la composición sin bajar | ✅ 2026-10-06 (Mac) |
| M8.3 | Mac: `vtdec`/cámara en GLMemory → filtros GL → `glvideomixer` sin bajar → vistas chicas → `vtenc` sin copia | `motor_mapa`: GLMemory de punta a punta | 🟡 2026-10-06: 403 enlaces GL / 176 sistema, 29 trabajos en CPU (eran 174), app ~19 % (era ~28 %). Falta desentrelazado y baptz en GL |
| M8.4 | Windows (Intel primero): D3D12 de punta a punta (`d3d12h264dec`/`qsvh264dec` → `d3d12compositor` → `qsvh264enc`/`d3d12h264enc`) | cero bajadas por cámara de red en una Intel | ⬜ |
| M8.5 | Linux: VA (Intel/AMD) y CUDA (NVIDIA) de punta a punta | ídem | ⬜ |
| M8.6 | Lo que solo existe en CPU (rótulos, tarjetas gst de CPU, plugins de Python, legalizador, bamejora): baja y sube solo en su entrada, visible como transición | `motor_mapa` lo marca | ⬜ |

### M7 — Después del cambio

| ID | Tarea | Estado |
|---|---|---|
| M7.1 | WebRTC en el proceso (`webrtcbin`) para invitados y sesión remota, con su mix-minus (ex E4) | ⬜ |
| M7.2 | Subtítulos 608/708 en la transmisión (§6, ex E5) | ⬜ |

---

## Registro

(Se completa a medida que se hace cada tarea: fecha, commit, qué se probó.)

- 2026-10-05 · P0.1, P0.2, P0.11:
  - **P0.1:** ningún elemento de una cadena ofrece su reloj (`provide-clock=false` a todos al armar, además de `gst_pipeline_use_clock`).
  - **P0.2:** `presentarVideo`/`presentarAudio` sellan con la hora en que la muestra tenía que salir (captura + demora de la entrada), traducida al reloj de libobs.
  - **P0.11:** con `--test` no se retoma lo que estaba al aire.
  - Probado en la app: el micrófono suena, la cámara da imagen y la playlist da imagen y sonido.
- 2026-10-05 · P0.3: `video/ElementosGst` revisa 29 elementos (imprescindibles y los del motor que viene). Lo que falta va al registro al arrancar, a Configuración → Sistema y datos y al MCP `gst_elementos`. En Mac están todos.
- 2026-10-06 · P0.4: una entrada con video que deja de darlo se queda con el último cuadro 3 s y después muestra la placa «<nombre> — sin señal», hecha una vez con GStreamer (`videotestsrc` + `textoverlay`) al tamaño del lienzo y mandada dos veces por segundo. Cuando vuelve el video, sigue sola. Recién creada tiene 10 s de gracia (redes de superresolución que tardan en cargar).
  - Probado: una entrada RTSP a una dirección sin nada muestra su placa.
  - Ya avisó de varias «Axis» con superresolución pesada que no daban cuadros.
  - `livesync` (tapar microcortes) queda para E3: con libobs y el modo colchón cambiaría los tiempos.
- 2026-10-06 · P0.5:
  - **Vigía de datos:** una entrada en vivo que daba video (o audio, si es solo de audio) y deja de darlo 10 s sin error se rearma.
  - **Reintentos con espera creciente:** 0,2 / 0,5 / 1 / 2 s y después cada 2 s. Medido en el registro: 0,27 → 0,57 → 1,0 → 2,0 s. La cuenta vuelve a cero apenas llegan datos.
  - **El registro:** el mismo error que se repite se anota a lo sumo una vez por minuto, con el nombre de la entrada.
- 2026-10-06 · P0.6: tope a la latencia del estudio.
  - **Funcionamiento:** la compensación y la latencia estándar solo toman las entradas dentro del tope. Una que pide más va con su propia demora y avisa «llega tarde» (en el registro, en `estado` y en la lista).
  - **Dónde se ve:** proc `ba_estudio_latencia`, MCP `estudio_latencia {tope_ms}` y medidor en Video → Sincronía y red (verde, o rojo con las que llegan tarde). El tope se guarda en `app.json`.
  - **Tope automático: 250 ms.** Con 3 cuadros quedaba afuera la cámara FaceTime con su plugin (186 ms) y su micrófono se adelantaba.
  - **Medido:** el estudio pasó de ~0,9–1,8 s (lo arrastraban las cámaras con superresolución) a 179 ms, alineado con la cámara del micrófono. Las seis Axis con superres quedan afuera, avisadas.
- 2026-10-06 · P0.7 y P0.8 pasan a E1:
  - **Por qué:** la escucha «programa» y el limitador del máster en libobs piden parchear libobs y rehacer `obs.dll` de Windows (T12 opción A). Con `audiomixer` (E1) salen solos: un bus ESCUCHA y un limitador en la rama del máster.
  - **Lo que ya estaba:** el medidor LUFS (T11) con su objetivo y la alarma «Loudness fuera de objetivo».
- 2026-10-06 · P0.9: tres alarmas nuevas en el motor de alarmas (`agent/AlertEngine`), de fábrica:
  - **Programa en negro:** 3 s con brillo < 16/255, sonido crítico.
  - **Programa congelado:** 30 s sin cambiar. Una placa fija también lo dispara: se sube `forSec` o se apaga.
  - **La grabación se cortó:** pedida y no graba, 5 s.
  - **Cómo se miran:** negro y congelado, solo al aire. Usan una copia chiquita del programa (160×90, 5 por segundo, `obs_add_raw_video_callback2`) con su brillo y una firma.
  - **Lo que ya había:** silencio, transmisión caída, entrada sin señal y las demás. Se ven en la GUI, suenan y van por MCP (`alert_*`).
  - **Actualizar:** un `alertas.json` viejo recibe las nuevas una sola vez. Si el operador las borra, no vuelven (`fabricaOfrecida`).
  - **Probado en la app grabando:** una escena vacía dio «negro» a los 3 s y «congelado» a los 30 s. La grabación cortada se probó en `test_alerts`. ctest: 102 de 102.
- 2026-10-06 · P0.10: volver como estaba, lo que faltaba.
  - **La escena al aire:** va a `Runtime/al-aire.json` apenas cambia, con `cerrado` (true solo en un cierre ordenado). Tras un corte vuelve esa escena, también con `--test`; las salidas no se retoman con `--test`.
  - **La playlist:** anota ítem, archivo y segundo en `Runtime/playlist-aire.json` cada 2 s. Tras un corte sigue en ese ítem y ese segundo (si la lista cambió, no hace nada) y avisa en la GUI y en el registro.
  - **Escritura atómica:** `app.json` ya se escribía con temporal + rename (y guarda `.anterior`). Ahora también `al-aire.json`, `playlist.json` y `playlist-aire.json`.
  - **Grabación en tramos → E2 (`splitmuxsink`):** el MKV de hoy ya aguanta el corte. Probado con `kill -9` grabando: el archivo se decodifica entero (h264 + aac); solo le falta la duración en la cabecera.
  - **Probado en la app:**
    - Playlist en la escena Playlist al aire y `kill -9` a los 28 s: al volver estaba al aire la escena Playlist y el clip seguía desde ~28 s.
    - Con un cierre ordenado (`app_quit`) no retoma nada.
- 2026-10-06 · Motor M0.1, M0.2 (en parte), M0.3 (en parte), M1.1: `src/motor/` (biblioteca `ba_motor`, sin OBS), commit 93ff0f2.
  - **Reloj:** un reloj y un base_time para todos los pipelines; latencia del estudio con tope (la que pasa no arrastra).
  - **Entrada:** la capa de `ba_gst_fuente` sin OBS. Termina en `intersink <id>:video` / `<id>:audio`. Lo en vivo entrega sin esperar; los mezcladores esperan la latencia.
  - **Lo que se aprendió (probado):**
    - `intersrc` pasa las horas tal cual. Un archivo (con su propio base_time, que cambia en cada salto y en cada pausa) lleva un offset en el pad de **salida** del elemento que entrega; en el de entrada de `intersink` recién valía con el próximo segmento.
    - El EOS no tiene que pasar al mezclador (`event-types` vacío): si pasa, el pad queda terminado para siempre.
  - **Mezcla:** `audiomixer` en vivo con ramas que se agregan y sacan andando, fader, mute, ganancia de escena con fundido de igual potencia y medidores.
  - **test_motor (ctest):**
    - un seno de 0,5 sale a −9,03 dBFS; uno segundo que entra andando suma a −6,02 sin rebobinar;
    - fader, mute y escena;
    - sacar una rama;
    - un archivo que termina, se reinicia, se pausa y vuelve;
    - bucle sin cortes;
    - video normalizado (NV12, 30 cuadros, píxel cuadrado).
- 2026-10-06 · M1.3: `balimitador` (plugin `baaudio`, DSP en `ba::dsp::LimitadorTP`), commit 84c1f4b.
  - **Cómo limita:** true peak con sobremuestreo ×4 y lookahead de 5 ms (mínimo deslizante + promedio, que garantiza el techo). Declara su demora en LATENCY. Apagado demora lo mismo.
  - **Medido con `ebur128level`:** +6 dBFS sale a −1,04 dBTP.
  - **En el motor:** el PROGRAMA lleva limitador y `ebur128level` a la salida.
  - **Realineado:** una fuente en vivo que trae sus propias horas (como `videotestsrc`, que arranca en 0) se corrige sola con el offset del pad.
- 2026-10-06 · M1.4, M1.1b, M0.6 (en parte): `motor::Motor` y `baanillosink` (plugin `baipc`).
  - **Motor:** dueño del reloj, las entradas y los buses, con su hilo. Las entradas globales suenan siempre; las de escena, solo con su escena al aire (`escenaAlAire`, con fundido).
  - **Retornos:** en mix-minus, un bus por invitado con todo menos su voz (mismos faders, mutes y escena) → `BA.Audio.Return.<n>`. El programa → `BA.Audio.ContentForReturn`. El gateway no cambia.
  - **test_motor_retornos:** el retorno del invitado se lee del anillo como lo lee el gateway y lleva solo el micrófono (−15,05 dB); el programa para el gateway lleva todo (−12,04); el mute y la escena valen en el retorno.
  - **Latencia:** no se fuerza la del pipeline del mezclador (quedaba por debajo de lo que declaran el bloque de salida y el limitador); solo el piso `min-upstream-latency`.
- 2026-10-06 · M1.2 y M1.5 en el motor.
  - **Pistas 2–6:** buses que se arman cuando una entrada va ahí (`ponerPistas`; la 1 es el programa).
  - **Escucha** (`motor::Escucha`): programa (espera lo que tarda en salir el programa: suena con su video), directo (un bus solo con las globales, 40 ms fijos) y PFL de una entrada. Va al sink de audio del sistema, con el dispositivo por su id estable.
  - **Regla nueva:** todo lo que consume un bus espera la latencia de salida de ese bus (`Mezcla::latenciaSalidaNs`); si no, todo le llega tarde. Vale para la escucha y el puente al gateway, y valdrá para la grabación (M4).
  - **Probado en test_motor_retornos** (la escucha a un `fakesink`, nada por los parlantes).
- 2026-10-06 · M2.1 y M2.2 (en parte): `motor::Composicion`.
  - **Escena:** capas (entrada, posición, tamaño, recorte, alfa, orden) como propiedades de pads de `compositor`.
  - **Transiciones:**
    - corte: la vieja se va cuando la nueva dio su primer cuadro;
    - mezcla: la nueva entra encima con su alfa;
    - la misma escena con cambios se actualiza andando, sin rearmar.
  - **En el Motor:** escenas guardadas, `armar` (PREVIEW), `take` (intercambia, como una mesa de corte) y `alAire`. El audio de las capas sigue al take con el mismo fundido.
  - **test_composicion:** colores leídos píxel por píxel a la salida (posiciones, corte, capa movida, mezcla a mitad y terminada, alfa 0,5).
  - **Trampa encontrada:** `videorate` sin `skip-to-first=true` rellena desde el principio del segmento hasta el primer cuadro. Con el base_time del estudio eso son ráfagas de cientos de cuadros duplicados al arrancar una cámara.
  - **El realineado** de fuentes con sus propias horas solo corrige si el corrimiento es estable (una que se está poniendo al día no se toca), y es por rama (video y audio).
- 2026-10-06 · M4.1, M4.2 y M4.3 (en parte): `motor/Salidas`.
  - **Codificador:** se elige el de la placa (`vtenc_h264_hw` en la Mac) con x264 de respaldo; las propiedades se ponen solo si las tiene.
  - **Grabación:** `splitmuxsink` + `matroskamux`, con el programa y las pistas pedidas; EOS al parar.
  - **Transmisión:** `flvmux ! rtmp2sink` o `mpegtsmux ! srtsink`. Reconecta con espera creciente (1, 2, 4, 8, 10 s). Su estado (conectada, reconectando, kbps medidos, cuadros perdidos) es lo que van a leer las alarmas. La clave nunca aparece en el estado.
  - **test_salidas:**
    - grabación de 3,13 s con H.264 y dos pistas AAC; 4 tramos de 1 s;
    - SRT a la misma máquina recibido y demultiplexado (117 cuadros, 184 bloques de audio);
    - RTMP sin servidor queda reconectando.
- 2026-10-06 · M5.1: la placa «<nombre> — sin señal» en el motor.
  - **Cómo:** el video en vivo de cada entrada pasa por un pipeline fijo (`intersrc <id>:crudo:video ! fallbackswitch ! intersink <id>:video`) con la placa de respaldo. Si la cadena se cae o se rearma, a los 3 s sale la placa; cuando vuelve, vuelve sola.
  - **Probado en test_composicion:** el programa muestra la placa (gris) y después la cámara otra vez, sin cuadro congelado ni negro.
  - **Trampa encontrada (horas y media de rastreo):** `videotestsrc`/`audiotestsrc` en vivo cuentan su tiempo desde 0. Con el base_time del estudio de hace segundos salen a ~90 cuadros por segundo para ponerse al día, y el consumidor se ahoga sin dar error. Arreglo: `timestamp-offset` = la hora del estudio al armar (`fuentesDePruebaEnHora`). Las fuentes de captura reales sellan con la hora del reloj y no tienen el problema.
- 2026-10-06 · M0.5 (en parte): tipos de entrada `color`, `imagen`, `texto` (con transparencia) y `pantalla`.
  - **Cómo:** el texto es `textoverlay` sobre un fondo transparente BGRA; la imagen, `imagefreeze`.
  - **Probado en test_composicion:** un texto sobre el rojo deja ver el rojo donde no hay letras, y un verde con alfa 0x80 se mezcla.
  - **Para la migración (M6.2):** son lo que reemplaza a `color_source_v3`, `image_source` y `text_ft2_source_v2`.
- 2026-10-06 · M3.1 (lado del motor): `motor::Vistas`.
  - **Cómo:** «programa», «preview» o «entrada:<id>», al tamaño pedido, con la latencia de lo que mira. Se arma al pedirla y se desarma al soltarla; el último cuadro va por número de serie.
  - **Probado en test_salidas.**
- 2026-10-06 · M2.4 (en parte): multiview en el motor.
  - **Qué muestra:** PREVIEW y PROGRAMA arriba, las entradas con video abajo, cada una con su tally (rojo al aire, verde armada, gris) y su nombre.
  - **Cómo está hecho:** con las piezas que ya había (composición, entradas de color y de texto). Una capa ahora puede leer cualquier productor (`video:programa`).
  - **Probado en test_salidas:** los colores del tally, leídos de la vista.
- 2026-10-06 · M2.3 (en parte): DSK y clean feed.
  - **Cómo:** el PROGRAMA son dos etapas. La escena sale limpia por un `tee` (`video:programa:limpio`) y un segundo compositor le pone encima las capas del DSK, que entran y salen con fundido y no cambian con la escena (`Motor::dsk`).
  - **Probado en test_composicion:** el DSK está en el programa y no en el limpio, y sigue al cambiar de escena.
- 2026-10-06 · M2.2: transiciones empuje y cortina.
  - **Cómo:** el empuje mueve la posición de los pads. La cortina recorta la fuente y achica el pad juntos, así no se deforma. Las dos en CPU, sin GL.
  - **Probado a mitad de camino** (los dos colores en su lado) **y al terminar.**
- 2026-10-06 · M6 el cambio (commits aa23feb, b25cc01): la app entera corre sobre el motor.
  - `FuentesApp`: propiedades por tipo (sin `obs_properties`), fuentes que pinta la app (chat, gráficos, subtítulos dibujados con Qt), filtros extra por entrada y acceso al motor para piezas que no lo reciben.
  - Elementos nuevos: `baptz` (PTZ digital), `bamejora` (mejorar imagen, CPU), `baplugins` (`baplugin`/`bapluginsrc`, antes en el módulo de OBS; el puerto va por el registro de GStreamer).
  - Motor: estado de filtros por entrada, propiedades y `ba/control`, tomas de audio por entrada (ceros si está silenciada; números únicos entre motores), escucha «selección» (la suma de las marcadas), `desfase_audio` aparte del desfase de la entrada, reloj publicado.
  - Audio sin libobs: Matriz (tomas del motor; entradas virtuales por `bavoz` estéreo), RackManager (siempre `barack`; escucha del nodo salida por QAudioSink), EquipoAudio y voces por la cola de `bavoz`, OperatorFeed (vista propia 640×360, toma de audio, monitores del lobby como entradas internas).
  - **Arreglado:** segfault al cerrar (RackManager recorría `filters_` mientras `detach` lo vaciaba).
- 2026-10-06 · M6.1, lo que el motor le tenía que dar a la app (`test_motor_app`):
  - entrada `tipo=app` (la app empuja cuadros: gráficos, chat);
  - composición de cualquier escena a pedido (el editor) y DSK con escenas enteras;
  - T-bar manual;
  - volumen del máster con fundido (FTB);
  - toma del audio del programa (loudness, picos, aplauso);
  - avisos por cuadro de una vista (el programa al gateway);
  - remux MKV→MP4 sin recodificar;
  - las capas pueden «contener» (entra sin deformarse, como hacía OBS).
  - **Arreglado:** la grabación escribía MP4 con nombre .mkv. `splitmuxsink` crea su muxer al pedirle las pistas, antes de que se le pase otro. Ahora `muxer-factory=matroskamux` va en la descripción y el test verifica el contenedor.
  - **Trampa:** `imagefreeze` con un `appsrc` no negocia con la normalización si las caps del `appsrc` no traen `pixel-aspect-ratio` e `interlace-mode`.
