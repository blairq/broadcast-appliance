# Entrenamiento avanzado del modelo local

Cómo llevar el modelo local (`broadcast-reactivo-qwen3.5-4b`, el de
Configuración → Modelos locales) de **elegir la herramienta** a **resolver el
pedido completo**, servir de **asistente genérico offline** y atender
**alertas** y **filtros de audio**. Sale de lo medido en
[voz-modelo-local.md](voz-modelo-local.md) y del banco
([voz-modelo-local-banco.md](voz-modelo-local-banco.md)).

## Dónde estamos

Hoy son dos llamadas al modelo:

1. **Elegir** (`elegir`): con el prompt fijo con el que se entrenó (las fichas
   de las 84 herramientas, `tools/lib_prompt.py`) y una gramática que solo deja
   escribir un nombre válido o `derivar`. Unos 170 ms, **98 % de acierto**.
2. **Completar los datos** (`generar`): el modelo **sin entrenar para esto**,
   guiado por un prompt de reglas (`datos/voz-argumentos.txt`), con la
   herramienta, su esquema, la parte del estado del aparato que usa y la frase.
   Unos 2,2 s. Lo que escribe lo valida `agent/ArgumentosLocales`: nombres que
   existen, rangos, enum, requeridos y los cambios relativos.

### Cómo se resolvió subir y bajar el volumen (y por qué funciona)

- El modelo **no hace cuentas**: escribe cuánto cambia como texto con signo
  (`"-4"`, `"+3"`; «un poco» son 3, sin cantidad son 6) y un valor exacto sin
  comillas. Antes, pidiéndole el volumen final, se equivocaba en la
  aritmética (en -12, «subile 3» daba +6 y «bajale 6» daba -30).
- La app suma el cambio al volumen **real** de la fuente (que va en el estado)
  y lo recorta al rango del esquema. Queda en la bitácora («relativo -4 dB
  sobre -12»).
- Los nombres los pone la app: «la música» → «Música», «el invitado 2» →
  «Invitado 2 (audio)». Si el nombre no existe, la acción se bloquea en vez
  de adivinar.

Medido con el aparato andando: ninguna acción de volumen ni de silencio quedó
distinta de lo pedido.

## Cómo se entrenaron los modelos actuales (la metodología que funcionó)

Los seis repos públicos (4B, 4B mtp-ud y 9B mtp-ud; MLX y GGUF de cada uno)
salen de **una sola fábrica reproducible**: `make modelo` →
`tools/entrenar/actualizar-modelo.py` → `tools/entrenar/fabricar-modelo.py`.
Lo que sigue es qué hace cada paso y por qué; el formato nuevo tiene que
reusar todo esto, no empezar de cero.

### Resultados (para comparar el formato nuevo)

Banco de orientación (1.423 frases escritas aparte, nunca entrenadas) con el
dial de `derivar` en 0, y examen (105 casos):

| modelo | cobertura | precisión | derivado | examen | MLX q4 fp16 (precisión) | margen < 0 |
|---|---|---|---|---|---|---|
| 4B (Qwen3.5-4B) | 92,4 % | 93,1 % | 88,9 % | 88,6 % | 92,3 % | 8 |
| 4B mtp-ud | 95,7 % | 94,2 % | 83,9 % | 85,7 % | 92,9 % | 0 |
| 9B mtp-ud (Qwen3.5-9B) | 93,7 % | 94,9 % | 91,2 % | 86,7 % | 95,1 % | 6 |

Cabeza MTP (validación aparte, tokens aceptados por ciclo de 3 borradores):

| modelo | nombres: original → entrenada | texto: original → entrenada |
|---|---|---|
| 4B | 1,61 → **1,82** | 1,62 → **1,63** |
| 4B mtp-ud | 1,57 → **1,82** | 1,50 → **1,58** |
| 9B mtp-ud | 1,64 → **1,82** | 1,53 → **1,65** |

Mismo corpus (7.800 ejemplos) para los tres: las diferencias entre ellos
caen dentro del ruido de una corrida a otra (2-4 puntos), salvo el 9B, que en
la curva del banco queda ~1 punto arriba a igual cobertura.

### 1. Un solo prompt, con firma

- `tools/lib_prompt.py` arma el prompt de entrenamiento **y** el de servir:
  sistema + fichas de las herramientas del perfil + la frase, con `<think>`
  vacío (no-thinking) escrito en el propio prompt. `partes()` devuelve
  cabeza y cola: el prefijo fijo (~2.800 tokens) se procesa una vez y se reusa.
- `huella()` firma el prefijo: si una herramienta nueva cambia las fichas, el
  modelo quedó viejo y la fábrica lo nota (hoy `3f9122d8064004c5`, 84
  herramientas).
- Cada repo lleva `prompt.json` (cabeza, cola, salidas, gramática, firma,
  frases de prueba) para que quien sirve no reconstruya nada.
- **Perfiles** (`datos/perfiles/*.json`): qué herramientas del MCP entran,
  por categoría. `reactivo` es el actual.
- Nombres **reales** de las herramientas como salida: las etiquetas legibles
  empataron y las de fragmentos empeoraron (v11).

### 2. El corpus: calidad antes que cantidad

Ver [bucle-corpus.md](bucle-corpus.md). Lo esencial:

- **Semillas** por herramienta (`datos/semillas-reactivas.json`), **15 a 20
  por grupo**. Crecer por cantidad empeoró: el salto vino de mejorar las que
  había.
- **Medir cada frase contra el conjunto**: con los estados internos del
  modelo se calcula la separación de cada grupo con el más cercano
  (Frobenius: distancia entre centros sobre la dispersión de los dos) y cuánto
  aporta cada frase. Se usa como **guía** de qué frase está mal, nunca para
  podar a ciegas. Si un grupo se corrió se realinea; si quedó apretado se abre
  con frases nuevas.
- **Pares que se pisan**: dos herramientas cuyas frases caen en la nube de la
  otra; se separan reescribiendo con el contexto real de cada ficha (regla del
  radio: solo cuenta un choque si la frase cae dentro de la nube ajena).
- **Margen (AUM)** sobre todas las semillas: logit de la correcta menos el
  mejor rival en los pasos donde la gramática decide. Las de margen negativo
  van al agente para arreglar.
- **Agentes con contexto real** (`tools/entrenar/agente.py` y `consignas/`):
  revisan el sentido de cada frase con la ficha de la herramienta, separan
  pares, reescriben; un juez ciego verifica. Bucle: test → agente arregla →
  test, hasta que todas dan bien (`bucle-corpus-completo.py`).
- Cada frase se expande en **4 variantes** (con 1 sola, la cuota de
  `derivar` se achicó y las frases de otros dominios se colaron como propias).
- Lo **ajeno** (`derivar`) está en el corpus con su peso: es lo que deja
  pasar al asistente grande lo que no es del modelo.
- **El examen y el banco nunca se entrenan ni se copian**: son lo único que
  mide de verdad.

### 3. Entrenar rápido en dos 3090

`tools/entrenar/entrenar-rapido.py`, LoRA r=32 sobre todas las lineales (las
de atención completa y las de la atención lineal de Qwen3.5), lr 2e-4, lote
16, 2 épocas.

- **Prefijo compartido**: las fichas (~3.000 tokens) se procesan **una vez**
  por lote y su caché se repite para las 16 frases. Qwen3.5 tiene 3 de cada 4
  capas de atención lineal con estado recurrente: transformers no continúa ese
  estado con varios tokens, así que `cache_hibrido.py` lo parchea (continuar,
  repetir, foto/volver).
- **Modo según el tamaño** (lo decide la fábrica leyendo el config): hasta
  ~5 GB de pesos, una copia por placa y medio lote cada una (torchrun); el 4B,
  repartido entre las dos placas (2 s/paso, ~30 min); el 9B, repartido y con
  **el prefijo recalculado capa por capa** en el backward (un solo checkpoint
  sobre todo el prefijo no bajaba el pico): 13,4 + 19 GB, 3,9 s/paso, 74 min.
- Kernels de triton de la atención lineal: cada capa corre con su placa
  activa y el autoajuste va con candado (los hilos del backward se pisaban).

#### Las dos placas como una (FSDP2, desde la v4 de tool calling)

Con el formato de *tool calling* el prefijo pasó a ~8.200 tokens (las fichas
de las herramientas) y el modelo repartido por capas deja una placa esperando
a la otra. `entrenar-rapido.py --fsdp` (con `repartido.py`):

- **FSDP2** (`fully_shard`; ZeRO-3, arXiv:1910.02054; PyTorch FSDP,
  arXiv:2304.11277): cada placa guarda la mitad de cada capa y calcula la
  mitad del lote **a la vez**; antes de usar una capa la arma trayendo la otra
  mitad por P2P (NCCL, mismo switch PCIe) y los gradientes se promedian con
  reduce-scatter. LoRA en fp32 repartido; se calcula en bf16.
- **Prefijo amortizado** (el esquema de *Schedule-Level Shared-Prefix Reuse*,
  arXiv:2606.01143): el prefijo hacia adelante una vez, su caché cortado en
  hojas que juntan el gradiente de todos los micro-lotes, y un solo backward
  del prefijo. Es exacto porque ese backward es lineal en el gradiente que
  llega por K/V.
- **Backward del prefijo capa por capa y a mano**
  (`cache_hibrido.atras_por_capas`). Con el recálculo por checkpoint, autograd
  recibía los gradientes de los cachés de las 32 capas a la vez y recalculaba
  muchas juntas: 10,9 → más de 23,5 GB en una placa. Ahora va de la última a la
  primera, cada una recalculada llamando al módulo (FSDP arma y suelta sus
  pesos), con una sola capa viva. Además resuelve el detalle de FSDP: sus
  ganchos esperan el gradiente por la salida de la capa, y el nuestro llega
  por el caché.
- **Verificado**: el gradiente con FSDP contra los mismos medios lotes en una
  sola placa da coseno 0,9998, igual que una placa contra sí misma (0,99965).
  El resto es el no determinismo de los kernels de triton de fla.
- **Reparto parejo**: el lote se ordena por largo y se reparte en zigzag;
  dentro de cada placa los micro-lotes juntan largos parecidos. Son los mismos
  16 ejemplos, así que el gradiente es el mismo.
- **Pesos que se sueltan o no** (`--soltar`). Por defecto lo decide el tamaño:
  si el modelo ocupa menos del 35 % de la placa (el 4B), se conservan armados
  entre el forward y el backward (19,3 GB por placa, 9,2 s/paso); si no
  (el 9B), se sueltan (14,8 GB con el 4B, 9,9 s/paso).
- **Dos modos** (`--pesos`, que por defecto se elige por el tamaño del modelo):
  - **`enteros`** (si el modelo ocupa menos del 35 % de una placa, como el
    4B): una copia del modelo por placa, el lote y **el prefijo repartidos**,
    y los gradientes sumados al final del paso.
  - **`repartidos`** (lo que no entra en una placa, como el 9B): FSDP2, cada
    placa con la mitad de cada capa y el prefijo entero en cada una.
  - Con el 4B, FSDP igual conservaba los pesos armados en las dos placas
    (20,8 GB por placa), así que repartirlos no ahorraba nada ahí.
- **El prefijo repartido por secuencia** (`prefijo_repartido.py`). La placa 0
  hace los tokens [0, m) y la 1 los [m, L).
  - Por causalidad solo cruza información de la 0 a la 1. En la atención
    lineal cruzan el estado recurrente al llegar a m (fla lo acepta como
    `initial_state`, diferenciable) y los 3 tokens del borde de la
    convolución. En la atención completa cruzan K y V. Es la idea de LASP
    (arXiv:2404.02882) en su forma más simple.
  - **Las placas van desfasadas una capa, no a la par.** Hacia adelante la 0
    manda sin esperar (`isend`) y sigue; la 1 va una capa atrás y **guarda lo
    que recibe** (~200 MB por paso). En el backward es al revés: la 1 recalcula
    cada capa con lo guardado y devuelve los gradientes de lo recibido; la 0 va
    una capa atrás y los suma como raíces de su backward. Solo se espera una
    capa al principio.
  - A la par, en cambio, en cada capa lineal una placa esperaba el núcleo
    recurrente de la otra (8,15 s/paso).
  - Con FSDP el desfase se trababa. Cada capa comunica dentro de su backward
    (trae la capa siguiente, reparte gradientes), y el orden de esas
    comunicaciones se cruzaba entre placas con el de las nuestras. Por eso el
    prefijo repartido va solo con los pesos enteros.
  - Los cachés de cada mitad se juntan para los sufijos. El gradiente del caché
    se suma entre placas (el backward del prefijo es lineal en él) y se reparte
    por rango de tokens.
  - Lo constante va en caché: los embeddings del prefijo (la tabla está
    congelada) y su RoPE.
- **Verificación del gradiente** (`verificar-gradiente.py --pesos
  enteros|repartidos`, con torchrun en dos placas; correrla después de tocar
  cualquiera de estas piezas). La verdad se calcula en **fp64** con el 0.8B,
  que tiene la misma arquitectura. Contra ella se miden, en fp32 sin TF32, el
  camino de una placa y el de dos.
  - Partir los tokens cambia el orden de las sumas: no puede dar idéntico bit
    a bit. Lo que se pide es que esté tan cerca de la verdad como una placa.
    Un término de más o de menos daría órdenes de magnitud de diferencia.
  - Resultados:

    | Modo | Caché del prefijo (1 placa / 2 placas) | Coseno del gradiente (1 / 2) | Resultado |
    |---|---|---|---|
    | `enteros` | 4,06e-5 / 4,51e-5 | 0,999999998 / 0,999999997 | Pasa |
    | `repartidos` | 4,06e-5 / 4,06e-5 | 0,999999998 / 0,999999998 | Pasa |

  - Con el kernel de fla (TF32 adentro) una y dos placas quedan igual de lejos
    de la verdad: coseno 0,99883 con una y 0,99890 con dos.
  - Además verifica que el optimizador reciba los parámetros correctos.
  - Dos trampas que encontró en el camino:
    - `from_pretrained(dtype=float32)` deja la base en bf16; hace falta
      `.float()`. En bf16 el redondeo cambia con el reparto y tapaba todo.
    - Con FSDP y los pesos conservados, un forward **sin backward** (la
      validación, o el de inicio de FSDP) los dejaba armados. El optimizador
      actualizaba los pedazos y el forward siguiente usaba la copia vieja: el
      modelo no aprendía. Ahora `repartido.soltar_todo()` va después de cada
      uno.
- **Medido con el 4B** (prefijo de 8.203 tokens, lote 16, micro 4):

  | Configuración | s/paso |
  |---|---|
  | Repartido por capas (antes) | 11,6 |
  | FSDP | 9,2 |
  | Prefijo repartido a la par | 8,15 |
  | Pesos enteros + prefijo repartido y desfasado | 7,18 |
  | **+ atención causal sin matriz + sin sincronizar por paso** | **6,24 (1,86×)** |

  - Memoria: 17,5 GB por placa. Uso de las placas: 98–99 %.
  - **Atención sin matriz de máscara.** La placa 1 pasaba la máscara causal
    como matriz de bool: el kernel genérico calculaba todos los bloques y leía
    la máscara de memoria. Era el 29 % del backward del prefijo, con solo 8
    capas. `causal_lower_right` (la variante causal con las consultas al final
    de las claves) saltea los bloques tapados.
  - **Sin ida y vuelta a la CPU.** El chequeo de «¿toca guardar el estado?»
    leía un valor de la placa en cada paso, y eso dejaba a la placa esperando
    a la CPU entre pasos. Ahora se pregunta cada 10 pasos.
  - **Entre placas, P2P directo.** NCCL va por `P2P/CUMEM` (mismo switch PCIe,
    `PIX`), sin pasar por la RAM.
  - Antes de esos dos arreglos, por paso: prefijo hacia adelante 1,0 s (el
    ideal es ~0,9), micro-lotes 2,9 s, backward del prefijo 3,0 s, suma de
    gradientes 0,03 s.
  - La pérdida de validación sigue la misma curva: 0,3933 contra 0,3914 a los
    15 pasos y 0,3564 contra 0,3566 a los 30.
  - El backward del prefijo no baja a la mitad exacta por el costo fijo por
    capa (32 backwards, muchos kernels chicos del LoRA), no por esperas: las
    dos placas tardan lo mismo.

**Reanudar.** Se llama con `python`, no con `torchrun`: el proceso supervisa.
Lanza torchrun en su propio grupo de procesos y, si la corrida se cae, termina
el grupo entero y relanza con `--reanudar`. Si la caída fue por memoria,
relanza con micro-lotes a la mitad (con FSDP la otra placa queda esperando en
una comunicación y no se puede reintentar adentro). Los trabajadores mueren
con su padre (`PR_SET_PDEATHSIG`): antes quedaban huérfanos con la placa
ocupada. Cada `--guardar-cada-min` (10) queda `<salida>/estado/estado.pt`
(~1 GB; escritura atómica): LoRA y optimizador completos, paso, barajado,
generador y el mejor hasta ahora. Lleva la huella del corpus y del plan, y no
retoma con otros datos. `tc-bucle` y la fábrica, si encuentran un estado a
medias, siguen desde ahí sin regenerar el corpus. La tokenización del corpus
queda en caché (`<corpus>/.tokens-<huella>.json`). Probado matando torchrun
con SIGKILL a mitad de la corrida: relanzó, siguió en el paso 13 y terminó.

### 4. Medir

`tools/entrenar/evaluar.py` con decodificación **restringida** a los nombres
posibles (más `derivar`):

- **Examen** y **curva del banco** en 5 puntos del dial de `derivar` (-4..+4):
  se comparan modelos **a igual derivado**, no en un punto suelto.
- **Margen** sobre todas las semillas, con cada checkpoint.
- **q4 de MLX simulado**: los pesos cuantizados y descuantizados exactos de
  MLX, medidos en GPU (MLX en CPU es lento). Cuesta ~1 punto.
- **fp16**: el q4 corriendo entero en fp16 (`evaluar.py --tipo fp16`), para
  saber si desborda: pierde 0,0-0,1 puntos.

### 5. Exportar para la Mac (MLX)

- Todo en **fp16** (la M1 no tiene bf16 nativo); lo único en fp32 es `A_log`
  (24 tensores chicos: el decaimiento de la atención lineal).
- **Con visión**: se entrena solo el texto, así que `con-vision.py` arma el
  checkpoint completo con la base y solo los módulos que el LoRA cambió (la
  torre de visión, de la base) y `mlx_vlm.convert` lo cuantiza a 4 bits
  (grupos de 64) **dejando la visión sin cuantizar**. mlx-vlm 0.7.4 escribe
  el config sin la cuantización: la fábrica la agrega. Carga en mlx-lm (texto)
  y en mlx-vlm (imágenes).
- La cabeza MTP va **aparte** (`mtp.safetensors` fp16 y `mtp-q8.safetensors`),
  fuera de `model*.safetensors`: si un peso `mtp.*` estuviera ahí, mlx-lm
  creería que el modelo no está convertido y correría todas sus normas. Normas
  de la cabeza en la convención de Hugging Face (sin +1): ba-modelo arma el
  drafter con `model_type qwen3_5` y mlx-swift-lm les suma 1 al cargarla
  (el formato de MTPLX, con el +1 ya aplicado, acá quedaría sumado dos veces).

### 6. Exportar para Linux y Windows (GGUF)

- GGUF bf16 con llama.cpp b11255 (la cabeza MTP entrenada adentro, bloque 32)
  y cuantizado con **imatrix calibrada con el propio corpus** (sin el examen).
- Q4_K_M, o la **receta UD de unsloth** (UD-Q4_K_XL: el tipo de cada tensor se
  lee de la cabecera de su GGUF con un pedido por rango, sin bajarlo entero, y
  se aplica a nuestros pesos con nuestra imatrix).
- `mmproj-F16.gguf` para la visión (`--mmproj`).

### 7. La cabeza MTP

`tools/entrenar/entrenar-mtp.py`, con el modelo principal congelado:

- **En cadena**: los 3 borradores juntos, cada paso con la salida del
  anterior (como `common/speculative.cpp` de llama.cpp) y una máscara que le
  da a cada paso exactamente el contexto que tiene en vivo (EAGLE-3,
  *training-time test*). Peso 0,8 por paso.
- **Con texto**: además de los nombres, prosa escrita por **el propio modelo
  ajustado** (frases del operador como pedidos de explicación y fragmentos de
  docs/): lo que la cabeza adivina es lo que el modelo va a escribir.
- **Peso 4 al texto y lr 5e-5**, con calentamiento: con peso 2 y lr 1e-4 los
  nombres (que se aprenden enseguida) tiraban para su lado y el texto bajaba
  (9B: 1,86 → 1,68 tokens por ciclo).
- **Se queda con la mejor**: cada 100 pasos se mide en una parte de
  validación y se guarda la mejor que no acepte menos texto que la original;
  lo que se informa sale de otra parte, que no participa de la elección.

### 8. Empaquetar, verificar y subir

- Repos con tarjeta (también versionada en `docs/modelos/`), licencia,
  `prompt.json`, firma y, si hay, equivalencias. El catálogo de la app
  (`datos/modelos-catalogo.json`) se actualiza al subir.
- **Espacio**: antes de cada etapa la fábrica estima lo que va a ocupar y
  para si no entra con 10 GB de margen; borra los intermedios grandes y los
  repos usan enlaces duros.
- **Se sube una sola vez, al final, verificado**: MLX con visión, fp16 y la
  cabeza en el formato de ba-modelo; GGUF con la cabeza nueva y el mmproj. La
  subida es lenta y subir algo que se va a rehacer es tiempo perdido.

## Qué entrenar

### 1. Herramienta y argumentos en el formato nativo de *tool calling* de Qwen

Qwen ya trae tool calling: las herramientas van en el system dentro de
`<tools>…</tools>` y el modelo contesta

```
<tool_call>
{"name": "audio_set", "arguments": {"name": "Música", "volumeDb": "-4"}}
</tool_call>
```

y el resultado vuelve como `<tool_response>…</tool_response>`. Entrenar en
**ese** formato (y no en uno propio) aprovecha lo que el modelo ya sabe:

- una sola salida con herramienta y argumentos (la segunda llamada y su
  prompt largo dejan de hacer falta: menos de 1 s en vez de 2,4 s);
- **estándar**: sirve con llama.cpp, MLX, vLLM, cualquier servidor
  compatible con OpenAI (hoy el formato solo lo entiende `ba-modelo`);
- **encadena**: `mixer_list` → ver → `audio_set`, o contestar una pregunta con
  lo que devolvió la herramienta;
- es la base del asistente genérico (ver 3).

Las fichas van fijas en el system y se procesan **una vez** (`ba-modelo` ya
guarda el estado después del prefijo), así que no es más lento por pedido. El
estado del aparato va en el mensaje del usuario, recortado a lo que usa cada
tipo de pedido (como hace hoy `argumentos::estadoPara`).

**Gramática**: después de `<tool_call>`, solo JSON válido con un `name` de la
lista y `arguments` según el esquema de esa herramienta (y los nombres, uno de
los del estado). Los errores de formato que vimos (claves sin comillas, el
nombre de la herramienta como valor, `guest:N`) quedan imposibles.

La validación de `ArgumentosLocales` se mantiene como red: existencia,
rangos, la cuenta del volumen.

### 2. El corpus

- **Frases**: `datos/semillas-reactivas.json` (84 herramientas, muchas frases).
- **Estados sintéticos** variados por frase (escenas, fuentes, volúmenes,
  personas con nombres distintos: «Música», «Cortina», «Micro Laura»,
  «Cámara 2»), para que aprenda a **copiar el nombre del estado** y no a
  memorizarlo.
- **La respuesta correcta se genera sola**: hay que anotar en cada semilla qué
  significa («bajar 4 dB a la fuente que suena a música») y un programa
  escribe la llamada con el nombre del estado de turno. 10 a 20 frases
  anotadas por herramienta alcanzan para empezar.
- **Dato real**: `Logs/voz-modelo.jsonl` guarda frase, estado, elección,
  argumentos, decisión y resultado:
  - lo confirmado con «Hacer» → ejemplo positivo;
  - «No» / «Al asistente» → negativo, o par de preferencia (DPO);
  - lo que el asistente grande resolvió tras un `derivar` → destilación.
- **Conversación general** (20-30 % del corpus) para que no pierda lo
  genérico.

### 3. Etiquetas

Como herramientas más del formato (igual que `derivar` hoy):

| etiqueta | para qué |
|---|---|
| `derivar` | no es suyo: al asistente grande (ya existe) |
| `aviso` | el texto es el aviso de un disparador, no una orden |
| `pregunta` / `orden` | contestar contra hacer |
| `charla` | conversación: contesta en texto, no llama nada |
| `falta_dato` | pedir lo que falta («¿a qué escena?») en vez de inventarlo |

### 4. Asistente genérico offline

El modelo base (Qwen 3.5 4B) conversa, resume y contesta en castellano; el
fine-tune le sumó elegir herramientas. Para usarlo como asistente cuando no
hay internet:

- mezclar charla general en cada entrenamiento (si solo ve comandos, se
  especializa y escribe peor);
- con `charla` o `derivar` sin asistente grande disponible, que conteste él en
  texto libre (el motor ya existe: es el que contesta las preguntas en voz
  alta);
- límites esperables de 4B: bien para lo simple; no para razonamientos largos
  ni muchas herramientas encadenadas.

Ya está el proveedor «Modelo local» en el asistente y en los agentes (hoy
todo pasa por elegir una herramienta).

### 5. Alertas (disparadores)

Medido: toma los avisos como órdenes («Terminó Apertura.mp4» → `record_stop`).
Por ahora un disparador nunca se hace solo: se dice en voz alta y se propone.
Para que las atienda bien:

1. entrenar con los textos de `AlertEngine` y la etiqueta `aviso`, donde lo
   correcto es leer, `derivar` o una acción solo cuando corresponde;
2. darle el **contexto** con el aviso: qué escena está al aire, si se
   transmite, qué sigue en el guion;
3. **reglas determinísticas del operador** para lo que es siempre igual
   («cuando termina un video, pasar a la escena X»): más confiables que el
   modelo. El modelo, para lo que depende del caso.

### 6. Filtros de audio (LSP)

- No enseñarle cada parámetro de los 50 filtros (demasiados, técnicos; 4B no
  los retiene).
- **Recetas y plantillas**: «voz de locución», «sacar zumbido», «comprimir
  invitado», armadas una vez con buenos valores; el modelo elige la receta y
  la fuente (ya existen plantillas de rack y el catálogo en castellano con
  «para qué sirve»).
- Ajuste fino con pocos ejemplos en los cinco de uso diario: ecualizador,
  compresor, compuerta, de-esser, limitador («la voz suena opaca» → realce en
  3-5 kHz; «subile 2 dB a los graves»).

### 7. Lo que el banco mostró que falla

- admitir a uno contra admitir a todos (`participant_admit` / `_all`);
- ocultar contra mostrar un gráfico;
- «¿cuánto está la música?» → `audio_measure` en vez de `mixer_list`;
- referencias vagas («ese orador», «el que habla», «ese compartido»): sumar al
  estado quién está al aire, el último compartido y la escena en previa;
- respuestas habladas que cuentan mal («diez» escenas con once): darle los
  datos ya contados y entrenar `resultado → una oración`.

## Lo que dice la investigación (arXiv, octubre de 2026)

Búsqueda sobre cómo entrenar un modelo chico para *tool calling*, aplicada a
este plan.

**La base sigue siendo Qwen 3.5 4B.** No existe un Qwen 3.6 4B: la familia 3.6
en Hugging Face trae solo el 27B y el 35B-A3B (MoE, 3B activos), grandes para
entrenar en las dos 3090 y para la M1 Pro.

1. **Enseñarle el catálogo funciona, con la receta que ya usamos.**
   [2605.17774](https://arxiv.org/html/2605.17774v2) ajusta Qwen3-4B (y Gemma
   4 E4B) sobre un catálogo fijo de 23 herramientas con ~1.700 ejemplos, LoRA
   r=32, lr 2e-4 y 2 épocas: 95-98 % de elección de herramienta, y el ajustado
   **sin** las descripciones en el prompt le gana al base que las tiene.
   Advertencia: **Qwen3-4B conservó solo el 61 % de su capacidad general**
   (MMLU/ARC; Gemma, el 80 %).
2. **Mezclar datos generales para no olvidar.**
   [Alopex, 2411.05209](https://arxiv.org/pdf/2411.05209) mitiga el olvido
   mezclando llamadas a funciones con texto general **1:1** (Gemma-2B,
   Qwen-1.8B). El 20-30 % de charla de este plan puede quedar corto para un 4B:
   **medir con 30 % y con 50 %**, y sumar un examen chico de capacidad general
   además del banco de herramientas.
3. **Lo que no es suyo (`derivar`, `charla`, `aviso`).**
   [Hammer, 2410.04587](https://arxiv.org/html/2410.04587v2) encontró que, al
   mejorar en elegir función, el modelo empeora en darse cuenta de que ninguna
   aplica; lo corrigen con ~10 % de ejemplos donde la función correcta no está y
   lo correcto es no llamar nada. Acá: **al menos 10 % del corpus** para esas
   etiquetas, con frases que **parecen** órdenes y no lo son. (Su «function
   masking», nombres al azar, es para generalizar a herramientas nunca vistas;
   el catálogo es fijo y ya vimos que las etiquetas de fragmentos empeoran, v11:
   no va.)
4. **La gramática como red, no como muleta.** En modelos chicos sin ajustar,
   forzar el JSON con gramática cuesta latencia y a veces calidad (repeticiones
   cuando el modelo «pelea» con la gramática):
   [2605.02363](https://arxiv.org/html/2605.02363v1). Entrenar **exactamente**
   en el formato `<tool_call>` para que la gramática casi nunca tenga que
   intervenir; así funciona hoy el 98 % con los nombres.
5. **Dato real, después: SFT y luego DPO.** La receta estándar es SFT y después
   DPO con pares elegido/rechazado (AWARE-US sobre Qwen3-8B,
   [2601.02643](https://arxiv.org/pdf/2601.02643)). «Hacer» contra «No» en
   `Logs/voz-modelo.jsonl` son esos pares: segundo paso, cuando se junten
   suficientes.
6. **Sintético con verificación automática.**
   [ToolWeave, 2605.12521](https://arxiv.org/pdf/2605.12521) y el punto 1
   coinciden: la llamada la genera un programa y se verifica, no la escribe un
   modelo. Es el enfoque de este plan: semilla anotada → llamada con el nombre
   del estado de turno → `ArgumentosLocales` valida.

**Ajustes al plan que salen de esto**: charla general 30-50 % (medir); al
menos 10 % de «no es suyo»; examen de capacidad general además del banco;
gramática solo como red; DPO con la bitácora como segundo paso. Y para la
cabeza MTP: la fábrica ya la reentrena con el mismo corpus, en cadena y con
texto escrito por el propio modelo (`tools/entrenar/entrenar-mtp.py`), así que
sale entrenada para el formato nuevo sin pasos extra.

Fuentes: [2605.17774](https://arxiv.org/html/2605.17774v2) ·
[2410.04587](https://arxiv.org/html/2410.04587v2) ·
[2411.05209](https://arxiv.org/pdf/2411.05209) ·
[2605.02363](https://arxiv.org/html/2605.02363v1) ·
[2601.02643](https://arxiv.org/pdf/2601.02643) ·
[2605.12521](https://arxiv.org/pdf/2605.12521) ·
[2512.15943](https://arxiv.org/abs/2512.15943) ·
[Qwen3.6-27B](https://huggingface.co/Qwen/Qwen3.6-27B).

## Cuidados

- **Un solo módulo de prompt** para entrenar y servir (`tools/lib_prompt.py`):
  un desajuste entre los dos hunde el acierto (ya pasó: de 56 % a 16 %).
- **MTP apagado** hasta reentrenar su cabeza con el mismo corpus.
- El 98 % de elección actual se tiene que **recuperar** en el formato nuevo
  antes de reemplazar el modelo que anda.

## Cómo medir

`tools/voz-banco.py` contra el aparato real, antes y después de cada
entrenamiento (84 herramientas: elección, argumentos, ejecución de lo seguro y
verificación del efecto), y `tools/voz-banco-informe.py` para el resumen.
Sumar una sala con dos invitados de prueba para cubrir sala, oradores y chat.

## Orden

1. **Un solo entrenamiento** en formato `<tool_call>` de Qwen: herramienta +
   argumentos, etiquetas (`derivar`, `aviso`, `pregunta`, `charla`,
   `falta_dato`), corpus con estados sintéticos y dato real, y charla general.
2. Las alertas con contexto y las reglas del operador.
3. Las recetas de audio.

Cada paso, medido con el banco.
