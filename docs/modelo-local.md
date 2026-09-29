# El modelo chico que atiende los eventos inmediatos

Cuando alguien grita **«está acoplando»**, el aparato tiene que elegir la
herramienta antes de que el operador se dé vuelta. Eso no lo puede hacer un
modelo remoto: la ida y vuelta se come el presupuesto. Tiene que correr acá.

Este documento junta **lo medido**, que es lo único que vale para decidir.

## La tarea, bien definida

No es «usar herramientas»: es **elegir una entre 73**, dada una frase corta de
cabina, y que el aparato ponga los argumentos. Clasificación, no razonamiento.

Por qué 73 y no 291: el repertorio reactivo —lo que se toca cuando algo se
rompe— es chico y **fijo**. Eso permite meter las fichas enteras en el prompt
y cachearlas. Para lo demás sigue estando el buscador (28 µs, 125/125 en las
primeras tres).

## Dónde se va el tiempo

Notebook, Ryzen mobile, 8 hilos, **sin GPU**. Qwen3-1.7B. 48 casos del examen.

| | prefill | generar | total |
|---|---|---|---|
| Primera vez (prefijo frío) | 23,0 s | 0,13 s | **23,1 s** |
| Con el prefijo cacheado | 0,63 s | 0,15 s | **0,79 s** |

**Cachear el prefijo saca 22 segundos.** Sin eso no hay producto. Se releen 16
tokens por pedido en vez de 2.647.

Y después del caché, **lo caro es escribir, no leer**: esta máquina genera a
11-14 tokens/s.

| forma de contestar | tokens | total |
|---|---|---|
| JSON entero (`{"herramienta":…, "argumentos":…}`) | ~30 | 2,8 s |
| sólo el nombre | ~4 | **0,79 s** |

Mismo acierto. Los argumentos los sabe el aparato, que tiene la lista de
canales y cuál está picando.

## Cuánto acierta sin entrenar

| | acierto | total |
|---|---|---|
| Q4_K_M | 58% | 0,79 s |
| Q8_0 | 62% | 0,92 s |
| Q4 + 73 ejemplos en contexto | 64% | 1,62 s |
| Q4 + 146 ejemplos en contexto | 62% | 1,87 s |

El aprendizaje en contexto **ayuda y se aplana**. Y deja ver que cacheado no es
gratis del todo: la atención sobre un KV más largo costó 400 ms por pedido.

**Línea de base a superar: 62%.**

## Con el fine-tune

Primer entrenamiento, 1.079 ejemplos, dos configuraciones en paralelo (una 3090
cada una, ~26 min). Mismo examen de 48 casos.

| | acierto |
|---|---|
| Base sin entrenar, bf16 (3090) | 62% |
| Base sin entrenar, Q8_0 (notebook) | 62% |
| ~~v2 entrenado, bf16~~ | ~~85%~~ **no vale, ver abajo** |

**Q8 no pierde nada**: 62% en bf16 y 62% cuantizado. Los 4 puntos que vimos
contra Q4 los paga Q4, no la cuantización en general. Eso sigue valiendo.

### El 85% estaba inflado, y así se descubrió

Escribiendo semillas nuevas noté que las había sacado de la lista de fallos
—y esa lista sale del examen—. Al medirlo: **25 de los 48 casos del examen
tenían una frase casi igual adentro del entrenamiento**.

La guarda existía pero miraba sólo coincidencias **exactas**: tiraba «se corto
la transmision» por idéntica y dejaba pasar «eh, se corto la transmision». El
modelo se aprendió medio examen.

Lo que se hizo:

1. La guarda ahora compara **por palabras compartidas** (60%), no por
   identidad, y **avisa qué semilla hay que reescribir** en vez de descartarla
   en silencio.
2. Se reescribieron **las 58 semillas** que chocaban: mismo pedido, otras
   palabras. Reescribir y no borrar, porque el pedido tiene que estar — si el
   modelo aprendió el concepto, el examen lo mide igual.
3. Verificado después: **0 de 48** casos contaminados, y el solapamiento más
   alto que queda es 0,57 («sacalo de la sala a esa persona» contra «echá a ese
   tipo de la sala»), que es otra forma de decir lo mismo.

El número real del fine-tune sale de reentrenar sobre el corpus limpio. Lo que
sí queda en pie es que **el corpus contaminado no explica todo**: el modelo sin
entrenar veía el mismo examen y sacaba 62%.

## Dónde se equivoca

Los fallos que miré uno por uno son **todos entre herramientas vecinas**, por
una palabra de superficie:

    poné al aire la apertura       -> speaker_on_air   (por «on_air» en el nombre)
    cortá a la cámara 2            -> transition_set   (por «corta» -> corte)
    dejá preparada la del cierre   -> scene_switch     (armar contra tomar)
    qué hay adentro de la escena   -> capture_source

Eso no se arregla con volumen. Por eso el corpus tiene pares escritos para
distinguir vecinas, y no sólo más paráfrasis de lo mismo.

### Y después de entrenar, el error cambia de forma

Los que quedan en v2 ya casi no son entre vecinas. Son otra cosa, peor:

    se está cortando la transmisión  -> stream_stop      (esperaba network_status)
    se cayó un invitado              -> participant_kick (esperaba room_status)
    se está llenando el disco        -> record_stop      (esperaba system_hardware)

**Lee el síntoma como una orden.** El operador reporta un problema y el aparato
lo ejecuta: dice que se está cortando la transmisión y el modelo la corta. En
una batería cuenta como un error más; en vivo no.

Tiene dos arreglos, y conviene hacer los dos:

1. **En el corpus**: pares *reporte* contra *orden* («se está cortando la
   transmisión» contra «cortá la transmisión»). Es la misma técnica que ya
   funcionó para armar contra tomar.
2. **En el aparato, que es más fuerte**: `docs/mcp.json` ya marca 24
   herramientas como destructivas. Que una destructiva elegida por el modelo
   chico pida confirmación convierte esto en una pregunta en vez de un
   accidente. Hacerlo imposible, no desaconsejarlo.

### El ciclo para mejorar, que es mecánico

`tools/entrenar/fallos.py` clasifica los fallos del examen en tres tipos y dice
a qué semillas hay que meterles mano. No es una heurística: sale de las marcas
que ya tiene `docs/mcp.json`.

| tipo | qué pasó | qué escribir |
|---|---|---|
| **PELIGROSO** | eligió una destructiva cuando todo lo aceptable era de sólo lectura | pares *reporte* contra *orden* |
| **VECINA** | misma categoría, se confundió por una palabra | frases que las **distingan** |
| **LEJOS** | otra categoría | vocabulario que le falta a la esperada |

    tools/bateria-local.py --modelo <gguf> --solo-nombre   # mide
    tools/entrenar/fallos.py docs/bateria-local.json       # dice qué escribir
    # …editar datos/semillas-reactivas.json…
    make corpus-entrenamiento                              # regenera
    # …reentrenar…

También avisa cuando una respuesta aceptable **está fuera del repertorio
reactivo**: ahí no faltan frases, el modelo directamente no puede elegirla
porque la gramática no la permite. Eso se arregla moviendo `REACTIVAS` en
`lib_prompt.py`, que cambia el prompt y obliga a reentrenar — y
`corpus-descubrir` lo va a avisar.

## Un error propio que vale más que cualquier número

Probando la respuesta corta dejé el sistema pidiendo JSON mientras la gramática
sólo dejaba escribir el nombre. **El acierto se desplomó de 56% a 16%.**

No era que el modelo no supiera: le pedíamos una cosa y lo forzábamos a otra.
Es el mismo desajuste que arruina un fine-tune cuando se entrena con una
plantilla y se sirve con otra, y por eso ahora el prompt lo arma **un solo
módulo** (`tools/lib_prompt.py`) para el banco, el corpus y el entrenamiento.

## Las piezas

| | |
|---|---|
| `tools/lib_prompt.py` | el prompt, una sola vez, para todos |
| `tools/corpus-entrenamiento.py` | arma `datos/entrenamiento/` desde las semillas |
| `datos/semillas-reactivas.json` | cómo pide un operador cada cosa, escrito a mano |
| `tools/entrenar/entrenar.py` | el fine-tune (servidor con GPU) |
| `tools/entrenar/evaluar.py` | el examen ahí mismo, para iterar rápido |
| `tools/bateria-local.py` | **la medición que vale**: GGUF en la notebook |
| `tools/deps/llamacpp.sh` | llama.cpp vendoreado (CPU: la GPU la usa libobs) |

## Lo que todavía no es

El corpus **no es dato real de operación**. Las frases son verosímiles, no
observadas: enseñan el mapa, no la distribución de lo que pasa un martes a las
ocho. Lo real sale de la traza del aparato —qué hizo el operador y en qué
estado estaba todo— y **todavía no se guarda**. Ese es el paso que más va a
mover el número.
