El procedimiento estándar para un evento **en salón**: público en la sala, oradores, transmisión y grabación, y si se pide invitados remotos y preguntas del público. Seguilo en orden; cada paso dice con qué herramienta. **Proponé antes de hacer** lo que cambia el evento o sale al aire, y no inventes datos: lo que no te dijeron, preguntalo.

**0. Lo que hay que preguntar** (antes de armar nada): nombre del evento; fecha, hora de inicio y **huso** (si alguien se conecta de otro lado); lugar; duración y partes (bloques) con su tiempo; quién habla en cada parte, con **nombre y cargo** tal como van en pantalla; si hay **invitados remotos**; si el **público pregunta y vota**; **dónde se transmite** (YouTube, Twitch, Facebook, otro servidor) y si se graba; música de espera y de cierre; videos que se pasan; si hay **acreditación** de asistentes; si hace falta teleprompter o subtítulos.

**1. Configurar** (todo lo que tiene que quedar puesto):
| Qué | Con qué |
|---|---|
| El evento y su guion: bloques con tiempos y cues (escena, orador con su tiempo, video, música, gráfico, teleprompter, nota, espera) | `evento_armar {nombre, fecha, inicio, huso, invitados, preguntas, bloques}`; después `guion_bloque` / `guion_cue` |
| Las escenas: cada cámara, plano general, pantalla compartida, invitados | `scene_define`, `scene_list` (help escenas) |
| Música y videos del evento (los cues los nombran) | `media_list`, `media_find` (help medios) |
| Zócalos de cada orador (nombre y cargo bien escritos) | gráfico `zocalo` en cues de tipo gráfico; `grafico_listar`, `grafico_probar` |
| Micrófonos de la sala y niveles | `audio_input_add`, `audio_measure`, `audio_loudness` (help audio) |
| Adónde se transmite | cuentas: `account_setup_guide`, `accounts_status`; servidor propio: `stream_destino_agregar` |
| La grabación (disco y calidad) | `record_disco`, `record_ajustes` |
| La página web del evento (una página por bloque, se actualiza sola) | `evento_web {accion: generar}`; `evento_web {accion: sitio, participacion: true}` si el público participa; dónde verlo en vivo: `sitio {vivos}` |
| Invitados remotos (si hay) | `room_link`, `room_pin`; lobby con `room_set`; quién está: `invitado_chequeo` |
| Preguntas del público (si hay) | la página del público (QR); moderación con `preguntas_ver` / `pregunta_moderar` |
| Asistentes y acreditación (si hay) | `padron_anotar`; en la puerta, `padron_puerta` |
| El principio y el final: señal de ajuste, placa «Antes de empezar», placa de fin, bloques Previa y Cierre | `evento_preparar {musica_espera, musica_cierre, bajada, cierre}` |

**2. Controlar** (lo automático): `evento_revisar` (escenas, archivos, disco, destino, entradas sin señal) → `evento_ensayo_rapido` (camina el guion entero) → `evento_verificar` (la lista de control completa de este tipo de evento). Arreglá cada «falta» con lo que dice `como` y volvé a correr `evento_verificar` hasta que no falte nada.

**3. Pedirle a la persona que controle**: `evento_verificar` deja en la lista de chequeo del evento lo que solo confirma alguien mirando y escuchando (estado «a_mano»). **Mostrale esa lista entera** a quien te pidió el evento y pedile que confirme cada cosa; a medida que te diga que sí, marcalo con `evento_chequeo {item, hecho: true, nota}`. No marques nada que no te hayan confirmado.

**4. Ensayar** (probar cada pieza, sin salir al aire):
- **Música:** `audio_escuchar {name}` la manda a los auriculares del operador; que la escuche y diga si el volumen está bien.
- **Videos:** `scene_preview` con la escena del video y `foto_fuente` para mirarlo; con su audio.
- **Gráficos:** `grafico_probar` con los datos reales de cada zócalo y placa (mirá las imágenes; nombres y cargos bien escritos).
- **Página web:** `evento_web {accion: ver}` y el QR con `qr_mostrar` para abrirla en un celular.
- **Grabación de prueba:** `record_start`, un minuto, `record_stop`, y que la reproduzcan.
- **Invitado de prueba** (si hay): `invitado_prueba`: se ve, se oye y nos oye.
- **Pregunta de prueba** (si hay): que alguien mande una desde el celular y moderala.
- **Transmisión de prueba:** con la señal de ajuste al aire, `stream_start` y `stream_salud`; que la persona la vea en la plataforma (barras, hora que corre, tono, que sea el evento correcto). Si falta mucho para empezar, `stream_stop`.
- **Todo el recorrido:** `evento_correr` con el ensayo prendido (help del evento) para ver el orden real de las escenas.

**5. Dejarlo listo para empezar:** `evento_verificar` sin faltas y con todo confirmado; después `evento_preparar {al_aire: true}` deja la señal de ajuste en el programa. Avisale a la persona que quedó listo y qué falta hacer el día.

**6. El día, de la señal de ajuste al final:**
1. **30 a 15 minutos antes:** `evento_iniciar`; la Previa del guion arranca con la señal de ajuste (`evento_correr`); `stream_start` y `record_start`. Confirmar que se ve y se oye en la plataforma (el cue de espera lo pide).
2. **10 a 5 minutos antes:** `evento_correr` → placa «Antes de empezar» con la música de espera.
3. **Es la hora:** `evento_correr` al primer bloque.
4. **Durante:** `evento_correr` para avanzar; zócalos a tiempo; preguntas moderadas; vigilar `stream_salud` y `audio_loudness`.
5. **Cierre:** el bloque Cierre pone la música y la placa de fin; cuando terminó, `stream_stop`, `record_stop`, `evento_terminar` y `evento_informe` (lo que duró cada parte, para el próximo). La página web pasa sola al cierre.
