# Plan del todo en uno (Parte I de obs-gstreamer-migration.md)

Tareas precisas para terminar el aparato en **una sola máquina**, sacadas de
[obs-gstreamer-migration.md](obs-gstreamer-migration.md) §1–§10. **No incluye** la Parte II (servidor avanzado,
dos máquinas, Congreso a escala): eso es otro bundle, después.

**A futuro (no ahora):** separar la toma de medios (Linux) del cliente/GUI (Linux o Mac). Se hace cuando el todo
en uno ande. Mientras tanto, regla para no cerrarse esa puerta: lo nuevo del motor no habla con la GUI por
punteros sino por el MCP / el bus de estado, así después puede ir por la red.

Estados: ⬜ falta · 🟡 en curso · ✅ hecho (con commit) · ⏸ necesita al usuario o hardware.

---

## Bloque P0 — Antes de la primera sesión real (con libobs todavía)

Orden de ataque: primero lo barato que mejora el aire, después lo grande.

| ID | Tarea | Dónde | Cómo se sabe que anda | Estado |
|---|---|---|---|---|
| P0.1 | **El reloj nunca lo pone un dispositivo.** Confirmar que ninguna cadena toma el reloj de un `osxaudiosrc`/`wasapi2src`/`pulsesrc` (hoy `gst_pipeline_use_clock` fuerza el del estudio) y poner `provide-clock=false` igual, como cinturón | `ba_gst_fuente.cpp` (`armar`), `audio/EntradasAudio.cpp` | Desenchufar el micrófono no manda CLOCK_LOST a nadie; prueba con `gst-launch` | ✅ |
| P0.2 | **Entregarle a libobs cada cuadro y bloque con su hora de captura**, no con la de entrega: hora del pipeline (base + PTS en tiempo de ejecución) traducida a `os_gettime_ns` leyendo los dos relojes juntos | `presentarVideo`/`presentarAudio` en `ba_gst_fuente.cpp` | Con una cámara y su micrófono: el desfase medido no cambia y el temblor baja (video_estado / medición de sincronía) | ✅ |
| P0.3 | **Elementos imprescindibles al arrancar**: revisar con `gst_element_factory_find` (`intersink`, `intersrc`, `fallbackswitch`, `livesync`, `audiomixer`, `compositor`, `ebur128level`, `avdec_speedhq`, `baanillo`, `bavideoanillo`, `barack`, `bavoz`, `bandisrc`, `baautomixer`, los de subtítulos) y avisar en Diagnóstico y por MCP | nuevo `diag/ElementosGst.*`, tool `gst_elementos` | En Mac todos «sí»; si falta uno, aviso claro (no silencio o negro) | ✅ |
| P0.4 | **Tapar el hueco sin corte**: al final de cada entrada en vivo (cámaras, red, micrófonos, invitados, NDI), `livesync` y un respaldo cuando no llega nada: video = último cuadro unos segundos y después placa «<entrada> sin señal»; audio = silencio con fundido. Vuelve solo con el primer dato bueno | `ba_gst_fuente.cpp` (rama de video y de audio antes del appsink) | Desenchufar la cámara: placa con su nombre en vez de negro/congelado; enchufar: vuelve sola. Un microcorte de 100 ms no se nota | ✅ (placa; `livesync` queda para E3) |
| P0.5 | **Vigía de datos**: si una entrada en vivo no entrega nada en N s sin haber dado error (dispositivo colgado), se rearma; reintento con espera creciente 0,2 / 0,5 / 1 / 2 s y después cada 2 s (hoy fijo 2 s) | `ba_gst_fuente.cpp` (`tic`, `reintento`) | Simular un dispositivo que deja de mandar: se rearma solo; el registro lo dice una vez | ✅ |
| P0.6 | **Tope a la latencia del estudio**: la compensación no pasa de un tope (de fábrica 250 ms, ajustable); una entrada que pide más queda con su propia demora y un aviso «llega tarde»; interruptor `estudio` por entrada en GUI y MCP; **medidor** «latencia del estudio: N ms» (verde / rojo con la entrada que la causa) | `ba_gst_fuente.cpp` (`compensacionEstudio`, `latenciaDelEstudio`), `video_estado`, página Video (Sincronía), MCP `video_entrada_red`/nuevo `estudio_latencia` | Con la cámara con superres: el estudio queda en ≤ tope y esa cámara avisa; el medidor lo muestra | ✅ |
| P0.7 | **Dos escuchas para el operador**: «directo» (la sala sin demora) y «programa» (lo que sale, audio con su video); una tecla para pasar | escucha de la app (auriculares: `RackManager::setAuriculares`, monitoreo de libobs) | Con «programa», el audio que se oye coincide con el video del programa; con «directo», sin demora | → E1 (ver el registro) |
| P0.8 | **Limitador del máster (true peak) y loudness (T12 + T11)**: limitador en el programa (de fábrica −1 dBTP, se puede apagar), medidor LUFS integrado/corto con objetivo (−14 YouTube / −23 TV) y aviso si se aleja | programa de audio (hoy libobs: filtro en la mezcla; mañana `audiomixer`), `audio/LoudnessPrograma.*` (ya mide) | Un tono a +6 dBFS sale a ≤ −1 dBTP; el medidor LUFS coincide con `ebur128level` | → E1 (el medidor LUFS y su aviso ya están; falta el limitador) |
| P0.9 | **Alarmas** de silencio (programa N s), negro (N s), cuadro congelado y salida caída (YouTube/SRT/grabación dejaron de mandar), visibles, audibles, en el bus (superficie de control / teléfono) y por MCP | nuevo `diag/Alarmas.*` sobre los medidores del programa y el estado de las salidas | Silenciar el programa 10 s → alarma; cortar la red → «YouTube dejó de recibir» | ✅ 2026-10-06 |
| P0.10 | **Volver como estaba (lo que falta)**: guardar y retomar la escena al aire y la playlist con su posición tras un corte; configuración escrita atómica (temporal + rename); grabación en tramos (MKV) para perder como mucho el último | `OperateMode.cpp` (`al-aire.json`), `AppSettings.cpp` (`saveAppConfig`), salida de grabación | `kill -9` grabando con la playlist andando: vuelve la escena, la playlist sigue donde estaba, la grabación anterior se abre bien | ⬜ |
| P0.11 | **`--test` no arranca al aire**: con `--test`, no se retoma transmisión/grabación al arrancar | `OperateMode.cpp` (`restoreOutputs`) | Arrancar con `--test` tras un corte: no transmite | ✅ |
| P0.12 | **Corrida larga y los otros sistemas**: script de prueba de 8 h (todas las entradas, transmisión a un canal de prueba, mide temperatura, memoria, deriva A/V, cuadros perdidos) y validación en Windows y Linux por SSH | `tools/prueba-larga/`, docs | El informe de la corrida | ⏸ (hardware y una cuenta de prueba) |

## Bloque P1 — Después, todavía con libobs

| ID | Tarea | Dónde | Cómo se sabe | Estado |
|---|---|---|---|---|
| P1.1 | **Lip-sync por entrada medible y ajustable** (tono + flash; ajuste en ms guardado) | `video/Sincronia.*` (ya mide entre entradas), `desfase` de `ba_gst_fuente` | La prueba da el desfase de cada par cámara-micrófono y lo corrige | ⬜ |
| P1.2 | **Modo «al aire» en la GUI**: con transmisión o grabación, confirmar o bloquear editar la escena al aire, cambiar ajustes de una entrada al aire, sacar fuentes, cambiar el rack de un micrófono al aire | QML + `StudioViewModel` (las guardas del MCP ya existen) | Al aire, cambiar algo riesgoso pide confirmación | ⬜ |
| P1.3 | **`ba/control` con al aire / preview / tally**: las tarjetas caras descansan cuando su entrada no se ve | `VideoGrafos::controlEntrada` (ya existe el canal), elementos | Una entrada fuera del aire baja su CPU | ⬜ |
| P1.4 | **Multiview estándar**: tally rojo/verde por recuadro, medidores de audio, alarmas por recuadro, nombre, reloj | multiview actual | Se ve el tally y los medidores | ⬜ |
| P1.5 | **Transmisión de respaldo** a la URL de respaldo de YouTube | salidas | Las dos ingestas reciben | ⬜ |
| P1.6 | **Un solo armador de ajustes de `ba_gst_fuente`** (hoy InputSpecs, VideoGrafos, Playlist, MonitorVoz) | `InputSpecs` | Una sola función por tipo; tests | ⬜ |
| P1.7 | **Pruebas por tipo de entrada con `fakesink` y reloj falso (`GstTestClock`)** | `src/gst-plugins/tests`, `src/obs-module/tests` | ctest | ⬜ |

## Bloque E — Las etapas del motor (sacar libobs)

Cada etapa deja el aire andando y se puede volver atrás. Antes de cada una, leer §5.2 (min-upstream-latency, la
primera entrada tarde, fallbackswitch).

| ID | Etapa | Qué | Cómo se sabe | Estado |
|---|---|---|---|---|
| E1 | Mezcla de audio | `audiomixer` para PROGRAMA, pistas 2–6, mix-minus por invitado/remoto y escucha; entradas por `intersink`/`intersrc`; `min-upstream-latency` = latencia del estudio + margen; segmento inicial fijo; la Matriz maneja los pads; libobs recibe el programa ya mezclado como una sola fuente | Mismo nivel y loudness; el invitado no se oye en su retorno; nada se cae al entrar el primer invitado | ⬜ |
| E2 | Salidas | Codificadores (vtenc/nv/qsv/d3d12/x264 + AAC), RTMP/SRT, grabación (`splitmuxsink` en tramos), replay, NDI desde GStreamer, alimentadas por el lienzo de libobs (`appsrc`) | Una hora transmitiendo sin cortes ni deriva; grabación multipista igual a la de hoy | ⬜ |
| E3 | Composición | PROGRAMA, PREVIEW, MULTIVIEW, take, transiciones (GstController, glshader), DSK y gráficos en `glvideomixer`/`compositor`; la GUI por `appsink`; fuente y proceso separados con `intersink` | Take frame-exacto; cambiar de escena no corta; una entrada caída muestra la placa y vuelve sola | ⬜ |
| E4 | WebRTC en el proceso | `webrtcbin` para invitados y sesión remota en vez de la memoria compartida, con su mix-minus | Latencia de ida y vuelta igual o menor; hora por RTCP | ⬜ |
| E5 | Subtítulos | §6 (608/708, cccombiner, h264ccinserter) | YouTube los muestra a tiempo | ⬜ |
| E6 | Sacar libobs | El audio del sistema y la escucha del lobby a GStreamer; fuera `libobs.framework` y sus plugins | La app arranca sin libobs | ⬜ |

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
