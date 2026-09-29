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

## Dónde se equivoca

Los fallos que miré uno por uno son **todos entre herramientas vecinas**, por
una palabra de superficie:

    poné al aire la apertura       -> speaker_on_air   (por «on_air» en el nombre)
    cortá a la cámara 2            -> transition_set   (por «corta» -> corte)
    dejá preparada la del cierre   -> scene_switch     (armar contra tomar)
    qué hay adentro de la escena   -> capture_source

Eso no se arregla con volumen. Por eso el corpus tiene pares escritos para
distinguir vecinas, y no sólo más paráfrasis de lo mismo.

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
