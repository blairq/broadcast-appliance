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

## Lo que no es suyo: `derivar`

El repertorio reactivo son 85 herramientas de un MCP de 292. Las otras 207 son
administrativas —armar una lámina, configurar una cuenta, editar una receta— y
el modelo chico no las tiene entre sus fichas.

**Sin una salida, no puede no contestar.** La gramática lo obliga a escribir uno
de los nombres que existen, así que ante un pedido ajeno elige el pariente más
cercano. Medido: con doce herramientas que no conocía, eligió la vecina **3 de 3
veces**, siempre con aplomo. Por eso hay un nombre más en la gramática.

**Es `derivar`, no «no puedo».** Decir que no es un callejón sin salida: el
pedido se pierde y hay que repetirlo. Derivar lo atiende — el asistente grande
tiene las 292 herramientas, el buscador y el estado del aparato, así que lo que
el chico no sabe hacer, aquel sí. El chico no decide si el pedido es *imposible*
(no puede saberlo), decide si es *suyo*.

**Una sola clase, no dos.** Separar «no se puede» de «no me toca» le pide una
distinción que va a errar, y errarla del lado malo es rechazar algo que el grande
podía hacer. Esa distinción la hace el grande.

**El contrato al servir:** el chico contesta `derivar` y el aparato le pasa al
grande la frase **original, tal cual se dijo**. No una interpretación: el chico
ya demostró que no la entendió.

Los ejemplos salen de dos lados: unas frases escritas a mano para lo que está
fuera del aparato (el clima, mandar un mail) y lo ambiguo («sacalo» sin decir a
quién), y las 207 herramientas administrativas, repartidas por categoría —si se
toman de corrido aprende a rechazar la palabra «agente» en vez de un tipo de
pedido—. La segunda mitad es la que importa: lo de afuera no se parece a nada
suyo y lo rechaza casi solo; lo de adentro que no le toca es lo que lo tienta.

Con una guarda al revés: si una frase generada se parece a una del repertorio, no
entra. Enseñarle a derivar algo que **sí** es suyo no se nota en el número
general y le saca una herramienta de las manos.

**Con techo del 12 %.** Derivar es la respuesta cómoda: nunca se equivoca de
herramienta. Si sobran ejemplos, el chico deriva todo y deja de servir para lo
único que sirve, que es contestar en 0,8 s sin salir de la máquina.

**Y el acierto a secas deja de medir.** Uno que deriva todo saca 28 % y parece
prudente; uno que no deriva nunca saca 72 % y parece mejor de lo que es. Los dos
errores además cuestan distinto: derivar de más cuesta segundos, elegir mal
cuesta al aire. `evaluar.py` informa las dos mitades separadas —cobertura de lo
suyo, precisión de lo que atendió— y cuánto de lo ajeno pasó bien.

**Lo que esto no entrena:** la *duda*. «Che, eso» con tres escenas posibles no es
fuera de alcance, es baja confianza, y se resuelve en el momento con un umbral
sobre la probabilidad del nombre elegido, no con ejemplos.

## Lo que dice la literatura, y qué de eso nos aplica

Buscado en septiembre de 2026, después de tener el método armado. Sirvió para
confirmar dos decisiones, corregir una y descartar una tercera.

### El fine-tune gemelo confirma el techo del 12 %

Existe un [Qwen3-1.7B afinado para tool calling](https://github.com/pmrccs/qwen3-1.7b-tool-calling-v4)
sobre la misma base `unsloth/Qwen3-1.7B`: LoRA r=16, 300 pasos, lr 5e-5 coseno.
Su mezcla de datos es **85 % llamadas a herramientas y 15 % `xlam-irrelevance-7.5k`**,
un corpus de pedidos que no corresponden a ninguna herramienta.

O sea que el fine-tune público más parecido al nuestro también le reserva a la
clase «esto no es para mí» una porción de ese orden. Nosotros pusimos techo en
12 % razonando desde cero. Llegamos al mismo número por caminos distintos.

(Sus números —BFCL v4: 72,6 % Non-Live AST, 58,6 % Live— no comparan con los
nuestros: ellos generan los argumentos, nosotros elegimos 1 entre 86 con
gramática, que es mucho más fácil. Lo que compara es el reparto.)

También anotan que los formatos de esquema ausentes del entrenamiento rindieron
mal. Es lo mismo que medimos acá al agregar 12 herramientas no vistas: 0/3.

### La confianza de un 1,7B no sirve como señal, y eso ordena las prioridades

[Revisiting Uncertainty Estimation and Calibration of LLMs](https://arxiv.org/abs/2505.23854)
barre la serie Qwen3 de 0,6B a 32B y encuentra que **los chicos —0,6B y 1,7B,
el nuestro— no producen incertidumbre útil**: los de 14B y 32B los superan
consistentemente en calibración (ECE) y en clasificación selectiva (AUROC).
Aparte, Qwen arranca fuertemente sobreconfiado: hace falta un escalado de
temperatura de T ≈ 4,6 para calibrarlo.

**Consecuencia para el diseño.** `derivar` como clase entrenada es el mecanismo
PRINCIPAL, no el secundario. Un 1,7B reconoce bastante bien que un pedido no se
parece a lo suyo, y bastante mal cuánta confianza tiene. Era al revés en el plan
original, que ponía un umbral sobre la probabilidad como la solución.

Un umbral sigue teniendo lugar para la *duda* dentro de lo suyo, pero **va
calibrado** (el escalado de temperatura es un parámetro solo, se ajusta sobre
`validar.jsonl` y es lo más barato que hay), y hay que medir antes de creerle.

### Sobre derivar: tres papers, y uno nos corrige

- [Conformal Cascade](https://arxiv.org/abs/2607.25018) (2026) sostiene que el
  umbral de confianza está mal como regla de derivación —mal calibrado, hay que
  ajustarlo por par de modelos y por dominio, no da garantías— y propone usar el
  **tamaño del conjunto conforme**: se acepta si el conjunto se reduce a una sola
  respuesta, se deriva si no. No necesita entrenamiento, sólo un set de
  calibración. Para nosotros es barato porque la gramática nos da una
  distribución honesta sobre los 86 nombres, no una confianza inventada sobre
  texto libre. **Pero** asume que el orden de las probabilidades separa aciertos
  de errores, y el paper de arriba dice que en 1,7B esa separación es floja: si
  los conjuntos salen grandes, deriva de más. Se mide antes de adoptarlo.
- [GATEKEEPER](https://arxiv.org/abs/2502.19335) (ICML 2025) es una pérdida que
  afina al chico para tener **confianza alta cuando acierta y baja cuando no**.
  Ataca la duda, que es justo lo que la clase `derivar` no cubre. Cambia
  `entrenar.py` y obliga a re-medir todo.
- [Signed Rescue Routing](https://arxiv.org/html/2609.07786) (2026) **corrige
  algo que estaba escrito mal acá**: derivar de más no cuesta sólo segundos. El
  grande puede elegir otra herramienta y equivocarse. Miden 12,4 % de casos donde
  el grande rescata y **5,3 % donde empeora**, y derivar el 100 % rindió peor que
  derivar el 50 %. La señal correcta no es «el chico duda» sino «derivar sirve».
  El techo del 12 % queda mejor justificado de lo que lo justificamos.

### Cuantización: la caída del Q4 es esperable, y las rotaciones

Nuestra medición fue Q4 58 % contra Q8 62 %. La literatura explica las dos
mitades: los modelos chicos son más frágiles a la cuantización que los grandes
—menos parámetros, menos margen para absorber el error numérico— y los modelos
con **GQA sufren especialmente con Q4**, porque compartir las proyecciones K/V
entre grupos interactúa mal con cuantizar los pesos. Qwen3-1.7B es GQA. **Va Q8
al aparato.**

**Rotaciones de Hadamard.** [QuaRot](https://arxiv.org/html/2404.00456) rota los
estados ocultos con matrices de Hadamard: como la rotación no cambia los
productos internos, la salida es la misma, pero los *outliers* —que son lo que
arruina la cuantización— se reparten y desaparecen. [SpinQuant](https://arxiv.org/abs/2405.16406)
aprende la rotación en vez de fijarla y le saca hasta 16 puntos.

Qué de esto nos toca, que es menos de lo que parece:

- **Del lado de los pesos, nada por ahora.** QuaRot y SpinQuant necesitan cirugía
  sobre el grafo para absorber las rotaciones en las capas lineales vecinas, y el
  cuantizador de llama.cpp no hace eso: ataca el mismo problema con **imatrix**,
  que es lo que ya usa `tools/entrenar/cuantizar.sh`.
- **Del lado de las activaciones, ya lo tenemos y ni lo sabíamos.** llama.cpp
  mergeó el 1 de abril de 2026 la rotación de Hadamard de Q/K/V antes de guardar
  en el caché ([PR #21038](https://github.com/ggml-org/llama.cpp/pull/21038)), y
  **el build que vendoreamos —b11255— la trae** (se ve el símbolo
  `LLAMA_ATTN_ROT_DISABLE` en la biblioteca). Mejora q4_0 … q8_0 del caché KV, no
  hace falta re-cuantizar nada, y pide que la dimensión de cabeza sea múltiplo de
  64: Qwen3-1.7B tiene 128.
- **Pero hoy no nos cambia el número**, porque el caché KV lo tenemos en fp16.
  Donde sí importaría es si algún día hay que achicar el prefijo cacheado —2.650
  tokens que son lo que hace que conteste en 0,8 s— para que entre en menos
  memoria: la rotación es lo que vuelve seguro cuantizar ese caché. Anotado para
  cuando la notebook apriete, no antes.

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
