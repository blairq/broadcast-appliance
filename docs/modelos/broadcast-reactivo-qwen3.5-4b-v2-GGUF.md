---
license: apache-2.0
base_model: Qwen/Qwen3.5-4B
language: [es]
library_name: gguf
pipeline_tag: image-text-to-text
tags: [function-calling, tool-calling, broadcast, live-tv, spanish, qwen3.5, gguf, llama.cpp]
---
# broadcast-reactivo-qwen3.5-4b-v2-GGUF

**ES** · V2 del modelo chico de un aparato de producción de TV en vivo: por cada pedido del operador
escribe **la llamada completa**, herramienta y argumentos, en el formato nativo de tool calling de
Qwen3.5 (`<tool_call><function=...><parameter=...>`), copiando los nombres del estado del aparato.
Si no es suyo llama a `derivar`; si falta un dato obligatorio, a `falta_dato` con la pregunta; si es
charla, contesta en texto.

**EN** · V2: writes the **full call** (tool + arguments) in Qwen3.5's native tool-calling format,
copying entity names from the appliance state; `derivar` hands off, `falta_dato` asks for missing data.

## Medidas (bf16 · MLX q4)

| | bf16 | MLX q4 |
|---|---|---|
| argumentos exactos (plantillas nunca vistas) | 87.6% | 86.6% |
| herramienta correcta (ídem) | 91.5% | 90.0% |
| formato válido | 100.0% | 100.0% |
| falta_dato cuando corresponde | 81.0% | 72.6% |
| banco: precisión (eleccion) | 91.6% | 86.5% |
| banco: cobertura | 81.7% | 74.8% |
| banco: derivado | 84.5% | 87.5% |
| examen (105) | 64.8% | 63.8% |

Charla general (respuestas del modelo base que no se entrenaron): pérdida 0.466 por
token; contesta charla con una llamada en el 16 % de los casos.

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

## Uso

```bash
llama-server -m broadcast-reactivo-qwen3.5-4b-v2-Q4_K_M.gguf --mmproj mmproj-F16.gguf -c 16384 --cache-reuse 256 --spec-type draft-mtp --spec-draft-n-max 3
```

Todo lo que hace falta para servirlo está en `prompt.json`: la cabeza exacta (system con las
84 herramientas y `derivar`/`falta_dato`), la forma del pedido, la cola y las convenciones:
nombres copiados del estado; cambios con signo (`-4`, `+3`; «un poco» = 3, sin cantidad = 6); valor
exacto con `=` (`=-10`); booleanos `true`/`false`. Firma del prompt: `900e34cd7e5900fb`.

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

## Entrenamiento

- Base [`Qwen/Qwen3.5-4B`](https://huggingface.co/Qwen/Qwen3.5-4B), LoRA r=32 sobre todas las lineales, fusionado.
- Corpus 8719 ejemplos: {"herramienta": 0.409, "falta": 0.102, "derivar": 0.132, "charla": 0.356} (herramienta con argumentos,
  falta_dato, derivar, charla). Las semillas curadas como plantillas con huecos, rendidas con estados
  del aparato sintéticos y distintos; la charla, respuestas del propio Qwen3.5 base (pedidos de
  databricks-dolly-15k en castellano y del oficio). Ver `docs/entrenamiento-avanzado.md` del aparato.

## Limitaciones

- Primera versión del formato: medida contra plantillas apartadas y el banco, todavía no con el
  aparato andando (`tools/voz-banco.py`).
- La app tiene que validar (`ArgumentosLocales`): nombres que existen, rangos, la cuenta del cambio.

## Archivos

`broadcast-reactivo-qwen3.5-4b-v2-Q4_K_M.gguf`: Q4_K_M, calibrado con imatrix sobre el propio corpus (sin el examen) e incluye la cabeza MTP entrenada.
`mmproj-F16.gguf`: la parte de visión para llama.cpp (`--mmproj`), igual a la de la base. `prompt.json`: todo para servirlo. `huella-prompt.json` y `LICENSE`.
