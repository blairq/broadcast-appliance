## 由外到内
1. `tiempos_mapa`：从每个输入到每个输出，每个节点（输入、卡片、音频模块、场景、混合、输出）增加了多少，以及累计值。`salidas[].camino` 是最慢的路径：从这里开始。
2. `tiempos_pipeline {"entrada"}`：深入查看可疑的输入，逐个元素（需要几秒钟的探测；不影响在播）。先读 `hallazgos`：已经按严重程度分析好，并指出有问题的卡片。
3. `tiempos_plugin {"entrada", "tarjeta"}`：有问题的卡片如何工作（它的内存、属性、文档）以及它在这条链中做了什么。对备选方案用 `tiempos_plugin {"elemento"}` 来比较（`interior.gpu`）。
4. 如果改了什么，再运行一次 `tiempos_pipeline` 并比较。要看随时间的变化：`tiempos_evento_agregar`（一条备注）和 `tiempos_evento_impacto`。

## 如何解读测量结果
- `agrega_ms` = 帧在元素输出端的帧龄减去输入端的帧龄。它既包括**耗时**，也包括**滞留**（保存帧的时间滤镜即使处理很快也会增加延迟）。插件报告的 `ms_por_cuadro` 只是耗时。
- `memoria_*` 是协商的结果；`puede_*` 是它能用的。一次跨越（`baja` 从 GPU 到 RAM，`sube` 从 RAM 到 GPU）会复制整帧：1080p 下每帧约 8 MB，还要更多 CPU 和内存带宽。一次**往返**（下来又上去）几乎总是因为两个在 GPU 上工作的插件之间夹了一个只在 RAM 上工作的插件。
- 如果一个插件只接受“sistema”：它没有 GPU 版本；找另一个（video_modulos，gl/d3d12 变体），或者把它放在链的开头或末尾，那里视频已经在 RAM 中，以免把链切断。
- 如果它 `puede` 用 GPU 却协商成了 RAM：是旁边的东西逼的（相邻的另一个只用 RAM 的插件，或者 GPU 处理不了的格式）。
- `declara_ms` 是元素告诉 pipeline 的值（工作室据此补偿）。`declara_menos`：实际耗时比声明的多；如果差值接近输入的缓冲，帧到达混合时就会迟到 → 调大缓冲（输入的 `latencia_ms`）或换插件。
- `conversion_cpu`：在 RAM 中的 videoconvert/videoscale。1080p 下耗时很多 = 最好让前一个元素直接输出后一个所需的格式。
- `nivel_cola_ms` 很高的队列说明在囤积视频：后面有东西慢了或在等待。

## 分辨率：让每个输入提供画面上实际需要的
`resolucion_plan {"entrada"}` 把输入在哪里显示（它在各场景中的最大尺寸）与每张卡片和摄像机交叉比对：放大已经足够大的画面的超分辨率、放在超分辨率之后的 PTZ、拉伸少量像素的数字变焦、输出过多或过少的摄像机（以及合适的模式）、帧率过低。同样的发现会给卡片着色（黄 / 橙 / 红），显示在“视频”和“延迟”中。每台摄像机的实际模式（原始、MJPEG、H.264 以及由谁解码）：`video_devices {"device"}`；它所声明的一切：`dispositivos_video`。

## 告诉操作员
用简体中文，并说明后果：“运动溶解滤镜滞留了 800 ms（24 帧），而且在 RAM 中工作：迫使视频从 GPU 下来再上去。”提出修改建议，在动在播的卡片之前先等对方同意。
