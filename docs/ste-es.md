# STE-ES de cabina: español técnico simplificado para el corpus

Las frases con que se entrena el modelo local las escribe y reescribe una LLM
(los agentes del bucle). ASD-STE100 (Simplified Technical English, el lenguaje
controlado de los manuales aeronáuticos) existe para que un texto técnico tenga
**un solo sentido posible**. Acá adoptamos su **alma**, no su forma, y en
castellano: el modelo tiene que entender a un operador real, rioplatense y
apurado, así que el registro queda libre; lo que se controla es el **sentido**.

> **El núcleo controlado, la superficie libre.** Cada frase tiene un núcleo
> (qué acción, sobre qué, cuánto) escrito con palabras de un solo significado;
> alrededor, el operador habla como habla: voseo, muletillas, sin tildes, algún
> typo. La variedad vive en la superficie y nunca cambia el núcleo.

## Por qué (lo que midió el corpus)

En las 1.370 semillas, **el 64 % de las frases empieza con una palabra que
comparten cuatro o más herramientas**: «poné» (24 herramientas), «pasá» (25),
«sacá» (20), «mostrá» (17), «mandá» (14), «cambiá» (11). Esas palabras no
dicen qué hacer: lo dice el objeto. Es justo donde los grupos se pisan en la
geometría (CDNV direccional, Frobenius: [bucle-corpus.md](bucle-corpus.md)): dos
herramientas que comparten el verbo solo se separan si el objeto las separa.
STE-ES ataca la causa en el texto; la matemática mide el efecto.

## Las reglas (el alma de STE100, adaptada)

1. **Una palabra, un significado.** Cada verbo de acción del diccionario
   (`datos/ste-es.json`) tiene UN significado y apunta a una familia de
   herramientas: «bajá/subí» = volumen; «silenciá/muteá» = silencio; «cortá a /
   pasá a [escena]» = escena al aire; «prepará/armá» = previa. Un verbo nunca se
   usa con otro sentido que el suyo.
2. **Las palabras de muchos sentidos llevan el objeto que fija cuál.**
   «Poné», «sacá», «pasá», «mandá», «tirá», «dale», «cambiá» se pueden usar
   (son las del oficio), pero siempre con el objeto que elige el sentido:
   «poné **el zócalo**», «sacá **a Laura** de la sala», «mandá **al chat**».
   Sin objeto que lo fije, no es una frase de entrenamiento válida.
3. **Un pedido, una acción.** Una frase pide una sola cosa. Dos acciones son dos
   frases (o `derivar`, si hace falta encadenarlas).
4. **El objeto siempre explícito.** Sobre qué escena, fuente, persona o gráfico.
   Si el operador no lo dice, la respuesta correcta es `falta_dato` (salvo el
   micrófono del operador, que es el predeterminado del audio).
5. **La cantidad explícita y en su unidad**, o con las convenciones del
   aparato: «un poco» = 3 dB, sin cantidad = 6 dB, «a cero» = exacto.
6. **Un término por cosa.** Cada concepto del aparato tiene un término canónico
   y una lista cerrada de sinónimos del oficio («zócalo» / «rótulo»; «escena» /
   «plano»): no se inventan otros.
7. **Orden o consulta, inequívoco.** Las órdenes van en imperativo (voseo) y en
   voz activa; las consultas, con su palabra de pregunta («¿cuántas…?», «¿qué…?»,
   «¿cómo está…?»). Una consulta nunca suena a orden ni al revés.
8. **Corta.** De 3 a 16 palabras: lo que se dice en cabina.
9. **La superficie no toca el núcleo.** Muletillas («che», «dale», «ya»), sin
   tildes, algún typo, mayúsculas: sí. Cambiar el verbo de acción, el objeto o
   la cantidad: eso es otra frase.

## Cómo se usa en el bucle

- `datos/ste-es.json`: el diccionario (acciones con un significado → sus
  herramientas; palabras de muchos sentidos → los objetos que las fijan;
  términos canónicos y sus sinónimos). Lo armó un agente desde las fichas reales
  de las herramientas y lo que dicen las semillas; se cura como las semillas.
- `tools/entrenar/ste_es.py`: el verificador. Dada una frase y su herramienta,
  devuelve qué regla rompe y por qué. `--informe` lo corre sobre todas las
  semillas: cumplimiento por herramienta y dónde están los choques de verbos.
- Las consignas de los agentes llevan las reglas, y lo que escriben pasa por el
  verificador además de la matemática: si una frase rompe STE-ES, vuelve al
  agente con el motivo, igual que cuando empeora la separación.

## Lo que no es

- No es reescribir las frases en un castellano rígido: el modelo tiene que
  entender al operador real. Es controlar qué significa cada frase.
- No copia el diccionario de ASD-STE100 (es inglés y es de ASD): toma sus
  principios y arma el propio, del oficio y del aparato.
