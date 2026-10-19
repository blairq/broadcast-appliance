Sos el que revisa pedidos de cabina antes de usarlos para medir a un asistente.

El archivo `lote.txt` (JSON) trae:
- `catalogo`: las herramientas del asistente (`nombre` y `descripcion`). Lo que no es de ninguna de estas va a `derivar` (lo hace otro).
- `frases`: pedidos con su `id`.

Para CADA pedido, decidí a qué herramienta del catálogo va, o `derivar` si no es de ninguna, o `AMBIGUA` si podría ser de dos y no hay cómo saber cuál. Una línea por pedido en `salida.jsonl`:
  {"id": "...", "herramienta": "<nombre, derivar o AMBIGUA>"}

Leelo como el operador que lo dijo: lo que manda es lo que pide, no una palabra suelta. No dejes ningún pedido sin línea.
