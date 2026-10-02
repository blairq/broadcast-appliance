---
license: apache-2.0
base_model: Qwen/Qwen3.5-4B
language: [es]
library_name: mlx
pipeline_tag: image-text-to-text
tags: [function-calling, tool-calling, broadcast, live-tv, spanish, qwen3.5, vision, mlx]
---

# broadcast-reactivo-qwen3.5-4b-v2-mlx-q4

> **ES** · El modelo local de un aparato de producción de TV en vivo. Por cada pedido del operador
> («bajale 4 a la música») escribe **la llamada completa** —herramienta y argumentos— en el formato
> nativo de tool calling de Qwen3.5, copiando los nombres del estado del aparato. Si el pedido no es
> suyo lo deriva; si falta un dato, lo pregunta; si es charla, contesta en texto. **Ve imágenes**
> (la torre de visión de Qwen3.5 va incluida).
>
> **EN** · Local model for a live-TV production appliance: it writes the **full tool call** (tool +
> arguments) in Qwen3.5's native format, copying names from the appliance state; it hands off what
> isn't its own and asks when data is missing. Includes Qwen3.5's vision tower. Rioplatense Spanish.

**84 herramientas** · base [`Qwen/Qwen3.5-4B`](https://huggingface.co/Qwen/Qwen3.5-4B) · MLX 4 bits, todo en fp16, con visión · cabeza MTP entrenada

## Resultados

| | bf16 | MLX q4 |
|---|---:|---:|
| Argumentos exactos (plantillas nunca vistas) | 87.6 % | 86.6 % |
| Herramienta correcta (ídem) | 91.5 % | 90.0 % |
| Formato válido | 100.0 % | 100.0 % |
| `falta_dato` cuando corresponde | 81.0 % | 72.6 % |
| Banco · precisión al elegir | 91.6 % | 86.5 % |
| Banco · cobertura | 81.7 % | 74.8 % |
| Banco · derivado | 84.5 % | 87.5 % |
| Examen (105 casos) | 64.8 % | 63.8 % |

Medido contra lo que **no** se entrenó: plantillas apartadas con estados nuevos, y el banco de
orientación (1.423 frases escritas aparte). *Cobertura*: de lo que es suyo, cuánto atiende solo ·
*precisión*: de lo que atiende, cuánto acierta · *derivado*: de lo ajeno, cuánto pasa al asistente
grande. Charla general: pérdida 0.47 por token sobre respuestas del modelo base no
entrenadas; contesta charla con una llamada en el 16 % de los casos.

## Uso rápido

```python
from mlx_vlm import load          # con imágenes; mlx_lm.load si es solo texto
modelo, proc = load("BlairQ/broadcast-reactivo-qwen3.5-4b-v2-mlx-q4")
# el prompt: prompt.json -> cabeza + "Estado: {...}\nPedido: ..." + cola
```
En la Mac del aparato lo sirve `ba-modelo` (mlx-swift-lm), con la cabeza MTP en `mtp.safetensors`.

## Visión

El modelo **ve imágenes**: la torre de visión de Qwen3.5 (333M parámetros) va **dentro de `model.safetensors`**, sin cuantizar y en fp16 como el resto. mlx-vlm la usa con imágenes; mlx-lm carga el mismo archivo como texto (la ignora). Es **la misma torre de la base**: el ajuste entrenó solo la parte
de texto y la visión no se toca, así que ve como Qwen3.5-4B (capturas de la salida, de una fuente, del
multiview). Verificado al armarlo: describe una imagen de prueba igual que el modelo base.

```python
from mlx_vlm import load, generate
from mlx_vlm.prompt_utils import apply_chat_template
modelo, proc = load("BlairQ/broadcast-reactivo-qwen3.5-4b-v2-mlx-q4")
p = apply_chat_template(proc, modelo.config, "¿Qué se ve en esta imagen?", num_images=1)
print(generate(modelo, proc, p, image=["captura.png"], max_tokens=80).text)
```

## Formato (el contrato para servirlo)

Todo sale de `prompt.json` (fuente: `tools/lib_tc.py` del aparato). Firma del prompt: `900e34cd7e5900fb`.

**1. Cabeza fija** (`cabeza`, ~8.200 tokens, idéntica byte a byte: procesarla una vez y reusar el
caché). Es el system nativo de tools de Qwen3.5: `# Tools … <tools>` con un JSON por línea por cada
herramienta (descripción corta y esquema compactado) más dos pseudo-funciones, **`derivar`** (sin
argumentos: no es para estas herramientas) y **`falta_dato`** (argumento `pregunta`), la instrucción
estándar de Qwen y las reglas del aparato. Termina en `<|im_start|>user\n`.

**2. Turno del usuario**

```
Estado: {JSON compacto del estado}
Pedido: <frase del operador>
```

JSON sin espacios (`separators=(',',':')`) y con tildes reales. El estado **completo**:
`escena_actual`, `escena_armada` (texto), `escenas` (nombres), `fuentes_de_la_escena_actual`
(`[{nombre, visible}]`), `audio` (`[{nombre, volumen_db, silenciada}]`; el primero es el micrófono
del operador, el predeterminado si no se nombra fuente), `participantes` (`[{id, nombre, estado}]`),
`graficos`, `carruseles` (nombres). Charla sin estado: solo la frase.

**3. Cola** (`cola`): `<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n` (sin razonamiento).

**4. Respuesta** (termina en `<|im_end|>`)

```
<tool_call>
<function=audio_set>
<parameter=name>
Música
</parameter>
<parameter=volumeDb>
-4
</parameter>
</function>
</tool_call>
```

- herramienta: `<function=nombre>` con sus `<parameter=…>`;
- no es suyo: `<function=derivar>` sin parámetros (al asistente grande);
- falta un dato obligatorio: `<function=falta_dato>` con `pregunta` («¿A qué escena?»);
- charla: texto, sin `<tool_call>`.

Para leerla: `<tool_call>\s*<function=([^>\n]+)>(.*?)</function>\s*</tool_call>` y los parámetros con
`<parameter=([^>\n]+)>\n?(.*?)\n?</parameter>`. Los valores salen como texto.

**5. Convenciones de los valores** (en XML no hay comillas: por eso el `=`)

| | se escribe | ejemplo |
|---|---|---|
| nombres | exactos, copiados del estado | `Micro Laura` |
| cambio relativo | con signo; «un poco» = 3, sin cantidad = 6; **quien sirve lo suma** al valor real y lo recorta al rango | `-4`, `+3` |
| valor exacto | con `=` adelante | `=-10` |
| booleanos | | `true` / `false` |
| enum | el valor exacto del esquema | |

**6. Recomendado al servirlo**

- Restringir el nombre: después de `<tool_call>\n<function=` solo un nombre de `funciones` y `>`;
  el resto, libre (así se midió el banco; el formato salió válido en el 100 % sin gramática).
- Dial de `derivar`: sumarle un sesgo al logit de su primer token, como en el v1.
- Validar siempre: nombres que existen, rangos, requeridos.
- MTP: la misma cabeza que el v1 (`mtp.safetensors` fp16 / `mtp-q8.safetensors` en el MLX; adentro
  del GGUF para `--spec-type draft-mtp`).

**Diferencias con el v1**: el v1 contesta solo el nombre de la herramienta (con otro prompt, de
fichas) y los argumentos van en una segunda llamada; el v2 hace las dos cosas en una. En el v1 el
valor exacto iba como número suelto; en el v2, `-10` es un cambio y `=-10` es exacto.

## Cómo se entrenó: el loop

Este modelo no sale de juntar muchos ejemplos: sale de un **loop** que mide cada frase, la corrige y
vuelve a medir, primero sobre el corpus y después sobre el modelo entrenado.

### 1. El corpus crece por calidad, no por cantidad

De 15 a 20 frases semilla por herramienta (84 herramientas), como las dice un operador de cabina
rioplatense. **Cada frase es un vector**: el estado interno del modelo cuando la lee. Cada herramienta
es una nube de vectores, con su centro μ y su dispersión Σ, y se mide cuánto se separa de las demás:

- **Separación de un grupo** contra el más peligroso (Frobenius):
  `Q(a) = min_b ‖μa − μb‖² / (‖Σa‖_F + ‖Σb‖_F)`
- **Puntaje de una frase**: cuánto cae Q sin ella, `(Q(a) − Q(a sin la frase)) / Q(a)`. Negativo: la
  frase empeora su grupo, y se sabe hacia qué vecina tira.
- **Choque entre dos grupos** (CDNV direccional, arXiv 2603.03530): la varianza de las dos nubes sobre
  el eje que las une, dividida por la distancia entre centros; acota el error del clasificador por
  centro más cercano. Un grupo bueno es **angosto hacia sus vecinas y ancho en todo lo demás**.
- **Margen del decodificador** (AUM): logit de la herramienta correcta menos el del mejor rival.

La matemática **no poda a ciegas**: señala qué frase está mal y hacia dónde tira.

### 2. Agentes que corrigen, con contexto real

Un agente (una LLM sin conexión, que solo ve su lote) recibe la ficha real de la herramienta, la de
sus vecinas y la frase señalada, y la reescribe atacando ese detalle; separa los pares que se pisan;
un juez ciego verifica. Después de cada cambio se recalcula el grupo: si se corrió se realinea, si
quedó apretado se abre con formas de pedir distintas. **Test → arreglar → test**, hasta que todas
dan bien.

### 3. STE-ES: el alma de ASD-STE100, en castellano de cabina

Las frases las reescribe una LLM, así que se le pone un lenguaje controlado: **una palabra, un
significado**; las palabras de muchos sentidos («poné», «sacá», «pasá») solo con el objeto que fija
cuál; un pedido, una acción; objeto y cantidad explícitos. El núcleo de la frase se controla y la
superficie (voseo, muletillas, typos) queda libre, porque el modelo tiene que entender al operador
real. En las semillas, el 64 % arrancaba con una palabra que comparten cuatro o más herramientas:
justo donde los grupos se pisan.

### 4. Lo que mide no se toca

Un examen (105 casos) y un banco de 1.423 frases escritas aparte **nunca se entrenan ni se le
muestran a un agente**. Es lo único que mide de verdad.

### 5. Herramienta y argumentos, con estados del aparato inventados

Cada semilla es una plantilla con huecos y sus argumentos («bajale 4 a `{audio}`» → `audio_set`,
`volumeDb: -4`), validada contra el esquema de la herramienta y contra STE-ES. Se rinde varias veces
con **estados del aparato sintéticos y distintos**: así el modelo aprende a **copiar** el nombre que
hay en el estado («Micro Laura», «Cámara 2») en vez de memorizarlo.

### 6. Lo que no es suyo, y no olvidar lo general

Además de las llamadas: «no es mío» (`derivar`, al asistente grande), «falta un dato»
(`falta_dato`, pregunta) y **charla general escrita por el propio modelo base**, para que el
ajuste no le borre lo genérico (ajustado solo a un catálogo, Qwen3-4B conserva el 61 % de su
capacidad general: arXiv 2605.17774).

### 7. El loop del modelo

Entrenar → medir con plantillas que nunca vio y con el banco → **autoexamen**: las plantillas
entrenadas con estados nuevos → un agente corrige las anotaciones o suma ejemplos de lo que falló
(validados por esquema y STE-ES) → otra vuelta. Se queda con la mejor.

### 8. Ingeniería

El system con las 84 herramientas son ~8.200 tokens: se procesan una vez por lote y se reusan. En el
backward cada capa se recalcula sola (con una foto de su caché que se repone antes), y los logits se
calculan solo donde hay pérdida: así entra en dos RTX 3090. La cabeza MTP se reentrena para este
modelo con los borradores en cadena, como se usan en vivo, y con texto escrito por el propio modelo.


## Datos de entrenamiento

- 8719 ejemplos: herramienta 41 % · falta 10 % · derivar 13 % · charla 36 % (herramienta con argumentos, `falta_dato`, `derivar`, charla).
- LoRA r=32 sobre todas las lineales (también las de atención lineal), lr 2e-4 coseno, fusionado.
- La charla: pedidos de [databricks-dolly-15k](https://huggingface.co/datasets/argilla/databricks-dolly-15k-curated-multilingual)
  en castellano (CC-BY-SA 3.0, solo las instrucciones) y del oficio, contestados por el Qwen3.5 base.
- Firma del prompt `900e34cd7e5900fb` · corpus `271addc85e22ac82` · código del aparato `3d791a1`.

## Cabeza MTP

Reentrenada para este modelo, con 3 borradores EN CADENA (cada paso recibe la salida del anterior, como se usa en vivo) y con nombres y texto escrito por el propio modelo. Medido en validación: tokens aceptados por ciclo (de 3) y acierto de cada paso dado que los anteriores acertaron.

| datos | cabeza | aceptados por ciclo | por paso |
|---|---|---|---|
| nombres de herramienta | original | 2.08 | 85% / 83% / 83% |
| nombres de herramienta | entrenada | 2.15 | 86% / 84% / 88% |
| nombres de herramienta | entrenada, lineales q8 | 2.15 | 86% / 84% / 88% |
| nombres de herramienta | entrenada, lineales q4 | 2.15 | 86% / 85% / 87% |
| texto libre | original | 1.32 | 70% / 58% / 54% |
| texto libre | entrenada | 1.54 | 74% / 67% / 65% |
| texto libre | entrenada, lineales q8 | 1.54 | 74% / 67% / 64% |
| texto libre | entrenada, lineales q4 | 1.52 | 74% / 66% / 64% |

## Limitaciones

- Primera versión del formato: medida contra plantillas apartadas y el banco, todavía no con el
  aparato andando.
- Atiende solo lo suyo: deriva al asistente grande más que la versión de solo nombres (ver cobertura).
- Quien lo sirve tiene que validar los argumentos: nombres que existen, rangos, la cuenta del cambio.

## Archivos

`model.safetensors`: MLX, 4 bits (afín, grupos de 64). Con la parte de visión (la torre sin cuantizar, igual a la de la base: el ajuste no la toca): mlx-vlm lo usa con imágenes y mlx-lm lo carga como texto. Lo que no está cuantizado (escalas, normas, visión) va en **fp16**: corriendo en fp16 el q4 pierde 0.2 puntos en el banco (las GPU M1/M2 no tienen bf16 nativo).
`mtp.safetensors` (fp16) y `mtp-q8.safetensors` (lineales en 8 bits, afín, grupos de 64): la cabeza MTP entrenada, como la carga `ba-modelo` (mlx-swift-lm). Normas en la convención de Hugging Face (sin el +1: lo suma mlx-swift-lm al cargarla). Van fuera de `model*.safetensors` a propósito: mlx-lm 0.31 tomaría cualquier peso `mtp.*` ahí como un checkpoint sin convertir y correría las normas del modelo.
`prompt.json`: todo para servirlo · `huella-prompt.json`: firma del prompt · `LICENSE`: la de la base.
