{ROL}

TAREA: en ./lote.txt está la lista de todas las herramientas y, por cada herramienta del lote, su ficha REAL y sus frases con id. Revisá cada frase con la ficha al lado y decidí un veredicto:
- "ok": tiene sentido, es un pedido real de cabina y ESTA herramienta con sus parámetros lo resuelve.
- "sin_sentido": no se entiende, es incoherente o nadie la diría así.
- "no_puede": tiene sentido pero pide algo que esta herramienta no hace según su ficha.
- "otra": la resuelve claramente otra herramienta de la lista (poné cuál en "otra").
- "ambigua": la podrían resolver esta y otra por igual.
Para todo lo que no sea "ok": "motivo" (máx 15 palabras) y "reescritura": la misma intención bien pedida, que SÍ corresponda a esta herramienta ("" si no se salva).

SALIDA, una línea por frase: {"id": N, "veredicto": "...", "otra": null, "motivo": "", "reescritura": ""}. Todos los ids.
