## Como esta armado
El texto lo muestra una pagina web: `/prompter/` es la pantalla del orador (solo lee) y `/prompter/#control` el control del operador (lleva clave). El estado —que texto, por donde va, a que velocidad— vive en el gateway, no en la tablet: si algo se reinicia, el discurso sigue donde estaba.

Los discursos son archivos de texto en `Discursos/`, DENTRO de la carpeta del evento abierto, con subcarpetas si hace falta (`Apertura/intendente.txt`). Son los MISMOS que carga el cue `teleprompter` del guion: no hay una biblioteca aparte.

## Para que te van a llamar
Casi siempre para lo mismo: **"este discurso dura nueve minutos y tengo seis"**. El camino es `prompter_leer`, contar, reescribir y `prompter_escribir`. A 140 palabras por minuto (una lectura normal en castellano), seis minutos son unas 840 palabras. `prompter_documentos` te dice de cada uno cuanto dura sin abrirlo.

## Como se escribe un texto que se va a leer en voz alta
- **Un renglon vacio separa parrafos**, y el control puede saltar de uno a otro: conviene que cada parrafo sea una idea, asi el operador puede saltear uno si hay que acortar en vivo.
- **Nada de markdown**: ni asteriscos, ni almohadillas, ni guiones de vinneta. En la tablet se ve tal cual esta escrito.
- Frases cortas. Lo que se lee bien en un papel no siempre se dice bien.
- Numeros redondos y escritos como se pronuncian ("tres mil quinientos", no "3500").

## Mientras alguien lo lee
`prompter_control` arranca y para el texto, cambia la velocidad, salta y le escribe avisos al orador ("Redondea"). **Del otro lado hay una persona leyendo en voz alta**: arrancar o parar el texto sin que te lo pidan se nota al aire al instante. Si el orador va mas lento que el texto, bajale la velocidad de a 10 ppm en vez de pararlo.

`prompter_estado` dice tambien que pantallas hay conectadas: antes de que alguien suba al atril, lo que importa es que haya al menos una vista `orador`. Si no hay ninguna, la tablet esta apagada o nadie abrio el enlace, y eso se avisa AHORA, no cuando el orador este parado ahi.