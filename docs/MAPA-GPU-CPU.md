# Mapa GPU ↔ CPU del motor

Dónde pasa el video (y el audio) de la GPU a la CPU, rama por rama, desde cada
entrada posible hasta la salida principal. Medido el 2026-10-06 en la Mac
(M-series, macOS 26) con la herramienta MCP `motor_mapa`. Las otras
arquitecturas, por análisis de los elementos que elige GStreamer en cada una.

## Cómo se mide

`motor_mapa` (MCP; código en `src/motor/Mapa.cpp`) recorre **todos** los
pipelines vivos del motor —cada uno se anota solo al pasar por
`Reloj::adoptar`— y, adentro de cada uno, todos los elementos (también los de
adentro de los bins: el decodificador que eligió `uridecodebin3`). De cada
enlace saca las caps **negociadas** y su *caps feature* de memoria:

| Memoria | Dónde viven los cuadros |
|---|---|
| `sistema` (sin feature) | RAM, la CPU los toca |
| `GLMemory` | textura OpenGL (Mac: sobre IOSurface; Linux/Windows: GL) |
| `D3D11Memory` / `D3D12Memory` | textura Direct3D (Windows: Intel, AMD, NVIDIA) |
| `VAMemory` / `DMABuf` | superficie VA-API (Linux: Intel, AMD) |
| `CUDAMemory` | NVIDIA |

Marca:
- **transiciones**: un elemento que recibe memoria de GPU y entrega de sistema
  (una *bajada*) o al revés (una *subida*);
- **trabajo en CPU**: un elemento que procesa video crudo en memoria de sistema
  (convertir, escalar, componer, rotular, filtrar) y no está en *passthrough*,
  y los decodificadores/codificadores que no son de hardware (GStreamer marca los
  de hardware con «Hardware» en la clase).

`{"detalle": true}` da cada elemento y cada enlace; `{"filtro": "Cámara"}`
limita a los pipelines cuya etiqueta lo contiene.

## Cómo está armado el motor (las ramas)

```
ENTRADA (un pipeline por entrada)                       PLACA (un pipeline por entrada)
  origen → [decodificador] → queue ba_origen               intersrc <id>:crudo:video ┐
  → normalizar (deinterlace, videorate, convertscale)                                ├→ fallbackswitch → intersink <id>:video
  → [cola + filtros: tarjetas, PTZ, mejora, superres]      videotestsrc → textoverlay ┘
  → caps del lienzo → intersink <id>:crudo:video
  → audio: … → barack → audioconvert/resample → intersink <id>:audio

COMPOSICIÓN (programa, preview, cada escena pedida, multiview)
  por capa: intersrc <id>:video → queue → videocrop → compositor/glvideomixer
  → videoconvert → tee → [limpio] / [DSK: compositor] → videoconvert → intersink video:<nombre>

SALIDAS: intersrc video:programa → [videoconvert] → codificador → muxer → archivo / RTMP / SRT
VISTAS:  intersrc <clave> → queue → videoconvert → videoscale → BGRA chico → appsink (la GUI)
MEZCLA:  intersrc <id>:audio … → audiomixer → limitador → intersink bus:<nombre>   (audio: siempre CPU)
```

Entre pipelines los cuadros viajan por `intersink`/`intersrc` (por adentro,
`appsink`/`appsrc`): **pasan el GstBuffer tal cual, sin copiar**. Si el buffer
es de GPU, sigue siendo de GPU del otro lado, siempre que los dos pipelines
compartan el contexto de GPU (ver más abajo).

## Lo medido en la Mac (estudio de prueba: 31 fuentes, 17 escenas)

**87 pipelines, 428 enlaces de video: los 428 en memoria de sistema. Ninguno en
GPU, ninguna transición: todo el video del motor corre en CPU.**

Trabajo real en CPU (sin contar los elementos en passthrough):

| Elemento | Cuántos | Dónde | Comentario |
|---|---|---|---|
| `textoverlay` | 27 | las **placas «sin señal»** (17, una por entrada) y las entradas de texto (10) | dibuja el texto **en cada cuadro, 30 por segundo, aunque nadie lo esté mirando**. El mayor desperdicio |
| `compositor` | 5 | programa (2 etapas: escena + DSK), preview, escenas pedidas | mezcla en CPU, NV12 1920×1080 |
| `videoconvert` | 5 | composiciones | NV12 → NV12/I420 del lienzo entre etapas |
| `videoconvertscale` | 3 | entradas que no llegan al formato del lienzo | |
| `videoscale` | 2 | vistas para la GUI | 1920×1080 → chico, en CPU |
| `baplugin` | 1 | la tarjeta de un plugin de Python en la cámara | el cuadro entero sale del proceso y vuelve |
| decodificación | — | las cámaras de red estaban caídas | `vtdec` es de hardware pero entrega a memoria de sistema: ver abajo |
| codificación | — | `vtenc_h264_hw` (hardware) | recibe NV12 de sistema: una copia a CVPixelBuffer por cuadro |

En passthrough (no tocan píxeles): 33 `videoconvert`, 30
`videoconvertscale`, 6 `videocrop`, 3 `baptz`, y `deinterlace` y `videorate`
con señal progresiva a los cuadros del lienzo. Están en la cadena, pero no
cuestan.

Las entradas estáticas (color, texto, imagen) arman toda la normalización
(`deinterlace` → `videorate` → `videoconvertscale`) y empujan **30 cuadros por
segundo de 1920×1080 BGRA** aunque la imagen no cambie: cerca de 250 MB/s de
memoria por cada fondo de color.

### Por qué no hay nada en GPU en la Mac

1. **La cámara** (`avfvideosrc`) puede entregar `GLMemory`, pero lo que la
   sigue (`queue ! deinterlace ! videoconvertscale`) solo acepta sistema, así que
   negocia sistema. La captura ya llega a la CPU.
2. **El decodificador** (`vtdec`, de hardware) decodifica en la GPU, pero
   detrás tiene `videoconvert`, que acepta solo sistema, así que **baja cada
   cuadro adentro del propio decodificador**. El mapa lo muestra como sistema
   desde el origen.
3. **La composición** usa `compositor` (CPU). El motor sabe usar
   `glvideomixer` (`op.gpu`), pero ese camino sube cada capa (`glupload`) y
   **baja enseguida** (`gldownload ! videoconvert`) para el `intersink`: dos
   copias por cuadro y por capa, peor que quedarse en CPU.
4. **Los pipelines no comparten un contexto GL.** Sin un `GstGLDisplay` y un
   `GstGLContext` comunes, un buffer GL que cruza un `inter` hacia otro pipeline
   no se puede usar del otro lado. Por eso todo baja antes de cada `intersink`.

## Las otras arquitecturas (análisis)

### Intel en Windows (la que más duele)

- **Decodificador:** `uridecodebin3` elige `d3d11h264dec` o `d3d12h264dec` (o
  `qsvh264dec`): decodifican en la GPU y entregan `D3D11Memory`/`D3D12Memory`.
  Con `videoconvert` detrás, **la bajada la hace el decodificador**, leyendo la
  textura con una copia a *staging*. En las Intel integradas esa lectura de
  memoria de GPU es lenta (memoria compartida no cacheada para la CPU): por
  cámara 1080p30 se va un núcleo entero solo en eso, y es justamente donde OBS
  andaba bien porque nunca bajaba.
- **Cámaras USB** (`mfvideosrc`): entregan a sistema (MJPEG o NV12); MJPEG se
  decodifica en CPU (`jpegdec`) salvo que haya `qsvjpegdec`/`d3d12mjpegdec`.
- **Composición:** `compositor` en CPU. Existen `d3d11compositor` y
  `d3d12compositor`.
- **Codificador:** `qsvh264enc` y `d3d12h264enc` aceptan `D3D11Memory`/
  `D3D12Memory` sin copia. Con sistema, **suben cada cuadro**.
- **Resultado hoy:** cada cámara de red o archivo baja de la GPU (lento en
  Intel), se compone en CPU y vuelve a subir para codificar. Tres pasos que en
  Intel cuestan caro.

### Intel/AMD en Linux

- `vah264dec` → `VAMemory`. Con `videoconvert` detrás, baja con
  `vaDeriveImage`/copia (lento en Intel por la memoria *tiled*).
- Hay `vacompositor`, `vapostproc` (escalar y convertir) y `vah264enc`/
  `vah264lpenc`, que aceptan `VAMemory` sin copia.

### NVIDIA (Windows y Linux)

- `nvh264dec` → `CUDAMemory`; `cudaconvertscale`, `nvh264enc` con `CUDAMemory`.
  Para componer: `cudacompositor`.

### Mac

- `vtdec` → `GLMemory`/IOSurface; `glvideomixer`, `glcolorconvert`,
  `glcolorscale`; `vtenc_h264_hw` acepta CVPixelBuffer (IOSurface) sin copia.
  La superresolución propia (`basuperres`/`bavtsuperres`) ya trabaja con
  IOSurface/CVPixelBuffer.

### Audio (todas)

Todo el audio corre en CPU, como corresponde: no hay mezcla de audio en GPU que
valga la pena. Los `audioconvert`, `audioresample`, `volume` y `level` que
aparecen están casi todos en passthrough. El costo real es el de los racks LSP y
el limitador, que es procesamiento de verdad.

## Qué hay que hacer, en orden de impacto

### 1. Cortar el desperdicio en CPU (rápido, en las tres plataformas)

- **Placas «sin señal»:** el texto se dibuja UNA vez (`textoverlay` sobre un
  solo cuadro y `imagefreeze`), no 30 veces por segundo por entrada.
- **Entradas estáticas** (color, texto, imagen): sin la normalización, a pocos
  cuadros por segundo o un cuadro congelado (`imagefreeze`). El color, al
  tamaño mínimo: la composición lo escala.
- **Vistas de la GUI:** escalar donde se compone (en GPU, ver 3), no con
  `videoscale` en CPU.

### 2. Un contexto de GPU para todo el motor

Un `GstGLDisplay` y un `GstGLContext` (Mac y Linux) o un `GstD3D12Device`
(Windows), creados por el motor y entregados a **cada** pipeline con
`gst_element_set_context` / mensajes `need-context`. Sin esto, ningún buffer de
GPU puede cruzar un `inter`, y cada pipeline baja a CPU antes de su `intersink`.

### 3. El camino de GPU de punta a punta

Por plataforma, la cadena que no baja nunca:

| | Decodificar | Filtros/normalizar | Componer | Vistas (GUI) | Codificar |
|---|---|---|---|---|---|
| Mac | `vtdec` → GLMemory | `glcolorconvert`, `glcolorscale`; superres por IOSurface | `glvideomixer` | `glcolorscale` + `gldownload` chico | `vtenc_h264_hw` |
| Windows (Intel/AMD/NVIDIA) | `d3d12h264dec` / `qsvh264dec` | `d3d12convert` | `d3d12compositor` | `d3d12convert` + `d3d12download` chico | `qsvh264enc` / `d3d12h264enc` / `nvh264enc` / `amfh264enc` |
| Linux Intel/AMD | `vah264dec` | `vapostproc` | `vacompositor` (o GL) | `vapostproc` + bajada chica | `vah264enc` |
| Linux NVIDIA | `nvh264dec` | `cudaconvertscale` | `cudacompositor` | bajada chica | `nvh264enc` |

Reglas para que no baje sin que nadie se entere:
- La normalización de la entrada (`deinterlace`, `videorate`,
  `videoconvertscale`) se reemplaza por sus versiones de GPU, o se deja que la
  haga la composición (que igual escala cada capa).
- Las caps entre pipelines llevan la feature de memoria (`video/x-raw(memory:…)`)
  y el `intersink` publica eso: el consumidor negocia GPU.
- Lo que solo existe en CPU (rótulos con `textoverlay`, tarjetas `gst` de
  CPU, plugins de Python, el legalizador, `bamejora`) baja y sube **solo en esa
  entrada**, y `motor_mapa` lo muestra como transición.
- Las vistas de la GUI bajan **chico** (después de escalar en GPU): 160×90 o
  640×360, no 1080p.
- Detrás del codificador, nada.

### 4. Medir antes y después

Cada paso se mide con `motor_mapa` (transiciones y trabajos en CPU) y con
`status.cpuPercent`, con el mismo estudio de prueba. La meta en la Mac: el video
de punta a punta en GLMemory, salvo las entradas con tarjetas de CPU. En Intel
con Windows: D3D12 de punta a punta y, para cada cámara de red, cero bajadas.
