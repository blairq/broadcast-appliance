---
license: apache-2.0
base_model: Qwen/Qwen3.5-9B
language: [es]
library_name: gguf
pipeline_tag: text-generation
tags: [tool-selection, function-calling, broadcast, live-tv, spanish, qwen3.5, gguf, llama.cpp, mtp]
---
# broadcast-reactivo-qwen3.5-9b-mtp-ud-GGUF

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

La cabeza MTP (multi-token prediction) se reentrenó para este modelo: acierta el token siguiente en el 99.8% de los casos de validación (la original, 94.3%), así que la decodificación especulativa casi siempre acepta el borrador.

## Uso

```bash
llama-server -m broadcast-reactivo-qwen3.5-9b-mtp-ud-UD-Q4_K_XL.gguf -c 8192 --cache-reuse 256 --spec-type draft-mtp --spec-draft-n-max 4
```
llama.cpp b11255 o posterior (arquitectura `qwen35`, con atención lineal). Restringir la salida con
una gramática a los nombres posibles.

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

`broadcast-reactivo-qwen3.5-9b-mtp-ud-UD-Q4_K_XL.gguf`: UD-Q4_K_XL (la receta «UD» de [unsloth/Qwen3.5-9B-MTP-GGUF](https://huggingface.co/unsloth/Qwen3.5-9B-MTP-GGUF): el mismo tipo por tensor que su `Qwen3.5-9B-UD-Q4_K_XL.gguf`, aplicado a estos pesos), calibrado con imatrix sobre el propio corpus (sin el examen) e incluye la cabeza MTP entrenada. `huella-prompt.json`: firma del prompt. `LICENSE`: la de la base.
