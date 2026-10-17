Tu trabajo: completar el diccionario de STE-ES de cabina (español técnico simplificado: el alma de ASD-STE100, «una palabra, un significado», con el registro rioplatense de cabina libre alrededor del núcleo).

El archivo `lote.txt` (JSON) tiene: `diccionario` (lo que ya hay: acciones con un significado, palabras de muchos sentidos con los objetos que fijan cada sentido), `herramientas` (nombre y descripción) y `casos`: frases de entrenamiento que el verificador no pudo leer, con su herramienta y el problema.

Para cada caso decidí UNA de tres cosas y escribí una línea JSON en `salida.jsonl`:

1. Falta un verbo de acción (uno del oficio, con UN significado que lleva a esa herramienta):
{"tipo": "accion", "verbo": "verificar", "formas": ["verificá", "verifica", "verificalo"], "significado": "…", "herramientas": ["audio_analyze"]}
   Si el verbo ya está en el diccionario con OTRO significado, no lo dupliques: es una palabra de muchos sentidos (2) o la frase está mal (3).

2. Falta un sentido de una palabra de muchos sentidos (existente o nueva):
{"tipo": "sentido", "palabra": "sacar", "formas": ["sacá", "sacame"], "objeto": "una imagen o captura", "ejemplo": "sacame una imagen de la cámara 2", "herramientas": ["foto_fuente"]}

3. La frase usa una palabra en un sentido que no es el suyo, o no tiene núcleo (orden sin verbo), o pide dos cosas: no se arregla el diccionario, se marca la frase:
{"tipo": "frase_mal", "herramienta": "audio_analyze", "frase": "<la frase>", "regla": "R1|R2|R3|R7", "motivo": "…", "propuesta": "<la frase reescrita en STE-ES, con el mismo pedido>"}

Reglas de STE-ES: 1 una palabra, un significado · 2 las de muchos sentidos llevan el objeto que fija cuál · 3 un pedido, una acción · 4 objeto explícito · 5 cantidad explícita · 6 un término por cosa · 7 orden en imperativo (voseo) o consulta con su palabra de pregunta · 8 de 3 a 16 palabras · 9 la superficie (muletillas, sin tildes, typos) no toca el núcleo.

Preferí ampliar el diccionario cuando el verbo es del oficio y tiene un solo sentido claro; marcá la frase cuando de verdad es ambigua para alguien que no ve la pantalla. Usá solo herramientas de la lista. Una línea JSON por caso, nada más.
