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

- **PDF: se resuelve vendoreando `mutool` (MuPDF)** — un binario chico y
  estático que renderiza páginas a PNG. Entra sin discusión en el bundle.
- **PowerPoint: es otra cosa.** Un `.pptx` es un zip con XML; sacar las
  diapositivas COMO SE VEN necesita un motor de maquetado, y el único que lo
  hace bien es LibreOffice: unos 400 MB. En un aparato que se define por caber
  en un pendrive, eso no es un detalle de implementación.

  **Decidido: se carga LibreOffice**, porque el caso real es que alguien traiga
  sólo el `.pptx` y no haya con quién discutirlo. Pero **no pesa en el
  pendrive del que no lo necesita**: `make deps-libreoffice` lo baja aparte y
  el bundle lo incluye sólo si la carpeta existe.

  Medido, y conviene saberlo: **no se puede quedar sólo con Impress.**
  LibreOffice fusiona el núcleo en `libmergedlo.so`, que sola pesa 85 MB.
  Sacando Writer (20 MB), Calc (19 MB) y el arte de la interfaz (38 MB, que
  headless no se usa) se baja de 355 MB a ~280 MB y ahí se termina.

  Se bajan **las dos plataformas** —el `.tar.gz` de `.deb` y el `.msi`— y se
  extraen sin instalar nada, porque el bundle de Windows se arma desde Linux.

## Y de paso, las tipografías

El tarball oficial de LibreOffice trae un juego de texto decente —Liberation,
Carlito, Caladea, DejaVu, Noto— y eso tapa el agujero número 2 del escenógrafo:
hasta ahora **lo único que viajaba era JetBrains Mono**, una monoespaciada de
programar, y todo lo demás dependía de las fuentes de la máquina. Una escena
diseñada acá se veía distinta en el equipo de respaldo.

Las fuentes se extraen **aparte** y **viajan aunque no se incluya LibreOffice**:
son para los gráficos y los rótulos, no para convertir nada. (El paquete de la
distribución no las trae —verificado, cero archivos—; el oficial sí.)

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
5. Traer un PDF (con `mutool` vendoreado).
6. PowerPoint con LibreOffice (`make deps-libreoffice`, ya está el vendoreo).
