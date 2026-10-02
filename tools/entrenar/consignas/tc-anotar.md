Tu trabajo: anotar frases del operador con la LLAMADA EXACTA que tiene que hacer el modelo (herramienta y argumentos), para entrenarlo en el formato de tool calling.

El archivo `lote.txt` (JSON) tiene una lista de herramientas, cada una con su `descripcion`, su `esquema` (los argumentos que acepta) y sus `frases` (lo que dice el operador para pedirla).

Para CADA frase escribí una línea JSON en `salida.jsonl`:

{"herramienta": "audio_set", "frase": "<la frase original>", "plantilla": "bajale 4 dB a {audio}", "args": {"name": "{audio}", "volumeDb": "-4"}}

REGLAS

1. **Entidades del aparato como huecos.** Si la frase nombra una escena, una fuente de audio, una fuente de video, una persona, un gráfico o un carrusel, reemplazá esa mención (con su artículo) por el hueco del tipo: `{escena}`, `{audio}`, `{fuente}`, `{persona}`, `{grafico}`, `{carrusel}`. En `args` poné el MISMO hueco donde va ese nombre. Si hay dos del mismo tipo, numeralos: `{escena}` y `{escena2}`. El programa después los llena con nombres de un estado del aparato distinto cada vez, así que la plantilla tiene que seguir siendo una frase natural con cualquier nombre (no dejes palabras que solo cierran con el nombre original).
   - Ejemplo: «pasá a la del cierre» → plantilla «pasá a {escena}», args {"name": "{escena}"}.
   - Si es audio y NO nombra la fuente, es el micrófono del operador: args {"name": "{audio_operador}"} (sin hueco en la plantilla).
   - Si la entidad depende del contexto («la que sigue», «la armada», «esta escena», «la que está al aire»), no inventes: poné el valor que el estado define, como texto literal entre corchetes: "[escena_armada]" o "[escena_actual]".
2. **Solo los argumentos que el pedido cambia.** Silenciar no cambia el volumen; cambiar el volumen no silencia. No pongas argumentos opcionales que la frase no pide.
3. **Números que suben o bajan: SIN cuentas, con signo, como texto**: "-4", "+3". «Un poco» = 3; sin cantidad = 6; si dice cuánto, eso. **Valor exacto** («ponelo en -10», «dejalo en cero»): con «=» adelante: "=-10", "=0". Números que no son cambios (segundos, cantidades): el número.
4. **Booleanos**: true / false. **Enum**: uno de los valores del esquema, exacto.
5. **Texto libre** que la frase dicta (un mensaje para el chat, el texto de un rótulo, un nombre nuevo para crear): copialo tal cual lo dijo, sin hueco.
6. **Falta un dato obligatorio**: si la frase no alcanza para un argumento requerido (y no es el micrófono del operador por defecto), en vez de `args` poné "falta": "<la pregunta corta que haría el asistente>", por ejemplo {"herramienta": "scene_switch", "frase": "cambiá de escena", "plantilla": "cambiá de escena", "falta": "¿A qué escena?"}.
7. **Además**, por cada herramienta que tenga argumentos obligatorios, escribí 2 frases NUEVAS que la pidan sin el dato obligatorio (como las diría el operador), con su "falta". Marcalas con "nueva": true.
8. Si una frase no corresponde a la herramienta (está mal en el corpus), igual anotala lo mejor posible y agregá "duda": "<por qué>".

No cambies la herramienta. No uses nombres de argumentos que no estén en el esquema. Una línea por frase, JSON válido, nada más en el archivo.

STE-ES (docs/ste-es.md: el alma de ASD-STE100 en castellano de cabina). El NÚCLEO de la frase (acción, objeto, cantidad) con palabras de un solo significado; la superficie (voseo, muletillas, sin tildes, algún typo) libre:
1. Una palabra, un significado: cada verbo de acción con su sentido, no otro. 2. Las palabras de muchos sentidos («poné», «sacá», «pasá», «mandá», «tirá», «dale», «cambiá») solo con el objeto que fija cuál («poné el zócalo», no «ponelo»). 3. Un pedido, una acción. 4. Objeto explícito (si falta, es falta_dato). 5. Cantidad explícita o con las convenciones. 6. Un término por cosa. 7. Orden en imperativo (voseo) o consulta con su palabra de pregunta; un síntoma («se acopla el mic de Laura») vale si nombra el objeto y el problema. 8. De 3 a 16 palabras.
Lo que escribas pasa por un verificador de STE-ES además de la matemática: si rompe una regla, vuelve con el motivo.
