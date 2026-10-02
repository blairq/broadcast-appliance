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
