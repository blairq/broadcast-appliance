Tu trabajo: anotar las PRIMERAS frases de entrenamiento de herramientas nuevas del aparato (todavía no tienen ninguna). El archivo `lote.txt` (JSON) trae cada herramienta con su `descripcion` y su `esquema`.

Por cada herramienta escribí entre 16 y 20 anotaciones en `salida.jsonl`, como las diría un operador de cabina argentino apurado (voseo, de 3 a 16 palabras, variadas: orden seca, pedido con contexto, pregunta si es de lectura, síntoma si corresponde). Una línea por anotación:

{"herramienta": "ptz_ir_posicion", "frase": "apuntá la dos a Laura", "plantilla": "apuntá {camara} a {posicion}", "args": {"entrada": "{camara}", "posicion": "{posicion}"}}

HUECOS (el programa los llena con un estado del aparato distinto cada vez; en `args` va el mismo hueco):
- `{camara}`: una cámara (entrada de video con PTZ). `{posicion}`: una posición guardada DE ESA cámara («el atril», «Laura», «la panorámica»); siempre junto con su `{camara}`.
- `{discurso}`: un discurso del teleprompter (va su ruta). `{medio}`: un archivo de medios (música, video; va su ruta). `{pregunta}`: una pregunta del público en la cola (va su id). `{pantalla}`: un televisor o pantalla de la red. `{receta}`: una receta (secuencia guardada; va su id). Y los de siempre: `{escena}`, `{carrusel}`.
- Si la frase no dice cuál y el argumento es obligatorio: "falta": "<la pregunta corta>" en vez de args (2 o 3 por herramienta como mucho).

REGLAS: solo los argumentos que el pedido pide; enum exacto del esquema; números de movimiento con signo (pan + derecha, tilt + arriba) y los demás como el esquema; booleanos true/false. Para `ptz_preset` el `numero` es entero y `accion` "ir" (casi siempre) o "guardar". Para `evento_correr` la `accion` del enum («siguiente», «atras», «pausar»...). Para `pregunta_moderar` la `accion` y el `{pregunta}`. Para `pantalla_mostrar` `tipo` del enum y lo que corresponda (qr, texto, url…).

STE-ES (docs/ste-es.md): el núcleo de la frase (acción, objeto, cantidad) con palabras de un solo significado; las de muchos sentidos («poné», «sacá», «pasá», «llevá», «mandá») solo con el objeto que fija cuál; un pedido, una acción. JSON válido, nada más en el archivo.
