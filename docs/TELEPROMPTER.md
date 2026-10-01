# El teleprompter

Tres pantallas para lo mismo, cada una con su trabajo:

| dónde | quién | qué hace |
|---|---|---|
| **página Teleprompter** (app) | el operador, antes y durante | escribir los discursos, mandarlos, manejar el texto |
| `/prompter/` | el orador, en la tablet del atril | leer. Nada más |
| `/prompter/#control` | quien maneja, desde el celular | arrancar, velocidad, avisos, el botón de pánico |

El teleprompter ya funcionaba: el estado y las órdenes viven en el gateway
(`src/gateway/Prompter.hpp`), las dos vistas son páginas web y el cue
`teleprompter` del guion carga un texto. Lo que faltaba era la cabina: dónde
escribir los discursos, cómo saber si la tablet del atril está prendida, y de
dónde sacar el enlace sin ir a buscarlo. Eso es la página.

## Los documentos viven en el evento

`Eventos/<evento>/Discursos/`, con subcarpetas (`Apertura/intendente.txt`).
Son **los mismos archivos** que lee el cue `teleprompter`. Podría haber una
biblioteca aparte, pero entonces habría dos carpetas parecidas y el cue leería
de una mientras el operador escribe en la otra. Sin evento abierto, la página
lo dice y no inventa dónde guardar.

`src/app/ui/Discursos.{hpp,cpp}` es esa carpeta y nada más, con las tres cosas
que conviene tener juntas y probadas (`test_discursos`):

1. **Nada sale de la raíz.** Las rutas relativas vienen del guion, que pudo
   escribirlo otro en otra máquina; un `..` no puede hacer que el programa lea
   o borre cualquier archivo del disco.
2. **Los nombres sirven en Windows y en Linux.** Un punto al final lo tira
   Windows en silencio y el archivo queda con otro nombre.
3. **La cuenta de palabras es la del gateway.** De ella sale la duración
   estimada; si las dos cuentas no coinciden, lo que dice la pantalla no es lo
   que después se ve en el atril.

## El estado se consulta, no se guarda

El dueño es el gateway. Si la app se reinicia en medio de un discurso, el
texto sigue corriendo en la tablet y la app al volver lo ve andando. Por eso
el ViewModel no guarda copia: pregunta cada 500 ms **mientras la página está a
la vista** y para al salir (`mirar`), porque preguntar cuando nadie mira es
gastar.

La barra de avance suma sola lo que corrió desde la última respuesta: si
mostrara la posición tal como vino, iría a tirones de medio segundo. Es la
misma cuenta que hacen la tablet y el control.

## Quién está conectado

No hace falta un canal aparte: las vistas piden el estado cada medio segundo,
así que se anota **quién pidió, cuándo y desde dónde**. Un aparato «está» si
pidió hace menos de 4 segundos (`Prompter::kVivoMs`), que aguanta un par de
pedidos perdidos sin que la lista parpadee.

La página manda su `id` (guardado en el navegador, así recargar no aparece
como un aparato nuevo), su `vista` (`orador` o `control`) y un `nombre` que
escribe quien la usa («Atril 1»). La tabla tiene tope: alguien probando URLs no
la hace crecer sin fin.

Lo que hay que mirar antes de que alguien suba al atril es que haya al menos
una vista `orador`. Si no hay ninguna, la tablet está apagada o nadie abrió el
enlace — y eso conviene saberlo ahí, no con el orador parado.

## Los QR y la clave

Dos enlaces distintos, con dos riesgos distintos:

- **El del atril** no lleva clave nunca: esa vista solo lee, y lo que muestra
  es un texto que alguien va a decir en voz alta en un rato.
- **El del control** sí. Frenarle el texto a alguien en medio de un discurso
  desde cualquier teléfono de la red sería un chiste caro. La clave la genera
  el gateway al arrancar y **no se puede cambiar desde la app**: cambiarla
  dejaría afuera al control que ya está en la mano de alguien. Se muestra para
  poder dictarla.

La clave viaja en el **fragmento** del enlace (`#control&k=…`), que no llega al
servidor ni queda en ningún registro. El QR puede ir sin ella, para el caso en
que quede pegado en una pared: entonces la página la pide y el operador la
dicta.

## Por MCP

Categoría `prompter` (`help {"topic": "prompter"}`): `prompter_estado`,
`prompter_documentos`, `prompter_leer`, `prompter_escribir`, `prompter_cargar`
y `prompter_control`. Pasan por el mismo ViewModel que la pantalla, así que lo
que cambia el asistente se ve en el acto.

Para lo que sirve de verdad: **«este discurso dura nueve minutos y tengo
seis»**. Leer, medir, reescribir y guardar es justo el trabajo que conviene
delegar, y `prompter_documentos` dice cuánto dura cada uno sin abrirlo.

## Lo que falta

- **Pantallas del escenario y Chromecast** (sección 4 de `EVENTOS.md`): el cue
  `pantalla` está previsto y no hace nada todavía. Para baja latencia habría
  que servir un receptor propio en el gateway, que necesita registrarlo en la
  consola de Google Cast y HTTPS con certificado de verdad.
- Que la página avise cuando un discurso del guion cambió después de cargarlo.
