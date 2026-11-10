# Control web: el tablero en el celular, la tablet o la PC

`/control/` es el tablero de la app en cualquier navegador de la red local: escenas, multipantalla,
línea de tiempo, mezclador, cámaras PTZ, salidas, conferencia, oradores, teleprompter, latencias y
estado. Todo lo que se toca ahí lo hace la app con sus mismas herramientas (las del asistente y la
botonera), con las mismas reglas: lo que cambia el aire pide confirmación en la pantalla del aparato
si hay transmisión o grabación.

> **En desuso.** El tablero de entrada es el de WebAssembly (`/control/app/`,
> [TABLERO-WEB.md](TABLERO-WEB.md)): el mismo dashboard de la app. Un aparato con el perfil
> «todo» que abre `/control/` pasa solo allá. Este tablero en HTML queda para los aparatos con
> un perfil acotado y, a pedido, en `/control/?clasico`. Emparejar sigue siendo acá, igual que
> siempre.

## Emparejar un aparato (una sola vez)

1. En la app, **Servicios → Control** muestra un QR.
2. El aparato lo escanea, se le pone un nombre («iPad de dirección») y pide el emparejamiento.
3. Una persona lo aprueba en la app y le elige un **perfil**: qué puede manejar (todo, o solo
   escenas, audio, la línea…; «solo mirar» para la producción).

Desde ahí el aparato entra solo: refrescar la página, cerrar el navegador o reiniciar la app no
vuelve a pedir nada. Para sacarle el acceso, se lo quita en la misma página de la app.

## La multipantalla

Arriba de todo, como en un switcher:

- **PROGRAMA** (borde rojo): lo que sale al aire, con el nombre de la escena.
- **PREVIEW** (borde verde): la escena armada, la que sale con el próximo TAKE.
- **TAKE**: pasa el preview al aire con la transición configurada (o la que se elija al lado para
  ese take: corte, fundido, barrido, deslizar, fundido a color). La que sale queda armada: otro
  TAKE vuelve a la anterior.
- **CORTE**: lo mismo, seco, sin transición.
- **T-bar**: la transición a mano; pasada la mitad, al soltar queda al aire.

Las imágenes son cuadros chicos (unos cuatro por segundo, solo cuando cambian) de lo que la app ya
está mostrando: no le agregan trabajo de video. En el celular las dos pantallas van una arriba de la
otra. **▴ Ocultar monitores** las esconde (los botones quedan) y la app deja de mandarlas; con la
pestaña en segundo plano tampoco se mandan.

Con **varias salidas** (varios programas), arriba aparece un botón por salida, con su color: el
aparato maneja la que esté marcada. Las escenas, el TAKE, la línea y lo demás del tablero son de esa
salida.

## Escenas

La tarjeta **Escenas** tiene todas las escenas. Un toque la **arma** (aparece en PREVIEW); mantenerla
apretada un momento la pone **al aire directo**. La que está al aire se ve en rojo y la armada en
verde.

## Ordenar el tablero

**✎ Disposición** (arriba a la derecha) pone el tablero en modo edición:

- **⠿** arrastra la tarjeta a otro lugar (con el dedo o el mouse); **◀ ▶** la corren de a una.
- **− / +** cambian cuántas columnas ocupa (de 1 a 4).
- **✕** la oculta; las ocultas quedan tenues mientras se edita y **👁** las vuelve a mostrar.
- **Volver a la de fábrica** deshace todo; **Listo** termina.

La grilla se acomoda sola al ancho: una columna en el celular, dos en la tablet, tres o cuatro en
una pantalla grande (una tarjeta nunca ocupa más columnas de las que hay). La disposición se guarda
**en la app**, en el registro del aparato: vuelve igual al refrescar o con el navegador borrado, y
si el mismo aparato tiene el tablero abierto en otra pestaña, cambia ahí también.

**⤢** en una tarjeta la abre sola, a pantalla completa, en otra pestaña (la de escenas, con la
multipantalla arriba).

## Qué se ve según el perfil

Una tarjeta aparece si el perfil del aparato deja usar lo suyo. Sin permiso para cambiar el aire, la
multipantalla muestra los monitores sin TAKE ni CORTE.
