Tu trabajo: convertir pedidos que un asistente de cabina ENTENDIÓ MAL en material para enseñarle.

El archivo `lote.txt` (JSON) trae `casos`. Cada caso tiene:
- `id` y la `frase` tal como la dijo un operador;
- `correcta`: lo que el asistente tenía que hacer (`nombre`, `descripcion`, `esquema` de argumentos). Si es `derivar`, el pedido no era de este asistente: lo tenía que pasar a otro.
- `eligio`: lo que hizo en cambio (`nombre` y `descripcion`). Es la confusión que hay que corregir.
- `variaciones`: cuántas variaciones escribir.

Para CADA caso escribí en `salida.jsonl`:

1. UNA línea `eje`: la frase TAL CUAL, con un solo cambio permitido: cada mención de una entidad del aparato (con su artículo) pasa a ser un hueco, y en `args` va el mismo hueco donde va ese nombre.
   {"id": "...", "tipo": "eje", "plantilla": "<la frase tal cual, con huecos>", "args": {...}}
2. `variaciones` líneas `variacion`: el MISMO pedido dicho de otras maneras por operadores distintos.
   {"id": "...", "tipo": "variacion", "plantilla": "...", "args": {...}}

Huecos: `{escena}`, `{audio}`, `{fuente}`, `{persona}`, `{grafico}`, `{carrusel}`, `{camara}`, `{posicion}`, `{discurso}`, `{medio}`, `{pregunta}`, `{pantalla}`, `{receta}`, `{banca}`, `{capa}`, `{punto}`, `{mensaje}`, `{documento}`; si hay dos del mismo tipo, `{escena}` y `{escena2}`. Ejemplo: «bajale 4 al micro de Juan» → plantilla «bajale 4 a {audio}», args {"name": "{audio}", "volumeDb": "-4"}. Si la entidad sale del contexto («la que está al aire», «la armada»), en args va "[escena_actual]" o "[escena_armada]". Si es audio y no nombra la fuente, es el micrófono del operador: {"name": "{audio_operador}"} sin hueco en la plantilla. Números que suben o bajan: con signo y como texto ("-4", "+3"); valor exacto con «=» ("=-10"). Solo los argumentos que el pedido cambia, con los nombres del esquema. Si `correcta` es `derivar`, `args` es {}.

Cómo escribir las variaciones (lo más importante):
- La frase original es el CENTRO: las variaciones van alrededor de ella, no todas hacia un lado. Algunas más cortas y otras más largas; algunas más formales y otras más coloquiales; algunas como orden y otras como pregunta o pedido de costado; algunas con otras palabras para la misma cosa. Que la original quede en el medio de todas.
- Todas piden EXACTAMENTE lo mismo que la original, para la herramienta `correcta`. Ninguna puede pedir lo que hace `eligio`.
- Que se note lo que separa `correcta` de `eligio` cuando la original lo dice; no agregues datos que la original no tiene.
- Rioplatense de cabina, como lo diría gente real con apuro. No copies la descripción.
- Distintas entre sí: no cambies dos palabras de la anterior.

Una línea JSON por elemento, nada más en el archivo. Todos los casos, cada uno con su eje y sus variaciones.
