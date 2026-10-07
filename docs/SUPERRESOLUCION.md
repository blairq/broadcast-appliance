# Superresolución y filtros de video: todo en GStreamer

Las tarjetas de imagen del grafo de video no corren en libobs. Corren en la **capa de entrada**, que es la
cadena de GStreamer de cada entrada (`ba_gst_fuente`), como elementos de GStreamer de verdad. Si mañana se saca
libobs, esto sigue igual.

## Cómo se arma

```
uridecodebin3 ─ queue ─ [filtros de la placa primero] ─ deinterlace ─ [píxel cuadrado] ─ queue(1, descarta) ─
   [filtros de sistema] ─ videorate ─ videoconvertscale ─ NV12/BGRA ─ appsink ─ colchón ─ libobs
            └─ audio ─ [filtros de audio] ─ audioconvert ─ audioresample ─ F32 ─ appsink ─ colchón ─ libobs
```

- **Qué tarjetas pasan a la entrada.** Las que cuelgan en fila de la entrada y son de imagen: color, croma,
  recorte, superresolución y módulos de GStreamer. `VideoGrafos::pasarAEntrada` las saca del grafo de libobs y
  se las manda a la fuente en el ajuste `filtros` (JSON). Lo demás (salidas, tomas, rostros, retardo, filtros de
  OBS) sigue en el grafo.
- **El nombre de cada filtro.** Cada uno es un elemento con nombre `ba_f_<id>`. Lo que el elemento acepta
  andando (`GST_PARAM_MUTABLE_PLAYING`) cambia en vivo; lo demás rearma la cadena.
- **Sincronía.** El colchón mide la demora real: lo viejo que llega cada cuadro y cada bloque de audio. Atrasa
  audio y video por igual. `video_estado` lo muestra en `retraso_video_ms`, `retraso_audio_ms` y
  `compensacion_ms`.
- **Estado de cada filtro.** `video_estado` → `gstreamer.filtros[]` trae el elemento, si anda, todas sus
  propiedades de solo lectura (ms, motor, factor, error…) y las caps que negoció.

## Elementos propios

| Plugin | Elemento | Qué hace |
|---|---|---|
| `bafiltros` | `bacolor` | Brillo, contraste, saturación y gamma. NV12, I420 e Y444 sin pasar a RGB; RGB de 4 bytes. En el lugar y pasante si es neutro. |
| | `bacroma` | Saca un fondo de color: queda alfa. La misma matemática que el efecto de la GPU. |
| | `barecorte` | Recorte en fracciones y espejo, en modo transparente o recortar (caps más chicas). Interfaz `GstVideoDirection`, tag `image-orientation`, navegación. NV12/I420/Y444/GRAY/AYUV/RGB. |
| `basuperres` | `basuperres` | Bin x2 que elige el motor: `apple`, `fsr`, `onnx`, `coreml` o `glsl`. |
| | `bacoremlsr` | Red CoreML (.mlmodelc) en el Neural Engine. Sin copias: toma el CVPixelBuffer de vtdec o de su pool de IOSurfaces (ofrecido en `propose_allocation`). Color y escala en la GPU (VTPixelTransferSession); CoreML escribe en una IOSurface que sale como buffer (`BaCoreVideoMemory` + `GstCoreVideoMeta`). Asincrónico, un cuadro en vuelo. |
| | `baonnxsr` | Red ONNX en la placa que haya (abajo). Modelos NV12 uint8 o float. Una x4 recibe la mitad. Con NVIDIA: `memory:CUDAMemory` sin bajar a la CPU. |
| | `baglsl` | Intérprete de user shaders de mpv: FSRCNNX, Anime4K. Corre en GstGL, con texturas intermedias RGBA16F. |
| | `bavtsuperres`, `bafsr` | VideoToolbox de Apple (macOS 26) y FSR 1 (OpenGL). |

Todos tienen lo de un elemento de GStreamer como corresponde: categoría de debug (`GST_DEBUG=bacoremlsr:5`),
consulta LATENCY con lo que tardan (y aviso si cambia), QoS, metadatos que no dependen del tamaño, propiedades
controlables en los filtros, y `transform_caps`/`fixate_caps` para el cambio de tamaño.

## Placas

| Placa | Sistema | Cómo |
|---|---|---|
| Mac (Apple Silicon) | macOS | `bacoremlsr` en el Neural Engine; las redes que se desbordan en FP16 van en FP32 en la GPU. ONNX con CoreML. |
| NVIDIA (hasta Blackwell, RTX 5060) | Windows, Linux | `superres_runtime_gpu_instalar {tipo: nvidia}` baja ORT 1.30 con CUDA 13, cuDNN 9 y TensorRT a Datos. TensorRT compila la red para la placa y la guarda en `<modelo>.cache/`. La entrada va en CUDAMemory, sin copias. |
| AMD dedicada o integrada (Ryzen) | Windows | DirectML, que viene con el programa: no hace falta bajar nada. |
| AMD | Linux | `{tipo: amd}`: onnxruntime-rocm, con ROCm/MIGraphX del sistema. MIGraphX compila y guarda la red. |
| Intel | Windows, Linux | `{tipo: intel}`: OpenVINO. |
| Cualquiera | todos | CPU, o `baglsl` (OpenGL) para los shaders. |

`baonnxsr proveedor=auto` prueba en orden lo que trae el ONNX Runtime cargado y se queda con el primero que
abre la red. El ONNX Runtime se elige al correr: el del programa, o el de `BA_ORT_GPU`, que la app pone si hay
uno bajado.

## Contrato de plugins (`Datos/Modelos/superres/<id>/plugin.json`)

| `tipo` | Qué es | Archivos |
|---|---|---|
| `integrado` | apple, fsr | — |
| `red` | Lo arma el laboratorio desde una receta del catálogo | `onnx`, `onnx_nv12`, `coreml` |
| `glsl` | Un user shader de mpv | `glsl` |
| `elemento` | **Cualquier elemento de GStreamer** | su biblioteca, opcional, en `<id>/gst/` |

Un plugin `elemento` sirve para enchufar una red propia compilada para la placa sin pasar por la CPU (por
ejemplo, un elemento con TensorRT para una 5060):

```json
{
 "id": "mi-red-trt", "nombre": "Mi red (TensorRT)", "tipo": "elemento",
 "elemento": "mitrt",
 "propiedades": {"engine": "{dir}/red-sm120.engine"},
 "memoria": "CUDAMemory",
 "memoria_salida": "CUDAMemory",
 "escala": 2, "medio": "video", "plataformas": ["windows", "linux"]
}
```

- **Biblioteca propia.** La que esté en `<id>/gst/` se registra en GStreamer al usarla
  (`gst_registry_scan_path`).
- **`{dir}`.** En las propiedades se reemplaza por la carpeta del plugin.
- **`memoria`.** Puede ser `CUDAMemory`, `GLMemory`, `D3D11Memory` o `D3D12Memory`. La cadena sube a esa
  memoria antes y baja después. Si el decodificador ya entrega en esa memoria (nvh264dec, d3d11h264dec…), la
  subida pasa de largo y el filtro va primero, antes de lo que solo anda en la CPU.
- **`memoria_salida: ""`.** Para un elemento que ya entrega en memoria de sistema.

## Cámaras calientes y programador

Con varias cámaras, todas tienen su red **cargada y caliente**, pero solo trabajan las que se ven grandes.

**Modos de las redes** (`bacoremlsr` y `baonnxsr`; propiedad `modo` o canal de control):
- **activo:** corre la red.
- **reposo:** sale el agrandado común en la GPU, que cuesta décimos de ms. Una pasada de mantenimiento cada ~5 s
  tiene la red lista y su demora medida.
- **barato:** reposo más un afilado.

En los tres modos la salida es x2: nada se renegocia ni salta en la escena. Al cambiar de modo, la mezcla gradual
entre el agrandado común y la red dura `transicion-ms` (400 ms de fábrica), y nunca sale un cuadro negro.

**Canal de control** (el mecanismo estándar de GStreamer):
- Es un evento personalizado, hacia abajo y fuera de banda (`GST_EVENT_CUSTOM_DOWNSTREAM_OOB`), con una
  GstStructure `ba/control`.
- La entrada lo empuja por su cadena (proc `control` de `ba_gst_fuente`, MCP `entrada_control`). Cada elemento
  que lo entiende lo usa y todos lo dejan pasar.
- Se recuerda: si la cadena se rearma, se vuelve a mandar.
- Para efectos o eventos futuros se agregan campos.

**Programador** (`VideoGrafos`, cada 250 ms; MCP `superres_programador`, panel «Programador»):
- Mira el programa y el preview (la escena armada), incluidas las escenas anidadas, los grupos y las salidas del
  grafo.
- Calcula cuánto se agranda cada cámara en pantalla.
- **activo:** hasta `max_activas` (3 de fábrica), con prioridad para lo que está al aire y después preview; a
  igual prioridad, la más grande.
- **barato:** lo que se ve grande y no tiene turno.
- **reposo:** lo que no se ve, o se ve más chico que la cámara.
- Como la red se prende al pasar a preview, la transición ocurre fuera del aire.

**Compensación única:** todas las entradas usan la mayor demora del estudio. El piso es la latencia que declara
cada cadena: una red en reposo declara la demora que tiene activa, así prenderla no mueve el audio. Las cámaras
quedan alineadas entre sí.

Probado en vivo:
- Con «Comparación IA» al aire, sus 3 cámaras se ven agrandadas x1,88 en pantalla → activo; las demás, en reposo.
- Al armar «Comparación», sus cámaras pasan a barato.
- Con el take se intercambian: las nuevas al aire pasan a activo, una de preview toma el turno libre y el resto
  queda en barato.
- Todo a 30 fps, 1536x1152 fijo, sin cuadros tarde.

## Audio

Los filtros de audio de una entrada (`entrada_filtros_audio`) son elementos de GStreamer en la cadena de su
audio. Por ejemplo, `baplugin medio=audio` de un plugin de Python, `audiodynamic` o `audioecho`. `baplugin`
declara su demora (LATENCY = `espera-ms`), y el colchón compensa la demora real.

## Laboratorio (`assets/superres/laboratorio.py`)

- **`instalar`.** Baja la receta (sha256), exporta a ONNX, a ONNX NV12 (todo en la placa) y, en la Mac, a
  CoreML. En CoreML compara FP16 contra PyTorch y, si se aleja, pasa a FP32.
- **`chica`.** Red propia luma x2, entrenada con la cámara.
- **`destilar`.** Red propia chica que imita a un maestro (Real-ESRGAN…) en lo que ve la cámara. Con `--cpu`,
  no usa la GPU.
- **`runtime_gpu.py`.** El ONNX Runtime de GPU, con versiones fijas de PyPI verificadas por sha256.

## Variantes por resolución (`assets/superres/variantes.py`)

Una red instalada sirve para cualquier tamaño, pero los aceleradores van mejor (o solo andan) con el tamaño
fijo: el Neural Engine corre CoreML solo para tamaños compilados, y TensorRT y DirectML optimizan para una forma.
Una **variante** es la misma red compilada para un tamaño de entrada, en `<plugin>/variantes/<id>/`:

- `modelo.mlmodelc` (Mac), sacado de `modelo.mlpackage` (lo guarda `instalar` desde esta versión: un plugin
  instalado antes hay que reinstalarlo para tener variantes CoreML);
- `modelo.onnx` / `modelo-nv12.onnx` con las dimensiones fijas (onnx deduce las de adentro y las de salida);
- `variante.json`: `id`, `plugin`, `nombre`, `descripcion`, `para` (qué cámara o uso), `entrada` (ancho, alto,
  formatos, color), `red` (lo que entra a la red: la mitad en una x4, escala, arquitectura), `salida`, `archivos`,
  `medido` (CoreML: ms y acelerador), `creada`, `herramienta` (con las versiones de coremltools y onnx), `licencia`.

No hace falta PyTorch: corre con el Python del programa, que trae coremltools (Mac), onnx y pillow
(`tools/deps/python.sh`). Puede haber muchas por plugin. La app arma la propiedad `variantes`
(`"1024x576=/ruta;…"`) de `basuperres`, `bacoremlsr` y `baonnxsr`, y el motor usa la del tamaño que le llega
(si no hay, la general). Si llega un tamaño sin variante, CoreML agranda común y lo avisa en `error`.
Los shaders GLSL no llevan variantes: la placa los compila para cualquier tamaño.

## En la interfaz

- **Tarjeta «Superresolución»** (Video, al seleccionarla):
  - el método;
  - instalar;
  - nitidez de FSR;
  - **dónde corre una red CoreML** (Neural Engine, GPU, CPU);
  - «Destilar una red chica de esta…»;
  - «Red propia, placa y plugins…».
- **Panel de superresolución** (ese botón, o el menú ✦):
  - entradas;
  - entrenar la red propia;
  - **placa de video**: detectada, runtime de GPU, bajar con o sin TensorRT;
  - **destilar**: entrada, maestro, minutos, CPU o GPU, aplicar;
  - **variantes por resolución**: la red, las cámaras sin variante y «Crear las que faltan», otro tamaño a mano
    (con «para qué» y qué motores), y cada variante con lo medido, su «para qué» editable y ✕;
  - **plugins propios**: crear, editar con las propiedades del elemento, borrar, ver si andan;
  - modelos.
- **Panel de sincronía**: cada entrada de GStreamer muestra lo que corre en su cadena (con la demora de cada filtro
  y la compensación) y sus **filtros de audio**:
  - «+ filtro…» abre el editor: un módulo de GStreamer con sus propiedades, o una tarjeta de audio de un plugin de
    Python con sus parámetros;
  - tocar un filtro lo edita, ‹ › lo mueve y ✕ lo saca;
  - «Rápidos…»: compresor, pasa altos, eco y volumen.

## Herramientas MCP

| Herramienta | Para qué |
|---|---|
| `superres_plugins`, `superres_instalar`, `superres_tarjeta {plugin}` | Elegir e instalar métodos y ponerlos en una tarjeta. |
| `superres_runtime_gpu`, `superres_runtime_gpu_instalar` | Ver la placa y bajar su runtime. |
| `superres_destilar` | Destilar una red. |
| `superres_plugin_elemento`, `superres_plugin_borrar` | Crear y borrar plugins propios. |
| `superres_variantes`, `superres_variante_crear`, `superres_variante_editar`, `superres_variante_borrar` | Variantes de una red por tamaño de entrada (`faltantes: true`: las cámaras que no la tienen). |
| `entrada_filtros_audio` | Los filtros de audio de una entrada. |
| `entrada_control` | Evento `ba/control` por la cadena de una entrada. |
| `superres_programador` | Ver y configurar el programador. |
| `video_estado` | Filtros, demoras y compensación de cada entrada. |

## Probado y sin probar

**Probado en la Mac:**

| Qué | Resultado |
|---|---|
| Filtros en la entrada | Color, recorte y croma andando. |
| CoreML sin copias | Cámara real, 100 % de los cuadros como IOSurface. |
| Dos redes a la vez | 30 fps de salida. |
| Contrato `elemento` | `baglsl` en GLMemory a 1536x1152 y 31 fps. |
| Filtro de audio en la cadena | Andando. |
| FSRCNNX y Anime4K | Andando. |
| ONNX NV12 de punta a punta | Colores bien. |
| SPAN en FP32 | 59 dB contra PyTorch. |
| Destilación | Prueba de humo. |

**Probado en Windows con AMD (Ryzen 9 5900X + Radeon RX 5700 XT, 2026-10-07):** lo que tarda la RED por
cuadro (el resumen de `baonnxsr` al parar; medir el tiempo total de la cadena engaña, porque el elemento
entrega el cuadro anterior cuando la red no llega):

| Red | Acelerador | Entrada | ms por cuadro |
|---|---|---|---|
| SPAN x2 | DirectML | 960×540 | 86,5 (fp16: 84,8) |
| SPAN x2 | DirectML | 1280×720 | 147 |
| general-x4v3 (a la mitad) | DirectML | 1280×720 | 80 (fp16: 79,6) |
| general-x4v3 (a la mitad) | DirectML | 1920×1080 | 172 (fp16: 170) |
| SPAN x2 | Vulkan (ncnn 20231027) | 960×540 | 97 |
| general-x4v3 | Vulkan (ncnn) | 1920×1080 | 207 |
| SPAN x2 | CPU | 960×540 | no termina 300 cuadros en 10 min |
| FSR (`bafsr`, sin red) | GL | 960×540 / 1280×720 / 1920×1080 | 2,9 / 5,2 / 11,1 (con la ida y vuelta a la placa) |

Conclusiones para esta placa (RDNA 1, sin núcleos de IA):
- Ninguna red llega a tiempo real (33 ms a 30 fps); fp16 no cambia nada en DirectML. Sirven para lo que no es en
  vivo (grabaciones, repeticiones) o para un plano fijo a pocos cuadros por segundo; en vivo, FSR o los shaders
  GLSL, o una red propia chica destilada para la cámara.
- **D3D12 sin copias (DirectML leyendo la textura de la placa) no vale la pena acá:** la copia de ida y vuelta
  cuesta unos pocos ms (FSR entero, con copias, tarda 11 ms a 1080p) contra 80–170 ms de red, y DirectML lee
  buffers, no texturas: haría falta un shader de cómputo propio para pasar de textura a tensor y otro de vuelta,
  más la sincronización entre las dos colas de la placa. Queda para una placa que corra la red en menos de 20 ms.
- `test_eventos` (todos los elementos, también las variantes GL y `basalida`) pasa en esta PC.

**Compilado pero sin probar (no hay placas acá):**
- NVIDIA: CUDA/TensorRT, CUDAMemory y el runtime de GPU.
- AMD en Linux: ROCm y MIGraphX.
- Intel: OpenVINO.
