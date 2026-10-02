# Lo dictado y el modelo local: de la frase a la acción

Qué pasa cuando el operador dicta algo (F9 o el botón 🎙 de la barra del
asistente), cómo el modelo local lo convierte en una acción con sus datos, y
lo que se midió con el aparato andando.

## El camino

1. **Reconocer** — Parakeet transcribe lo dictado (`audio/Escucha`).
2. **Elegir** — el modelo local (`broadcast-reactivo-qwen3.5-4b`, MLX)
   elige la herramienta entre sus 84 reactivas, o `derivar` si no es suya.
   Unos 170 ms.
3. **Completar los datos** — si la herramienta tiene parámetros, el mismo
   modelo los escribe (`generar`) con la herramienta ya elegida, su esquema,
   **el estado del aparato** (los nombres reales de escenas, fuentes, audio con
   su volumen, participantes, gráficos; solo la parte que usa esa
   herramienta) y las reglas de `datos/voz-argumentos.txt`. Unos 2 s.
4. **Validar** — `agent/ArgumentosLocales`: solo claves del esquema; cada
   nombre, a uno que exista del tipo que maneja la herramienta (sin
   mayúsculas ni acentos, el único que lo contiene, o uno muy parecido); los
   cambios relativos («bajale 4 dB») los calcula el aparato sobre el valor
   actual (el modelo no hace cuentas: escribe `"-4"`); valores de un enum,
   rangos y requeridos. Lo que no se puede validar **bloquea**: no se ejecuta.
5. **Proponer o hacer** — la barra del asistente muestra exactamente qué se va
   a hacer (`name: Música · volumeDb: -16`) con *Hacer*, *Hacer y no volver a
   preguntar*, *Al asistente*, *No*. Lo que el operador dejó «sin volver a
   preguntar» (Configuración → Audio y voz) se hace solo, siempre que no haya
   bloqueos.
6. **Registrar** — `Logs/voz-modelo.jsonl`: cada paso con el mismo `id` (lo
   reconocido, el prompt, la respuesta cruda, los argumentos validados, la
   decisión, lo ejecutado y su resultado).

Sin modelo listo, con `derivar`, o desde *Al asistente*: la frase va tal cual
al asistente grande (si está instalado y corriendo; si no, queda escrita en su
barra).

## Con el modelo v2: la llamada en una pasada

`broadcast-reactivo-qwen3.5-4b-v2` (su `prompt.json` dice `"formato": "tc"`)
no elige y después completa: recibe el estado y el pedido juntos y escribe la
llamada entera, en el formato nativo de Qwen.

- **Cabeza fija** (~8.200 tokens: las 86 funciones con sus esquemas): se
  procesa una vez y queda guardada en disco al lado del modelo
  (`.prefijo-<huella>-<vision|texto>.safetensors`); la primera vez tarda unos
  20 s, después carga en décimas de segundo.
- **El turno**: `Estado: {JSON compacto}\nPedido: <frase>`. El estado lo arma
  la app (escena al aire y armada, escenas, fuentes de la escena, audio con
  volumen y silencio, participantes, gráficos, carruseles). En `audio` va
  primero el micrófono del operador (el elegido en Conferencia, o si no hay,
  el primer micrófono del equipo): es el que se toca cuando el pedido no
  nombra fuente.
- **La respuesta** (`ba-modelo`, op `llamar`): si el primer token es
  `<tool_call>`, se fuerza `<function=` y el nombre se camina por el árbol de
  las funciones (no puede inventar una); los parámetros salen libres.
  - `derivar` → al asistente grande.
  - `falta_dato` → la `pregunta` se muestra y se dice («¿A qué escena?»).
  - texto sin llamada → es charla: se muestra y se dice.
- **Los valores**: con signo (`-4`, `+3`) es un cambio sobre el valor actual;
  con `=` (`=-10`) es exacto; sin signo, tal cual. Después pasa por la misma
  validación de siempre (`ArgumentosLocales`: nombres que existen, rangos,
  enums, requeridos).

Con la app (M1 Pro, sin MTP), 2 de octubre:

| pedido | respuesta | |
|---|---|---|
| subile un poco a la música | `audio_set` Música `+6` | ✓ aunque «un poco» debería ser `+3` |
| bajá la música en menos diez | `audio_set` Música `-10` (cambio) | ambiguo: ¿cambio o «a -10»? |
| silenciá el micrófono del operador | `audio_set` Micrófono de MacBook Pro, muted | ✓ |
| pasá a la escena | `falta_dato` «¿A qué escena?» | ✓ |
| prepará la escena Apertura | `scene_preview` Apertura | ✓ |
| ocultá el logo | `falta_dato` «¿Qué fuente?» | ✓ (el logo no está en la escena al aire) |
| sacá el zócalo | `falta_dato` «¿Qué zócalo saco?» | ✓ (no está en la escena al aire) |
| hola, ¿cómo andás? | `derivar` | ✓ |
| armame un resumen… y mandáselo por mail | `derivar` | ✓ |

### Más rápido: el estado se lee mientras se habla

El turno es `Estado: …\nPedido: <frase>` y el estado va **antes** de la frase.
Al apretar F9, la app arma el estado y `ba-modelo` lo lee (op `preparar`, ~0,8 s
mientras el operador todavía habla); cuando llega la frase, solo falta « <frase>»
y la cola. Se usa solo si los tokens de lo preparado son exactamente el
comienzo de los del pedido entero; si no (otro estado, el borde tokenizado
distinto), se procesa todo como antes. Lo preparado sigue sirviendo mientras el
estado no cambie.

Además, lo forzado de la llamada (`<tool_call>\n<function=` y el resto de un
nombre una vez elegido) entra al modelo en una sola pasada: los logits solo
hacen falta donde el árbol de nombres se abre. De a un token eran ~250 ms.

Medido fuera de la app (M1 Pro, 6 estados reales × 23 frases = 92 pedidos):

| | primer token | pedido entero |
|---|---|---|
| antes | 1.151 ms | 1.412 ms |
| sin preparar | 1.109 ms | 1.370 ms |
| **preparado** | **332 ms** | **780 ms** |

Lo preparado dio **exactamente** la misma salida que antes en los 92. Sin
preparar hubo 1 diferencia en 92 («dale play a la tanda»: un empate entre
`derivar` y `speaker_on_air` que se da vuelta por redondeo al pasar los tokens
juntos). En la app: primer token ~340 ms preparado, contra ~1.240 ms en frío.

### Charla, derivar y contestar: lo medido

- **Charla**: con o sin estado da lo mismo. Las preguntas las contesta en texto
  (qué es un take, RTMP, chroma, 7×8, un chiste); los saludos y lo social
  («hola», «gracias», «buen día», «te quiero») los deriva. Es del corpus: la
  charla salió de preguntas (dolly), no de saludos. Contesta en neutro
  («mantén», «gestiona»), no en rioplatense.
- **Derivar sin asistente grande**: se avisa («Eso es para el asistente grande,
  y no está andando») y la frase queda en la barra. No se le pasa a una charla
  del mismo modelo sin herramientas: con «escribile a Juan que llegue temprano»
  contestó «le escribo a Juan…» sin haber hecho nada.
- **Asistente «Modelo local»**: lo derivado no vuelve al asistente (era un
  bucle: dictado → asistente local → el mismo modelo → derivar → …).
- **Contestar una lectura** (status, scene_list…): sigue con su prompt propio.
  La forma nativa (`<tool_response>`) anda peor: la v2 no se entrenó con varios
  turnos y en 4 de 6 casos vuelve a llamar a `derivar`.
- Para el próximo entrenamiento: «un poco» da `+6` (es `+3`); «¿cuánto está la
  música?» elige `audio_set`; «¿está silenciado el micrófono?», `audio_escuchar`;
  «¿hay alguien en la sala?», `derivar`; saludos como charla; rioplatense.

## Por qué así

- **El modelo no hace cuentas.** Pedirle el volumen final daba errores de
  aritmética («subile 3 dB» a una fuente en -12 → +6, «bajale 6» → -30). Con
  el cambio con signo y la cuenta en el aparato, no hubo un solo error de
  volumen en las pruebas.
- **Los nombres los pone el aparato.** El modelo escribe «Invitado 2» o
  «música»; la validación lo lleva al nombre real («Invitado 2 (audio)»,
  «Música») o lo bloquea si no existe. Una escena solo puede ser una escena:
  «corte a cámara 3» no se va a la fuente «Cámara».
- **Solo el estado que hace falta.** Mandar todo el estado tardaba 3 s; solo
  la parte de la herramienta, 2,2 s.
- **Se confirma.** Las pruebas encontraron acciones con datos válidos pero
  con un efecto distinto del pedido (ver abajo): la tarjeta las muestra antes
  de que salgan al aire.

## Cómo se mide

`tools/voz-banco.py` — con la app abierta: para cada una de las 84
herramientas, frases de `datos/semillas-reactivas.json`, elegir → datos →
validar → **ejecutar las seguras** (lecturas, audio, escenas, previa,
transición, gráficos, oradores) y verificar el efecto (el mezclador, la
escena al aire). Después de cada una restaura el mezclador y vuelve a
«Principal». Nunca ejecuta transmisión, grabación, PIN, gateway, ni nada
destructivo. `tools/voz-banco-informe.py` arma las tablas de abajo.

El aparato de prueba: escenas Principal, Apertura, Entrevista, Panel, Doble
cuadro, Tanda, Noticias, Clima, Deportes, Móvil y Cierre; fuentes Cámara,
Logo, Zócalo, Música, Invitado 1 y 2; el micrófono de la Mac. Sin invitados
conectados (por eso las herramientas de sala y oradores dan «no existe» o
error del entorno: no hay a quién admitir).

## Resultados

### Corrida completa (v5): 84 herramientas × 3 frases = 252

| | frases | % |
|---|---:|---:|
| Elegida, datos válidos y **ejecutada** (verificada si se puede) | 75 | 30 % |
| Elegida y datos válidos, no se ejecuta en la prueba | 61 | 24 % |
| Bloqueada: nombra algo que no está en el aparato | 62 | 25 % |
| Ejecutada, la herramienta dio error (casi todo del entorno) | 27 | 11 % |
| Ejecutada con un efecto distinto del pedido (verificado) | **0** | 0 % |
| Bloqueada: falta un dato requerido | 13 | 5 % |
| El modelo no escribió un JSON usable | 9 | 4 % |
| Eligió otra herramienta | 5 | 2 % |

- **Elección: 247 de 252 (98 %)**, unos 170 ms.
- **Datos: mediana 2,45 s**, p90 3,3 s.
- **Ninguna acción verificada quedó distinta de lo pedido** (volumen, silencio,
  escena al aire).
- Los «no existe» son, en su gran mayoría, **correctos**: la frase nombra
  personas o escenas que no hay en este aparato (no había invitados
  conectados; «Sponsors», «Split 3», «Tomás»…). Se bloquean en vez de
  adivinar.
- Los errores de ejecución son del entorno: sin invitados, sin fila de
  oradores, el gateway solo escucha en esta máquina, sin carruseles de
  láminas, y tres que esperaban aprobación mientras el banco corría con la
  sesión contaminada (ya resuelto, ver abajo).

Detalle por herramienta y cada caso: [voz-modelo-local-banco.md](voz-modelo-local-banco.md).

### Las correcciones (v6), sobre las herramientas que fallaban: 51 frases

| | antes (v5) | v6 |
|---|---:|---:|
| JSON inválido | 9 | **0** |
| Eligió otra herramienta | 2 | **0** |
| Efecto distinto del pedido | 0 | **0** |

Lo que quedó bloqueado en v6 es correcto: carruseles, archivos compartidos o
personas que no existen, o referencias vagas («ese compartido») sin dato.

### Con la app (el camino real, no el banco)

«bajale 3 dB a la música» → `audio_set` (99,99 %) → `{"name":"Música","volumeDb":"-3"}`
→ validado a -3 dB sobre 0 → ejecutado solo (automática) → el mezclador
quedó en -3,0. «silenciá el micrófono» → silenciado. Todo en
`Logs/voz-modelo.jsonl`.

### Contestar con voz

La voz contesta por los auriculares del operador (Configuración → Audio y voz
→ «Salida de auriculares del operador»; nunca al aire): «Listo.» cuando hizo
algo, «No pude…» o «No me quedó claro…» con el motivo, y **la respuesta** a
una pregunta: una herramienta de lectura se hace sin pedir confirmación (no
cambia nada) y el mismo modelo local resume lo que devolvió en una oración.

| pregunta | herramienta | dijo | |
|---|---|---|---|
| ¿cómo está la transmisión? | `status` | «la transmisión está bien, sin errores y con 30 fps» | ✓ (1,7 s) |
| ¿qué escenas tengo? | `scene_list` | «tenés diez escenas: Principal, Apertura, …, Cierre» | nombra las 11 pero dice «diez» (2,7 s) |
| ¿cuánto está la música? | `audio_measure` | «la música está a -20 decibelios» | ✗ eligió medir la cadena, no el mezclador (2,9 s) |

## Lo que no funciona (todavía)

**Del modelo (para el próximo fine-tune, `datos/semillas-reactivas.json`):**

- `participant_admit` contra `participant_admit_all`: «admitilo a Federico,
  el resto que espere» elige admitir a todos (3 de 3).
- `grafico_ocultar` contra `grafico_mostrar`: «sigue clavado el gráfico,
  bajalo» elige mostrar.
- `imagen_analizar`: «mirá la toma armada y contame cuántas personas» deriva.
- «¿cuánto está la música?» elige `audio_measure` (mide la cadena de
  módulos) en vez de `mixer_list` (el volumen): contesta un nivel de prueba.
- Al resumir una respuesta cuenta mal («diez» escenas con once nombradas).

**De los datos (lo que el modelo no puede saber con el estado de hoy):**

- **Referencias vagas**: «ese orador», «el que está hablando», «ese
  archivo compartido», «subí esa escena». El estado no dice quién está al
  aire, cuál fue el último compartido ni cuál escena se está mirando. Se
  bloquean. Arreglo: sumar al estado «al aire», «último compartido»,
  «escena en previa».
- **Cambios con efecto equivocado en `source_update`**: «mandá el logo al
  frente» movió el logo a la banda de abajo (`layout: lower-third`) en vez
  de cambiar el orden; «agrandá la cámara al doble» puso medidas fuera del
  lienzo (`w: 2`); «ahora habla Gómez» dejó solo «Gómez» como texto. Los
  datos son válidos según el esquema, así que la validación no los frena:
  **para esta herramienta conviene no dejarla automática** y mirar la
  tarjeta antes de hacer. Arreglo: reglas y ejemplos propios de layout y
  orden en `datos/voz-argumentos.txt`, y validar que las medidas entren en
  el lienzo.
- **Valores de otro tipo de dato**: «subile 2 minutos a Pérez» escribe
  `"+2:00"`; los tiempos y posiciones relativos sin valor actual se toman
  con signo.
- **Escenas armadas enteras** (`scene_define`): con 96 tokens se cortaba;
  ahora tiene 256, sin medir todavía con aparato.

**Del entorno de prueba:** sin invitados conectados, todas las herramientas de
sala, oradores, chat y capturas de personas quedan sin probar de punta a
punta: el modelo escribe los datos, pero no hay a quién aplicarlos. Para
probarlas hace falta una sala con dos invitados de prueba.

## Seguridad y aprobaciones

- La voz lee el estado del aparato en una sesión MCP y actúa en otra. Leer
  nombres de invitados contamina la sesión que lee (por la salvaguarda del
  MCP), y si fuera la misma, cada acción de voz pediría aprobación.
- Configuración → Asistente (MCP) → «Aprobar acciones del asistente»: «Si leyó
  contenido de invitados» (recomendado), «Siempre» o «Nunca: aprobación
  automática». Es del operador y se aplica al guardar.
- Las acciones de voz automáticas son otra lista (Configuración → Audio y
  voz), y solo se hacen si la validación no encontró nada raro.

## Próximos pasos

1. Sumar al estado «al aire», «último compartido» y «en previa», para las
   referencias vagas.
2. Reglas y ejemplos de `source_update` (layout y orden) y validar medidas.
3. Correr el banco con una sala con dos invitados de prueba.
4. Llevar los casos de elección errada a las semillas y reentrenar.
5. Medir `scene_define` con 256 tokens.
