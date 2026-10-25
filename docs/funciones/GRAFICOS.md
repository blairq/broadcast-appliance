# Cómo escribir un gráfico animado

Instrucciones para quien escriba un gráfico (persona o asistente). Cada regla
salió de un problema real armando el primero: si algo está acá es porque ya
nos hizo perder tiempo.

Un gráfico es una carpeta con dos archivos (y sus medios, si los tiene):

```
<nombre>/
├── grafico.qml     el dibujo y las animaciones (o la descripción, adentro de grafico.json)
├── grafico.json    qué parámetros acepta
└── medios/         GIF, imágenes y videos pasados a cuadros (ver «Medios»)
```

---

## Las cinco que más van a morder

**1. Para mover algo que pasa por un efecto, mové EL EFECTO, no el origen.**
`MultiEffect` dibuja su origen en **su propio** rectángulo: animar la posición
del item de origen no mueve nada en pantalla. El item de origen queda quieto
y escondido; quien se desliza es el `MultiEffect`. Escondelo con un
`ShaderEffectSource { sourceItem: contenido; hideSource: true; visible: false }`
como fuente del efecto, **no** con `visible: false` en el origen: un item
invisible no se «pule», y lo que se arma ahí (las formas `Shape`, o sea las
tortas) sale vacío. Así lo genera el nivel declarativo.

**2. Un item con anchors IGNORA `x` e `y`.** Si lo vas a deslizar, no lo
ancles: dale `width` y `height` a mano. Un `anchors.fill: parent` y una
animación sobre `x` conviven sin error y sin efecto.

**3. `QtGraphicalEffects` no existe acá.** Es de Qt 5 y es lo que usan casi
todos los ejemplos que andan dando vueltas. Acá va
`import QtQuick.Effects` con `MultiEffect` (desenfoque, sombra, máscara).
Tampoco está `Qt5Compat.GraphicalEffects`.

**4. Las animaciones tienen que arrancar solas.** El motor adelanta un reloj:
nadie aprieta play. Usá `running: true` o la forma
`NumberAnimation on <propiedad> { ... }`.

**5. No existe el tiempo real.** El reloj es virtual y lo maneja el motor. Un
`Timer`, un `Date.now()` o cualquier cosa atada al reloj de pared no va a
funcionar como esperás. Todo el movimiento va en animaciones.

---

## Los parámetros

- Van declarados como **propiedades del Item raíz**, y repetidos en
  `grafico.json` con título, tipo, valor por defecto y una ayuda.
- Un parámetro que el gráfico no declara es un **error**, no un silencio: casi
  siempre es un nombre mal escrito, y descubrirlo al aire es tarde.
- La ayuda tiene que decir lo que importa, no lo obvio: «menos de 250 ms se lee
  como un salto» sirve; «la duración de la entrada» no.

De `grafico.json` salen solos el formulario de la interfaz, los campos del cue
del guion y el esquema de la herramienta MCP. Por eso el gráfico **declara** lo
que necesita en vez de inventar cómo se le pasan las cosas.

---

## Los botones que ofrece (`acciones`)

Un gráfico puede traer, en su `grafico.json`, una lista de **pedidos ya
escritos**:

```json
"acciones": [
  { "icono": "◉", "titulo": "El que está hablando",
    "prompt": "Poné en este zócalo el nombre y el cargo del invitado que está al aire ahora…" }
]
```

Cada uno aparece como un botón en la pantalla de **Rótulos**, junto a la fuente
que muestra ese gráfico, y al apretarlo se le manda ese texto al asistente con
el contexto puesto (qué escena, qué fuente, qué gráfico). En vivo, eso es la
diferencia entre apretar un botón y tener que escribirle un párrafo.

- Se escriben **mientras se diseña el gráfico**, que es cuando se sabe para qué
  sirve cada parámetro. El editor lo pide ahí mismo («Proponer botones»).
- El `prompt` va redactado **como lo diría el operador**, no como una orden a
  una máquina: el que lo va a leer es el asistente.
- **No más de cuatro.** Una fila de botones que no entra en la tarjeta no la
  mira nadie.
- Un botón que necesita datos de otro lado (el guion, la sala, el chat) tiene
  que **decir de dónde sacarlos**: «mirá el guion y buscá el cue de orador que
  está corriendo» funciona; «poné al que habla» deja al asistente adivinando.

---

## La transparencia

- El fondo es transparente: el gráfico se compone **sobre el video**. No pintes
  un fondo opaco de lado a lado salvo que sea a propósito.
- Texto sobre transparencia: `renderType: Text.QtRendering`. Sin eso el
  suavizado es subpíxel y las letras salen con flecos de color en cuanto hay
  algo detrás.
- Probá el gráfico sobre una imagen clara y otra oscura. Un zócalo blanco se ve
  perfecto sobre gris y desaparece sobre una camisa.

---

## Los tiempos

- Entrada entre **250 y 800 ms**. Menos se lee como un salto; más se hace lento.
- La entrada y la salida son **animaciones separadas**, y la salida va atada a
  un parámetro (`salidaEn`, 0 = se queda). Un gráfico que tiene que quedarse no
  puede irse solo.
- El texto entra **escalonado** detrás del contenedor, no junto: primero la
  caja, después el nombre, después el cargo. Todo junto se lee como un golpe.

---

## Lo que cuesta

Gratis: mover, escalar, rotar, opacidad, recortar. Son transformaciones, las
hace la placa de video.

Cuesta, y proporcional al **área**: desenfoque y sombra. Un zócalo de 900×220 a
60 cuadros por segundo no lo siente nadie; un desenfoque a pantalla completa en
una placa integrada, sí.

Por eso cada gráfico **declara su tamaño** en `grafico.json` y se dibuja solo
ese rectángulo, no el lienzo entero.

---

## Lo que NO hay que hacer

- **No** animar propiedades que rehacen la distribución en cada cuadro (el
  ancho de un texto, por ejemplo). Animá transformaciones y opacidad.
- **No** guardar estado entre cuadros. Cada cuadro se dibuja **desde cero**: un
  contador que supone continuidad va a dar cualquier cosa.
- **No** usar shaders propios. Hace falta compilarlos con una herramienta que
  no viaja en el bundle. `MultiEffect` cubre casi todo.
- **No** cargar un `.svg` como imagen si lo vas a agrandar: se rasteriza una
  vez al tamaño que le digas y después se pixela. Para formas animables usá
  `Shape` con `PathSvg`, que acepta el atributo `d` de un SVG tal cual y sigue
  siendo vector.
- **No** tocar la red ni el disco desde un gráfico. Es un dibujo.
- **No** dar un gráfico por bueno mirando **un** cuadro. Una animación no se
  juzga quieta.

---

## Dos cosas del nivel declarativo que conviene saber

- Un **texto sin `ancho` crece en una sola línea** y se sale del gráfico. En
  cuanto el texto lo escribe otro —una pregunta del público, el tema de una
  charla— ponele `ancho` y corta por palabras.
- **`desplaza`** (píxeles por segundo) hace que un texto cruce la pantalla en
  bucle: es un ticker. Terminá el texto con un separador («  •  ») para que el
  reinicio no se lea como un corte, y **no le pongas `izquierda`**: la animación
  manda sobre la posición y una cosa pelea con la otra.

Los ejemplos que vienen de fábrica (`Graficos/`) cubren un caso de cada forma:
`zocalo` un texto fijo, `placa-titulo` una placa a pantalla completa,
`proximo-orador` una tarjeta que entra de costado, `ticker` el bucle, y
`pregunta` un texto largo que corta solo. Copiar el que más se parezca sale
mejor que empezar de cero.

---

## Las de asamblea (categoría «Asamblea»)

Vienen de fábrica seis, todas declarativas, con `"categoria": "Asamblea"` en su `grafico.json` (la galería las agrupa por ese campo; sin él, «General»):

| Gráfico | Para qué | Vínculo típico |
|---|---|---|
| `zocalo-integrante` | Nombre, cargo y bloque de quien habla desde su banca | `{orador.nombre}`, `{orador.cargo}`, `{orador.bloque}` |
| `punto-orden-del-dia` | «PUNTO N» y el asunto | `{punto.n}`, `{punto.titulo}` |
| `votacion` | Resultado OFICIAL de una votación, pantalla completa | `{votacion.asunto}`, `{votacion.afirmativos}`, `{votacion.negativos}`, `{votacion.abstenciones}`, `{votacion.resultado}`, `{votacion.color}` |
| `cuarto-intermedio` | Pausa de la sesión, con la vuelta | a mano, o «Volvemos a las …» con `{hora}` |
| `minuto-silencio` | Placa sobria, sin color | a mano |
| `quorum` | Etiqueta chica: con o sin quórum | `{quorum.estado}`, `{quorum.texto}` |

Los orígenes salen de lo ya validado: el orador del Cuerpo (nombre y cargo validados), el punto en tratamiento del Orden del día y la última votación cargada como oficial en Quórum (nunca un conteo estimado). `rotulo_vinculos` los lista con su valor de ahora.

**Armar en el preview.** Un gráfico que no está en la escena al aire se prepara ahí: en Rótulos, las fuentes de las otras escenas tienen «Armar en PVW» (lo dibuja y deja esa escena armada; sale con TAKE). Por MCP, `grafico_mostrar {…, "armar": true}`; si la fuente está en la escena al aire, se niega (armarlo se vería).

**Lo que todavía no hay.** HTML: el render sigue siendo de cuadros dibujados de antemano. Un vínculo se vuelve a disparar cuando cambia lo que resuelve; con `vivo: true` se redibuja sin volver a entrar (un reloj con segundos, o una votación con tortas o barras que se mueven: ver «Cambiar al aire»).

## Medios: GIF, imágenes y videos con alfa adentro de un gráfico

Un gráfico puede llevar un GIF animado, una imagen o un video con canal alfa
(WebM VP9 con alfa, ProRes 4444, una secuencia de PNG), con **su tiempo atado
al del gráfico**: el cuadro del medio sale del reloj del renderizador, así que
la vista previa, `grafico_probar` y el aire muestran lo mismo en el mismo
instante, y arrastrar la barra para atrás funciona.

- **Agregarlo**: «Agregar medio…» en Gráficos, o `grafico_medio_agregar`.
  Queda en `medios/` de la carpeta del gráfico (viaja con él). Un GIF o una
  imagen se copian; una carpeta de PNG se copia entera; **un video se pasa una
  vez a cuadros PNG con su alfa** (GStreamer: `vp9alphadecodebin` para el VP9
  con alfa, `avdec_prores` para el ProRes) con un `secuencia.json` que dice a
  cuántos cuadros por segundo va. Si el video no trae alfa, se avisa: va a
  tapar todo lo de atrás.
- **Usarlo** en la descripción: `{"tipo": "medio", "archivo": "medios/bandera.gif",
  "ancho": 200, "alto": 120, "inicio": 300, "bucle": true}`. `inicio` es el ms del
  gráfico en que arranca (antes no se ve), `bucle` false lo deja en el último
  cuadro, `velocidad` y `ajuste` (ajustar, llenar, estirar). En un QML:
  `import BA.Grafico` y `Medio { source: "medios/bandera.gif"; inicio: 300 }`
  (las rutas son relativas a la carpeta del gráfico, aunque el texto venga del
  editor sin guardar).
- **Por qué cuadros y no el video**: `AnimatedImage` y `MediaPlayer` corren con
  su propio reloj de pared, no con el del renderizador; y buscar un cuadro
  exacto adentro de un video en cada pedido (para atrás, como la barra) es lento.
  Un gráfico dura segundos: los cuadros en disco salen baratos.

## Tortas y barras con datos

Capas `torta` (con `grosor` > 0, un anillo) y `barras` (vertical u horizontal),
alimentadas con un parámetro de tipo `datos`:

```json
{"clave": "votos", "titulo": "Votos", "tipo": "datos", "porDefecto": "Sí: 0; No: 0; Abstenciones: 0"}
...
{"tipo": "torta", "datos": "{votos}", "izquierda": 60, "arriba": 80, "ancho": 420, "alto": 420}
```

- Los datos se escriben como lista `[{"etiqueta", "valor", "color"}]`, como
  objeto `{"Sí": 34}` o como texto en un campo (Rótulos, el editor):
  `Sí: 34; No: 12 #c62828; Abstenciones: 3` (punto y coma o renglón; el color
  al final, opcional; «12,5» es doce y medio). Una sola lectura para todo
  (`normalizarDatos` en src/grafico/Declarativo.cpp, y `Datos.normalizar` en QML).
- **Entrada**: la torta gira desde arriba en sentido horario; las barras crecen
  de a una (`escalonado`) con el número contando. `aparece` es cuándo empiezan.
- `mostrar`: porcentaje, valor, ambos o nada; `decimales`, `sufijo`,
  `tamanoTexto`, `colorTexto`. Sin `color` en un dato, sale de una paleta.

## Cambiar al aire (una votación en vivo)

Cambiarle los valores a un gráfico que **ya está en pantalla** no lo hace
entrar de nuevo: se dibuja sin la entrada y las tortas y las barras se mueven
de los valores de antes a los nuevos (`"cambio": {"duracion": 700}` en la
ficha, 700 ms de fábrica). Pasa con `grafico_actualizar`, con «Al aire» en
Rótulos si el gráfico de esa fuente es el mismo, y con un vínculo `vivo`.

- Sabe animar un cambio un gráfico con una torta, unas barras, un parámetro de
  tipo `datos`, o que lo declara con `"cambio"` en su ficha. Los demás reemplazan
  su cuadro quieto, como siempre.
- Un cambio deja el gráfico puesto: no programa la salida (`salidaEn` va en 0).
- Un QML escrito a mano se entera si declara en su raíz `property var anteriores`
  (los valores de antes, por clave) y `property bool sinEntrada`: el sistema las
  llena solo si existen. Con `Torta { datos: raiz.votos; anteriores:
  raiz.anteriores ? raiz.anteriores.votos : null }` alcanza.

## Gráficos de un plugin

Un plugin de Python puede proveer gráficos animados con `@plugin.grafico`
(ejemplo: `assets/plugins/ejemplo-grafico`). El gráfico queda en el catálogo
como `plugin-<id>-<nombre>` (categoría «Plugins»): lo ven igual el editor,
Rótulos, el guion y el asistente, y sale al aire por el mismo camino. Dos formas:

- **Describirlo** (`def f(valores) -> dict`): Python arma la descripción
  (`{"declarativo": {...}}`) o el QML (`{"qml": "..."}`, con los valores ya
  puestos) y lo dibuja el renderizador. Lo que conviene casi siempre: las
  cuentas y los datos los decide Python y la animación es la de siempre. Usá
  `"{clave}"` en los textos y `"datos": "{clave}"` en tortas y barras para que
  el cambio al aire se anime.
- **Dibujarlo** (`def f(lienzo, ms, valores)`): cada cuadro con numpy
  (`lienzo` (alto, ancho, 4) RGBA transparente). El SDK dibuja todos los
  cuadros y los guarda crudos en `datos/.graficos` del plugin (con los mismos
  valores no se redibujan), y el renderizador los muestra con un `Medio`.

Cómo viaja: el plugin declara el gráfico (`{"t": "grafico", ...}`), la app
escribe su ficha en `Graficos/plugin-<id>-<nombre>/grafico.json` y, cuando hay
que dibujarlo, `graficos::Emisor::resolver` le pide al plugin
(`pedido {"accion": "grafico", "args": {nombre, valores, ancho, alto, fps,
duracion_ms}}`) y sigue con lo que contestó. Con el plugin apagado, el gráfico
sigue en la lista y al usarlo dice que el plugin no está corriendo.
`plugin_probar` prueba cada gráfico del plugin (chico y corto) y lo informa.

## Cómo verificarlo

1. **Validar**: carga el QML y devuelve los errores. Un gráfico que no compila
   no llega nunca a una escena.
2. **Tira de cuadros**: pedí el gráfico en los momentos que importan —0, mitad
   de la entrada, recién asentado, y bastante después— y **mirá las imágenes**.
   Ahí se ve si el texto entra tarde, si la barra tapa el nombre, si al salir
   queda un borde.
3. Recién entonces, ponerlo en una escena.

Si un cuadro sale vacío o idéntico al anterior, la causa casi siempre es una de
las cinco de arriba.
