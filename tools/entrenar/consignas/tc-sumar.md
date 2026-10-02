Tu trabajo: sumar anotaciones NUEVAS al corpus de tool calling, para corregir lo que las pruebas del modelo en el aparato mostraron que falla. El archivo `lote.txt` (JSON) tiene herramientas con su `descripcion`, su `esquema`, si es `solo_lectura`, sus `anotaciones` actuales (plantillas con huecos) y `vecinas` (herramientas que tocan lo mismo).

Por cada herramienta escribí en `salida.jsonl` entre 4 y 8 anotaciones nuevas, de estos tres tipos, las que correspondan:

1. **Pregunta contra orden.** En vivo, «¿cuánto está la música?» NO puede tocar el volumen. Si la herramienta es `solo_lectura`, escribí PREGUNTAS que la piden («¿cuánto está {audio}?», «¿está silenciado {audio}?», «¿hay alguien en la sala?»), con su palabra de pregunta. Si NO es de solo lectura y tiene una vecina de lectura, escribí ÓRDENES que se parezcan a esas preguntas pero pidan el cambio («poné {audio} en menos diez»). El contraste entre las dos es lo que tiene que aprender.
2. **Síntomas.** El operador apurado describe el problema en vez de dar la orden: «se acopla el mic de {persona}», «no se escucha {audio}», «se ve todo negro en {fuente}». Si un síntoma lleva claramente a ESTA herramienta, escribilo (nombrando el objeto y el problema).
3. **Lo que NO es suyo.** Una o dos frases que suenan parecidas pero son de otra cosa que el aparato no hace (mandar un mail, buscar en internet): `"herramienta": "derivar"`, `"args": {}`.

Formato de cada línea (igual que al anotar):
{"herramienta": "mixer_list", "frase": "¿cuánto está la música?", "plantilla": "¿cuánto está {audio}?", "args": {}, "tipo_nueva": "pregunta"}
{"herramienta": "audio_set", "frase": "...", "plantilla": "subile un poco a {audio}", "args": {"name": "{audio}", "volumeDb": "+3"}, "tipo_nueva": "orden"}

Reglas de las anotaciones: huecos `{escena}`, `{audio}`, `{fuente}`, `{persona}`, `{grafico}`, `{carrusel}` (el mismo hueco en args); audio sin nombrar = `{audio_operador}` solo en args; solo los argumentos que el pedido cambia; cambios con signo («un poco» = 3, sin cantidad = 6), exacto con «=» («en X», «a X», «dejala en X» = exacto); si falta un dato obligatorio, "falta": "<la pregunta>" en vez de args. Frases de operador argentino apurado, voseo, de 3 a 16 palabras, sin copiar las anotaciones que ya tiene.
