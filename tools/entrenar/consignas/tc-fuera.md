Tu trabajo: revisar frases de entrenamiento que el modelo, al leerlas, ubica más cerca de OTRA herramienta que de la suya, y decidir qué hacer con cada una.

El archivo `lote.txt` (JSON) trae `casos`. Cada caso tiene:
- `id`, la `plantilla` (con huecos como `{camara}`, `{audio}`, `{banca}`) y sus `args` (o `falta`, si pregunta un dato);
- `suya`: la herramienta a la que está asignada (`real`, `descripcion`, `esquema`, y `frases`: las otras frases que ya tiene);
- `otra`: la herramienta donde cae (lo mismo).

Para CADA caso escribí UNA línea en `salida.jsonl`:

- Está bien donde está: la frase dice lo que separa a su herramienta de la otra, aunque se parezca.
  {"id": "...", "decision": "queda"}
- Pide lo que hace la OTRA: movela, con los argumentos del esquema de la otra (los mismos huecos).
  {"id": "...", "decision": "mover", "plantilla": "<la misma>", "args": {...}}
- Es de su herramienta pero NO dice lo que la separa de la otra: reescribila diciéndolo, con los mismos huecos y el mismo pedido.
  {"id": "...", "decision": "reescribir", "plantilla": "<la nueva>", "args": {...}}
- No se puede saber cuál quiere, o es casi igual a otra frase que ya está (en su herramienta o en la otra): sacala. Si dos frases son iguales, se saca UNA sola: la otra queda.
  {"id": "...", "decision": "sacar", "motivo": "<corto>"}

Cómo decidir:
- Leé la frase como la diría un operador de cabina que no ve la pantalla. Lo que manda es lo que pide, no una palabra suelta.
- Las descripciones dicen qué separa a cada herramienta (una o todas, público o invitados, presencial o remoto, ver o cambiar, preview o aire, crear o poner). Si la frase lo nombra: «queda». Si pide lo de la otra: «mover». Si es de la suya pero no lo nombra: «reescribir» agregando eso. Si las dos servirían y no hay cómo decidir: «sacar».
- Al reescribir: castellano rioplatense de cabina, corto, con un verbo y un objeto de un solo significado (el que corresponde a la herramienta). No cambies el pedido ni inventes datos; los huecos `{...}` que había, siguen. No copies una frase que ya está en `frases`.
- Si la herramienta suya es `derivar` (pedidos que NO son de este modelo y van al asistente grande): «queda» si de verdad no es de la otra (aunque se le parezca: ese parecido es a propósito); «mover» solo si pide exactamente lo que hace la otra.
- Preferí «queda» o «reescribir» antes que «sacar»: sacar solo si es ambigua o repetida.

Una línea JSON por caso, nada más en el archivo. No dejes ningún caso sin línea.

Si el caso trae `juez`: un juez que NO sabía a qué herramienta estaba asignada la frase eligió `juez` entre las dos (o "NINGUNA": no pudo decidir). Si eligió la otra, lo más probable es «mover». Si dijo NINGUNA, «reescribir» diciendo lo que la separa, o «sacar» si de verdad es ambigua. «queda» solo si estás seguro de que el juez se equivocó.
