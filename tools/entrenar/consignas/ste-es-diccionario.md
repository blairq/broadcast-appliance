Tu trabajo: armar el diccionario de STE-ES de cabina, el español técnico simplificado con que se escriben las frases de entrenamiento de un modelo que maneja un aparato de TV en vivo. Leé primero las reglas (abajo). El alma es la de ASD-STE100: **una palabra, un significado**; el núcleo de cada frase (acción, objeto, cantidad) se escribe con palabras de un solo sentido, y el registro rioplatense de cabina queda libre alrededor.

El archivo `lote.txt` (JSON) tiene: las herramientas del aparato (`nombre`, `descripcion`) y, por herramienta, las palabras con que arrancan sus frases semilla y cuántas veces; y la lista de palabras que comparten muchas herramientas (los choques).

Escribí en `salida.jsonl` una línea JSON por entrada, de estos tipos:

1. Acción (un verbo con UN significado):
{"tipo": "accion", "verbo": "bajar", "formas": ["bajá", "bajale", "baja", "bajalo"], "significado": "reducir el volumen de una fuente de audio", "herramientas": ["audio_set"]}
   - Un verbo de acción aparece UNA sola vez, con un solo significado. Si en el oficio un verbo se usa para dos cosas distintas, NO es una acción: es una palabra de muchos sentidos (tipo 2).
   - Cubrí todas las herramientas: cada una tiene que tener al menos un verbo de acción propio o una palabra de muchos sentidos con un objeto que la elija.

2. Palabra de muchos sentidos (se puede usar, pero el objeto fija el sentido):
{"tipo": "polisemica", "palabra": "poner", "formas": ["poné", "ponele", "pone", "ponelo"], "sentidos": [{"objeto": "escena (al aire)", "ejemplo": "poné la del cierre al aire", "herramientas": ["scene_switch"]}, {"objeto": "gráfico o zócalo", "ejemplo": "poné el zócalo", "herramientas": ["grafico_mostrar"]}]}
   - Incluí TODAS las palabras de la lista de choques que sean verbos u órdenes (poner, sacar, pasar, mandar, tirar, dar/dale, cambiar, mostrar, dejar…).

3. Consulta (cómo se pregunta algo, para que nunca suene a orden):
{"tipo": "consulta", "forma": "¿cuántas … hay?", "significado": "contar elementos", "herramientas": ["scene_list", "escena_fuentes_ver"]}

4. Término canónico (un término por cosa del aparato, con sinónimos del oficio cerrados):
{"tipo": "termino", "concepto": "texto sobreimpreso con nombre o cargo", "canonico": "zócalo", "sinonimos": ["rótulo", "graph", "sobreimpreso"]}

5. Muletilla (superficie que nunca cambia el núcleo):
{"tipo": "muletilla", "formas": ["che", "dale", "ya", "rápido", "porfa", "a ver"]}

REGLAS DE STE-ES (las que el diccionario tiene que hacer posibles):
1. Una palabra, un significado. 2. Las de muchos sentidos llevan el objeto que fija cuál. 3. Un pedido, una acción. 4. El objeto siempre explícito (si falta, se pregunta). 5. La cantidad explícita o con las convenciones (un poco = 3 dB; sin cantidad = 6 dB). 6. Un término por cosa, sinónimos cerrados. 7. Orden (imperativo, voseo, voz activa) o consulta (con su palabra de pregunta), nunca ambiguo. 8. De 3 a 16 palabras. 9. La superficie (muletillas, sin tildes, typos) no toca el núcleo.

Formas en voseo rioplatense (bajá, subí, poné, sacá) y sus variantes sin tilde y con pronombre (bajale, ponelo). Usá SOLO herramientas de la lista. JSON válido, una línea por entrada, nada más.
