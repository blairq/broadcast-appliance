---
license: apache-2.0
base_model: Qwen/Qwen3.5-4B
language: [es]
library_name: gguf
pipeline_tag: image-text-to-text
tags: [tool-selection, function-calling, broadcast, live-tv, spanish, qwen3.5, gguf, llama.cpp, mtp]
---
# broadcast-reactivo-qwen3.5-4b-GGUF

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
| -4 | 97.5% | 91.4% | 74.0% |
| -2 | 96.2% | 91.8% | 83.8% |
| 0 | 92.4% | 93.1% | 88.9% |
| 2 | 89.1% | 94.0% | 92.9% |
| 4 | 83.2% | 96.0% | 95.9% |

| | bf16 | MLX q4 |
|---|---|---|
| precisión (sesgo 0) | 93.1% | 92.3% |
| derivado (sesgo 0) | 88.9% | 87.0% |
| examen (105 casos) | 88.6% | 87.6% |

## Cabeza MTP

Reentrenada para este modelo, con 3 borradores EN CADENA (cada paso recibe la salida del anterior, como se usa en vivo) y con nombres y texto escrito por el propio modelo. Medido en validación: tokens aceptados por ciclo (de 3) y acierto de cada paso dado que los anteriores acertaron.

| datos | cabeza | aceptados por ciclo | por paso |
|---|---|---|---|
| nombres de herramienta | original | 1.64 | 94% / 94% / 92% |
| nombres de herramienta | entrenada | 1.83 | 100% / 100% / 100% |
| nombres de herramienta | entrenada, lineales q8 | 1.83 | 100% / 100% / 100% |
| nombres de herramienta | entrenada, lineales q4 | 1.83 | 100% / 100% / 100% |
| texto libre | original | 1.93 | 83% / 77% / 76% |
| texto libre | entrenada | 1.88 | 81% / 77% / 77% |
| texto libre | entrenada, lineales q8 | 1.88 | 81% / 77% / 77% |
| texto libre | entrenada, lineales q4 | 1.88 | 81% / 77% / 78% |

## Uso

```bash
llama-server -m broadcast-reactivo-qwen3.5-4b-Q4_K_M.gguf --mmproj mmproj-F16.gguf -c 8192 --cache-reuse 256 --spec-type draft-mtp --spec-draft-n-max 3
```
llama.cpp b11255 o posterior (arquitectura `qwen35`, con atención lineal). Restringir la salida con
una gramática a los nombres posibles.

**El prompt tiene que ser exactamente el de entrenamiento**: el system con las fichas de las
herramientas del perfil, el pedido como turno de usuario y el turno del asistente con el bloque de
razonamiento vacío (`<think>\n\n</think>\n\n`). Su firma está en `huella-prompt.json` (`3f9122d8064004c5`):
con otras fichas es otro modelo, aunque siga contestando.

## Entrenamiento

- Base: [`Qwen/Qwen3.5-4B`](https://huggingface.co/Qwen/Qwen3.5-4B) (revisión `851bf6e806ef`), LoRA r=32 sobre todas
  las lineales (también las de atención lineal), fusionado. 2 épocas, lote 16, lr 2e-4 coseno,
  pérdida solo sobre la respuesta.
- Perfil `reactivo`: 84 herramientas de las categorías audio, escenas, fuentes, sala, oradores, imagenes, estado, salidas, graficos, laminas. Lo que no es de
  ellas va a `derivar`.
- Corpus: 7800 ejemplos (firma `b9475ce58078ba6d`), generado desde frases semilla curadas con un bucle
  que mide la separación entre herramientas (CDNV, Frobenius), el margen del decodificador (AUM) y
  revisa el sentido de cada frase contra la ficha real de la herramienta. Código del aparato: commit
  `17d3142`.

## Limitaciones

- Elige la herramienta; los argumentos los completa el aparato o el asistente grande.
- Entrenado para castellano rioplatense de cabina; otros registros no están medidos.
- Fuera del perfil no hace nada útil: lo deriva.

## Herramientas del perfil

`audio_analyze`, `audio_escuchar`, `audio_measure`, `audio_set`, `audio_sincro`, `capture_feed`, `capture_multiview`, `capture_people`, `capture_person`, `capture_program`, `capture_sequence`, `capture_source`, `capture_ui`, `chat_history`, `chat_send`, `chat_wait`, `gateway_restart`, `grabacion_individual`, `grafico_en_escena`, `grafico_listar`, `grafico_mostrar`, `grafico_ocultar`, `guest_source_add`, `imagen_analizar`, `laminas_aplicar`, `laminas_buscar`, `laminas_ir`, `laminas_listar`, `laminas_ver`, `mixer_list`, `network_status`, `participant_admit`, `participant_admit_all`, `participant_kick`, `participant_profile`, `participant_reject`, `participant_request`, `participant_requeue`, `participant_talk`, `record_start`, `record_stop`, `room_link`, `room_pin`, `room_set`, `room_status`, `rotulo_desvincular`, `rotulo_vincular`, `rotulo_vinculos`, `scene_copy`, `scene_create`, `scene_define`, `scene_delete`, `scene_export`, `scene_folder`, `scene_list`, `scene_move`, `scene_preview`, `scene_protect`, `scene_rename`, `scene_switch`, `scene_take`, `share_delete`, `share_list`, `share_take`, `share_view`, `source_add`, `source_list`, `source_properties`, `source_remove`, `source_set_property`, `source_types`, `source_update`, `speaker_move`, `speaker_next`, `speaker_on_air`, `speaker_presenter`, `speaker_queue`, `speaker_time`, `speakers_config`, `speakers_status`, `status`, `stream_start`, `stream_stop`, `transition_set`

## Archivos

`broadcast-reactivo-qwen3.5-4b-Q4_K_M.gguf`: Q4_K_M, calibrado con imatrix sobre el propio corpus (sin el examen) e incluye la cabeza MTP entrenada.
`mmproj-F16.gguf`: la parte de visión para llama.cpp (`--mmproj`), igual a la de la base. `huella-prompt.json`: firma del prompt. `LICENSE`: la de la base.
