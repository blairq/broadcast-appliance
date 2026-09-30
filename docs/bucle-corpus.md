# El bucle de corpus: una frase entra sólo si mejora la geometría

`tools/entrenar/bucle-corpus.py`

Es la herramienta con la que se cura el corpus de entrenamiento del modelo
chico. Nació para el repertorio reactivo (85 herramientas más `derivar`), pero
está escrita para cualquier conjunto: la idea es aplicarla a las ~207 que
todavía no entrenamos. Este documento explica qué mide, por qué esa medición y no
otra, cómo se usa y qué salió mal las primeras veces.

## La idea en un párrafo

Una persona o una LLM grande **propone** frases de entrenamiento; una medición
hecha adentro del modelo chico **decide** si entran. La medición no le cree a la
redacción: le cree a dónde cae la frase en el espacio del modelo. Si una frase
suena bien pero el modelo la interpreta como otra herramienta, no entra. Si es
una variante de algo que ya está, no entra. Si acerca a su herramienta a una
vecina, no entra. Como medir no requiere entrenar —un forward de 12 ms por
frase—, se pueden probar cientos de candidatas en segundos, en vez de esperar dos
horas de entrenamiento por cada idea.

## Qué se mide

### El estado oculto de cada frase

La frase se pasa por el modelo con el prompt de servicio completo (fichas
incluidas) y se toma el vector de la última capa en la posición donde el modelo
va a contestar: el residual previo a la norma final, pasado por esa RMSNorm con
su peso. Es exactamente lo que la cabeza de salida multiplica para elegir la
etiqueta. El prefijo del prompt, igual para todas las frases, se procesa una
sola vez y se reusa su caché KV. Los estados se guardan en disco
(`~/.cache/ba-nubes/`) con una firma del modelo y del prompt, así que las
corridas siguientes no recalculan nada.

Las frases de una herramienta forman una **nube**. Cuanto mejor separadas estén
las nubes, más fácil le resulta al modelo distinguir las herramientas.

### El criterio: CDNV direccional

Para un par de grupos `i`, `j`, con centros `μ_i`, `μ_j`:

```
u_ij = (μ_i − μ_j) / ‖μ_i − μ_j‖           el eje que separa los dos grupos

CDNV_dir(i,j) = (var_i sobre u_ij + var_j sobre u_ij) / (2 · ‖μ_i − μ_j‖²)
```

Es la varianza de los grupos **sobre el eje que los separa**, dividida por la
distancia al cuadrado entre sus centros. [Directional Neural Collapse Explains
Few-Shot Transfer](https://arxiv.org/html/2603.03530v1) (2026) demuestra que el
error del clasificador por centro más cercano queda acotado por `4 · CDNV_dir`.
Viene de la CDNV original de [Galanti et al.](https://arxiv.org/pdf/2212.12532),
que sumaba la varianza en todas las direcciones; la versión direccional cuenta
sólo la que afecta el margen, porque la varianza perpendicular al eje no mueve
ningún punto hacia la otra nube.

Es la inversa al cuadrado del Fisher proyectado que miden `nubes.py` y
`variedad.py`: `CDNV_dir ≈ 1/Fisher²`.

| Fisher | CDNV_dir | cota del error |
|---|---|---|
| 2 | 0,25 | 100 % (no garantiza nada) |
| 3 | 0,11 | 44 % |
| 4 | 0,06 | 25 % |
| **6** | **0,028** | **11 %** ← objetivo por defecto |
| 10 | 0,01 | 4 % |

No hay un valor universal. El objetivo de 0,028 sale de dos cosas: es donde la
cota empieza a decir algo útil, y es el extremo bajo del rango que alcanzan los
modelos bien entrenados en los experimentos del paper (de ~2¹ a ~2⁻⁵). Se cambia
con `--objetivo`.

### El buen tamaño de un grupo

La tentación es apretar cada grupo lo más posible. El mismo paper lo desaconseja:
una buena representación es **angosta sobre el eje hacia cada vecina y ancha en
todas las demás direcciones**. El colapso completo le quita al modelo la
capacidad de reconocer formas nuevas de pedir lo mismo.

Lo medimos: antes de este bucle, las nubes de entrenamiento estaban separadísimas
—Fisher medio 21, 0 % de solapamiento— y aun así el 41 % de las preguntas del
examen caía más cerca de otra herramienta. Las nubes no estaban mal separadas:
estaban **demasiado concentradas**. Las peores tenían dimensión efectiva 1,1:
toda la variedad de su corpus cabía en una línea, porque venía de variar
mecánicamente una o dos frases.

Por eso la variedad que suma una frase candidata se mide **fuera de los ejes
hacia las vecinas**: ahí la varianza es gratis, y sobre esos ejes cuesta.

### Por qué medir sobre datos apartados

[Limitations of Neural Collapse](https://arxiv.org/pdf/2202.08384v1) muestra
que un colapso perfecto en entrenamiento no implica nada fuera de él: se pueden
construir modelos con colapso perfecto en entrenamiento que en test contestan al
azar. Es lo que nos pasaba. Por eso el diagnóstico informa además el acierto por
centro más cercano sobre `validar.jsonl`, con los centros armados **sólo** con la
parte de entrenamiento del corpus generado.

El examen **nunca entra al bucle**, ni siquiera para mirarlo: un bucle que ve el
examen termina optimizando el examen.

## Las decisiones

Cada candidata `{"herramienta", "frase"}` pasa, en orden:

1. **No repite una semilla** ni **se parece al examen** (la misma guarda de
   solapamiento de palabras que usa el generador de corpus, importada, no
   copiada).
2. **Cae en su grupo**: el centro más cercano es el de su herramienta.
3. **Suma variedad útil**: la fracción de su desvío respecto del centro que queda
   fuera de los ejes hacia las vecinas y de las direcciones que el grupo ya cubre
   es al menos `--novedad-min` (0,45).
4. **No acerca a nadie**: con la frase agregada, ningún par del grupo con
   **cualquier** otro grupo puede subir su CDNV más de `--tolerancia` (5 %) y
   quedar arriba del objetivo.

Una candidata con `"quitar": true` propone **sacar** una semilla. Se acepta si
baja la peor CDNV del grupo contra todos los demás, sin dejarlo con menos de 3
semillas ni perder más del 20 % de su dimensión efectiva. Para un centro de
choque, sacar la semilla que lo empuja hacia una vecina rinde más que agregar
diez: agregar no despega.

Si la semilla quitada venía de la jerga (`datos/jerga.json`), se anota en
`excluir` dentro de la herramienta, y `corpus-entrenamiento.py` la respeta para
no volver a meterla.

Un **grupo vacío** acepta su primera frase sólo si no cae adentro de otro grupo:
su distancia al centro más cercano tiene que superar el radio típico de un grupo.

Las aprobadas se aplican en el orden del archivo y **cada una cambia el grupo**:
la siguiente se mide contra el grupo ya modificado.

## Metodología: arreglar donde se pierde, no donde se ve feo

Las dos primeras rondas se orientaron por la **oferta**: se atacaron los pares con
peor geometría. Eso tiene dos defectos de método, y los dos se ven en los datos.

**Un par feo al que ningún pedido se acerca no importa, y un par mediano donde se
pierden pedidos es urgente.** Con el banco de prueba, lo más urgente resultó ser
`source_set_property → audio_set`, con 5 errores, y ese par no aparecía entre
los peores por geometría. Si se ordena por lo feo, no se toca nunca.

**Y la vara la movía el propio bucle.** La validación que se usaba
(`validar.jsonl`) sale de nuestras semillas, la escribió el mismo autor que
escribe las candidatas y se regenera cada vez que el bucle las toca. Un
instrumento que el proceso puede mover no mide el proceso.

### Tres conjuntos, tres roles que no se mezclan

| conjunto | quién lo escribe | quién lo lee | para qué |
|---|---|---|---|
| **corpus** (`semillas-*.json`) | el que propone | el bucle, el entrenamiento | lo que el modelo aprende; el bucle lo edita |
| **banco de orientación** (`datos/orientacion.jsonl`) | alguien que **no leyó** el corpus | sólo el bucle | dice dónde se pierde; nunca se entrena |
| **examen** (`preguntas.json`) | escrito antes de entrenar | sólo la evaluación final | el juez; nadie lo mira durante el bucle |

El banco de orientación es la pieza nueva y la que más importa:

- **Lo escribe otro autor.** Tiene que venir de alguien que no haya visto las
  semillas: la traza real del aparato cuando exista (la mejor fuente, porque es
  cómo se pide de verdad), otra persona, o una instancia de LLM a la que se le da
  sólo el título y la descripción de cada herramienta, con la consigna del
  registro de cabina, y nunca el corpus. El peor autor posible es el mismo que
  escribe las candidatas, porque sus sesgos quedan de los dos lados.
- **Es fijo.** No se regenera: si cambia entre rondas, no hay contra qué comparar.
- **Nunca se entrena, y no se puede copiar.** El bucle rechaza las candidatas
  que se parecen a una frase del banco con la misma guarda que usa para el
  examen. Una semilla calcada de una frase del banco convierte al banco en
  entrenamiento disfrazado.
- **Se parte en dos mitades** por un hash fijo de la frase. La mitad A orienta,
  porque de ella salen las prioridades; la mitad B sólo confirma. Si se orienta
  muchas rondas contra el mismo banco, el banco se vuelve el objetivo y deja de
  medir (Goodhart). **Si sube A y no sube B, es sobreajuste al banco**: hay que
  parar o renovarlo.
- **Tamaño:** entre 4 y 6 frases por herramienta. Con 86 herramientas son unas
  400; con 292, unas 1.500.

### El ciclo

```
1. MEDIR LA DEMANDA   pasar el banco (mitad A) por el modelo; cada frase va al
                      grupo del centro más cercano; cada error se atribuye al
                      par (verdadera -> a donde fue)

2. PRIORIZAR          prioridad(par) = errores x CDNV
                      un par feo sin errores no se toca; uno con errores es
                      urgente aunque su CDNV sea moderada

3. TRIAGE             cada par priorizado se clasifica por su CAUSA (tabla de
                      abajo), porque cada causa se arregla distinto

4. PROPONER GUIADO    para cada objetivo, la guía trae lo que hay que cubrir,
                      las palabras que hay que evitar y las semillas culpables

5. DECIDIR            los filtros de siempre, más la guarda contra el banco

6. REGISTRAR          una línea por ronda en datos/bitacora-corpus.jsonl

7. COMPARAR Y PARAR   sobre los mismos pares, y las dos mitades del banco
```

### El triage: cuatro causas, cuatro remedios

Confundir las causas fue lo más caro de las primeras rondas: se agregaron
frases a grupos cuyo problema era otro. Cada causa se reconoce con una prueba
medible:

| causa | cómo se reconoce | remedio |
|---|---|---|
| **etiquetado** | la mitad o más de las semillas del propio grupo caen más cerca de otro: la nube está centrada en otra cosa | **leer**: decidirlo es cosa de una persona. `operator_feed` tenía dos frases del registro de eventos y ninguna propia |
| **contaminado** | hay semillas del lado ajeno del punto medio en el par | **sacar** o reescribir las culpables (`"quitar": true`). Para un centro de choque, sacar rinde más que agregar |
| **hueco** | las frases del banco que fallan caen fuera de lo que el grupo cubre (novedad media ≥ 0,5) | **escribir formas nuevas**, guiadas por las frases que fallan. Otras palabras, mismo significado |
| **inseparable** | con 6 o más frases propuestas, entró el 15 % o menos | **decisión de producto**: fusionar las herramientas, derivar, o aceptar las dos respuestas. Más frases no lo arreglan |

Un par puede tener más de una causa, y se atiende en este orden: primero el
etiquetado, porque si el grupo está centrado en otra cosa todo lo demás mide mal;
después la contaminación, porque sacar es más barato que agregar; después el
hueco. La inseparabilidad se escala: no se sigue insistiendo.

### Proponer guiado

El diagnóstico con `--orientacion --json` trae, para cada par priorizado:

- **`cubrir`**: las frases del banco que se pierden. Muestran **qué significado
  falta**. No se copian (la guarda lo impide): se escribe *otra* forma de pedir
  lo mismo.
- **`evitar`**: las palabras que usa la vecina y el grupo no. Son las que
  arrastran hacia el lado equivocado: «en pantalla» hacia los gráficos, «al final
  de la lista» hacia volver a la fila.
- **`culpables`**: las semillas del lado ajeno, que son candidatas a sacar.

Así, quien propone deja de escribir a ciegas. En las rondas sin guía se aprobó el
44 % y después el 29 %, y la mayoría de los rechazos fueron «cae en X»: frases
escritas sin saber hacia dónde había que escribir.

### Cuándo parar

**Por herramienta**, se cierra cuando:

- todas sus frases del banco caen en su grupo y su peor CDNV queda bajo el
  objetivo, o
- se clasifica como *inseparable*. En ese caso pasa a decisión de producto y se
  deja de proponer.

**Por ronda**, se corta el bucle cuando pasa cualquiera de estas cosas:

- la tasa de aceptación cae por debajo del 20 %;
- sobre los pares comunes, los que empeoran superan el 60 % de los que mejoran
  (en la ronda 2 fue 213 contra 266, o sea 80 %: ya era hora de cortar);
- la mitad que confirma no sube en dos rondas seguidas;
- la mitad que orienta sube y la que confirma no. Esto es sobreajuste, así que se
  para o se renueva el banco.

**Y siempre:** el examen se mira recién después de entrenar. Si una ronda de
bucle se evalúa contra el examen, la ronda siguiente ya está optimizando el
examen.

### El esfuerzo va donde está la prioridad

No se escriben cinco frases por herramienta porque sí. El presupuesto de cada
ronda se reparte en proporción a la prioridad del par: una herramienta con cinco
errores en el banco recibe más propuestas que una con uno, y una herramienta sin
errores y con su peor CDNV bajo el objetivo no recibe ninguna. Eso es lo que
«orientar el corpus a donde es necesario» quiere decir en la práctica.

## Cómo se usa

```bash
M=~/entrenamientos/reactivo-v7      # un modelo ya entrenado, que mide
PY=.venv-entrenar/bin/python
B=datos/orientacion.jsonl           # el banco, escrito por otro autor

# 1. Diagnosticar con el banco: dónde se pierde, por qué causa, y la guía para
#    proponer (cubrir, evitar, culpables). --json es la entrada para quien propone.
$PY tools/entrenar/bucle-corpus.py --modelo $M --diagnosticar --orientacion $B --json diag-0.json

# 2. Proponer: escribir candidatas.json a mano o con una LLM, mirando diag-0.json.
#    [{"herramienta": "scene_move", "frase": "reubicá la escena en el orden del programa"},
#     {"herramienta": "speaker_presenter", "frase": "este es el que conduce", "quitar": true}]

# 3. Evaluar y aplicar. Con --orientacion, rechaza también las que copian el
#    banco. Cada ronda deja una línea en datos/bitacora-corpus.jsonl.
$PY tools/entrenar/bucle-corpus.py --modelo $M --evaluar candidatas.json --orientacion $B --aceptar

# 4. Regenerar el corpus y comparar contra la ronda anterior: los mismos pares y
#    las dos mitades del banco.
make corpus-entrenamiento
$PY tools/entrenar/bucle-corpus.py --modelo $M --diagnosticar --orientacion $B \
    --json diag-1.json --previo diag-0.json
```

Sin `--orientacion`, el bucle funciona igual pero se orienta por la geometría, o
sea por la oferta. Sirve para arrancar; para orientar el trabajo hace falta el
banco.

Y se repite hasta que la tasa de aceptación cae y cada ronda empeora casi tantos
pares como mejora: ahí lo que queda ya no se arregla con frases.

### Cómo proponer buenas candidatas

Lo que aprendimos, casi todo porque primero lo hicimos mal:

- **Otra forma de pedir, no otra manera de escribir lo mismo.** Otro verbo, otro
  punto de vista (pregunta en vez de orden, reporte de un problema en vez de
  comando), otro nivel de detalle. Las variantes no pasan el filtro de variedad,
  y está bien que no pasen.
- **Sin negación contrastiva.** «dejame verlo primero, no lo pongas» cayó en
  `speaker_presenter`. El modelo no lee el «no»: lee las palabras de la vecina.
  La negación que **es** el significado sí va: «no se escucha», «no lo dejes
  entrar», «poné ministro, no secretario».
- **Cuidado con las palabras que la vecina usa.** «en pantalla» empuja hacia los
  gráficos; «al final de la lista», hacia volver a la fila; «bajá», hacia borrar
  (bajar es descargar y también sacar).
- **Para un centro de choque, primero sacar.** El diagnóstico lista las semillas
  culpables: las que caen del lado ajeno del punto medio en un par pegado.

## Aplicarla a las otras herramientas

```bash
$PY tools/entrenar/bucle-corpus.py --modelo $M \
    --semillas datos/semillas-otras.json \
    --herramientas lista-de-herramientas.txt \
    --sin-derivar --diagnosticar
```

Tres cosas a tener en cuenta:

1. **El prompt con el que se mide tiene que incluir las fichas de las
   herramientas que se miden.** Hoy `lib_prompt.partes()` arma el prompt con las
   85 del repertorio reactivo. Una frase de una herramienta administrativa medida
   con ese prompt cae donde el modelo la pondría hoy, que es `derivar`. Para medir
   las otras hay que medir con un prompt que las liste. Es el único prerrequisito
   que no está resuelto.
2. **Todos contra todos escala bien.** Con 292 herramientas son ~42.000 pares, y
   cada uno es una proyección sobre un eje: segundos.
3. **Las etiquetas también tienen que ser únicas contra todo el MCP**:
   `elegir-etiquetas.py` ya prohíbe nombres exactos y primeros tokens de las que
   quedan fuera del subconjunto.

## Lo que salió mal, y cómo quedó corregido

Cada uno de estos errores produjo un número que parecía un hallazgo:

- **Vecinas medidas con el centro viejo.** Al sacar una semilla el centro se
  mueve, y si se acerca a una vecina nueva esa vecina no se miraba. Tres quitas
  «aprobadas» dejaron a `speaker_presenter` peor que antes (0,198 → 0,206).
  Ahora se miran las vecinas de antes y de después.
- **Sólo las 5 vecinas más cercanas.** La CDNV depende de la varianza sobre el
  eje, no sólo de la distancia entre centros: el peor par puede estar con un
  grupo que no es de los cinco más cercanos. Ahora se mide contra todos.
- **Agregados comparados entre rondas.** Cuando un grupo pasa de 2 a 3 semillas
  se vuelve medible y entran de golpe todos sus pares: en la primera ronda, de
  2.485 a 3.570. Los totales empeoraban sin que nada empeorara. Ahora se compara
  par por par con `--previo`.
- **Validación contaminada.** Los centros armados con todas las semillas
  —incluidas las que originaron las frases de validación— daban 95,8 %. Con
  centros sólo de entrenamiento, 93,6 %.
- **Semillas mal etiquetadas.** `operator_feed` tenía dos semillas, «que paso
  recien» y «dame el diario de lo que fue pasando», que son del registro de
  eventos. El bucle lo destapó porque ninguna frase correcta caía en su grupo.
  Esto la geometría no lo decide: hay que leer. Una herramienta cuyas candidatas
  caen todas afuera es la señal.

## Resultados de la primera aplicación

Repertorio reactivo, medido con v7, dos rondas, 97 candidatas propuestas: 33
agregadas y 5 quitadas.

| sobre los mismos 2.485 pares | antes | después |
|---|---|---|
| pares sobre el objetivo | 156 | 143 |
| peor par (CDNV) | 0,198 | 0,126 |
| cota de error del peor par | 79 % | 50 % |
| pares que mejoraron / empeoraron más de 5 % | — | 246 / 76 |
| dimensión efectiva mediana | 2,54 | 2,59 |

Además, 14 herramientas que tenían una o dos semillas —todo su corpus era
variación mecánica de una frase— pasaron a ser medibles, y sólo el 2 % de sus
pares queda sobre el objetivo.

La tasa de aceptación bajó del 44 % en la primera ronda al 29 % en la segunda, y
en la segunda empeoraron 213 pares contra 266 que mejoraron. Ahí se cortó.

**Lo que queda no se arregla con frases**, y el bucle lo muestra con claridad:

- `operator_feed` («cómo te ven y oyen en el lobby»): de 9 frases correctas, 1
  cae en su grupo; las demás caen en `audio_escuchar`, `audio_set` o
  `capture_feed`. El modelo no tiene un concepto propio para esto.
- `share_take`, `share_delete`, `share_view`: el modelo los agrupa por el objeto
  («el archivo compartido») y no por lo que se hace con él.
- La familia `speaker_*`: mejoró mucho (`speaker_presenter` bajó su peor par de
  0,198 a 0,104), pero sigue siendo la zona más densa.

Son decisiones de producto —fusionar, derivar o aceptar las dos respuestas—, no
de corpus.

## Lo que enseñó medir con el banco y entrenar (v8)

### Comparar curvas, no puntos

v8 se entrenó con pedidos administrativos reales en lugar de palabras sueltas.
Sobre el banco, derivar pasó de 44 % a 73 % y el acierto de 52 % a 71 %. Parecía
un salto. Pero v7 con el sesgo del logit de `derivar` en +4,5 ya daba 72 % de
derivación, y con más cobertura:

| a igual derivación | v7 cobertura | v8 cobertura | precisión (los dos) |
|---|---|---|---|
| 58 % | 95 % | 88 % | 78-79 % |
| 74 % | 88 % | 80 % | 80-81 % |
| 86 % | 75 % | 71 % | 84 % |

**Un cambio de corpus puede mover el punto de equilibrio sin hacer un modelo
mejor.** Más ejemplos de una clase corren el modelo hacia esa clase, que es lo
mismo que hace el dial. Para saber si el modelo mejoró hay que barrer el dial en
los dos y comparar las curvas: si la del nuevo queda arriba, mejoró; si sólo se
corrió a lo largo de la misma curva, no.

Con el evaluador rápido, barrer seis valores sobre el banco son cinco minutos
con las dos placas.

### Lo que sí aprendió, y lo que perdió

Caso por caso (`comparar-modelos.py`), v8 arregló 365 y rompió 100. Lo
arreglado es real: la derivación por área administrativa subió 40 o 50 puntos
(audio avanzado 15 % → 66 %, equipo 12 % → 64 %, rostros 23 % → 71 %). Lo roto
tiene un patrón: **deriva pedidos propios que traen nombres y detalles
concretos** («Hacé pasar a Marcela», «¿qué hay en la escena Noticias?»). Las
semillas propias casi no tenían nombres y los pedidos administrativos nuevos sí,
así que el modelo aprendió que "detalle concreto" es "de otro".

Remedio: semillas propias con nombres de personas, escenas y carruseles. El
banco lo venía mostrando: la mayoría de sus frases que fallaban nombraban a
alguien.

### Dos herramientas más mal etiquetadas

`operator_feed` no era la única. `participant_talk` («hablar en privado con
alguien del lobby») tenía tres semillas que eran de darle la palabra a un orador:
«dejalo hablar», «dale la palabra a ese». Sacaba 0 de 5 en el banco. **Una
herramienta que saca 0 con frases de otro autor es la primera sospechosa de
etiquetado**, antes que de falta de frases.

### El banco reserva las formas naturales

Con el bucle guiado por el banco, más o menos un tercio de las candidatas se
rechazó por copiarlo. Para herramientas como `room_link`, las maneras obvias de
pedirlo («pasame el QR», «el link del tablero») ya están en el banco, justamente
porque son las naturales. La guarda obliga a escribir formas menos obvias. Es el
costo de tener un instrumento honesto, y conviene saberlo antes de frustrarse
con la tasa de aceptación.

### Superposiciones que no son del corpus

Varias administrativas se escapan hacia una reactiva porque **son casi la misma
acción en otro nivel**: `twitch_live_stop` contra `stream_stop`,
`carrusel_listar` contra `laminas_listar`, un limitador contra `audio_set`,
`grafico_probar` contra `grafico_mostrar`. Corregir el apellido de una persona
(`participant_profile`) cae en `grafico_mostrar` porque en cabina eso también es
corregir el zócalo. Esto no se arregla con frases: es superposición del diseño
del MCP, y se decide fusionando, aceptando las dos respuestas o derivando.

## Límites

- **Mide con un modelo que no vio las frases nuevas.** Dice cómo las interpreta
  un modelo que conoce el dominio, que es un buen filtro de significado, pero no
  garantiza el acierto después de reentrenar. La prueba que decide es entrenar y
  pasar el examen.
- **La geometría no juzga el significado.** Una frase puede caer en el grupo
  correcto por razones equivocadas. Las aprobadas se leen.
- **La cota es una cota.** 4 · CDNV acota el error del clasificador por centro
  más cercano sobre esos dos grupos; no predice el acierto del modelo.
