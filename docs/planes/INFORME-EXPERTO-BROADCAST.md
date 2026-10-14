# Crítica de experto: Broadcast Appliance frente a una producción de TV en vivo

> Segundo informe. El primero es [INFORME-FUNCIONES.md](INFORME-FUNCIONES.md), con el inventario de funciones y las 400 herramientas MCP. Este lo mira con los ojos de un técnico de control room que hizo vivos con switchers Ross, Grass Valley y ATEM, gráficos Chyron y Vizrt, playout, consolas de audio de broadcast, intercom y transmisiones de sesiones.
>
> **Caso de uso que ordena las prioridades: las sesiones de una asamblea deliberativa.** Son largas (3 a 8 horas), con muchos oradores sentados en bancas, uso de la palabra por turno, votaciones, cuartos intermedios, versión taquigráfica y archivo. Además hay una obligación de accesibilidad: la Ley 26.522, art. 66, pide subtitulado oculto, lengua de señas y audiodescripción en la TV abierta. Una transmisión institucional por streaming no está obligada en lo formal (suposición: depende de cómo se emita), pero es el estándar que se va a pedir.
>
> Todo lo que se afirma sobre el sistema sale del código, con las rutas citadas en el informe 1. Las comparaciones con productos comerciales son de conocimiento general del mercado.

**Escala de prioridad**

| Nivel | Significado |
|---|---|
| **Imprescindible** | Sin esto una sesión real corre riesgo, o el resultado no es aceptable profesionalmente. |
| **Importante** | Lo espera cualquier operador con experiencia; su falta cuesta tiempo o calidad todos los días. |
| **Deseable** | Suma, pero se puede vivir sin eso. |

**Escala de esfuerzo** (estimación gruesa para este equipo y este código)

| Esfuerzo | Duración aproximada |
|---|---|
| **Chico** | días |
| **Medio** | 1 a 3 semanas |
| **Grande** | más de un mes, o depende de hardware o terceros |

---

## Juicio general

El aparato es sorprendentemente amplio. Hace en un solo bundle portable cosas que en un control room tradicional son cinco equipos:
- switcher;
- sala de videoconferencia con lobby;
- gráficos;
- teleprompter;
- votación y padrón;
- transcripción y traducción offline;
- control PTZ;
- automatización con un asistente.

La capa de "producción asistida" (MCP con 400 herramientas, roles, disparadores, ensayo del guion) no la tiene ningún producto comercial de este rango.

Las debilidades están donde un profesional mira primero:
1. **Audio de broadcast.** No hay mix-minus, no hay medición de loudness del programa ni true peak, y los plugins corren sin aislamiento.
2. **Redundancia y supervivencia.** Hay un solo destino de salida, el latido depende del hilo de la interfaz, no hay salida de respaldo y la grabación es la misma codificación que el stream.
3. **Accesibilidad normativa.** No hay closed captions en el stream, solo subtítulos quemados.
4. **Interfaz física.** No hay superficies de control, tally de hardware, GPIO ni hotkeys globales.
5. **Interconexión profesional.** No hay salida NDI, SDI, SRT ni ST 2110, ni timecode.

Para una asamblea la mayoría de esas faltas se pueden cubrir sin convertirlo en un Ross. Pero las primeras dos categorías son las que, si fallan, **tiran abajo una sesión**. Ahí hay que poner el esfuerzo antes que en features nuevas.

---

## 1. Escenas, preview/program y transiciones

**Qué esperaría un profesional.** Bus de preview y program (lo tiene), y además:
- **T-bar** o fader para transiciones manuales;
- **transiciones por take** (CUT y AUTO como botones distintos, con la transición elegida en el momento);
- **stingers** y wipes con borde;
- **DSK/keyers** independientes de la escena (zócalo, logo, "EN VIVO") que se suben y bajan sin cambiar de escena;
- **audio follow video** por fuente;
- **macros**;
- **FTB (fade to black)** como botón dedicado.

vMix y ATEM tienen todo eso. OBS tiene Studio Mode con T-bar y overrides de transición por escena.

**Riesgos en vivo.**
- El botón "Al aire" de `ScenesPage` hace **corte directo, sin preview**. En el editor es facilísimo sacar al aire algo a medio armar.
- La tecla **T** hace TAKE desde dos tarjetas. Si el foco quedó ahí mientras alguien escribe, una "t" suelta saca al aire. Hay que verificar que un campo de texto con foco no la deje pasar (suposición: no verificado en todos los casos).
- Sin keyers independientes, cada combinación cámara + zócalo es una escena aparte. Las escenas se multiplican y crecen los errores de "esta escena no tenía el logo".

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | Que "Al aire" en el editor **arme** (preview) en vez de cortar, o que pida confirmación cuando el guion está `enVivo`. Lo mismo para cualquier corte directo con teclado. |
| Imprescindible | Chico | Botón **FTB / placa de seguridad** ("Cuarto intermedio", "Volvemos enseguida") a un toque o tecla, que no dependa de que exista la escena. |
| Importante | Medio | **DSK/overlays globales**: 2 a 4 capas (logo, zócalo, "EN VIVO", reloj) encima de cualquier escena, con su botón ON/OFF y transición propia. En libobs se hace con una escena "overlay" anidada en la salida o con un canal superior. |
| Importante | Medio | **CUT / AUTO** separados y transición elegible por take, más override de transición por escena. **Stinger** (video con alfa + punto de corte): el plugin `obs-transitions` de OBS trae `obs_stinger_transition` (suposición: verificar que esté en el bundle; hoy solo se exponen cinco tipos). |
| Deseable | Medio | T-bar en pantalla (y por MIDI, ver §4). Audio-follow-video opcional por fuente. |

---

## 2. Salida: transmisión y codificación

**Qué esperaría un profesional.**
- **Al menos dos destinos simultáneos**: la plataforma principal y un respaldo, o YouTube y el sitio oficial.
- Protocolos de contribución robustos (**SRT** como mínimo, idealmente RIST).
- Un encoder de respaldo.
- Medición de salud de la salida en pantalla: bitrate real, cuadros perdidos, RTT.
- Soporte de **29,97 / 59,94** si la señal va a una TV o a una distribuidora.

vMix y Wirecast hacen multidestino de fábrica. OBS 30+ trae SRT/RIST/WHIP y multitrack.

**Riesgos en vivo.**
- **Un solo `rtmp_output`.** Si YouTube se cae o rechaza la clave, no hay plan B.
- `startStreaming` **devuelve "ok" sin hacer nada** si ya había una transmisión: el asistente o un operador creen haber cambiado de destino y no cambió. Es un error silencioso, el peor tipo de error en vivo.
- Solo fps enteros: si mañana hay que entregar a un canal de TV en 59,94, no se puede.
- RTMP sobre redes inestables (la asamblea saliendo por un enlace de oficina) no tiene ni recuperación de paquetes ni latencia controlada.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | `startStreaming` con un destino distinto estando al aire: **error explícito** o cambio en caliente controlado, nunca "true" silencioso. |
| Imprescindible | Medio | **Multidestino**: N salidas (`obs_output`) que comparten encoder, cada una con su estado, reconexión y botón propio. Mínimo dos: principal y respaldo. |
| Importante | Chico | Panel de **salud de la salida**: kbps reales, cuadros perdidos del encoder y de la red, tiempo de reconexión, y la salud de la ingesta de YouTube (`youtube_live_health` ya existe: llevarlo a la tarjeta Salidas). |
| Importante | Medio | **SRT de salida** (caller y listener) con `latency` y passphrase. libobs lo trae vía `ffmpeg_mpegts_muxer` con `srt://`. |
| Importante | Chico | fps fraccionarios (`fpsDen` = 1001) en Configuración → Video. |
| Deseable | Medio | Encoder secundario de respaldo (por ejemplo x264 si el de hardware falla en medio del aire, no solo al arrancar). |

---

## 3. Grabación

**Qué esperaría un profesional.**
- **Grabación del programa independiente del stream**: otra calidad, idealmente intra-frame o de bitrate alto.
- **ISO de cada cámara** con interfaz.
- **Doble grabación** en dos discos.
- Timecode embebido.
- Grabación multipista de audio (los micrófonos separados para la versión taquigráfica).
- **Replay** para repetir un momento: en una sesión, "repetimos la votación" o la jugada de la discusión.

**Riesgos en vivo.**
- Grabación y stream **comparten encoder**: si el stream va a 4 Mbps para que entre por la red de la asamblea, el archivo oficial de la sesión también queda en 4 Mbps.
- La ISO existe **pero solo por MCP**: un operador humano no la puede prender.
- Después de una caída de A, la grabación sigue en un archivo nuevo. Correcto en MKV, pero si alguien eligió MP4, el archivo viejo queda corrupto.
- No hay control de espacio en disco visible antes de arrancar. El disparador `disk` existe, pero solo grabando y solo si alguien lo creó.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Forzar MKV** (o MP4 fragmentado / remux al cerrar) para la grabación oficial, y remux automático a MP4 al terminar. |
| Imprescindible | Chico | **Chequeo de disco al arrancar la grabación**: horas que quedan con el bitrate actual, en la tarjeta Salidas. Disparador `disk` de fábrica. |
| Importante | Medio | Encoder **separado para la grabación** (opción "calidad de archivo": CQP/CRF alto o ProRes en Mac). |
| Importante | Chico | **Interfaz para la ISO**: un botón por entrada en Entradas o en la tarjeta Salidas. El motor ya está. |
| Importante | Medio | **Pistas de audio separadas** en el archivo del programa (las 6 pistas de OBS ya existen: grabar pista 1 = programa y 2..6 = micrófonos o buses). Sirve para taquigrafía y para corregir la mezcla después. |
| Deseable | Medio | Segunda copia simultánea en otro disco. Replay buffer de libobs (`replay_buffer`) con botón "repetir últimos N s". |

---

## 4. Multiview, dashboard, control físico y tally

**Qué esperaría un profesional.**
- **Multiview** con bordes de tally (rojo PGM, verde PVW), nombre de cada fuente, vúmetros por celda, reloj y alarma de pérdida de señal.
- **Superficies de control**: botonera de switcher, Stream Deck, MIDI, X-keys.
- **Tally físico** a las cámaras (GPIO o TSL UMD sobre red).
- **Hotkeys globales** configurables.
- **Intercom** con camarógrafos y piso.

**Riesgos en vivo.**
- Operar un vivo de 6 horas solo con mouse y una pantalla táctil es **cansador y propenso a errores**. Las teclas existen solo con foco en ciertas tarjetas.
- El **tally es una página web en un televisor**. Si el Wi-Fi se cae, el camarógrafo no sabe si está al aire. En la asamblea, con cámaras PTZ fijas, importa menos para el camarógrafo, pero mucho para **el orador remoto**: necesita saber que está al aire, y eso sí lo tiene en la web del invitado.
- El multiview no avisa que una fuente perdió señal: una entrada de red que se cae queda congelada o en negro **sin alarma visual**.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Alarma de pérdida de señal** por entrada en el multiview y en la tarjeta Sistema: el diagnóstico (`video_estado`) ya sabe si llegan cuadros. Llevarlo a un borde rojo titilante + disparador de fábrica. |
| Importante | Chico | **Tally en el multiview** (borde rojo/verde + nombre) en `VideoWall.qml`: `publicarTally()` ya calcula qué fuentes están en PGM y PVW. |
| Importante | Medio | **Hotkeys globales configurables** (TAKE, CUT, FTB, siguiente cue, siguiente orador, mute de cada micrófono), aunque la ventana no tenga foco. |
| Importante | Medio | **MIDI y Stream Deck nativos**, o como mínimo un plugin de ejemplo oficial. Lo más barato es exponer una API HTTP/WebSocket simple compatible con **Bitfocus Companion**: con eso funcionan Stream Deck, X-keys, botoneras MIDI y paneles de switcher sin escribir un driver por marca. |
| Deseable | Medio | **TSL UMD v5** sobre UDP para tally y nombre en monitores y cámaras profesionales. GPIO vía un plugin USB (relés). |
| Deseable | Grande | Intercom (fuera del alcance: recomendar un sistema aparte, o usar `participant_talk` como "IFB" del lobby, que ya existe para hablarle a un invitado). |

---

## 5. Sala de videoconferencia (invitados remotos)

**Qué esperaría un profesional** (vMix Call, Teams NDI Out, Zoom ISO, Riverside):
- **Mix-minus automático por invitado**: cada uno oye el programa **menos su propia voz**.
- Retorno de video del programa (lo tiene).
- **IFB / talkback**: hablarle a un invitado al aire sin que salga.
- Estadísticas de conexión por invitado (bitrate, pérdida, RTT) con alarma.
- Control de calidad: forzar 720p o bajar bitrate a uno que anda mal.
- Grabación ISO de cada invitado.

**Riesgos en vivo.**
- **Sin mix-minus** el invitado escucha su voz con 200-500 ms de retardo. Mucha gente no puede hablar así: se traba y se corta. Además, si el invitado usa parlantes, el AEC del navegador tiene que cancelar su propia voz demorada; cuando falla, **hay eco al aire**. En una sesión con integrantes conectados desde sus despachos es el problema número uno.
- No hay alarma de mala conexión: el operador se entera cuando el invitado se congela al aire.
- El cupo de fábrica es 2 invitados y subirlo **reinicia el gateway**: no se puede cambiar en medio de la sesión.
- Un solo hilo de red de libwebrtc es el techo de escala documentado. Con 16 invitados no está medido desde otro equipo.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Medio | **Mix-minus por invitado.** En vez de `obs_add_raw_audio_callback(0, …)` para todos, cada invitado recibe programa − su propio audio. La forma más simple: en B, restar del retorno la señal `BA.Audio.Guest.n` de ese invitado, alineada (las dos pasan por el mismo reloj de A). La forma limpia: en A, una mezcla por invitado excluyendo su fuente (la Matriz ya sabe armar buses: un bus "retorno-n" por slot, con todas las fuentes menos la del invitado n). |
| Imprescindible | Chico | **Estadísticas por invitado** (`getStats()`: bitrate, pérdida, jitter, RTT) en la tarjeta del invitado, con un semáforo y un disparador `guest_quality`. |
| Importante | Chico | **IFB**: tecla "hablarle a X" que mande la voz del operador **solo** a ese invitado, también estando en la sala (hoy `participant_talk` es solo para el lobby). |
| Importante | Medio | Cambiar el cupo de invitados sin reiniciar el gateway (reservar las 16 sesiones y habilitarlas en caliente). |
| Importante | Chico | **Prueba técnica previa** guiada (ya existe `invitado_chequeo`): sumarle prueba de eco ("decí algo") y medición de la red. |
| Deseable | Medio | ISO de cada invitado (ya existe la ISO por fuente: exponerla en la tarjeta del invitado). Simulcast o techo de bitrate por invitado. |

---

## 6. Modo oradores (uso de la palabra)

**Qué esperaría un profesional de sesiones.** El uso de la palabra lo maneja la Presidencia de la sesión, normalmente con un **sistema de conferencia** (Bosch DICENTIS/DCN, Televic Confidea, Shure MXCW, Sennheiser). Cada banca tiene micrófono con botón de pedido de palabra, y la consola de Presidencia lo habilita. Las transmisiones profesionales **siguen al micrófono**: cuando se abre el micrófono de la banca 12, la cámara va al preset de la banca 12 y sale el zócalo con nombre y bloque.

**Lo que tiene.** Fila de oradores con tiempos, al aire de a uno o varios, presentador fijo, reloj que cuenta en negativo y no corta a nadie (correcto para una asamblea: el tiempo lo administra la Presidencia, no la máquina) y tablero de solo lectura.

**Riesgos en vivo.**
- Funciona solo para quien está **conectado a la sala WebRTC**. Los integrantes presentes en el recinto no están en la fila. Hoy se resuelve con cues del guion, que no sirven para un debate donde la palabra cambia cada 2 minutos sin orden previo.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Medio | **Oradores presenciales en la fila**: entradas "banca" (persona del padrón + preset de cámara + micrófono), manejadas con el mismo `speaker_on_air`. Un toque en "Banca 12" y sale la cámara, el zócalo y arranca el reloj. |
| Imprescindible | Grande | **Integración con el sistema de micrófonos del recinto** (Bosch DICENTIS y Televic tienen API IP; Shure MXCW y algunos Sennheiser también): micrófono abierto → preset PTZ + rótulo + reloj. Es la automatización que más trabajo le saca al operador. Se puede hacer como plugin de Python (la arquitectura ya lo permite), pero tiene que ser un plugin **oficial y probado**. |
| Importante | Chico | Si no hay sistema de conferencia: **"seguir al micrófono abierto"** con los niveles del mezclador (el `audio_peak` y el analizador ya existen). Micrófono de la banca con nivel sostenido → propuesta de corte a su preset, que el operador confirma con una tecla. |
| Deseable | Chico | Exportar los tiempos de uso de la palabra por integrante al acta. |

---

## 7. Audio: mezclador, racks y medición

**Qué esperaría un profesional.**
- **Medidor de loudness del programa** siempre visible: momentáneo, corto e integrado desde el inicio, **true peak**, LRA. Los objetivos son **-23 LUFS / -1 dBTP** (EBU R128) o **-24 LKFS** (ATSC A/85) para TV, y -14 a -16 LUFS para plataformas.
- Medidores con balística PPM o VU, no solo pico digital.
- **PFL/AFL** en cada canal.
- **Automixer** (Dugan) para mesas con muchos micrófonos abiertos.
- **Limitador de seguridad** en el máster.
- Retardo de sincronía por fuente con interfaz.
- Procesos que no puedan tirar abajo el audio.

**Riesgos en vivo.**
- **Un plugin CLAP que crashea tira `ba-app` entera**, con la transmisión. Con ~200 plugins LSP y un asistente que puede agregar cualquiera, es un riesgo real.
- **Sin loudness del programa** nadie sabe si la sesión sale a -30 LUFS (inaudible en un celular) o a -10 (la plataforma la normaliza y bombea).
- **Una asamblea grande tiene 20-40 micrófonos abiertos en potencia.** Sin automixer, el ruido de sala y el filtrado por peine de varios micrófonos abiertos degradan mucho la inteligibilidad. El Automixer está escrito en `src/core/dsp/` y **no se usa**. Muchas veces el sistema de conferencia del recinto ya entrega una mezcla; si no, hace falta.
- Sin compensación de latencia, poner un limitador con lookahead en un micrófono y no en otro **desfasa** micrófonos que captan la misma fuente (filtrado por peine).
- El PFL no está en el mezclador: el operador no puede escuchar rápido "¿de dónde viene ese zumbido?".

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Medidor de loudness del programa** permanente en la tarjeta Mezclador (M, S, I, true peak con oversampling ×4, LRA), con objetivo configurable (-23 / -16 / -14) y reseteo del integrado al arrancar la grabación. El `Analyzer` BS.1770 ya existe: falta engancharlo a la mezcla 0 y agregar true peak. |
| Imprescindible | Chico | **Limitador de seguridad en el máster** de fábrica (-1 dBTP), fuera del alcance de edición accidental. |
| Imprescindible | Grande | **Aislar los plugins de audio**: un proceso host aparte para los CLAP (como hacen Bitwig y Reaper con "plugin bridging"), con *watchdog* que ante un cuelgue saltee el rack (bypass) en vez de tirar la app. Paso intermedio (medio): try/catch de señales es imposible en C++ sobre crash real, pero sí un **watchdog de tiempo** por bloque que ponga el rack en bypass si un plugin tarda más de X ms. |
| Importante | Chico | **Botón PFL** en cada canal del Mezclador (ya existe `audio_escuchar`). |
| Importante | Chico | **Retardo de sincronía por fuente en la interfaz**, y conectar `SincroLabios` (el método del aplauso ya está escrito y testeado). |
| Importante | Medio | **Automixer** para grupos de micrófonos (las bancas): usar el `Automixer` del núcleo como filtro de OBS sobre un grupo, con indicador de "micrófono que abrió". También sirve de entrada para el "seguir al micrófono" del §6. |
| Importante | Medio | Compensación de latencia de plugins (`CLAP_EXT_LATENCY`) dentro del rack, y avisar cuando dos fuentes correlacionadas tienen distinta latencia. |
| Deseable | Chico | Balística PPM (EBU tipo IIb) además del pico, y un "loudness por fuente" opcional en la Matriz. |

---

## 8. Matriz de audio y buses

**Qué esperaría un profesional.** La Matriz está bien pensada: 6 pistas nativas, buses virtuales, salidas físicas y escucha. Un profesional agregaría:
- **mix-minus generados** (ver §5);
- **salidas a varios dispositivos a la vez** (hoy el monitoreo de libobs sale por una sola);
- **audio sobre IP** (AES67/Dante) si la asamblea tiene consola digital.

**Riesgos en vivo.** Los buses virtuales suman 20-30 ms: si alguien mete por ahí la mezcla de programa y la vuelve a mezclar, el desfase aparece. No hay advertencia en la interfaz.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Medio | Capa de salida de audio propia (CoreAudio, WASAPI, PulseAudio) para varias salidas físicas simultáneas (`docs/planes/IDEAS.md` ya lo identifica). |
| Importante | Chico | Marcar en la Matriz los cruces que agregan retardo, y bloquear realimentaciones (bus → fuente → mismo bus). |
| Deseable | Grande | AES67 (hay implementaciones abiertas). Dante requiere licencia (Dante Virtual Soundcard funciona como dispositivo del sistema, sin código: documentarlo). |

---

## 9. Grafo de video, entradas de red y sincronía

**Qué esperaría un profesional.**
- **Placa de "sin señal"** automática.
- **Frame sync** en cada entrada.
- **Genlock** o, en IP, PTP (ST 2059).
- Corrección de color con LUT (la tiene).
- **Scopes**: forma de onda, vectorscopio, histograma.
- **Legalizador** de niveles (broadcast safe).

**Lo que hace muy bien.** El colchón con hora de captura, los relojes NTP/PTP/red, la medición de desfase por correlación y el diagnóstico por entrada están **por encima** de lo que ofrecen vMix u OBS. Eso es nivel de producto de contribución.

**Riesgos en vivo.**
- Cuando una entrada de red cae, **no hay placa**: congelado o negro al aire hasta que el operador lo nota. Se reintenta cada 2 s, pero el aire ya lo mostró.
- Cada tarjeta `gst` baja la imagen de GPU a RAM y la vuelve a subir. Con varias cámaras 1080p y varias tarjetas, la CPU y el bus de memoria pueden no alcanzar, y **no se ve el costo por tarjeta** para diagnosticarlo.
- Las cámaras locales no siguen el reloj del estudio: mezclar una USB con cámaras de red sincronizadas puede derivar.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Placa de pérdida de señal** por entrada (imagen configurable, o último cuadro con cartel "SIN SEÑAL" en el multiview pero **no** al aire), más alarma. Al aire, una opción "si se cae, pasar a la escena de respaldo". |
| Importante | Medio | **Scopes** (forma de onda, vectorscopio, histograma) en la página Video y como celda del multiview. Hay elementos de GStreamer y shaders simples para esto. |
| Importante | Chico | **Costo por tarjeta** (ms/cuadro) visible en el grafo. `docs/funciones/VIDEO-GRAFO.md` ya lo tiene como pendiente. |
| Deseable | Medio | Legalizador broadcast-safe en la salida de programa si va a TV. |
| Deseable | Grande | ST 2110 / ST 2059: no tiene sentido para una asamblea hoy. **NDI de salida** sí (ver §12). |

---

## 10. Superresolución

**Opinión.** Es una función valiosa para el caso real: cámaras PAL o 720p viejas del recinto. Los motores Apple y FSR sirven para vivo; ONNX con SPAN es para probar y Real-ESRGAN no sirve para vivo. Bien que esté documentado así.

**Riesgos.**
- Si la superres tarda más que el colchón de la entrada, se pierde la sincronía con el audio. Hoy el aviso es una cifra en `video_estado`.
- Entrenar exige una entrada de red y baja 800 MB.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | Alarma automática "la superres no llega a tiempo" (ms por cuadro > 80 % del colchón), con bypass automático. |
| Deseable | Medio | Permitir entrenar desde cámaras locales (grabando del grafo, no de la URI). |

---

## 11. PTZ y seguimiento

**Qué esperaría un profesional.**
- **Presets rápidos** con miniatura.
- **Velocidad de preset** (la cámara llega en N segundos, sin mostrar el viaje al aire).
- **VISCA por serie (RS-422)**, que es lo que tienen muchas cámaras de recinto.
- **Panel joystick físico** (Sony RM-IP, Panasonic AW-RP, Skaarhoj).
- Interlock: **nunca mover una cámara que está al aire**.

**Riesgos en vivo.**
- **No hay protección de "cámara al aire".** Un preset, un seguimiento automático o el asistente pueden mover la cámara que está saliendo, y se ve el barrido. En una sesión, ver la cámara "buscando" al integrante queda muy amateur.
- Todo el PTZ está probado solo contra cámaras simuladas. Antes de la primera sesión hay que probarlo con las cámaras reales del recinto.
- El seguimiento por rostro con pulsos proporcionales sobre una cámara al aire puede oscilar.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Interlock de tally**: si la cámara está en PGM, los movimientos (preset, seguir, apuntar, asistente) se bloquean o piden confirmación. Excepción explícita para el seguimiento suave. |
| Imprescindible | Medio | **Prueba con las cámaras reales** del recinto y tabla de compatibilidad. VISCA serie (RS-232/422 por adaptador USB) si las cámaras lo usan. |
| Importante | Chico | **Presets con miniatura** (capturar el cuadro al guardar la posición) y grilla de bancas para tocar. |
| Importante | Medio | Joystick físico: USB HID genérico vía plugin, o VISCA-over-IP desde controladores comerciales (que hablan VISCA a la cámara: el aparato podría "escuchar" como cámara virtual). |
| Deseable | Chico | Velocidad de preset por posición (llegar en N segundos). |

---

## 12. Entradas y salidas de interconexión (NDI, SDI, SRT)

**Qué esperaría un profesional.**
- **Salida NDI** del programa y de la mezcla limpia (sin gráficos), para el canal de TV, la sala de prensa o un segundo sistema.
- **SDI** (Decklink/AJA) si se entrega a una unidad móvil o a un canal.
- **SRT** de entrada (la tiene) y de salida (no la tiene).
- **NDI|HX** de entrada (muchas PTZ NDI son HX).

**Riesgos.**
- La única forma de que otro equipo reciba el programa es por RTMP o WebRTC a un televisor. Para que un canal de TV local tome la señal de la sesión, hoy no hay una salida profesional.
- La entrada NDI es de ingeniería inversa en estado alfa: no conviene usarla en una sesión sin pruebas largas.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Medio | **SRT de salida** (ver §2), que es la forma estándar de entregar a un canal. |
| Importante | Medio | **Salida "clean feed"** (programa sin gráficos) como segunda salida: para TV que pone su propio zócalo, o para archivo. |
| Deseable | Medio | Salida NDI (con el SDK oficial, que no es libre: choca con "portable innegociable" y su licencia; evaluar). Decklink de salida reactivando el plugin de OBS en el bundle. |

---

## 13. Gráficos, rótulos y zócalos

**Qué esperaría un profesional** (Chyron, Vizrt, CasparCG, LiveOS, H2R):
- **Gráficos con datos vivos** en tiempo real: reloj, contador de tiempo del orador, resultados de votación que se actualizan.
- **Playlist de gráficos** con preview y "next".
- **Plantillas de diseño institucional** fijas.
- Zócalo con **nombre + cargo + bloque político**.
- Placas de "Orden del día", "Votación: Afirmativo N / Negativo M / Abstenciones K".
- Ticker.
- **Preview del gráfico antes de salir.**
- Integración con una base de datos de integrantes.

**Lo que tiene.** El motor propio en proceso aparte (bien pensado: un gráfico roto no cuelga la app), el IDE, los vínculos automáticos a quien está al aire y la página Rótulos con "nada sale hasta apretar".

**Riesgos en vivo.**
- **Pre-render de hasta 15 s y último cuadro congelado:** un reloj o un contador de votos que cambia obliga a re-renderizar. Si el render tarda, el dato llega tarde o el gráfico parpadea.
- El zócalo "del que habla" depende de que esté `onair` en la sala o del cue. Para un integrante presente en el recinto, el zócalo no sale solo y depende de la memoria del operador.
- El cargo sale de lo que el invitado escribió en su campo `info`: **riesgo de que salga al aire un texto mal escrito o inapropiado.** Es contenido de terceros en el zócalo institucional.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **El zócalo nunca toma texto escrito por un invitado sin validar**: el nombre y el cargo salen del padrón o de la libreta de Personas (cargados por la organización); el `info` del invitado queda como sugerencia. |
| Imprescindible | Medio | **Base de integrantes** (nombre, bloque, distrito, foto, banca, preset PTZ) como fuente única para zócalos, padrón, rostros y bancas. Una sola tabla con muchos usos. |
| Importante | Grande | **Render en tiempo real** (el pendiente de 30 fps por memoria compartida) para datos vivos: reloj, cronómetro del orador, resultados de votación. |
| Importante | Medio | Plantillas de asamblea de fábrica: zócalo de orador con bloque, "Orden del día" con el punto actual, resultado de votación nominal, "Cuarto intermedio", "Minuto de silencio". |
| Importante | Chico | **Preview del gráfico en el bus de preview** (que el gráfico que se va a sacar se vea primero en PVW). |
| Deseable | Medio | Gráficos HTML (CasparCG o plantillas H2R) vía `obs-browser`, que ya está en el bundle, para aprovechar diseños existentes. |

---

## 14. Voz a texto, subtítulos y accesibilidad

**Qué esperaría un profesional.**
- **Closed captions** (CEA-608/708 embebidos en el stream; YouTube los acepta por el propio RTMP o por su API de subtítulos HTTP) que el espectador prende o apaga.
- Subtitulado en vivo con **corrección humana** opcional (estenotipista o "respeaker").
- **Intérprete de Lengua de Señas Argentina** (LSA) en un recuadro fijo, presente en la pantalla en todo momento.
- WebVTT para el archivo.
- Transcripción con marcas de tiempo enlazadas al video para la versión taquigráfica.

**Lo que tiene.** Transcripción offline buena (Parakeet + VAD), subtítulos quemados con buen manejo de tiempos, .srt y .txt, traducción y voz. Todo sin red: muy valioso para privacidad y costo.

**Riesgos en vivo.**
- **Subtítulos quemados = no se pueden apagar.** Para una parte del público son un estorbo, y un error de reconocimiento queda escrito en la imagen y en la grabación oficial. En un recinto, un nombre propio o una cifra mal transcriptos quedan en el archivo para siempre.
- Sin resultados parciales y con 1-3 s por frase, los subtítulos llegan 3-5 s tarde: aceptable para CC en vivo, pero conviene saberlo.
- Nombres propios: el riesgo está señalado en `docs/funciones/AUDIO-TTS.md` y no resuelto. **Para la asamblea es crítico** (apellidos, localidades, nombres de leyes).

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Medio | **Closed captions en el stream**: libobs tiene `obs_output_output_caption_text2()` para CEA-608 en H.264 sobre RTMP, y YouTube lo toma. Además, la **API de subtítulos de YouTube** (HTTP POST por evento) como alternativa. Los subtítulos quemados quedan como opción, no como única salida. |
| Imprescindible | Chico | **Vocabulario propio para el STT**: hotwords y contexto en sherpa-onnx, con nombres de integrantes, localidades y términos (el glosario de traducción ya existe, reusarlo). Si el modelo no admite hotwords, al menos corrección por diccionario después del reconocimiento. |
| Importante | Chico | **Recuadro fijo de intérprete LSA**: una plantilla de escena o un overlay global (ver DSK, §1) con la cámara del intérprete, que no se pueda sacar por accidente al cambiar de escena. |
| Importante | Chico | Exportar **WebVTT** además de SRT, y sumar la transcripción con tiempos al acta y a la carpeta del evento. |
| Importante | Medio | **Modo "respeaker" o corrección humana**: una persona ve la frase antes de que salga y puede corregir un nombre con un clic (demora de 2-3 s más, configurable). |
| Deseable | Medio | Transcripción en streaming con parciales (los modelos están identificados en el doc). Separación por hablante usando el micrófono abierto (§6) para que la transcripción diga quién habló. |

---

## 15. Texto a voz y traducción en vivo

**Opinión.** Para una asamblea, la traducción a otros idiomas es un **deseable**: sirve en visitas oficiales o eventos internacionales, no en una sesión ordinaria. El TTS al aire con voz sintética en un ámbito institucional es delicado: conviene que siempre esté rotulado como "traducción automática".

**Riesgos.**
- La latencia acumulada (STT + armado + traducción + TTS) puede pasar los 5-8 s. Un error de traducción al aire en un contexto político es un riesgo de comunicación.
- El glosario no lo revisó nadie de la asamblea.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | Rótulo automático "Traducción automática" cuando se muestra una traducción o sale una voz sintética. |
| Importante | Chico | **Revisión del glosario** por el área de prensa o taquigrafía de la asamblea antes de usarla. |
| Deseable | Medio | Traducción como **pista de audio aparte** (la pista 2 ya está prevista en la receta): el espectador elige el idioma en la plataforma, en vez de mezclarlo al programa. |

---

## 16. Teleprompter

**Qué esperaría un profesional.** Control con **pedal o rueda de mano** (Autoscript, Ikan, ProPrompter, Glide Gear), marcas de cue, edición en vivo, reflejo para cristal, y **seguimiento por voz**.

**Lo que tiene.** Lo esencial: control desde el celular, espejo, avisos y "pánico". Muy bien que el orador remoto vea el texto en su propia página.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | **Control por teclado y HID**: pedales USB y ruedas de prompter se presentan como teclado o joystick. Mapear teclas a velocidad, pausa y saltos. |
| Deseable | Medio | Seguimiento por voz: la transcripción continua ya existe, y alinear lo transcripto con el texto es directo. |

---

## 17. Carrusel de láminas, medios y playout

**Qué esperaría un profesional.** Un **playout** con playlist de clips, **cuenta regresiva del clip al aire**, auto-next, loop, y preview con *in/out*. vMix y Wirecast lo traen.

**Riesgos.**
- Pasar un video institucional en medio de la sesión sin cuenta regresiva visible es la receta de "se terminó el video y quedó negro al aire". El disparador `media_end` existe, pero no hay un contador grande en la tarjeta.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | **Cuenta regresiva del medio al aire** (tiempo restante) en la tarjeta Dirección y en el multiview, con aviso a los 10 s. |
| Importante | Medio | Playlist de medios con preview y auto-next (con el guion ya se puede, pero hace falta una vista simple). |
| Deseable | Chico | Conversión de PPTX sin LibreOffice (pedir PDF). |

---

## 18. Pantallas externas, tally y QR

**Opinión.** Chromecast como destino de pantallas de sala es práctico y barato, pero no profesional: la latencia de DashCast, la dependencia del Wi-Fi y que el primer casteo real no se haya probado lo hacen poco confiable para algo crítico. Para la sala está bien (QR, votación, reloj). El tally no debería depender solo de eso.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | **Probar el casteo contra televisores reales** del recinto antes de depender de eso. |
| Importante | Chico | Implementar el **cue `pantalla`** del guion (hoy cae en "todavía no sé"). |
| Deseable | Medio | Salida a **navegador en kiosco** (una Raspberry o un mini-PC con la URL) como alternativa a Chromecast: mismas páginas, sin protocolo Cast. Ya funciona hoy abriendo la URL a mano: documentarlo como el camino recomendado. |

---

## 19. Evento, guion y cues

**Qué esperaría un profesional.**
- **Rundown** (ENPS, iNews, Rundown Creator) con tiempos de back-timing: "para terminar a las 20:00 te quedan X".
- **Timecode / hora de inicio fija.**
- Cues que disparan varias cosas a la vez (macro).
- **Integración con el orden del día** de la sesión.

**Lo que tiene.** El guion con bloques y cues, el ensayo sin aire, la revisión previa y el informe de previsto contra real están muy bien. Para un evento armado (acto, conferencia) es muy completo.

**Riesgos en vivo.**
- **Una sesión no sigue un guion de tiempos:** sigue un **orden del día** cuyos puntos duran lo que dure el debate. El guion de cues sirve para la apertura, el himno y el cierre, pero no para el cuerpo de la sesión.
- El cue Pantalla no se ejecuta y el ensayo acelerado informa mal qué se ejecuta: el ensayo **miente** sobre algo que después pasa o no pasa al aire.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | Corregir el **ensayo acelerado** para que diga la verdad sobre cada tipo de cue, e implementar o bloquear el cue Pantalla (que la revisión previa lo marque como error). |
| Importante | Medio | **Modo "orden del día"**: lista de puntos (expedientes) con su estado (en tratamiento, votado, pasado a comisión), que alimenta el zócalo "Punto N: …", el acta y los capítulos de la grabación. Es más útil que cues por tiempo para una sesión. |
| Importante | Chico | **Back-timing**: hora de fin prevista y tiempo restante del bloque en la tarjeta Dirección. |
| Deseable | Chico | La revisión previa verifica archivos de medios, dispositivos, espacio en disco, destino de stream y credenciales válidas (lo que hoy no revisa). |

---

## 20. Padrón, quórum, votación y acta

**Qué esperaría un profesional de sesiones.** El **voto nominal oficial** lo da el sistema de conferencia del recinto o el tablero electrónico de votación, no un sistema de streaming. Lo que espera la transmisión es **mostrar** el resultado oficial: quórum en pantalla, resultado de votación con detalle nominal, y el registro de presentes.

**Lo que tiene.** Padrón, acreditación por rostro, QR o mostrador, preguntas del público, votación formal con código y acta. Es muy completo para **eventos** (audiencias públicas, asambleas, jornadas).

**Riesgos.**
- **Que la votación de este sistema se tome como la votación oficial del cuerpo.** No tiene firma, auditoría, regla de mayoría codificada ni respaldo legal. El voto por "código de votante" en un teléfono no es un sistema de votación oficial.
- El quórum es un conteo sin regla: no dice "hay quórum" o "no hay quórum" según el reglamento.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Etiquetar la votación como "consulta / no vinculante"** en la interfaz y en el acta cuando se use en sesiones, salvo que la asamblea la adopte formalmente. |
| Importante | Chico | **Regla de quórum y de mayorías configurable** (mitad más uno de los miembros, dos tercios de presentes, etc.) con indicador "hay quórum: sí/no" en Dirección y en la pantalla de proyección. |
| Importante | Medio | **Importar el resultado oficial** del tablero o sistema de votación del recinto (CSV, API, o carga manual rápida) y mostrarlo con la plantilla de resultado nominal. |
| Deseable | Medio | Acta con firma (hash) y transcripción adjunta. |

---

## 21. Plataformas, chat y preguntas

**Riesgos.**
- Un solo destino a la vez (ver §2).
- El chat en pantalla **sin moderación** es un riesgo político en una transmisión institucional: un insulto o una consigna aparece en el programa oficial.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Chat en pantalla solo con aprobación**: el operador o el moderador elige qué mensaje sale ("destacar"), nunca el flujo automático. Lo mismo para preguntas del público al aire. |
| Importante | Chico | Que las preguntas de YouTube, Twitch y Facebook puedan entrar a la cola con un botón ("sumar a preguntas") desde el chat. |

---

## 22. Asistente, agentes y modelo local

**Opinión.** Es la parte más original del sistema y bien resuelta en seguridad: contaminación por sesión, aprobación, roles, formularios de credenciales que el modelo no ve, y traza completa. Para un control room, un asistente que **propone** y el operador que **confirma con una tecla** es un buen modelo.

**Riesgos en vivo.**
- **Un agente que toca el aire.** La política de fábrica (`invitados`) deja que el asistente ejecute sin aprobación si no leyó nada de los invitados. Un `scene_switch`, `stream_stop` o `participant_kick` mal interpretado sale al aire.
- **Las acciones de los disparadores corren sin rol** y ven todo: un disparador mal creado puede, por ejemplo, cortar la transmisión ante un silencio.
- El asistente interactivo tiene **Bash**: puede tocar archivos de la carpeta del sistema.
- Las acciones de voz "automáticas" se ejecutan sin confirmar: un reconocimiento erróneo ("subí" contra "salí") puede ejecutar otra cosa.
- Dependencia de red y de proveedores externos (Claude, Gemini, OpenRouter) para el asistente grande.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Lista de herramientas "de aire"** (`scene_switch`, `preview_al_aire`, `stream_stop`, `record_stop`, `participant_kick`, `gateway_restart`, `app_quit`, `settings_set` con alcance gateway o restart, `ptz_*` sobre la cámara al aire) que **siempre piden confirmación** mientras se transmite, sin importar la política ni la contaminación. Incluye disparadores, recetas, plugins y voz. |
| Imprescindible | Chico | Las acciones de los disparadores, con un rol restringido (no pueden `stream_stop` ni `app_quit` salvo marca explícita). |
| Importante | Chico | Mientras `enVivo`: asistente interactivo **sin Bash**, y `agente_configurar consola=true` bloqueado. |
| Importante | Chico | Las acciones de voz automáticas, limitadas a lecturas y a acciones reversibles (mute, volumen) mientras se transmite. |
| Deseable | Medio | Modelo local en Windows y Linux (llama.cpp) para no depender de la red durante la sesión. |

---

## 23. Plugins de Python y extensiones

**Opinión.** El aislamiento por proceso con espera máxima por cuadro (25 ms, y si no llega sale el último) es la decisión correcta: un plugin no puede congelar el video. Las **extensiones QML no tienen sandbox** y corren en el proceso de la interfaz: una extensión mal escrita puede trabar la UI y, por el latido, **matar la transmisión** (ver §25).

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Chico | Extensiones QML apagadas por defecto durante `enVivo`, o cargadas en un `Loader` con tiempo máximo. |
| Importante | Medio | Plugins en Windows, si la asamblea opera con Windows. |
| Deseable | Chico | Firmar o fijar por hash los plugins "aprobados" para el aire. |

---

## 24. Registro, monitoreo y diagnóstico

**Qué esperaría un profesional.** Un **panel de salud** único con todo lo que puede fallar en semáforo: entradas, salida, disco, CPU/GPU, temperatura, invitados, audio (silencio y saturación), loudness, sincronía. Además, **alarmas sonoras** en el puesto y **notificación remota** (Telegram, mail) al técnico de guardia.

**Lo que tiene.** El registro unificado de los tres procesos, la traza MCP y los disparadores son una buena base. La tarjeta Sistema muestra CPU, fps, encoder y cuadros perdidos.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Disparadores de fábrica activos** (no hay que crearlos por MCP): silencio al aire, saturación, transmisión caída o con pérdidas, disco bajo, entrada sin señal, invitado caído. Con alarma sonora en los auriculares del operador. |
| Importante | Chico | **Interfaz para crear disparadores** (hoy solo MCP). |
| Importante | Medio | Notificación remota al técnico (webhook genérico: Telegram, mail, Slack). |
| Deseable | Medio | Volcados de caída en macOS y Linux (Crashpad, "fase 9"). Sin volcado, una caída en la sesión no se puede diagnosticar. |

---

## 25. Supervisor, redundancia y recuperación

**Qué esperaría un profesional.**
- **Redundancia 1+1**: dos máquinas en paralelo, o un encoder de respaldo que toma el aire.
- UPS con aviso.
- Respaldo de red (dos enlaces, *bonding*).
- **Recuperación sin corte del aire** ante una caída de la interfaz.

**Lo que tiene.** Watchdog, reinicio con espera progresiva, *circuit breaker* a modo seguro, y **retoma de la transmisión y de la grabación** después de una caída (`Runtime/al-aire.json`). Para un sistema de este tamaño es muy bueno.

**Riesgos en vivo.**
- **El latido sale del hilo de la interfaz.** Si la interfaz se traba más de 5 s, sea por un QML pesado, una extensión, un diálogo modal o un `coreaudiod` colgado en una llamada síncrona, el supervisor **mata a A y corta el aire**, aunque libobs estuviera codificando perfecto. El remedio termina siendo peor que la enfermedad.
- Un crash de un plugin CLAP (§7) mata A.
- Después del reinicio, el aire vuelve en varios segundos (arranque de libobs + RTMP). La plataforma puede cerrar el evento si tarda (YouTube suele tolerar un corte corto; suposición: depende de la configuración del evento).
- `core/persist` (doble generación con checksum) no se usa: si `app.json` queda corrupto, no hay generación anterior automática (sí hay respaldos de actualización).

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Imprescindible | Chico | **Separar el latido del hilo de la interfaz**: latir desde un hilo propio que verifique que libobs sigue entregando cuadros (contador de cuadros codificados) y que la salida está activa. Si la UI se traba pero el aire va bien, **no matar**: avisar y dejar que el operador decida. |
| Imprescindible | Medio | **Procedimiento y prueba de respaldo**: una segunda máquina (o un encoder de hardware barato) con la misma escena básica, lista para tomar el aire, y un ensayo de caída antes de la primera sesión real. |
| Importante | Medio | Usar `SnapshotStore` (ya escrito y testeado) para la configuración, con dos generaciones y checksum. |
| Importante | Chico | Medir y documentar el tiempo real de vuelta al aire después de una caída, y avisar al operador con un cartel "Reconectando: causa…" (`docs/planes/IDEAS.md` lo tiene pendiente). |
| Deseable | Grande | Arquitectura "aire separado de la interfaz": libobs y la salida en un proceso, la UI en otro (la arquitectura A/B ya separa el gateway; es el mismo paso para el motor). |

---

## 26. Seguridad y credenciales

**Riesgos.**
- **El pendrive con el sistema lleva todas las claves**: stream, YouTube, cámaras, token MCP, WireGuard. Si se pierde o lo roban, quien lo tenga puede transmitir en el canal oficial de la asamblea.
- Cualquier proceso del mismo usuario puede leer el token MCP y controlar todo.
- El túnel y el acceso remoto exponen la sala a internet: PIN y tokens están bien, pero un PIN débil fijo es un riesgo.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Medio | **Passphrase opcional** para el almacén de credenciales (`SecretsStore` con Argon2id ya existe), pedida al arrancar, o Keychain/DPAPI cuando no importa la portabilidad. |
| Importante | Chico | Corregir la documentación sobre la clave de stream (dice que no se guarda y se guarda). |
| Importante | Chico | PIN de sala aleatorio por sesión de fábrica y caducidad de los enlaces de invitado. |
| Deseable | Chico | Rotar el token MCP en cada arranque, con el puente leyendo el nuevo. |

---

## 27. Interfaz y operación multi-operador

**Qué esperaría un profesional.** En una sesión con transmisión decente hay **2 a 4 personas**:
- director/switcher;
- audio;
- gráficos y zócalos;
- sala remota y cámaras.

Cada uno necesita su puesto con su vista y sus permisos.

**Lo que tiene.** Páginas que se sacan a otras ventanas y monitores, el Rotulador desde el celular, el control del prompter desde el celular y el tablero de oradores. Todo eso sirve para varias personas. Pero es **una sola instancia de la interfaz en una sola máquina**: dos operadores comparten el mismo teclado y mouse, o se reparten monitores de la misma computadora.

**Mejoras.**

| Prioridad | Esfuerzo | Propuesta |
|---|---|---|
| Importante | Medio | **Puestos remotos por navegador** para funciones acotadas: zócalos (el Rotulador ya es esto), sala y lobby, preguntas y votación, prompter. El gateway ya sirve páginas web: falta una consola web de "gráficos" y otra de "sala" con permisos por rol. |
| Importante | Chico | **Bloqueo de pantalla del puesto principal** durante `enVivo` para páginas peligrosas (Configuración, Plugins, Asistente), con contraseña o confirmación. |
| Deseable | Grande | Perfiles de operador con permisos (audio no puede cortar escenas, etc.). |

---

## Top 15 de mejoras priorizadas

Ordenadas por impacto en una sesión real de la asamblea, contra el esfuerzo.

| # | Mejora | Área | Prioridad | Esfuerzo |
|---|---|---|---|---|
| 1 | **Latido independiente de la interfaz**: no matar el aire si la UI se traba y el encoder anda | Supervisor (§25) | Imprescindible | Chico |
| 2 | **Mix-minus por invitado** en el retorno de audio | Sala (§5) | Imprescindible | Medio |
| 3 | **Medidor de loudness del programa + true peak + limitador de seguridad en el máster** | Audio (§7) | Imprescindible | Chico |
| 4 | **Herramientas "de aire" siempre con confirmación** mientras se transmite (asistente, voz, disparadores, recetas, plugins) | Asistente (§22) | Imprescindible | Chico |
| 5 | **Multidestino + error explícito al cambiar de destino** estando al aire | Salida (§2) | Imprescindible | Medio |
| 6 | **Placa y alarma de pérdida de señal** por entrada, con escena de respaldo | Video (§9, §4) | Imprescindible | Chico |
| 7 | **Closed captions CEA-608 / API de subtítulos de YouTube** + vocabulario propio (nombres de integrantes) en el STT | Accesibilidad (§14) | Imprescindible | Medio |
| 8 | **Bancas presenciales en la fila de oradores** (persona + preset + micrófono + zócalo de la base de integrantes) | Oradores (§6, §13) | Imprescindible | Medio |
| 9 | **Interlock de PTZ**: no mover la cámara que está al aire | PTZ (§11) | Imprescindible | Chico |
| 10 | **Disparadores de fábrica activos** con alarma sonora (silencio, saturación, stream, disco, señal, invitado) e interfaz para crearlos | Monitoreo (§24) | Imprescindible | Chico |
| 11 | **Grabación oficial robusta**: MKV/remux, chequeo de disco, pistas de audio separadas, encoder propio | Grabación (§3) | Importante | Medio |
| 12 | **Aislar los plugins de audio** (o al menos watchdog de tiempo con bypass automático) | Audio (§7) | Imprescindible | Grande |
| 13 | **Hotkeys globales + API compatible con Bitfocus Companion** (Stream Deck, MIDI, X-keys) | Control (§4) | Importante | Medio |
| 14 | **Overlays globales (DSK)**: logo, "EN VIVO", intérprete LSA y zócalo encima de cualquier escena; botón FTB / "Cuarto intermedio" | Escenas (§1, §14) | Importante | Medio |
| 15 | **Integración con el sistema de micrófonos del recinto** (micrófono abierto → cámara + zócalo + reloj) | Oradores (§6) | Importante | Grande |

**Para hacer antes de la primera sesión real, aunque no sea código:**
- probar PTZ, casteo y NDI con el hardware real del recinto;
- un ensayo de caída (matar `ba-app` al aire y cronometrar la vuelta);
- revisión del glosario por la asamblea;
- una máquina de respaldo preparada.

---

## Riesgos que pueden tirar abajo una transmisión

Ordenados por probabilidad × impacto, con lo que el código hace hoy y lo que lo mitiga.

| # | Riesgo | Qué pasa hoy | Mitigación inmediata |
|---|---|---|---|
| 1 | **La interfaz se traba más de 5 s** (QML pesado, extensión, `coreaudiod`, diálogo) | El supervisor mata `ba-app` y **corta el aire** aunque el encoder anduviera bien (`AppRuntime::tick` late desde el hilo de la UI) | Latido desde un hilo propio que mire el encoder (Top 1). Mientras tanto, no usar extensiones QML al aire. |
| 2 | **Crash de un plugin de audio CLAP/LSP** | Se cae `ba-app` entera; vuelve sola en segundos con la transmisión retomada, pero hay corte | No experimentar con plugins durante la sesión; usar solo los 6 módulos con nombre propio, probados de antemano. |
| 3 | **Se cae el destino RTMP o la red** | Reconexión de libobs; no hay segundo destino ni respaldo | Multidestino (Top 5); un enlace de red alternativo; vigilar `youtube_live_health`. |
| 4 | **Eco o retorno de su propia voz a un integrante remoto** | Sin mix-minus: el invitado se oye con retardo; si usa parlantes y el AEC falla, hay eco al aire | Exigir auriculares a los remotos (guía del invitado) hasta tener mix-minus (Top 2). |
| 5 | **Una acción automática toca el aire** (asistente, voz mal reconocida, disparador mal armado, receta) | Con la política `invitados`, el asistente ejecuta sin preguntar si no leyó contenido de invitados; las acciones de voz "automáticas" no confirman; los disparadores corren sin rol | Política `siempre` durante las sesiones; vaciar la lista de acciones de voz automáticas; revisar disparadores antes de empezar. A mediano plazo, Top 4. |
| 6 | **Una entrada de cámara se cae** | Negro o congelado al aire sin aviso; reintento cada 2 s | Tener la escena de respaldo (plano general) a una tecla; disparador de "sin señal" (Top 6). |
| 7 | **Se llena el disco** | La grabación falla; el disparador `disk` solo existe si alguien lo creó | Chequeo manual de espacio antes de cada sesión; crear el disparador `disk` (Top 10). |
| 8 | **Corte directo involuntario** (botón "Al aire" del editor, tecla T con foco en la tarjeta) | Sale al aire una escena a medio armar | No editar escenas en la máquina de aire durante la sesión; que "Al aire" arme en vez de cortar (§1). |
| 9 | **Cambio de destino que no ocurre** | `startStreaming` devuelve "ok" estando al aire en otro destino: el operador cree que transmite a YouTube y sigue en otro lado | Siempre verificar con `youtube_live_health` o la página de la plataforma; corregir el código (Top 5). |
| 10 | **Cambiar un ajuste de alcance `gateway` en medio de la sesión** (cupo de invitados, puertos) | Reinicia la sala y todos los remotos se reconectan | Configurar el cupo antes; el modo `enVivo` ya bloquea al asistente, falta bloquear al humano o pedir confirmación fuerte. |
| 11 | **Dos instancias del sistema en la misma máquina** | Se roban el canal de control; el supervisor reinicia en bucle hasta modo seguro (`docs/planes/IDEAS.md`) | Nunca abrir un segundo bundle en la máquina de aire. |
| 12 | **Pérdida del pendrive o de la máquina** | Quien la tenga puede transmitir en los canales oficiales (credenciales sin passphrase) | Revocar las claves de stream y los tokens después de cada sesión si el equipo sale del edificio; passphrase (§26). |
| 13 | **Hardware no probado** (PTZ real, Chromecast real, NDI alfa) | Comportamiento desconocido en la primera sesión | Ensayo técnico completo con el hardware del recinto antes del primer uso oficial. |
| 14 | **Subtítulo o zócalo con un error al aire** (nombre mal transcripto, `info` escrito por un invitado) | Queda quemado en la imagen y en la grabación oficial | Zócalos solo desde el padrón; subtítulos quemados solo con revisión, o mejor CC apagable (Top 7). |
| 15 | **Sobrecarga de CPU/GPU** (muchas tarjetas `gst`, superres, 16 invitados, modelo local, transcripción, traducción, todo junto) | Cuadros perdidos en el encoder; la sala degrada; nada avisa con anticipación | Medir la carga real de una sesión completa en el equipo de aire y fijar un "presupuesto"; apagar lo que no se usa (traducción, superres) durante la sesión. |

---

*Informe basado en la lectura del código al 2026-10-04. Las estimaciones de esfuerzo son orientativas y suponen el ritmo y la base de código actuales.*
