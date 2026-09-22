# Cómo escribir un gráfico animado

Instrucciones para quien escriba un gráfico (persona o asistente). Cada regla
salió de un problema real armando el primero: si algo está acá es porque ya
nos hizo perder tiempo.

Un gráfico es una carpeta con dos archivos:

```
<nombre>/
├── grafico.qml     el dibujo y las animaciones
└── grafico.json    qué parámetros acepta
```

---

## Las cinco que más van a morder

**1. Para mover algo que pasa por un efecto, mové EL EFECTO, no el origen.**
`MultiEffect` dibuja su origen en **su propio** rectángulo: animar la posición
del item de origen no mueve nada en pantalla. El item de origen queda quieto
(y con `visible: false`); quien se desliza es el `MultiEffect`.

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
