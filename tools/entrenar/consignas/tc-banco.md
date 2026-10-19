Sos {ESTILO}

Estás en la cabina de un aparato que transmite eventos en vivo (sesiones de un concejo, actos, programas). Hay un asistente al que le hablás para que haga cosas en el aparato. No sabés cómo está programado: le hablás como hablás vos.

El archivo `lote.txt` (JSON) trae:
- `propias`: herramientas del asistente (`nombre` y `descripcion`). Para CADA una escribí {POR_HERRAMIENTA} pedidos que harías vos para que el asistente la use.
- `ajenas`: cosas que el aparato hace pero que este asistente NO hace (las hace otro). Escribí {AJENAS} pedidos que le harías y que van a esas, o a nada de la lista: preguntas generales, cosas fuera del aparato, configuraciones, pedidos raros de cabina. Esos se marcan `derivar`.

Una línea JSON por pedido en `salida.jsonl`:
  {"frase": "<lo que decís>", "herramienta": "<el nombre de la propia>"}
  {"frase": "<lo que decís>", "herramienta": "derivar"}

Cómo escribirlos:
- Como hablás de verdad en una transmisión: rioplatense, con apuro o con calma, a veces con nombres concretos (el micro de Juan, la cámara 2, la escena Mesa), a veces con el contexto («el que está hablando», «esa», «la de antes»), a veces preguntando, a veces dando la orden de costado («¿podés...?», «necesito que...», «fijate si...»).
- Que los {POR_HERRAMIENTA} de una misma herramienta sean DISTINTOS entre sí: otras palabras, otra forma, otro motivo. No cambies dos palabras de la anterior.
- No copies la descripción: decilo como lo pedirías sin haberla leído.
- Que cada pedido sea de UNA sola herramienta y que se entienda cuál. Si un pedido podría ser de dos, cambialo hasta que sea de una.
- Los de `derivar`: que se parezcan a lo que se le pide a este asistente (es lo que lo confunde), pero que sean de otra cosa.
