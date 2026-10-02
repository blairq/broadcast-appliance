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
  | filtro | 1 → 1 | `color`, `croma`, `recorte`, `obs` (cualquier filtro de video de OBS) |
  | varias entradas | N → 1 | `compositor`: puertos **a pedido** `in_0`, `in_1`… y siempre uno libre |
  | destino | 1 → 0 | `escena`, `salida` (publica una fuente con nombre) |

- **Cables de puerto a puerto** (`desde`, `salida`, `hacia`, `entrada`). A cada puerto de entrada le llega uno
  (conectar otro lo reemplaza); de una salida salen varios (el `tee`). Sin ciclos en todo el grafo.
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
- **Salidas:** la fuente `ba_video_salida` dibuja lo último que le mandó el grafo; si nadie dibujó su entrada en
  ese cuadro, la despierta.
- **Lo que cruza entradas** (el compositor, la escena de una entrada alimentada desde otra) lo ejecuta el estudio:
  es el paso siguiente.

## Lo que sigue

1. ~~Modelo global con sub-grafos, puertos y puertos a pedido.~~
2. Vista Estudio (como la del audio): entradas como bloques, cables entre ellas.
3. Nodos que cruzan entradas: controladores compartidos con cables de control (p. ej. una corrección de color para
   todas las cámaras) y el compositor de N entradas.
4. Sincronizar (reloj y retardos por entrada) y Normalizar (resolución, fps, espacio de color).
5. Rostros como nodo de análisis (Personas y el PTZ lo usan).
6. Islas GStreamer (fuentes y salidas de red, `videotestsrc`), con el costo por cuadro a la vista.

## Pruebas

- `test_grafo_video`: el modelo (sub-grafos, puertos a pedido, cables entre entradas, ciclos, migración, lo que
  recibe cada filtro, ids con tildes en el locale de la app).
- `test_video_grafo` (`src/obs-module/tests/`): en la GPU real, cada tarjeta, las ramas, las salidas, y las
  tarjetas de OBS (el PTZ digital; en Mac, la corrección de color de obs-filters).
