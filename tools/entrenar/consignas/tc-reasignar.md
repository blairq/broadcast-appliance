Tu trabajo: revisar las frases de entrenamiento de un GRUPO de herramientas que el modelo confunde entre sí, y dejar cada frase en la herramienta que de verdad hace lo que pide.

El archivo `lote.txt` (JSON) trae `herramientas`: cada una con su nombre `real`, el `nombre` con que la ve el modelo, su `descripcion` (lo que la separa de las otras del grupo) y su `esquema`; y `anotaciones`: cada frase con su `id`, la herramienta (`real`) a la que está asignada hoy, su `plantilla` (con huecos como `{camara}`, `{audio}`, `{banca}`) y sus `args` (o `falta` si pregunta un dato que falta).

Para CADA anotación escribí UNA línea en `salida.jsonl`:

- Está bien donde está:
  {"id": "chat_history#3", "decision": "queda"}
- Pide lo que hace OTRA herramienta del grupo: movela, con los argumentos de ESA herramienta (su esquema; los mismos huecos que la plantilla):
  {"id": "chat_history#5", "decision": "mover", "a": "chat_recientes", "plantilla": "<la misma plantilla>", "args": {}}
- No se puede saber cuál quiere (las dos serían razonables y la frase no dice nada que las separe): sacala.
  {"id": "bancas_listar#2", "decision": "sacar", "motivo": "«¿quién se anotó?» vale para el recinto y para los remotos"}

Cómo decidir:
- Leé la frase como la diría un operador de cabina que NO ve la pantalla. Lo que manda es lo que pide, no la palabra suelta: «identificá a la persona de la cámara 3» es identificar a alguien, no ubicar caras en el cuadro.
- Las descripciones dicen qué separa a cada herramienta de sus vecinas (público o invitados, presencial o remoto, una o todas, guardada o de fábrica, crear o poner un archivo). Si la frase nombra eso, va a la que corresponde; si no lo nombra y las dos sirven, es «sacar».
- «mover» solo dentro del grupo. `a` es el nombre REAL. Los args tienen que valer para el esquema de destino; si para moverla habría que inventar un dato que la frase no dice, usá "falta": "<la pregunta corta>" en vez de args.
- No reescribas la frase para que entre: si no se entiende, «sacar». No dejes ninguna anotación sin línea.

Una línea JSON por anotación, nada más en el archivo.
