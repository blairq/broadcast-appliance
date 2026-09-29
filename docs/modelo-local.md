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

### Cómo se arregla eso desde el entrenamiento

Si la confianza de un 1,7B no sirve como señal, la salida es enseñarle la
incertidumbre en vez de leérsela. Hay tres caminos, de más a menos aplicable acá.

**R-Tuning** ([NAACL 2024, paper destacado](https://aclanthology.org/2024.naacl-long.394/))
es el que más nos sirve, y es casi el que ya estamos corriendo sin saberlo. El
método: se pasa el conjunto de entrenamiento por el modelo, se parte en **lo que
contesta bien y lo que contesta mal**, y a lo que contesta mal se le cambia la
etiqueta por la expresión de incertidumbre. O sea que el «no sé» no sale de lo
que alguien supone que el modelo no sabe, sale de **lo que el modelo demostró no
saber**. Resultados: mejora el acierto sobre lo que sí contesta *y* la capacidad
de rechazar, y el rechazo **generaliza a tareas que no vio** — lo tratan como una
meta-habilidad, no como una lista de casos.

Nosotros ya tenemos las dos piezas: `evaluar.py` mide, `fallos.py` clasifica los
errores de cada versión. Lo único que falta es la decisión de qué hacer con un
error, y hoy hacemos siempre lo mismo: escribirle una frase correctiva que le
enseñe la respuesta buena. R-Tuning dice hacer lo contrario: etiquetarlo
`derivar`.

**Los dos no se contradicen, se ordenan.** La regla que queda:

- error de **v1** → frase correctiva. Puede que sólo le falte verlo escrito.
- error que **v2 sigue teniendo** después de habérselo enseñado → `derivar`.
  Si dos rondas de corrección no lo movieron, no es que le falte un ejemplo: no
  lo distingue. Y una herramienta equivocada al aire se ve; una derivada, no.

Eso ata `derivar` a lo que el modelo de verdad no sabe, en vez de a las cuatro
familias que escribí yo a ojo. Es la continuación natural del ciclo que ya está
armado, y no pide tocar la pérdida ni el serving.

**Calibration-Tuning** ([CT](https://arxiv.org/html/2609.33290)) afina con LoRA
para que el modelo prediga **si su propia respuesta es correcta**, con una
pérdida de clasificación más un regularizador de divergencia para no arruinarle
lo que ya sabía. Es más máquina que R-Tuning y produce un número de confianza
propio en vez de una etiqueta. Queda para después de medir R-Tuning.

**Sondas sobre los estados ocultos.** El hallazgo repetido en varios trabajos es
que *los estados ocultos codifican linealmente si la respuesta va a ser correcta*
— una regresión logística sobre el último estado alcanza, y calibra bastante
mejor que pedirle al modelo que verbalice su confianza, con un orden de magnitud
menos de cómputo ([probes](https://arxiv.org/html/2512.22245),
[CCPS](https://arxiv.org/abs/2505.21772)). Sería lo más barato de todo…

…salvo por dónde servimos. **Acá el problema es llama.cpp, no el método**: la
sonda necesita el estado oculto de cada pedido, y nuestro camino de servicio es
decodificación con gramática, no extracción de embeddings. Sacar las dos cosas
del mismo paso no es gratis. Por eso va último, aunque en el papel sea el más
barato: el costo no está en entrenarlo, está en el aparato.

### Lo que se midió, y el error que casi lo entierra

**v6** (85 herramientas, jerga, láminas y `derivar`), 67 casos del examen:

| | cobertura | precisión | derivado | acierto |
|---|---|---|---|---|
| sin dial | 100 % | 88 % | 5/8 (62 %) | 85 % |
| sesgo +2 | 100 % | 88 % | **6/8 (75 %)** | 86 % |
| sesgo +6 | 98 % | 87 % | **7/8 (87 %)** | 86 % |
| sesgo +8 | 94 % | 87 % | 7/8 | 83 % |
| sesgo +12 | 72 % | 90 % | 7/8 | 68 % |

Dos cosas importan más que el acierto. Una: **cobertura 100 % sin dial** — no
deriva ni una sola vez algo que sí es suyo, que era el riesgo que el techo del
12 % trataba de evitar. Dos: **+2 sale gratis**, sube las derivaciones de 62 %
a 75 % sin costar un punto de cobertura ni de precisión. `+6` compra el 87 % de
las derivaciones por un punto de cobertura. De ahí para arriba se paga caro.

Los tres que no deriva sin dial son los difíciles de verdad: «cargá el padrón»,
«sacalo» y «probá cómo queda este gráfico» —esta última es `grafico_probar`,
una administrativa cuyo pariente reactivo, `grafico_mostrar`, está a una
palabra—.

**El vector de control también anda**, y más fuerte de lo previsto:

| escala | cobertura | precisión | derivado |
|---|---|---|---|
| 0 | 100 % | 88 % | 5/8 |
| 0,02 | 93 % | 89 % | 7/8 |
| 0,05 | **0 %** | — | 8/8 |

O sea que la dirección existe y empuja. Pero entre 0,02 y 0,05 pasa de andar
bien a derivar absolutamente todo: es un interruptor disfrazado de perilla. Al
mismo nivel de derivaciones (7/8), el sesgo pierde un punto de cobertura y el
vector pierde cuatro.

**Conclusión: el dial es el sesgo del logit.** Es monótono, fino, no necesita
entrenamiento —anda sobre cualquier versión, incluso la base— y llama.cpp ya lo
trae (`--logit-bias`). El vector queda como resultado interesante y herramienta
de diagnóstico, no como el mando.

#### El error, que vale más que los números

Las dos primeras corridas de todo esto dieron «0 de 8 derivaciones, en todas
las escalas», y escribí dos conclusiones sobre esa base: que entrenar la clase
no instalaba el comportamiento, y que la dirección del vector codificaba *de
qué habla* el pedido y no la decisión. **Las dos eran falsas.**

`evaluar.py` armaba su propia lista de salidas con `herramientas_reactivas()`
—las 85, sin `derivar`— y con ella construía el árbol de continuaciones
válidas. `derivar` era una respuesta **imposible**. El «0 de 8» no medía al
modelo: medía la lista.

Lo delató el barrido del sesgo: plano hasta +20. Sumarle veinte al logit de un
token y que no cambie nada es imposible **si ese token pudiera salir**.

Es el mismo error que este proyecto ya se comió con el prompt —el sistema
pidiendo una cosa y la gramática permitiendo otra, 40 puntos— y por el mismo
motivo: dos lugares armando lo que tiene que ser uno. Por eso ahora existe
`lp.nombres_posibles()` y la arma sola. `bateria-local.py` nunca tuvo el
problema porque pasaba por `gramatica_nombre()`; el que se salió del camino fue
el evaluador.

La moraleja operativa: **un resultado plano no es un resultado, es un síntoma.**
Si mover un mando a fondo no cambia nada, lo primero que hay que sospechar no es
el modelo, es que el mando no esté conectado.

### Forzar el comportamiento por dirección, como una abliteración al revés

La idea es de Jorge y tiene respaldo. La abliteración funciona porque
[el rechazo en una LLM está mediado por una sola dirección](https://arxiv.org/abs/2406.11717)
(Arditi et al., NeurIPS 2024): se encuentra esa dirección en el espacio de
activaciones y se la borra. Lo que se propone acá es el signo contrario —en vez
de borrar la dirección del rechazo, **empujarla**— y esa construcción ya tiene
nombre y método: vectores de control / adición contrastiva de activaciones
([CAA](https://arxiv.org/abs/2312.06681)). Se toman pares contrastivos, se
promedia la diferencia de activaciones capa por capa, y eso es el vector.

**Por qué hay razones para creer que funciona justo acá.** Hay un trabajo de
interpretabilidad sobre **Qwen3** ([2608.18106](https://arxiv.org/abs/2608.18106))
que encuentra exactamente la asimetría que explica nuestro problema: la certeza
sale de *una coalición amplia de rasgos compartidos*, mientras que la
incertidumbre está implementada como **un override disperso, mediado por un
grupo chico de rasgos dedicados**. Un grupo chico y dedicado es precisamente lo
que se puede empujar. Y no se quedaron en la correlación: intervenir sobre esos
rasgos *causalmente* reduce los errores por exceso de confianza, y el mismo
conjunto de rasgos generaliza entre idiomas y a una tarea fuera de distribución.

Eso también explica por qué el 1,7B «no produce incertidumbre útil»: no es que
no tenga el mecanismo, es que por defecto pierde contra la coalición de la
certeza.

**Y el serving ya lo soporta.** No hace falta nada nuevo en el aparato: el
llama.cpp que vendoreamos (b11255) acepta `--control-vector`,
`--control-vector-scaled FNAME:SCALE` y `--control-vector-layer-range`. Se
aplica en inferencia y no cuesta tiempo.

Los pares contrastivos **ya los tenemos**: el corpus está partido entre pedidos
que son suyos y pedidos que van a `derivar`. El vector se calcula en la misma
corrida donde ya pasamos el modelo por el conjunto.

**Lo que lo hace valioso no es sólo que funcione, es que es un dial.** La escala
es un escalar, se barre sobre el examen y se mira la curva de cobertura contra
precisión. Eso da algo que ningún método de entrenamiento da: **mover el punto de
operación sin volver a entrenar**. Para un aparato en vivo es la diferencia entre
un programa de aire donde conviene que el chico arriesgue y uno donde conviene
que derive a la primera duda.

**Medido después**: la dirección existe y empuja de verdad (0,53 de separación
relativa en la capa 22, y a escala 0,02 sube las derivaciones de 5/8 a 7/8).
Pero satura de golpe —a 0,05 deriva el 100 % de todo— y como mando es peor que
sesgar el logit. Ver «Lo que se midió» más arriba.

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

## Adónde se decide: la anatomía de los errores de v7

Cuatro instrumentos nuevos y una conclusión que cierra una familia entera de
ideas. Vale la pena leer el orden en que pasó, porque tres de los cuatro
instrumentos tuvieron bugs y cada bug fabricó un hallazgo entusiasmante.

### Lo que sí quedó en pie

**Ningún error es un volado.** De 16 fallos, **cero** tienen margen menor a 0,60
entre la elegida y la correcta; 13 de 16 están por encima de 0,90 y varios en
1,00. La hipótesis de que los dominios se intersectan tanto que la decisión cae
de un lado o del otro al azar queda descartada: el modelo está **seguro y
equivocado**, que es otro problema.

**La temperatura no puede hacer nada.** Se decodifica por argmax y la
temperatura divide todos los logits por igual: no cambia cuál es el mayor. Es
una operación nula. Y ahora está la prueba empírica además de la estructural: no
hay margen chico que sacudir. Muestrear sólo agregaría ruido.

**La decisión se calcula en el último tercio, y la hace la atención.** Con
atribución directa al logit —descomponer la diferencia de logit en lo que
aporta cada bloque, con auditoría de que la suma cierre (0,7 % de error
mediano)—:

| | contribución media |
|---|---|
| capas 1 a 18 | **−0,06** |
| capas 19 a 28 | **−6,47** |

Las primeras dieciocho capas **no participan**. No es que pierdan contra las
últimas: su aporte a distinguir una herramienta de otra es cero coma cero seis.
Los que deciden son la atención de las capas 22 (−1,05), 25 (−2,83) y 27
(−1,80). El único componente que empuja fuerte hacia la respuesta correcta es
el MLP de la capa 28 (+1,62).

### Por qué eso mata las intervenciones del lado de la lectura

Si no hay una respuesta correcta formada temprano, no hay nada que rescatar. Y
se probó con el decodificador de verdad —gramática incluida—, no con sondas:

| | acierto |
|---|---|
| sin tocar nada | **90/106** |
| capa 28 amortiguada al 50 % | 90 |
| capa 28 apagada | 89 |
| capas 27+28 apagadas | 88 |
| MLP de la 28 amplificado ×1,5 / ×2 / ×3 | 87 / 84 / 57 |
| atención de la 25 apagada | 89 |
| atención de 22+25+27 al 70 % | 89 |

Nada le gana a no tocar nada. **Y el motivo es la lección que importa:** la
atribución se calculó sobre los 16 fallos, pero esos mismos componentes son los
que aciertan los otros 90. Una atribución sobre los errores **no es una
receta**, porque el mecanismo es compartido. Amortiguar lo que se equivoca
amortigua también lo que funciona.

Eso cierra, para esta tarea: salida temprana, DoLa y contrastes entre capas,
amortiguar o semi-abliterar las capas finales. La literatura ya avisaba que esos
métodos son inestables en modelos chicos ([Active Layer-Contrastive
Decoding](https://arxiv.org/pdf/2505.23657) mide que DoLa incluso degrada en
algunos Qwen); acá hay además un motivo mecánico y no sólo un experimento
fallido.

### Los tres bugs, porque cada uno inventó un hallazgo

1. **El logit lens comparaba un token contra sí mismo.** Para los pares de la
   misma familia —`capture_feed` y `capture_program` comparten `capture`—
   comparar el *primer* token da siempre empate, y el instrumento informaba «la
   correcta nunca estuvo adelante» en exactamente los 6 pares de la misma
   familia. Corregido: busca el token donde los nombres **se separan**.

2. **La atribución elegía el candidato por el primer token.** Informaba 75
   fallos donde hay 16, y proyectaba sobre una dirección que no era la que el
   modelo estaba decidiendo. Lo delató que la suma diera **+799 hacia la
   correcta**: si eso fuera cierto, el modelo la habría elegido. De ahí salió la
   regla de auditar que la descomposición cierre, que es lo que separa una
   atribución de un número inventado.

3. **`hidden_states[-1]` ya viene normalizado, y yo lo normalizaba otra vez.**
   En transformers el modelo hace `self.norm(hidden_states)` y *después* lo
   agrega a la tupla. Mi «capa 28» era `norm(norm(h))`: daba 73,6 % donde el
   decodificador real da 84,9 %, y de esa diferencia salió la conclusión —falsa—
   de que el modelo perdía diez puntos en las dos últimas capas. El residual
   pre-norma no está en la tupla; ahora se saca con un gancho en la norma final.

El amortiguado fue la única de estas mediciones hecha con el decodificador real
desde el principio, y fue la única que dio el resultado correcto: no recuperaba
nada porque no había nada que recuperar. **Cuando una sonda y el decodificador
se contradicen, tiene razón el decodificador.**

### Lo que queda, entonces

Los errores son genuinos: la atención tardía lee el pedido y elige mal con
convicción. No hay atajo en la lectura. Queda el lado del entrenamiento —pares
contrastivos para las vecinas que se confunden— la geometría de las etiquetas
—los 86 nombres comparten 25 primeros tokens; ver `tokens-nombres.py`— y una
decisión de producto: de los 16 fallos, varios son casos donde las dos
herramientas hacen algo razonable para el mismo pedido, y eso el examen lo
tiene que decir.

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
