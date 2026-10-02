Tu trabajo: arreglar lo que el modelo de tool calling hace mal, corrigiendo o ampliando las anotaciones con que se entrena.

El archivo `lote.txt` (JSON) tiene, por herramienta: su `descripcion`, su `esquema`, sus `anotaciones` actuales (plantillas con huecos y los argumentos que corresponden) y los `errores` del modelo: casos rendidos con un estado del aparato, con lo `esperado` y lo `obtenido`.

Para cada error decidí:

A. **La anotación estaba mal** (lo esperado no es lo que pide la frase, o el argumento no corresponde): escribí la anotación corregida con "corrige": "<la plantilla original>".

B. **La anotación estaba bien y el modelo se equivocó**: escribí 3 anotaciones NUEVAS para esa herramienta que enseñen justo lo que falló (otra forma de decir lo mismo, el mismo tipo de argumento, el mismo cambio relativo/exacto, la misma entidad), sin copiar la frase del error. Marcalas con "nueva": true.

Cada línea de `salida.jsonl`:

{"herramienta": "audio_set", "plantilla": "subile un toque a {audio}", "args": {"name": "{audio}", "volumeDb": "+3"}, "nueva": true}
{"herramienta": "audio_set", "plantilla": "<la corregida>", "args": {...}, "corrige": "<la plantilla original>"}

Las mismas reglas que al anotar:
- Entidades como huecos `{escena}`, `{audio}`, `{fuente}`, `{persona}`, `{grafico}`, `{carrusel}` (numerados si hay dos del mismo tipo); el mismo hueco en `args`. Audio sin nombrar = `{audio_operador}` solo en args. Contexto del estado: "[escena_armada]" / "[escena_actual]".
- Solo los argumentos que el pedido cambia. Cambios con signo como texto ("-4", "+3"; «un poco» = 3, sin cantidad = 6); valor exacto con «=» ("=-10"). Booleanos true/false. Enum exacto del esquema.
- Si falta un dato obligatorio: "falta": "<la pregunta>" en vez de args.
- Frases como las dice un operador argentino apurado: voseo, cortas, naturales con cualquier nombre en el hueco.

Si un error es del modelo y no ves cómo enseñarlo mejor, igual escribí las 3 nuevas. Una línea JSON por anotación, nada más.

STE-ES (docs/ste-es.md: el alma de ASD-STE100 en castellano de cabina). El NÚCLEO de la frase (acción, objeto, cantidad) con palabras de un solo significado; la superficie (voseo, muletillas, sin tildes, algún typo) libre:
1. Una palabra, un significado: cada verbo de acción con su sentido, no otro. 2. Las palabras de muchos sentidos («poné», «sacá», «pasá», «mandá», «tirá», «dale», «cambiá») solo con el objeto que fija cuál («poné el zócalo», no «ponelo»). 3. Un pedido, una acción. 4. Objeto explícito (si falta, es falta_dato). 5. Cantidad explícita o con las convenciones. 6. Un término por cosa. 7. Orden en imperativo (voseo) o consulta con su palabra de pregunta; un síntoma («se acopla el mic de Laura») vale si nombra el objeto y el problema. 8. De 3 a 16 palabras.
Lo que escribas pasa por un verificador de STE-ES además de la matemática: si rompe una regla, vuelve con el motivo.
