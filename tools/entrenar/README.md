# Entrenar el modelo chico de eventos inmediatos

Lo que se quiere: que cuando alguien diga **«está acoplando»**, el aparato
elija `audio_measure` o `audio_set` en menos de lo que tarda el operador en
darse vuelta. Hoy eso lo decide un modelo de 1.7B corriendo en la notebook, en
CPU, y **acierta poco**: ese es el problema que este entrenamiento ataca.

## Qué es la tarea, exactamente

No es «usar herramientas». Es **elegir una entre 73**, dada una frase corta de
cabina. Clasificación con relleno de huecos, no razonamiento — y por eso un
modelo chico puede servir.

El buscador del aparato (`McpIndice`, 28 µs) ya resuelve la parte de *buscar*
entre 291. Para los reactivos ni siquiera hace falta: el repertorio es fijo y
chico, así que van las 73 fichas enteras en el prompt, **cacheadas**.

## Las mediciones que justifican cada decisión

Todas en la notebook (Ryzen 4000/5000, 8 hilos, sin GPU), 48 casos del examen.

| | prefill | generar | total | acierto |
|---|---|---|---|---|
| Primera vez (prefijo frío) | 23,0 s | 0,13 s | 23,1 s | |
| Con el prefijo cacheado | 0,63 s | 0,15 s | **0,79 s** | 58% (Q4) |
| Q8_0 | | | 0,92 s | 62% |
| Q4 + 73 ejemplos en contexto | | | 1,62 s | 64% |
| Q4 + 146 ejemplos en contexto | | | 1,87 s | 62% |

De acá salen cuatro decisiones:

1. **Cachear el prefijo** saca 22 segundos de encima. Sin eso no hay producto.
2. **Contestar sólo el nombre**, no el JSON: 0,79 s contra 2,8 s, mismo
   acierto. Los argumentos los pone el aparato, que sabe qué canal está
   picando.
3. **Los ejemplos en contexto ayudan poco y se aplanan** (58 → 64 → 62). Sirven
   como piso, no como solución. Y cacheado no es gratis del todo: la atención
   sobre un KV más largo costó 400 ms por pedido.
4. Por lo tanto, **entrenar**. Con el 62% del Q8 como línea de base a superar.

## El método, y por qué cada parte

- **El prompt es el mismo que se sirve**, fichas incluidas, armado por
  `tools/entrenar/lib_prompt.py` — un solo módulo para el banco, el corpus y esto. Este
  proyecto ya se comió el error contrario: con el sistema pidiendo JSON y la
  gramática dejando escribir sólo el nombre, el acierto cayó de 56% a 16%.
  Parecía que el modelo no sabía; le pedíamos una cosa y lo forzábamos a otra.
- **La pérdida, sólo sobre la respuesta.** El prompt son 2.650 tokens idénticos
  en todos los ejemplos: dejarlo aportar es enseñarle a predecir su propio
  prompt. El script **imprime sobre qué texto calcula la pérdida** antes de
  arrancar, porque una máscara mal puesta no falla, entrena mal.
- **LoRA sobre todas las lineales**, también las del MLP. Con sólo `q,v` el
  modelo aprende a reconocer palabras pero no a reasignar clases parecidas, y
  ahí es donde están los errores.
- **Validación partida por frase semilla**, no al azar: si la misma intención
  queda de los dos lados, validar mide memoria.
- **El examen no se toca jamás.** Son las preguntas del corpus de siempre
  (`src/app/tests/datos/preguntas.json`), escritas antes de que existiera la
  idea de entrenar. El generador descarta las variantes que caen encima de una
  de ellas (38 esta vez).

## Dónde están los errores hoy

Los cuatro fallos que miré uno por uno eran **todos entre herramientas
vecinas**, por una palabra de superficie:

    poné al aire la apertura       -> speaker_on_air   (por «on_air» en el nombre)
    cortá a la cámara 2            -> transition_set   (por «corta» -> corte)
    dejá preparada la del cierre   -> scene_switch     (armar contra tomar)
    qué hay adentro de la escena   -> capture_source

Eso **no se arregla con volumen**. Por eso el corpus tiene pares escritos para
distinguir a las vecinas, y no sólo más paráfrasis de lo mismo.

## Correrlo

En el servidor con las 3090:

    pip install unsloth trl datasets
    tools/entrenar/entrenar.py --prueba          # 20 pasos, a ver que arranca
    tools/entrenar/entrenar.py --salida modelos/reactivo-v1

Un 1.7B entra entero en una 3090, así que no hace falta QLoRA. Con ~1.100
ejemplos de 2.700 tokens son unos pocos minutos por época.

Después, **en la notebook**, contra el mismo examen:

    tools/entrenar/bateria-local.py --modelo modelos/reactivo-v1-q8_0/...gguf --solo-nombre

## Cuando agregás herramientas al MCP

El corpus no se entera solo. Y hay dos cosas distintas que se rompen:

1. **La herramienta nueva no existe para el modelo**: nunca la vio como
   respuesta, así que no la va a elegir nunca.
2. **El prompt cambió**: las fichas son parte de lo que se entrenó. Con una
   ficha más, al modelo le estás mostrando algo distinto de lo que vio —y
   encima puede elegir una herramienta que no aprendió—.

Lo segundo es peor porque no se nota: el modelo sigue contestando.

    make mcp-inventario      # el aparato andando: refresca docs/mcp.json
    make corpus-descubrir    # que falta, y si el prompt cambio

`corpus-descubrir` compara la **firma del prompt** contra la que quedó guardada
con el conjunto (`datos/entrenamiento/huella.json`) y avisa cuando el modelo
que tenés fue entrenado con otras fichas.

Con `--generar` propone frases para lo nuevo, sacadas de las **etiquetas del
buscador** —lo más parecido a «cómo lo pediría alguien» que hay escrito—. Pero
son bolsas de palabras, no frases dichas: salen cosas como `imagen labios
imagen`. Van a `datos/semillas-auto.json` marcadas como propuestas, entran al
entrenamiento igual (mejor eso que no existir) y **se corrigen y se promueven a
mano** a `datos/semillas-reactivas.json`. El generador dice cuántas herramientas
del corpus son de segunda mano.

    make corpus-descubrir ARGS=--generar   # o: tools/entrenar/corpus-descubrir.py --generar
    # …editar datos/semillas-auto.json, mover lo bueno a semillas-reactivas.json…
    make corpus-entrenamiento

## Cómo mejorar (el ciclo)

Medir da un número; **clasificar los fallos da la próxima tarea**. Los fallos
no son todos iguales y cada tipo se arregla distinto — con más volumen no se
arregla ninguno.

    tools/entrenar/bateria-local.py --modelo <gguf> --solo-nombre   # mide y deja el json
    tools/entrenar/fallos.py docs/modelos/bateria-local.json       # los clasifica

Salen en tres grupos, separados con las marcas de `docs/mcp.json` y no a ojo:

- **PELIGROSO** — eligió una herramienta destructiva cuando todo lo aceptable
  era de sólo lectura: había que **mirar** y fue a **tocar**. Es el tipo que
  quedó después de entrenar, y el único que importa más que su cuenta.
  Se arregla con pares *reporte* contra *orden*.
- **VECINA** — misma categoría, confundida por una palabra de superficie.
  Se arregla con frases que las distingan, una en cada semilla.
- **LEJOS** — otra categoría. Falta vocabulario en la esperada.

El informe termina diciendo **a qué semillas meterles mano** y cuántas frases
tiene cada una hoy. Después: `make corpus-entrenamiento` y reentrenar.

## Lo que este corpus no es

No es dato real de operación. Las frases son verosímiles, no observadas:
enseñan el mapa, no la distribución de lo que de verdad pasa un martes a las
ocho. Lo real sale de la traza del aparato —qué hizo el operador y en qué
estado estaba todo— y **todavía no se guarda**. Ese es el próximo paso, y el
que más va a mover el número.

## El vector de control: empujar a `derivar` sin reentrenar

Idea de Jorge, y es una abliteración con el signo cambiado. Una abliteración
borra la dirección del rechazo en el espacio de activaciones y apaga el
comportamiento; esto encuentra la dirección que separa «es mío» de «lo paso al
grande» y la **empuja**.

    tools/entrenar/venv.sh                       # el entorno, por script
    make vector-control MODELO=~/entrenamientos/reactivo-v6

Eso hace dos cosas: arma el `.gguf` y después barre la escala sobre el examen.

**Lo que lo distingue de todo lo demás de esta lista: es un dial.** La escala es
un solo escalar que se aplica al servir, así que el punto de operación se elige
*después* de entrenar. Cuanto más alta, más deriva.

    llama-cli --control-vector-scaled reactivo-v6-derivar.gguf:1.5

**Cómo se lee la curva.** Precisión es lo que se ve al aire; cobertura es
velocidad —lo que deriva de más lo resuelve el grande igual, pero tarda—. Se
elige la escala más baja que deje la precisión donde la quieras. **No** la que
maximice el acierto total: el acierto total premia derivar de más, que es justo
lo que no queremos.

**Qué mirar para saber si sirvió.** El script imprime la norma de la dirección
por capa. Si salen todas parejas, no hay dirección: hay ruido, y el vector no va
a empujar nada. Lo esperable es que unas pocas capas del medio manden — que es
lo que el trabajo de interpretabilidad sobre Qwen3 predice, porque ahí la
incertidumbre es un override disperso de pocos rasgos dedicados.

El vector se calcula desde `entrenar.jsonl`, **nunca desde el examen**: si
saliera del examen, la curva estaría ajustada a él y no mediría nada.

## Curar el corpus: el bucle

    tools/entrenar/bucle-corpus.py --modelo <m> --diagnosticar --json diag.json
    tools/entrenar/bucle-corpus.py --modelo <m> --evaluar candidatas.json --aceptar

Una persona o una LLM grande propone frases; una medicion adentro del modelo
decide si entran: tienen que caer en su grupo, sumar variedad fuera de los ejes
hacia las vecinas, y no subir la CDNV direccional de ningun par por encima del
objetivo. Sirve para cualquier conjunto de herramientas.

**Documentacion completa, con la matematica y lo que salio mal:
[docs/modelos/bucle-corpus.md](../../docs/modelos/bucle-corpus.md).**
