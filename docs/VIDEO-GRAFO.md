# El grafo de video del estudio

Un solo grafo para todo el video, como un pipeline de GStreamer con sus bins: cada entrada de video es un
**sub-grafo** y los cables pueden cruzar entre entradas. Lo corre la GPU, dentro de libobs.

## El modelo (`src/app/video/GrafoVideo`)

- **Sub-grafos por entrada.** Cada entrada (cámara, red, NDI) tiene su nodo `entrada` (la cámara ya encuadrada por
  el PTZ) y su nodo `escena` (lo que ven las escenas que la usan). Ids legibles: `camara-1.entrada`,
  `camara-1.croma`. Los nodos sin entrada son **del estudio** (`estudio.compositor`).
- **Nodos por sus puertos:**

  | Tipo | Puertos | Nodos |
  |---|---|---|
  | fuente | 0 → 1 | `entrada` |
  | filtro | 1 → 1 (+ `ctl`) | `color`, `croma`, `recorte`, `obs` (cualquier filtro de video de OBS), `gst` (cualquier módulo de video de GStreamer) |
  | varias entradas | N → 1 | `compositor`: puertos **a pedido** `in_0`, `in_1`… y siempre uno libre |
  | destino | 1 → 0 | `escena`, `salida` (publica una fuente con nombre) |
  | control | 0 → `ctl` | `control`: el molde de una tarjeta (tipo y params), del estudio |

- **Puertos con tipo:** `video` o `control`. Un cable une puertos del mismo tipo.

- **Cables de puerto a puerto** (`desde`, `salida`, `hacia`, `entrada`). A cada puerto de entrada le llega uno
  (conectar otro lo reemplaza); de una salida salen varios (el `tee`). Sin ciclos en todo el grafo.
- **Controladores (una corrección para varias cámaras).** Cada filtro tiene una entrada de control `ctl`. Una
  tarjeta que recibe un cable de un controlador corre con los params del controlador, y encima los suyos propios
  (`params.local`): la cámara Sony, la Pelco y la Panasonic comparten la corrección de color y la Pelco retoca el
  matiz. El controlador maneja tarjetas de su tipo (y del mismo filtro de OBS o módulo de GStreamer).
  `controlarDesde(tarjeta)` lo crea a partir de una tarjeta; `agregarControlada(controlador, entrada)` agrega una
  manejada en otra entrada; `soltar(tarjeta)` la desconecta con los valores que usaba. En la interfaz: «Usar en
  varias entradas…» en la tarjeta; los cables de control son ámbar y punteados. Lo que se ajusta en una tarjeta
  manejada va al controlador (cambia en todas).
- **Propiedades por puerto:** las del compositor van en `params.capas["in_0"]` (posición, tamaño, opacidad, orden).
- Se guarda en `Config/video-grafo.json` (versión 2). El formato anterior (`video-grafos.json`, un grafo por
  entrada) se migra solo.

## La ejecución

- Cada entrada tiene el filtro `ba_video_grafo` (`src/obs-module/ba_video_grafo.cpp`), después de los del PTZ.
  Recibe `GrafoVideo::paraFiltro(entrada)`: lo que se alcanza desde esa entrada por nodos de una entrada. Calcula
  una vez por cuadro, con ramas.
- **Tarjetas de OBS:** cada una tiene una fuente privada «portador» que dibuja la textura de la tarjeta con el
  filtro de OBS real encima. Así corre cualquier filtro de video de OBS (los de obs-filters, Mejorar imagen, el PTZ
  digital) dentro del grafo. Un ajuste nuevo no recrea el filtro.
- **Módulos de GStreamer** (`src/obs-module/ba_gst.cpp`, `TarjetaGst`): la textura de la tarjeta se baja a la
  RAM (alfa recto), entra a un pipeline `appsrc ! videoconvert ! <módulo> ! videoconvert ! videoscale !
  video/x-raw,format=RGBA ! appsink` y lo que sale se sube y se vuelve a premultiplicar. Los módulos de OpenGL
  (`gl…`) se envuelven con `glupload ! glcolorconvert` y `gldownload`. Las propiedades cambian en vivo
  (`gst_util_set_object_arg`); si se saca una, el pipeline se rearma. Un módulo que no existe deja pasar la
  imagen.
- **Salidas:** la fuente `ba_video_salida` dibuja lo último que le mandó el grafo; si nadie dibujó su entrada en
  ese cuadro, la despierta.
- **Lo que cruza entradas** (el compositor, la escena de una entrada alimentada desde otra) lo ejecuta el estudio:
  es el paso siguiente.

## GStreamer en el programa

- El framework oficial de GStreamer (1.28.7, solo arm64; `make deps-gstreamer-macos`,
  `tools/deps/gstreamer-macos.sh`) viaja en `bin/64bit/GStreamer.framework`. Sin python, gtk, devtools ni los
  módulos de edición (ges, nle), que dependen de gst-validate.
- `ba::platform::prepararGst` hace que se usen SOLO esos plugins (`GST_PLUGIN_SYSTEM_PATH`, el escáner y el
  registro en Cache). `BA_GST_RAIZ` lo apunta a otro lado (desarrollo).
- **El catálogo** (`src/app/video/CatalogoGst`): pregunta al registro qué módulos de video hay (194) y lee sus
  propiedades por introspección (tipo, rango, de fábrica, opciones de un enum, si cambian en vivo). La
  documentación en castellano de cada uno (título, categoría, para qué sirve, cómo se usa en vivo, si va como
  tarjeta) está en `datos/gst-modulos.json`, que genera `tools/gst-modulos-docs.py`. Al agregar un módulo nuevo
  al framework, se lo documenta ahí (`test_catalogo_gst` falla si queda alguno sin documentar).
- En la interfaz, «+ Módulos…» abre el catálogo (los de GStreamer y los filtros de OBS juntos): búsqueda en
  castellano o en inglés, categorías, y el detalle con la documentación, la licencia y todas las propiedades.
  Por MCP: `video_modulos`.
- **Entradas virtuales:** la fuente `ba_gst_fuente` corre la parte de origen de un pipeline
  (`videotestsrc pattern=smpte-rp-219 is-live=true`, `rtspsrc … ! decodebin`) y cuenta como una entrada más,
  con su sub-grafo. Hay presets (barras HD, 75 %, barras con la hora, negro, blanco, verde, placa de zonas,
  damero, pelota, ruido) al tamaño del lienzo. Interfaz: «+ Entrada virtual…»; MCP: `video_entrada_virtual`.

## Lo que sigue

1. ~~Modelo global con sub-grafos, puertos y puertos a pedido.~~
2. Vista Estudio (como la del audio): entradas como bloques, cables entre ellas.
3. Nodos que cruzan entradas: ~~controladores compartidos con cables de control~~; falta ejecutar el compositor de
   N entradas.
4. Sincronizar (reloj y retardos por entrada) y Normalizar (resolución, fps, espacio de color).
5. Rostros como nodo de análisis (Personas y el PTZ lo usan).
6. ~~Módulos de GStreamer como tarjeta y entradas virtuales.~~ Falta: salidas de GStreamer (red) y el costo por
   cuadro de cada tarjeta a la vista.

## Pruebas

- `test_grafo_video`: el modelo (sub-grafos, puertos a pedido, cables entre entradas, ciclos, migración, lo que
  recibe cada filtro, ids con tildes en el locale de la app, controladores con tres cámaras y ajuste local).
- `test_catalogo_gst`: el catálogo de GStreamer del bundle, sus propiedades y que todos estén documentados
  (`--volcar` lo imprime entero).
- `test_video_grafo` (`src/obs-module/tests/`): en la GPU real, cada tarjeta, las ramas, las salidas, y las
  tarjetas de OBS (el PTZ digital; en Mac, la corrección de color de obs-filters), los módulos de GStreamer
  (videobalance, el cambio en vivo, uno de OpenGL, uno que no existe) y la fuente `ba_gst_fuente`.
