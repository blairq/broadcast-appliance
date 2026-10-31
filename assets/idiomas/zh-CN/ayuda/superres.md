**它是视频图中的一个卡片**（一个放大 x2 的 GStreamer 滤镜），可用于任何输入：`superres_tarjeta {"entrada", "motor"}`。它后面的处理都以两倍尺寸进行，场景收到的是放大后的画面：输出是输入的两倍（720x576 的 SD 输出 1440x1152）。对于网络输入，也可以直接放在来源本身上（`superres_aplicar`），在进入视频图之前。音频不经过它；只要处理耗时在输入的缓冲（`latencia_ms`）之内，同步就能保持：如果 `superres.ms_por_cuadro`（video_estado）接近缓冲值，就把它调大（video_entrada_red 的 `latencia_ms`）。

## 引擎（`superres_estado` 列出可用的）
| 引擎 | 是什么 | 开销 | 何时用 |
|---|---|---|---|
| `apple` | Apple 的超分辨率（VideoToolbox，macOS 26），输入最大为系统上限（目前 960x960） | 约 3 ms | 在 Mac 上：首选 |
| `fsr` | AMD 的 FSR 1（EASU + RCAS），不用模型，基于 OpenGL | GPU 上 <1 ms | 任何 GPU（Windows、Linux），或作为基础方案 |
| `onnx` + 小模型 | 用**该**摄像机训练的亮度模型（`superres_entrenar`） | CoreML 约 14 ms；仅 CPU 约 65 ms | 摄像机有自身的压缩或噪声时 |
| `onnx` + SPAN | SPAN x2（Apache），忠实还原 | Mac 上约 140 ms | 测试 / 强力 GPU |
| `onnx` + Real-ESRGAN | 紧凑型 x4（BSD），去噪很强 | Mac 上约 290 ms | 在这里跑不了直播；用于对比 |

## 做法
- Mac 上的一台旧摄像机：`superres_tarjeta {"entrada": "Cámara 1", "motor": "apple"}`。
- 试用 FSR：`superres_tarjeta {"entrada", "motor": "fsr"}`。
- 在尺寸特殊的摄像机上用模型（CoreML 会改用普通放大，并在 `error` 中提示）：`superres_variante_crear {"plugin", "faltantes": true}` 为每台还没有对应模型的摄像机按其尺寸编译模型（不需要 PyTorch；superres_estado 显示进度）；`superres_variantes {"plugin"}` 列出它们及其参考卡（用途、尺寸、实测 ms）。
- 一键训练：`superres_entrenar {"entrada": "Cámara 1", "minutos": 5}`（在不影响在播的情况下录制该摄像机约 1 分钟，训练并应用；会说明相对普通放大提升了多少 dB）。第一次会下载训练环境（PyTorch，约 800 MB，只下载一次）。再次训练会在之前的模型基础上继续。
- SPAN 或 Real-ESRGAN：`superres_modelo {"cual": "span"}`，然后 `superres_tarjeta {"entrada", "motor": "onnx", "modelo": "span-x2.onnx"}`。
- 移除：`superres_aplicar {"entrada", "motor": ""}`。
- 应用之后，过几秒查看 `video_estado`：新的 `tamaño` 和 `superres.ms_por_cuadro`。如果超过画布一帧时长的约 3/4（30 fps 时约 25 ms，60 fps 时约 12 ms），该输入就会丢帧：换一个更轻的引擎。
