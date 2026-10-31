工作室的所有视频共用一个图，就像一条 GStreamer pipeline。每个输入（摄像机、网络、虚拟）是一个子图：`<entrada>.entrada` -> 卡片 -> `<entrada>.escena`（场景看到的内容）。图像卡片运行在每个输入的 GStreamer 链中；引擎还没有运行的那些（输出、镜头、合成器）会在其状态中说明。所有输入都跟随工作室的视频时钟运行（`status`：画布和 fps）。

## 工作方法（始终按此顺序）
1. **查看**：动手之前先 `video_estado`（全部输入及其诊断）和 `video_estado {"entrada": ...}`：尺寸、fps、格式、色彩（BT.601/709、范围）、是否隔行、时钟、网络缓冲。`video_grafo` 显示卡片。
2. **搭建**用 `video_pipeline`（每个输入**一次**调用，全部校验，任何一处失败就什么都不改）。单独的工具（`video_tarjeta_agregar`、`_ajustar`、`_quitar`、`video_cable`）用于微调。
3. **确认**：再次 `video_estado {"entrada": ...}`（`diagnostico` 必须没有错误），并用 `foto_fuente {"name": ...}` 查看画面。

## 做法
**多台摄像机使用同一个色彩校正**（改一次，全部跟着变），**一次**调用完成：
`video_pipeline {"entradas": ["Sony", "Pelco", "Panasonic"], "tarjetas": [{"tipo": "gst", "elemento": "videobalance", "propiedades": {"contrast": 1.1, "saturation": 0.9}, "compartida": "Color camaras"}], "local_por_entrada": {"Pelco": {"propiedades": {"hue": 0.04}}}}`。控制器为 `estudio.color-camaras`。如果输入已有需要保留的卡片，用 `modo: "agregar"`（默认会替换）。之后调整共用设置：对任一受控卡片执行 `video_tarjeta_ajustar`（会作用到控制器），或直接对控制器执行。`video_controlador` 用于基于一张已有卡片建立控制器。

**校正一台摄像机**（**始终**先用 GStreamer）：`gst` 卡片配 `videobalance`（亮度、对比度、饱和度、色调）和 `gamma`；偏色（偏绿、偏品红）：小幅调 `hue`（±0.02 到 ±0.08）。只有 GStreamer 不够用时：才用自有的 `color` 卡片。

**隔行 / 声明错误**（`video_estado` 会指出）：`{"tipo": "normalizar", "params": {"desentrelazar": "yadif"}}`（在来源处去隔行，场在那里）；声明为 601 的 HD 色彩偏移：`{"matriz_declarada": "601", "matriz_real": "709"}`。与画布不同的帧率由输入自行转换（重复或跳帧）：不需要卡片。

**拼贴 / 画中画 / 并排**：`video_mosaico {"escena", "entradas": [...], "disposicion": "mosaico|pip|lado", "nombre"}`，**一次**调用（带着每个输入的卡片取用，并在该场景中隐藏单独的输入：拼贴仍会接收它们）。微调：对合成器执行 `video_tarjeta_ajustar`，用 `capas.in_N` {x, y, ancho, alto 0..1, opacidad, orden}。

**拼接摄像机：全景或 360**（全部在 GPU 上）：`video_panoramica {"camaras": [...], "preset": "fila"|"360", "campo", "escena", "nombre"}`，**一次**调用。把最多 8 台摄像机（每台一路流；第一台是镜头 1）在其画布上并排拼接，并校正每台的畸变以及它们如何衔接。`preset: "fila"`：视场为 `campo` 度的普通摄像机，相互重叠 10%；输出为**条带**（柱面全景），尺寸为各分辨率之和（4 台 1080p = 7680x1080；最大宽度 16384）。`preset: "360"`：两个背靠背的 190° 鱼眼；输出为**视图**，即一台作为虚拟 PTZ 的普通摄像机（`yaw`、`pitch`、`fov` = pan、tilt、zoom）。把全部内容放在**一路**流里发送的 360 摄像机（等距柱状投影或两个圆并排）：在其链中加 `vista360` 卡片（`modo`）。
手动校准：对卡片（`estudio.panoramica`）执行 `video_tarjeta_ajustar`，用 `foto_fuente` 查看：
1. `ver: "entrada"`：原始画面，显示鱼眼的圆和每台摄像机的边界；移动 `lenteN_x`、`lenteN_y`、`lenteN_radio`，直到圆对齐。
2. `ver: "tira"`（或 `"panorama"`）配合 `guia: "esfera"`（每 15° 的纬线和经线）：每台如何衔接（`lenteN_yaw`、`_pitch`、`_roll`）以及它的 `_campo`，直到地平线水平、看不出接缝（`mezcla`：接缝的宽度，单位为度）。
3. 在本身是矩形或圆形的物体（一扇门、一个钟）上放一个 `rectangulo` 或 `circulo` 参考框（`guia_x`、`guia_y`、`guia_ancho`、`guia_alto`，以输出的比例表示）：调整畸变（`lenteN_k1`、`_k2`；桶形 < 0 < 枕形）直到吻合。`lenteN_tipo`：`ojo`（鱼眼）或 `normal`。
逐个参数说明：`video_tarjeta_ayuda {"tipo": "panoramica"}`。在界面中：“视频”页面，该卡片，每一步都有快捷方式。

**网络流**（SRT、RTMP、RTSP）：`video_entrada_red {"nombre", "uri", "escena", "latencia_ms"}`（这种情况绝不要用 `network_input_add`：那个不做同步）。每一帧在其采集时间 + 延迟（自身的缓冲）时输出。延迟越大 = 对网络的容忍度越高，延时也越长。

**同步输入**：`video_sincronia {"a": ..., "b": ..., "aplicar": true}` 通过比较画面来测量一个比另一个慢多少（需要有共同的变化内容：同一画面、动作、一次拍手）并进行修正；再测一次应约为 0。手动：`video_desfase {"entrada", "ms"}`。
- RTSP 带有发送端的采集时间（RTCP/NTP）：使用同一个时钟时会自动对齐。
- SRT 和 RTMP 不带：用 `video_sincronia` 测量。
- 本地摄像机只能延后（延时）；要与网络摄像机对齐，就延后本地的，或提前网络的（其偏移量接受负值）。

**时钟**：`video_reloj` 不带参数时显示工作室时钟以及它在每个网络输入上的状态。`{"tipo": "ntp", "servidor": ...}`（全部对同一个 NTP）、`{"tipo": "ptp", "dominio": 0}`（IEEE 1588）、`{"tipo": "red", "servidor": ip, "puerto": 8554}`（**另一台**设备的时钟）、`{"publicar_puerto": 8554}`（发布本机的时钟供他人使用）、`{"latencia_ms": ...}`（所有网络输入的缓冲）。

**GStreamer 混合**（任何多输入模块：`compositor`、`glvideomixer`、`d3d12compositor`、`glmosaic`、`smpte`、`shapewipe`、`glstereomix`；`video_modulos {"soloMezclas": true}` 列出本系统上的）：`video_tarjeta_agregar {"tipo": "mezcla", "elemento": "smpte", "params": {"propiedades": {"type": "bar-wipe-lr"}, "nombre": "Cortina"}}`。它的端口就是模块的端口（`sink_0`、`sink_1`……按需创建，始终有一个空闲；smpte 上是 `sink1`/`sink2`；shapewipe 上是 `video_sink`/`mask_sink`）：从每个输入用 `video_cable` 接到其端口。每个输入的属性（xpos、ypos、width、height、alpha、zorder……）：`video_filtros {"id", "puerto"}` 列出，`video_tarjeta_ajustar {"id", "params": {"pads": {"sink_1": {"xpos": 960}}}}` 修改。它以 `nombre` 作为来源输出（`video_salida_a_escena`）。模块支持时在 GPU 上运行（Windows 上 D3D12，Mac/Linux 上 GL）。

**GStreamer 模块**：`video_modulos`（附有说明的目录，说明用西班牙语编写；`buscar`、`categoria`、`elemento` 及其属性）。只有 `tarjeta: true` 的能放在输入上；`mezcla: true` 的作为 GStreamer 混合使用。4:2:0 或带延迟的模块会由诊断提示。属性用其 GStreamer 名称传递（enum 用其 nick）。

**“一切正常吗？”（全面检查）**：不带参数的 `video_estado`（所有输入及其诊断）-> 应用严重程度为 `aviso` 或 `error` 的修复（每条都附带确切的调用；`info` 只是供参考）-> 如果改动过偏移或时钟，在应该一致的输入之间运行不带 aplicar 的 `video_sincronia`（应约为 0）-> `status`（fps、丢帧、CPU）-> `mixer_list`（它们带了什么音频：在拼贴中被隐藏的输入，如果带音频，在该场景中仍会发声）。“llegaron tarde”（迟到）配合“recien conectada”（刚连接）是启动时的突发：不要调大缓冲，10 秒后再看。

**查看画面**：对场景或来源使用 `imagen_preguntar`，提一个具体问题（“有没有被拉伸或黑掉的部分？”、“每个小框里的钟显示几点？”）。适用于构图、黑场、拉伸和读取文字；**不**适用于比较各小框之间细微的亮度或色彩：那要看数值（`video_grafo`，实际生效的值）。

**人脸**：使用 `entrada:<camara>` 时，“人员”会自动加上 `rostros` 卡片；`marcar` 会把人脸画出来。

**任何模块都做不到的事**（自有效果、少见的滤镜、分析：计数、检测运动、从画面中读取内容）：**不要**说做不到。可以用 Python 写成插件卡片（`baplugin`），内部用 numpy 或 GStreamer（用 Python 编写的自有链和元素）：`help {"topic": "plugins"}` 的“Video”一节，以及模板 `ejemplo-video-gst`。

## 须知
- `gst` 卡片会增加 1 帧延迟（不会拖慢视频）。如果比较两个输入而只有一个有 `gst`，就会差一帧：要考虑到这一点，或用 `video_sincronia` 测量。
- `video_grafo_limpiar {"entrada"}` 把输入恢复成原样。
- 用于测试的虚拟输入（彩条、分区测试卡、小球）：`video_entrada_virtual {"nombre", "preset", "escena"}`。
