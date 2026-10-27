**Es una TARJETA del grafo de video** (un filtro de GStreamer que agranda x2), en cualquier entrada: `superres_tarjeta {"entrada", "motor"}`. Lo que la sigue trabaja al doble y la escena la recibe grande: sale al doble de lo que entra (una SD de 720x576 sale en 1440x1152). En una entrada de red también se puede poner en la fuente misma (`superres_aplicar`), antes del grafo. El audio no pasa por ahí; la sincronía se mantiene mientras lo que tarda entre en el colchón de la entrada (`latencia_ms`): si `superres.ms_por_cuadro` (video_estado) se acerca al colchón, subilo (video_entrada_red `latencia_ms`).

## Motores (`superres_estado` dice cuáles hay)
| Motor | Qué es | Costo | Cuándo |
|---|---|---|---|
| `apple` | La superresolución de Apple (VideoToolbox, macOS 26), entrada hasta el máximo del sistema (hoy 960x960) | ~3 ms | En una Mac: la primera opción |
| `fsr` | FSR 1 de AMD (EASU + RCAS), sin red, en OpenGL | <1 ms en GPU | Cualquier GPU (Windows, Linux) o como base |
| `onnx` + red chica | Una red de luma entrenada con LA cámara (`superres_entrenar`) | ~14 ms con CoreML; ~65 ms solo CPU | Cuando la cámara tiene compresión o ruido propios |
| `onnx` + SPAN | SPAN x2 (Apache), fiel | ~140 ms en una Mac | Pruebas / GPU potente |
| `onnx` + Real-ESRGAN | Compacto x4 (BSD), limpia mucho | ~290 ms en una Mac | No da en vivo aquí; para comparar |

## Recetas
- Una cámara vieja en una Mac: `superres_tarjeta {"entrada": "Cámara 1", "motor": "apple"}`.
- Probar FSR: `superres_tarjeta {"entrada", "motor": "fsr"}`.
- Una red en una cámara de un tamaño raro (CoreML agranda común y lo avisa en `error`): `superres_variante_crear {"plugin", "faltantes": true}` compila la red para el tamaño de cada cámara que no tiene la suya (sin PyTorch; superres_estado muestra cómo va); `superres_variantes {"plugin"}` las lista con su ficha (para qué, tamaños, ms medidos).
- Entrenar con un botón: `superres_entrenar {"entrada": "Cámara 1", "minutos": 5}` (graba ~1 minuto de la cámara sin tocar el aire, entrena y la aplica; dice cuánto mejora en dB contra el agrandado común). La primera vez baja el laboratorio (PyTorch, ~800 MB, una vez). Volver a entrenar sigue desde la red anterior.
- SPAN o Real-ESRGAN: `superres_modelo {"cual": "span"}` y después `superres_tarjeta {"entrada", "motor": "onnx", "modelo": "span-x2.onnx"}`.
- Sacarla: `superres_aplicar {"entrada", "motor": ""}`.
- Después de aplicar, a los pocos segundos `video_estado`: el `tamaño` nuevo y `superres.ms_por_cuadro`. Si pasa de ~3/4 de lo que dura un cuadro del lienzo (~25 ms a 30 fps, ~12 ms a 60), la entrada pierde cuadros: usá un motor más liviano.