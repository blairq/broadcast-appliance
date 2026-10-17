---
license: apache-2.0
base_model: Qwen/Qwen3.5-9B
language: [es]
library_name: mlx
pipeline_tag: image-text-to-text
tags: [tool-selection, function-calling, broadcast, live-tv, spanish, qwen3.5, mlx]
---
# broadcast-reactivo-qwen3.5-9b-mtp-ud-mlx-q4

**ES** · Modelo chico para un aparato de producción de TV en vivo: por cada pedido del operador de
cabina («bajale al mic 2», «take a la armada», «¿cuántas personas se ven en el programa?») elige
**qué herramienta del aparato ejecutar**, o contesta `derivar` para pasarle el pedido al asistente
grande. No genera texto ni argumentos: contesta solo el nombre real de la herramienta del MCP (o `derivar`).

**EN** · Small model for a live-TV production appliance: for each operator request (Rioplatense
Spanish) it picks **which tool of the appliance to run** among 84, or answers `derivar` to hand the
request to a larger assistant. It outputs only the tool name.

## Medidas

Banco de orientación: 1.423 frases escritas por agentes independientes (no se usan para entrenar).
*cobertura*: de lo que es suyo, cuánto atiende solo · *precisión*: de lo que atiende, cuánto acierta
(lo que se ve al aire) · *derivado*: de lo ajeno, cuánto pasa al grande. El sesgo mueve el punto de
operación sumándole al logit de `derivar`.

| sesgo | cobertura | precisión | derivado |
|---|---|---|---|
| -4 | 99.0% | 93.6% | 79.1% |
| -2 | 98.0% | 93.8% | 85.9% |
| 0 | 93.7% | 94.9% | 91.2% |
| 2 | 88.8% | 96.0% | 95.0% |
| 4 | 81.7% | 97.2% | 96.7% |

| | bf16 | MLX q4 |
|---|---|---|
| precisión (sesgo 0) | 94.9% | 95.1% |
| derivado (sesgo 0) | 91.2% | 91.3% |
| examen (105 casos) | 86.7% | 84.8% |

## Cabeza MTP

Reentrenada para este modelo, con 3 borradores EN CADENA (cada paso recibe la salida del anterior, como se usa en vivo) y con nombres y texto escrito por el propio modelo. Medido en validación: tokens aceptados por ciclo (de 3) y acierto de cada paso dado que los anteriores acertaron.

| datos | cabeza | aceptados por ciclo | por paso |
|---|---|---|---|
| nombres de herramienta | original | 1.64 | 94% / 97% / 88% |
| nombres de herramienta | entrenada | 1.82 | 100% / 100% / 100% |
| nombres de herramienta | entrenada, lineales q8 | 1.82 | 100% / 100% / 100% |
| nombres de herramienta | entrenada, lineales q4 | 1.82 | 100% / 100% / 100% |
| texto libre | original | 1.53 | 75% / 67% / 66% |
| texto libre | entrenada | 1.65 | 76% / 71% / 72% |
| texto libre | entrenada, lineales q8 | 1.65 | 77% / 70% / 72% |
| texto libre | entrenada, lineales q4 | 1.64 | 76% / 71% / 71% |

## Uso

```python
from mlx_lm import load
modelo, tok = load("<usuario>/broadcast-reactivo-qwen3.5-9b-mtp-ud-mlx-q4")
# prompt = el MISMO de entrenamiento: fichas de las herramientas + el pedido (ver abajo)
```
Elegir con decodificación restringida a los nombres posibles (como `tools/entrenar/evaluar-mlx.py`
del repo del aparato): el prefijo con las fichas se procesa una vez y se reusa entre pedidos.

**El prompt tiene que ser exactamente el de entrenamiento**: el system con las fichas de las
herramientas del perfil, el pedido como turno de usuario y el turno del asistente con el bloque de
razonamiento vacío (`<think>\n\n</think>\n\n`). Su firma está en `huella-prompt.json` (`3f9122d8064004c5`):
con otras fichas es otro modelo, aunque siga contestando.

## Entrenamiento

- Base: [`Qwen/Qwen3.5-9B`](https://huggingface.co/Qwen/Qwen3.5-9B) (revisión `c20223623576`), LoRA r=32 sobre todas
  las lineales (también las de atención lineal), fusionado. 2.0 épocas, lote 16, lr 2e-4 coseno,
  pérdida solo sobre la respuesta.
- Perfil `reactivo`: 84 herramientas de las categorías audio, escenas, fuentes, sala, oradores, imagenes, estado, salidas, graficos, laminas. Lo que no es de
  ellas va a `derivar`.
- Corpus: 7800 ejemplos (firma `b9475ce58078ba6d`), generado desde frases semilla curadas con un bucle
  que mide la separación entre herramientas (CDNV, Frobenius), el margen del decodificador (AUM) y
  revisa el sentido de cada frase contra la ficha real de la herramienta. Código del aparato: commit
  `ae6088f`.

## Limitaciones

- Elige la herramienta; los argumentos los completa el aparato o el asistente grande.
- Entrenado para castellano rioplatense de cabina; otros registros no están medidos.
- Fuera del perfil no hace nada útil: lo deriva.

## Herramientas del perfil

`audio_analyze`, `audio_escuchar`, `audio_measure`, `audio_set`, `audio_sincro`, `capture_feed`, `capture_multiview`, `capture_people`, `capture_person`, `capture_program`, `capture_sequence`, `capture_source`, `capture_ui`, `chat_history`, `chat_send`, `chat_wait`, `gateway_restart`, `grabacion_individual`, `grafico_en_escena`, `grafico_listar`, `grafico_mostrar`, `grafico_ocultar`, `guest_source_add`, `imagen_analizar`, `laminas_aplicar`, `laminas_buscar`, `laminas_ir`, `laminas_listar`, `laminas_ver`, `mixer_list`, `network_status`, `participant_admit`, `participant_admit_all`, `participant_kick`, `participant_profile`, `participant_reject`, `participant_request`, `participant_requeue`, `participant_talk`, `record_start`, `record_stop`, `room_link`, `room_pin`, `room_set`, `room_status`, `rotulo_desvincular`, `rotulo_vincular`, `rotulo_vinculos`, `scene_copy`, `scene_create`, `scene_define`, `scene_delete`, `scene_export`, `scene_folder`, `scene_list`, `scene_move`, `scene_preview`, `scene_protect`, `scene_rename`, `scene_switch`, `scene_take`, `share_delete`, `share_list`, `share_take`, `share_view`, `source_add`, `source_list`, `source_properties`, `source_remove`, `source_set_property`, `source_types`, `source_update`, `speaker_move`, `speaker_next`, `speaker_on_air`, `speaker_presenter`, `speaker_queue`, `speaker_time`, `speakers_config`, `speakers_status`, `status`, `stream_start`, `stream_stop`, `transition_set`

## Archivos

`model.safetensors`: MLX, 4 bits (afín, grupos de 64). Con la parte de visión (la torre sin cuantizar, igual a la de la base: el ajuste no la toca): mlx-vlm lo usa con imágenes y mlx-lm lo carga como texto. Lo que no está cuantizado (escalas, normas, visión) va en **fp16**: corriendo en fp16 el q4 pierde 0.0 puntos en el banco (las GPU M1/M2 no tienen bf16 nativo).
`mtp.safetensors` (fp16) y `mtp-q8.safetensors` (lineales en 8 bits, afín, grupos de 64): la cabeza MTP entrenada, como la carga `ba-modelo` (mlx-swift-lm). Normas en la convención de Hugging Face (sin el +1: lo suma mlx-swift-lm al cargarla). Van fuera de `model*.safetensors` a propósito: mlx-lm 0.31 tomaría cualquier peso `mtp.*` ahí como un checkpoint sin convertir y correría las normas del modelo. `huella-prompt.json`: firma del prompt. `LICENSE`: la de la base.
