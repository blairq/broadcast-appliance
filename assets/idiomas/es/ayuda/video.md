Un solo grafo para todo el video del estudio, como un pipeline de GStreamer. Cada entrada (camara, red, virtual) es un sub-grafo: `<entrada>.entrada` -> tarjetas -> `<entrada>.escena` (lo que ven las escenas). Las tarjetas de imagen corren en la cadena de GStreamer de cada entrada; las que el motor todavia no corre (salida, toma, compositor) lo dicen en su estado. Todas las entradas marchan con el reloj de video del estudio (`status`: lienzo y fps).

## Como trabajar (siempre en este orden)
1. **Mirar**: `video_estado` (todas, con su diagnostico) y `video_estado {"entrada": ...}` antes de tocar nada: tamaño, fps, formato, color (BT.601/709, rango), si llega entrelazada, reloj, colchon de red. `video_grafo` muestra las tarjetas.
2. **Armar** con `video_pipeline` (UNA llamada por entrada, valida todo y no cambia nada si algo falla). Las herramientas sueltas (`video_tarjeta_agregar`, `_ajustar`, `_quitar`, `video_cable`) son para retoques.
3. **Verificar**: `video_estado {"entrada": ...}` otra vez (el `diagnostico` tiene que quedar sin errores) y `foto_fuente {"name": ...}` para ver la imagen.

## Recetas
**La misma correccion de color en varias camaras** (que cambiando una vez cambie en todas), en UNA llamada:
`video_pipeline {"entradas": ["Sony", "Pelco", "Panasonic"], "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.9}, "compartida": "Color camaras"}], "local_por_entrada": {"Pelco": {"propiedades": {"hue": 0.04}}}}`. El controlador queda como `estudio.color-camaras`. `modo: "agregar"` si la entrada ya tiene tarjetas que hay que conservar (de fabrica las reemplaza). Despues, ajustar la comun: `video_tarjeta_ajustar` sobre cualquiera de las manejadas (va al controlador) o sobre el controlador. `video_controlador` para hacerlo a partir de una tarjeta que ya existe.

**Corregir una camara** (SIEMPRE primero GStreamer): tarjeta `gst` con `videobalance` (brillo, contraste, saturacion, matiz) y `gamma`; una dominante (verde, magenta): `hue` chico (±0.02 a ±0.08). Solo si GStreamer no alcanza: la tarjeta `color` propia.

**Entrelazada / mal declarada** (`video_estado` lo dice): `{"tipo": "normalizar", "params": {"desentrelazar": "yadif"}}` (desentrelaza en la fuente, donde estan los campos); colores corridos en HD que declara 601: `{"matriz_declarada": "601", "matriz_real": "709"}`. Los cuadros por segundo distintos al lienzo los convierte la entrada sola (repite o salta cuadros): no hace falta tarjeta.

**Mosaico / imagen en imagen / lado a lado**: `video_mosaico {"escena", "entradas": [...], "disposicion": "mosaico|pip|lado", "nombre"}` en UNA llamada (toma cada entrada con sus tarjetas y oculta las sueltas en esa escena: el mosaico las sigue recibiendo). Para retocar: `video_tarjeta_ajustar` sobre el compositor con `capas.in_N` {x, y, ancho, alto 0..1, opacidad, orden}.

**Unir cámaras: panorámica o 360** (todo en la placa de video): `video_panoramica {"camaras": [...], "preset": "fila"|"360", "campo", "escena", "nombre"}` en UNA llamada. Une hasta 8 cámaras (un stream cada una; la primera es la lente 1) lado a lado en su lienzo y corrige la deformación de cada una y cómo encajan. `preset: "fila"`: cámaras comunes de `campo` grados repartidas con un 10% que se pisa; sale la **tira** (panorámica cilíndrica) del tamaño de la suma de las resoluciones (4 de 1080p = 7680x1080; máximo 16384 de ancho). `preset: "360"`: dos ojos de pez de 190° espalda con espalda; sale la **vista**, una cámara común que es un PTZ virtual (`yaw`, `pitch`, `fov` = pan, tilt, zoom). Una 360 que manda todo en UN stream (equirectangular o los dos círculos juntos): tarjeta `vista360` en su cadena (`modo`).
Calibrar a mano con `video_tarjeta_ajustar` sobre la tarjeta (`estudio.panoramica`), mirando con `foto_fuente`:
1. `ver: "entrada"`: lo crudo con los círculos de los ojos de pez y los límites de cada cámara; mover `lenteN_x`, `lenteN_y`, `lenteN_radio` hasta que los círculos encajen.
2. `ver: "tira"` (o `"panorama"`) con `guia: "esfera"` (paralelos y meridianos cada 15°): cómo encaja cada una (`lenteN_yaw`, `_pitch`, `_roll`) y su `_campo`, hasta que el horizonte quede derecho y no se noten las costuras (`mezcla`: el ancho de la unión, en grados).
3. Una guía `rectangulo` o `circulo` (`guia_x`, `guia_y`, `guia_ancho`, `guia_alto` en fracciones de la salida) encima de algo que lo sea (una puerta, un reloj): la deformación (`lenteN_k1`, `_k2`; barril < 0 < cojín) hasta que coincida. `lenteN_tipo`: `ojo` (ojo de pez) o `normal`.
Los parámetros, uno por uno: `video_tarjeta_ayuda {"tipo": "panoramica"}`. En la interfaz: página Video, la tarjeta, con atajos para cada paso.

**Streams de red** (SRT, RTMP, RTSP): `video_entrada_red {"nombre", "uri", "escena", "latencia_ms"}` (nunca `network_input_add` para esto: esa no se sincroniza). Cada cuadro sale a su hora de captura + latencia (un colchon propio). Mas latencia = mas tolerancia a la red y mas retraso.

**Sincronizar entradas**: `video_sincronia {"a": ..., "b": ..., "aplicar": true}` mide cuanto atrasa una de otra comparando lo que se ve (necesita algo en comun que cambie: la misma imagen, movimiento, un aplauso) y lo corrige; volver a medir debe dar ~0. A mano: `video_desfase {"entrada", "ms"}`.
- RTSP trae la hora de captura del que manda (RTCP/NTP): con el mismo reloj se alinea solo.
- SRT y RTMP no la traen: se mide con `video_sincronia`.
- Camaras locales: solo se pueden atrasar (retardo); para alinear con una de red, se atrasa la local o se adelanta la de red (su desfase acepta negativos).

**Relojes**: `video_reloj` sin nada dice el reloj del estudio y como esta en cada entrada de red. `{"tipo": "ntp", "servidor": ...}` (todos contra el mismo NTP), `{"tipo": "ptp", "dominio": 0}` (IEEE 1588), `{"tipo": "red", "servidor": ip, "puerto": 8554}` (el reloj de OTRO aparato), `{"publicar_puerto": 8554}` (publicar el de este para que otros lo usen), `{"latencia_ms": ...}` (el colchon de todas las de red).

**Mezcla de GStreamer** (cualquier modulo de varias entradas: `compositor`, `glvideomixer`, `d3d12compositor`, `glmosaic`, `smpte`, `shapewipe`, `glstereomix`; `video_modulos {"soloMezclas": true}` lista los de este sistema): `video_tarjeta_agregar {"tipo": "mezcla", "elemento": "smpte", "params": {"propiedades": {"type": "bar-wipe-lr"}, "nombre": "Cortina"}}`. Sus puertos son los del modulo (`sink_0`, `sink_1`… a pedido, siempre uno libre; `sink1`/`sink2` en smpte; `video_sink`/`mask_sink` en shapewipe): `video_cable` desde cada entrada a su puerto. Las propiedades de cada entrada (xpos, ypos, width, height, alpha, zorder…): `video_filtros {"id", "puerto"}` las lista y `video_tarjeta_ajustar {"id", "params": {"pads": {"sink_1": {"xpos": 960}}}}` las cambia. Sale como fuente con `nombre` (`video_salida_a_escena`). Corre en la placa si el modulo puede (D3D12 en Windows, GL en Mac/Linux).

**Modulos de GStreamer**: `video_modulos` (catalogo documentado en castellano; `buscar`, `categoria`, `elemento` con sus propiedades). Solo los `tarjeta: true` van en una entrada; los `mezcla: true`, como Mezcla de GStreamer. Un modulo en 4:2:0 o con latencia lo avisa el diagnostico. Las propiedades se pasan con su nombre de GStreamer (los enum por su nick).

**"¿Esta todo bien?" (revisar todo)**: `video_estado` sin nada (todas las entradas con su diagnostico) -> aplicar los arreglos de gravedad `aviso` o `error` (cada uno trae la llamada exacta; los `info` son solo para contar) -> si se tocaron desfases o relojes, `video_sincronia` sin aplicar entre las que tienen que coincidir (debe dar ~0) -> `status` (fps, cuadros perdidos, CPU) -> `mixer_list` (que audio traen: las entradas ocultas en un mosaico siguen sonando en esa escena si traen audio). Un «llegaron tarde» con «recien conectada» es la rafaga del arranque: no subas el colchon, volve a mirar en 10 s.

**Ver la imagen**: `imagen_preguntar` sobre una escena o fuente, con una pregunta concreta («¿hay algo estirado o en negro?», «¿que hora marcan los relojes de cada recuadro?»). Sirve para encuadre, negros, estirados y leer texto; NO para comparar brillo o color fino entre recuadros: para eso estan los numeros (`video_grafo`, valores efectivos).

**Rostros**: la tarjeta `rostros` la pone Personas sola al usar `entrada:<camara>`; `marcar` dibuja las caras.

**Lo que ningun modulo hace** (un efecto propio, un filtro raro, un analisis: contar, detectar movimiento, leer algo de la imagen): NO digas que no se puede. Se programa en Python como tarjeta de plugin (`baplugin`), con numpy o con GStreamer adentro (cadenas y elementos propios escritos en Python): `help {"topic": "plugins"}`, seccion «Video», y el molde `ejemplo-video-gst`.

## Cosas a saber
- Las tarjetas `gst` agregan 1 cuadro de latencia (no frenan el video). Si dos entradas se comparan y solo una tiene `gst`, eso es un cuadro de diferencia: tenelo en cuenta o medilo con `video_sincronia`.
- `video_grafo_limpiar {"entrada"}` deja una entrada como vino.
- Entradas virtuales de prueba (barras, placa de zonas, pelota): `video_entrada_virtual {"nombre", "preset", "escena"}`.