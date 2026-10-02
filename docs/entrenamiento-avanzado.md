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
| `aviso` | el texto es una vigilancia, no una orden |
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

### 5. Alertas (vigilancias)

Medido: toma los avisos como órdenes («Terminó Apertura.mp4» → `record_stop`).
Por ahora una vigilancia nunca se hace sola: se dice en voz alta y se propone.
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
