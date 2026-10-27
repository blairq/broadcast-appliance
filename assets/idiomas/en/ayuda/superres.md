**It is a CARD in the video graph** (a GStreamer filter that upscales x2), on any input: `superres_tarjeta {"entrada", "motor"}`. What follows it works at double size and the scene receives it large: it comes out at twice what goes in (an SD 720x576 comes out at 1440x1152). On a network input it can also be put on the source itself (`superres_aplicar`), before the graph. Audio doesn't go through it; sync holds as long as what it takes fits in the input's buffer (`latencia_ms`): if `superres.ms_por_cuadro` (video_estado) gets close to the buffer, raise it (video_entrada_red `latencia_ms`).

## Engines (`superres_estado` tells which ones there are)
| Engine | What it is | Cost | When |
|---|---|---|---|
| `apple` | Apple's super-resolution (VideoToolbox, macOS 26), input up to the system's maximum (today 960x960) | ~3 ms | On a Mac: the first choice |
| `fsr` | AMD's FSR 1 (EASU + RCAS), no network, on OpenGL | <1 ms on GPU | Any GPU (Windows, Linux) or as a base |
| `onnx` + small network | A luma network trained with THE camera (`superres_entrenar`) | ~14 ms with CoreML; ~65 ms CPU only | When the camera has its own compression or noise |
| `onnx` + SPAN | SPAN x2 (Apache), faithful | ~140 ms on a Mac | Tests / powerful GPU |
| `onnx` + Real-ESRGAN | Compact x4 (BSD), cleans a lot | ~290 ms on a Mac | Not fast enough live here; for comparison |

## Recipes
- An old camera on a Mac: `superres_tarjeta {"entrada": "Cámara 1", "motor": "apple"}`.
- Try FSR: `superres_tarjeta {"entrada", "motor": "fsr"}`.
- A network on a camera with an odd size (CoreML upscales plainly and reports it in `error`): `superres_variante_crear {"plugin", "faltantes": true}` compiles the network for the size of each camera that doesn't have its own (no PyTorch; superres_estado shows the progress); `superres_variantes {"plugin"}` lists them with their profile (what for, sizes, measured ms).
- Train with one button: `superres_entrenar {"entrada": "Cámara 1", "minutos": 5}` (records ~1 minute from the camera without touching the air, trains and applies it; says how many dB it improves over plain upscaling). The first time it downloads the lab (PyTorch, ~800 MB, once). Training again continues from the previous network.
- SPAN or Real-ESRGAN: `superres_modelo {"cual": "span"}` and then `superres_tarjeta {"entrada", "motor": "onnx", "modelo": "span-x2.onnx"}`.
- Remove it: `superres_aplicar {"entrada", "motor": ""}`.
- After applying, a few seconds later `video_estado`: the new `tamaño` and `superres.ms_por_cuadro`. If it goes over ~3/4 of a canvas frame's duration (~25 ms at 30 fps, ~12 ms at 60), the input drops frames: use a lighter engine.
