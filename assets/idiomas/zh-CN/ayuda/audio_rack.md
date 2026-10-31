## 结构
每个带音频的来源（麦克风、采集、视频）都可以有一个 JACK 式的**机架**：`in`（原始来源）-> 用线连接的 LSP 模块 -> `out`（送往混音的内容）。对方在 **音频** 选项卡（跳线盘）中可以看到它；`audio_show {"source"}` 会在该来源上打开它。
- `audio_sources`：带音频的来源及其是否有机架。`audio_rack {"source"}`：节点、连线和电平表。
- `audio_plantillas`：把一个机架以某个名字保存，再套用到另一个来源。为 `ba_guest_audio` 标记为 `porDefecto` 的模板，**新**嘉宾连接时会自动套用：这是在活动开始时、不必逐个设置就能让他们声音好听的唯一办法。
- 每个模块都有自己的电平表：`entradaDb`、`salidaDb` 和 `cambioDb`（**此刻**降低或提升了多少：在门限/压缩器上就是增益衰减量）。
- 空机架不会改变声音。`audio_module_remove {"node": "*"}` 移除整个机架。
- 在选项卡中，每个卡片排列在网格中（“整理”按钮可以全部对齐），点开其箭头可看详情：模块每个端口的真实名称以及是单声道还是立体声。

## 端口和声道（L/R）
一个模块可以有多个输入和输出：`in`（主输入）、`sc`（侧链），如果更多还有 `in2`、`in3`……以及 `out`、`out2`……（`audio_rack` 会在 `entradas`/`salidas` 中列出，并在 `puertosEntrada`/`puertosSalida` 中给出真实名称和各自的声道数）。一个 16 路输入的 LSP 混音器会列出全部 16 路。
- 默认情况下，一根线连接**整个端口**：左对左，右对右。
- 在 `audio_patch` 中加上 `canalSalida` 和 `canalEntrada`（"L" 或 "R"）可以只连接**单个声道**：例如把一个模块的右声道接到另一个模块的左声道，或只用一个声道、另一个保持静音。
- 同样两个端口之间，整端口连线和单声道连线不能共存：后一个会替换前一个（否则音频会被叠加两次）。

## 通过本机的某个输出（耳机）监听
用于检查机架中某一点的效果，而**不**影响在播内容和节目混音。
1. `audio_outputs`：本机的输出，按音频接口分组。
2. `audio_output_set {"source", "salida": <id>}`：在该来源的图中添加一个以该输出命名的卡片。
3. `audio_patch {"source", "conectar": [{"desde": "in", "hacia": "salida-audio"}]}`：到这一步才能听到（可以接任意模块的输出，不只是 `in`）。
注意：引擎**一次只能**通过一个输出播放；在另一个来源上选了别的输出，所有来源的输出都会随之改变。

## 本机的麦克风和音频输入
它们不会自动出现：需要添加。`audio_devices` 列出它们（`inputs`），`audio_input_add {"device"}` 添加。它进入一个**全局**通道（像嘉宾的音频一样）：在**所有**场景中都有声音，不是场景中的项目，并会保存到下次启动。之后像任何来源一样用机架处理。

## 黄金法则：测量、修改、再测量
1. `audio_analyze {"source", "punto": "in", "segundos": 5}`：峰值、RMS、LUFS、`pisoDeRuidoDbfs`、`nivelActivoDbfs`、`rangoDinamicoDb`、1/3 倍频程频谱和分区频谱（低频、厚度、中频、临场感、齿音、空气感）、削波以及 `pistas`。如果显示没有音频，说明来源静音、未激活或没人在说话：请对方说话（`operator_ask`）。
2. 用有名称的工具修改（数值用真实单位：dB、ms、Hz）。
3. 确认：`audio_analyze {"punto": "out"}` 和/或 `audio_measure`（离线副本，不影响在播）。
把你测了什么、改了什么，连同数字一起告诉对方。

## 典型校准（人声）
- **噪声门**（`audio_gate`）：测**两次**，一次对方不说话（底噪），一次说话（有效电平）。`umbral_db` = 底噪 + 6 到 10 dB，并始终远低于有效电平。`reduccion_db` -20 到 -40（不需要完全静音），`ataque_ms` 1-5，`sosten_ms` 50-100，`liberacion_ms` 100-250。用 `audio_measure {"tipo": "curva"}` 确认（在哪里打开），并听一听有没有切掉字尾。如果底噪和人声相差不到约 15 dB，噪声门就不够用了：建议把麦克风靠近些，或调低输入增益。
- **压缩器**（`audio_compressor`）：预设 `voz`，调整 `umbral_db`，让 `cambioDb` 在大声的部分显示 -3 到 -6 dB（而不是一直如此）。`ratio` 2-4，`ataque_ms` 5-20，`liberacion_ms` 80-200，用 `ganancia_db` 补回电平。
- **去齿音器**（`audio_deesser`）：看分析结果中的 `sibilancia` 区（5-9 kHz）。`deteccion_hz` 设在齿音的峰值处（通常 5-7 kHz），`umbral_db` 让它只在齿音处降低 3-6 dB。`escuchar: true` 可以听到它检测到的内容（仅用于调整：记得关掉）。
- **均衡器**（`audio_eq`，按 `bandas`）：人声用 70-100 Hz 高通；如果 `cuerpo` 区（200-400 Hz）突出，用钟形 -2 到 -4 dB；如果不够清晰，在 3-5 kHz 提升 +1 到 +3 dB。改动要小。用 `audio_measure {"tipo": "respuesta"}` 确认。
- **多段压缩**（`audio_multiband`）：当单个压缩器出现抽吸感，或问题集中在某一区（低频轰鸣、高频刺耳）时使用。按频段设置 `corte_hz` 和阈值。
- **限幅器**（`audio_limiter`，预设 `seguridad`）：放在链的末端，上限 -1 dBFS。它是安全网，不是用来提音量的。
- 常见顺序：高通 EQ -> 噪声门 -> 压缩器 -> 去齿音器 -> 精细 EQ -> 限幅器。`audio_patch` 重新连线；`audio_bypass` 对比开/关效果。

## 响度
推流时，混音的综合响度目标为 -16 到 -14 LUFS（YouTube/Twitch 会标准化到 -14 左右）。单独的人声：`out` 上 -18 到 -16 LUFS。碰到 0 dBFS 的峰值（`recortes`）要在输入增益上解决，而不是靠限幅器。

## 信号发生器和其他模块
- `audio_generator {"senal": "rosa"|"seno"|"barrido"|"blanco", "nivel_db", "conectar_a"}` 创建一个**独立的**测试信号（例如接到 `audio_module_add {"module": "analizador"}` 的分析器）。把它接到在播内容需要 `al_aire: true`：事先与对方确认。
- `audio_modules {"buscar"}` 列出约 200 个 LSP 插件；`audio_module_add` 添加任意一个，`audio_module_get`/`audio_module_set` 读取和修改其全部参数。
- `audio_editor` 打开 LSP 模块原本的图形界面（给对方用）。
