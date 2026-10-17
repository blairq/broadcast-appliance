# El modelo local: índice

El modelo chico que corre en el aparato (Qwen3.5 + LoRA) y atiende lo inmediato:
elige la herramienta para un pedido de cabina (y hoy también sus argumentos),
pregunta si falta un dato y deriva al asistente grande lo que no es suyo.

## Qué leer, según para qué

| para | documento | estado |
|---|---|---|
| **entrenar hoy** (scripts, datos, perfil, medidas, recetas, resultados) | [bucle-tool-calling.md](bucle-tool-calling.md) | vigente (17/10/2026) |
| el método y las decisiones de fondo (las dos placas, FSDP2, prefijo compartido, exportar, qué entrenar y por qué) | [entrenamiento-avanzado.md](entrenamiento-avanzado.md) | vigente |
| la geometría (CDNV) para curar el corpus y la metodología de datos apartados | [bucle-corpus.md](bucle-corpus.md) | el método sigue (tc-geometria); los números son del formato de solo nombres |
| cómo se definió la tarea y lo que se midió al principio | [modelo-local.md](modelo-local.md) | historia: formato de solo nombres (73–84 herramientas) |
| lo dictado: de la voz a la acción, con el aparato andando | [voz-modelo-local.md](voz-modelo-local.md) | el camino de la app; las medidas son de la v5 |
| el banco de voz, herramienta por herramienta | [voz-modelo-local-banco.md](voz-modelo-local-banco.md) | datos de la v5 |
| el castellano controlado de las frases | [../ste-es.md](../ste-es.md) | vigente |

Los scripts están en `tools/entrenar/` (el formato de tool calling: `tc-*.py`,
`entrenar-rapido.py`, `lib_tc.py`; ver la tabla de bucle-tool-calling.md) y su
[README](../../tools/entrenar/README.md) es del formato anterior.

## Los dos formatos

- **Solo nombres** (hasta el 01/10/2026): el modelo elige el NOMBRE de la
  herramienta con salida restringida; los argumentos los arma otra llamada.
  Precisión en el banco 93–95 %. Son los modelos `broadcast-reactivo-qwen3.5-4b`,
  `-4b-mtp-ud` y `-9b-mtp-ud`.
- **Tool calling** (desde la v2, 02/10/2026): una sola respuesta con la
  herramienta y los argumentos en el formato nativo de Qwen3.5, `falta_dato`,
  `derivar` o charla. Más trabajo en una sola llamada: la precisión al elegir
  bajó (83–93 %) y es el frente abierto (ver «Pendiente» en bucle-tool-calling.md).

## Los modelos publicados

Las tarjetas (las mismas que van a Hugging Face) y el resumen de lo que define a
cada uno (`<nombre>.json`: base, corpus, perfil, medidas) están en
[publicados/](publicados/); las escribe `tools/entrenar/fabricar-modelo.py`.

| modelo | formato | herramientas | banco: precisión | banco: cobertura | examen |
|---|---|---|---|---|---|
| broadcast-reactivo-qwen3.5-4b | solo nombres | ~84 | 93,1 | 92,4 | 88,6 |
| broadcast-reactivo-qwen3.5-4b-mtp-ud | solo nombres | ~84 | 94,2 | 95,7 | 85,7 |
| broadcast-reactivo-qwen3.5-9b-mtp-ud | solo nombres | ~84 | 94,9 | 93,7 | 86,7 |
| broadcast-reactivo-qwen3.5-4b-v2 | tool calling | 84 | 91,6 | 81,7 | 64,8 |
| broadcast-reactivo-qwen3.5-4b-v3 | tool calling | 84 | 92,7 | 76,6 | 65,7 |
| broadcast-reactivo-qwen3.5-4b-v5 | tool calling | 102 | 86,9 | 75,5 | 68,6 |

(bf16, banco completo de 1.423 frases con el evaluador de cada momento; cada
uno en GGUF y MLX q4.) Lo entrenado después y no publicado (v6b, v7) está en
bucle-tool-calling.md, «Resultados».

## Otros archivos de esta carpeta

- `bateria-local.json`: la última batería del modelo de solo nombres
  (`tools/entrenar/bateria-local.py`); la lee `tools/entrenar/fallos.py`.
