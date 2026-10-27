# El carrusel de la escena

Una tira de imágenes que se muestra en una escena, con un control para que el
operador salte a cualquiera **rápido**, y un buscador que filtra mientras se
escribe.

El caso real: un orador manda su presentación, y en el medio de la charla dice
«volvé a la del gráfico de barras». Con veinte diapositivas y un botón de
siguiente, eso son veinte clics y diez segundos de aire perdido.

## Lo que ya existe y lo que no

Probado contra el aparato:

- **`slideshow_v2` de libobs anda** y rota solo con `slide_time`.
- **Pero no se puede controlar.** Sus propiedades son `bool`, `int` y `list`:
  ningún botón. Su propio texto lo dice —«Manual (Use hotkeys to control
  slideshow)»— y los atajos no están expuestos por MCP.
- **Cambiar la imagen de un `image_source` EN VIVO sí anda** (`source_set_property`).

## La decisión que ordena todo: el carrusel es nuestro

No se usa `slideshow_v2`. La escena lleva un **`image_source` común** y el
aparato le cambia el archivo. El carrusel —la lista, en qué posición está, cada
cuánto avanza— vive en el aparato.

Eso da lo único que importa: **saltar a cualquier imagen al instante**, que es
justamente lo que la fuente de libobs no deja hacer. Y de paso se acaba la
dependencia de los atajos de teclado.

## Las tarjetas se ven grandes

La vista previa no es un adorno: es cómo el operador encuentra la que busca. Se
ven todas, en grilla, del tamaño suficiente para reconocerlas de un vistazo —no
iconos—. La que está al aire se distingue de lejos.

## Buscar: etiquetas hechas por una LLM que ve

No hay OCR en el bundle y no hace falta: cada imagen se le pasa **una vez** al
rol `vision`, que devuelve sus etiquetas. Eso cubre más que el OCR —lee el
texto de la diapositiva Y describe lo que se ve («gráfico de barras», «foto del
salón»)— y usa el camino que ya existe para imágenes, con su contrato estricto
y sin meter la imagen en el contexto de nadie.

Las etiquetas se guardan al lado del carrusel: se calculan una vez y viajan con
el evento.

El filtro usa el MISMO índice que la búsqueda de herramientas (`McpIndice`),
que mide **24 µs**: escribir y ver la grilla filtrarse, sin esperar.

## Traer una presentación

Un orador manda un PDF o un PowerPoint y eso tiene que volverse un carrusel.

- **PDF: lo resuelve Qt**, sin dependencia nueva. Qt 6.6.3 tiene el módulo
  `qtpdf` (`qt.qt6.663.addons.qtpdf`, verificado que existe) y se pide con el
  MISMO `aqtinstall` que ya baja Qt. Renderiza las páginas a imagen en el
  proceso, así que el progreso sale página por página sin inventar nada.

  Se eligió sobre MuPDF —que habría sido compilar un tarball de 69 MB— porque
  usa el vendoreo que ya existe y anda igual en Linux y en Windows.

  **Cuidado con el centinela:** el de Qt es `Qt6Config.cmake`, que NO cambia al
  agregar un módulo. Sin un segundo chequeo por `Qt6PdfConfig.cmake`, en una
  máquina que ya tenía Qt el módulo no se bajaría nunca y nadie se enteraría
  hasta que un PDF no abriera. Está contemplado en las dos plataformas.
- **PowerPoint: es otra cosa.** Un `.pptx` es un zip con XML; sacar las
  diapositivas COMO SE VEN necesita un motor de maquetado. Al principio el
  único que lo hacía bien era LibreOffice (~280 MB podado, 508 MB en la Mac,
  ~1,3 GB en Windows) y viajaba por eso.

  **Ahora: DeckCraft** (2026-10-27). Es una reimplementación de PowerPoint en
  Rust ([storytold/deckcraft](https://github.com/storytold/deckcraft),
  Apache-2.0) con una herramienta de línea de comandos que convierte sin
  pantalla, en milisegundos: `deckcraft-cli convert x.pptx x.pdf`. Junto con
  WordCraft (Word) y GridCraft (planillas) reemplazaron a LibreOffice también en
  la documentación del evento (`evento/APdf`). Se bajan los releases oficiales
  con versión y sha256 fijos (no se compilan) y viaja solo la herramienta de
  línea de comandos de cada uno: unos 160 MB en la Mac. Ver `tools/deps/craft.sh`.
  Lo que se perdió: el `.ppt` viejo (binario) y el `.odp` no se leen; se
  exportan a `.pptx` o a PDF. Además tienen MCP: el asistente puede armar
  presentaciones, documentos y gráficos.

## Y de paso, las tipografías

LibreOffice traía un juego de texto decente —Liberation, Carlito, Caladea,
DejaVu, Noto, Alef, Rubik, Gentium, Linux Libertine— y eso tapó el agujero
número 2 del escenógrafo: hasta entonces **lo único que viajaba era JetBrains
Mono**, una monoespaciada de programar, y todo lo demás dependía de las fuentes
de la máquina. Una escena diseñada acá se veía distinta en el equipo de respaldo.

Sin LibreOffice, siguen viajando: `tools/deps/fuentes-texto.sh` baja **los
mismos paquetes con que se arma LibreOffice**, de su espejo de dependencias y
con el sha256 de su `download.lst`. Son para los gráficos y los rótulos, no
para convertir nada.

## La tarjeta

- Va al tablero como una tarjeta más (`carrusel`), y **se saca a su propia
  ventana** con el mismo mecanismo que el padrón y las preguntas: en un evento
  el que pasa las diapositivas no siempre es el que opera.
- El **control** es aparte de la grilla y tiene efecto 3D: las de al lado se
  ven de costado, la del centro de frente. No es adorno — en una tira larga,
  ver de dónde venís y hacia dónde vas es lo que evita pasarse de largo.

## Orden

1. El modelo: la lista, la posición, el avance solo, el salto. Con pruebas.
2. La tarjeta con la grilla grande y el salto al tocar.
3. Las etiquetas por `vision` y el filtro instantáneo.
4. El control 3D.
5. Traer un PDF (con el módulo `qtpdf`, ya pedido en el Makefile).
6. PowerPoint con DeckCraft (`make deps-craft`).
