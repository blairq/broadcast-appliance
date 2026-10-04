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
| `entrada_filtros_audio` | Los filtros de audio de una entrada. |
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

**Compilado pero sin probar (no hay placas acá):**
- NVIDIA: CUDA/TensorRT, CUDAMemory y el runtime de GPU.
- AMD: DirectML, ROCm y MIGraphX.
- Intel: OpenVINO.
- D3D11/D3D12 en Windows.
