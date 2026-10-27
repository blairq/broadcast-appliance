## Como esta armado
Cada fuente con audio (microfonos, capturas, videos) puede tener un **rack** tipo JACK: `in` (la fuente cruda) -> modulos LSP conectados con cables -> `out` (lo que sigue a la mezcla). La persona lo ve en la pestaña **Audio** (patchbay); `audio_show {"source"}` la abre en esa fuente.
- `audio_sources`: fuentes con audio y si tienen rack. `audio_rack {"source"}`: nodos, cables y medidores.
- `audio_plantillas`: guardar un rack con nombre y ponerselo a otra fuente. Marcada `porDefecto` para `ba_guest_audio`, los invitados NUEVOS la toman solos al conectarse: es la unica forma de que suenen bien sin armarles nada mientras el evento empieza.
- Cada modulo tiene medidores propios: `entradaDb`, `salidaDb` y `cambioDb` (lo que baja o sube AHORA: en un gate/compresor es la reduccion de ganancia).
- Un rack vacio no cambia el sonido. `audio_module_remove {"node": "*"}` quita el rack entero.
- En la pestaña, cada tarjeta se acomoda en una grilla (boton "Ordenar" para alinear todo) y su chevron abre el detalle: nombre real de cada puerto del modulo y si es mono o estereo.

## Puertos y canales (L/R)
Un modulo puede tener varias entradas y salidas: `in` (principal), `sc` (sidechain) y, si tiene mas, `in2`, `in3`... y `out`, `out2`... (`audio_rack` las informa en `entradas`/`salidas`, y en `puertosEntrada`/`puertosSalida` con su nombre real y cuantos canales tiene cada uno). Un mezclador LSP de 16 entradas informa las 16.
- Por defecto un cable lleva el **puerto entero**: izquierda con izquierda y derecha con derecha.
- Se puede cablear **un canal suelto** agregando `canalSalida` y `canalEntrada` ("L" o "R") en `audio_patch`: p. ej. llevar la derecha de un modulo a la izquierda de otro, o usar un solo canal y dejar el otro en silencio.
- Entre los mismos dos puertos no conviven un cable de puerto entero y uno de canal: el ultimo reemplaza al anterior (si no, el audio se sumaria dos veces).

## Escuchar por una salida del equipo (auriculares)
Sirve para revisar como quedo un punto del rack SIN tocar lo que sale al aire ni la mezcla del programa.
1. `audio_outputs`: las salidas de esta maquina, agrupadas por placa.
2. `audio_output_set {"source", "salida": <id>}`: agrega al grafo de esa fuente una tarjeta con el nombre de la salida.
3. `audio_patch {"source", "conectar": [{"desde": "in", "hacia": "salida-audio"}]}`: recien ahi se oye (se puede enganchar la salida de cualquier modulo, no solo `in`).
OJO: el motor reproduce por UNA salida a la vez; elegir otra en otra fuente cambia la de todas.

## Microfonos y entradas de audio del equipo
No aparecen solas: hay que sumarlas. `audio_devices` las lista (`inputs`) y `audio_input_add {"device"}` la agrega. Va a un canal GLOBAL (como el audio de los invitados): suena en TODAS las escenas, no es un item de escena y queda guardada para el proximo arranque. Despues se procesa como cualquier fuente con su rack.

## Regla de oro: medir, cambiar, volver a medir
1. `audio_analyze {"source", "punto": "in", "segundos": 5}`: pico, RMS, LUFS, `pisoDeRuidoDbfs`, `nivelActivoDbfs`, `rangoDinamicoDb`, espectro por tercios y por zonas (graves, cuerpo, medios, presencia, sibilancia, aire), recortes y `pistas`. Si dice que no llega audio, la fuente esta muda, inactiva o no hay nadie hablando: pedile a la persona que hable (`operator_ask`).
2. Cambiar con las herramientas con nombre (valores en unidades reales: dB, ms, Hz).
3. Verificar: `audio_analyze {"punto": "out"}` y/o `audio_measure` (copias fuera de linea, no toca el aire).
Contale a la persona que mediste y que cambiaste, con los numeros.

## Calibraciones tipicas (voz)
- **Gate** (`audio_gate`): medir DOS veces, con la persona callada (piso de ruido) y hablando (nivel activo). `umbral_db` = piso + 6 a 10 dB, y siempre bastante por debajo del nivel activo. `reduccion_db` -20 a -40 (no hace falta mudo total), `ataque_ms` 1-5, `sosten_ms` 50-100, `liberacion_ms` 100-250. Verificar con `audio_measure {"tipo": "curva"}` (donde abre) y escuchando que no corte finales de palabra. Si el piso y la voz estan a menos de ~15 dB, el gate no alcanza: sugerir acercar el micro o bajar la ganancia de entrada.
- **Compresor** (`audio_compressor`): preset `voz` y ajustar `umbral_db` para que `cambioDb` marque -3 a -6 dB en las partes fuertes (no todo el tiempo). `ratio` 2-4, `ataque_ms` 5-20, `liberacion_ms` 80-200, `ganancia_db` para recuperar el nivel.
- **De-esser** (`audio_deesser`): mirar la zona `sibilancia` (5-9 kHz) del analisis. `deteccion_hz` en el pico de las eses (5-7 kHz tipico), `umbral_db` para que baje 3-6 dB solo en las eses. `escuchar: true` deja oir lo que detecta (solo para ajustar: recorda apagarlo).
- **EQ** (`audio_eq`, por `bandas`): pasa altos 70-100 Hz para voz; si la zona `cuerpo` (200-400 Hz) sobresale, campana -2 a -4 dB; si falta claridad, +1 a +3 dB en 3-5 kHz. Cambios chicos. Verificar con `audio_measure {"tipo": "respuesta"}`.
- **Multibanda** (`audio_multiband`): cuando un solo compresor bombea o el problema esta en una zona (graves que retumban, agudos que se disparan). `corte_hz` y umbrales por banda.
- **Limitador** (`audio_limiter`, preset `seguridad`): al final de la cadena, techo -1 dBFS. Es red de seguridad, no volumen.
- Orden habitual: EQ pasa altos -> gate -> compresor -> de-esser -> EQ fino -> limitador. `audio_patch` reconecta; `audio_bypass` compara con/sin.

## Loudness
Para la emision apunta a -16 a -14 LUFS integrados en la mezcla (YouTube/Twitch normalizan cerca de -14). Una voz sola: -18 a -16 LUFS en `out`. Los picos que tocan 0 dBFS (`recortes`) se arreglan en la ganancia de entrada, no con el limitador.

## Generador y otros modulos
- `audio_generator {"senal": "rosa"|"seno"|"barrido"|"blanco", "nivel_db", "conectar_a"}` crea una señal de prueba SUELTA (p. ej. hacia un analizador de `audio_module_add {"module": "analizador"}`). Conectarla a lo que sale al aire exige `al_aire: true`: confirma con la persona antes.
- `audio_modules {"buscar"}` lista los ~200 plugins LSP; `audio_module_add` agrega cualquiera y `audio_module_get`/`audio_module_set` leen y cambian todos sus parametros.
- `audio_editor` abre la interfaz grafica original del modulo LSP (para la persona).