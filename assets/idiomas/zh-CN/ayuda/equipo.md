- `system_hardware`：一次列出所有内容（显示器、音频输入和输出接口、摄像机/采集卡、GPU、硬件编码器、CPU、内存、磁盘）。
- `monitors`：按物理顺序排列，带固定的 `id` 和 `orden`（“输出 -> 屏幕”中的编号）、像素分辨率、位置、缩放、Hz 以及是否为主显示器。`output_window.screen` 和 `monitor_asignar` 接受 id 或序号。
- `audio_devices`：来自本机设备登记的音频输入和输出：`name` 是操作员给它起的名字（`tecnico` 是系统名称），`conectado` 表示是否已连接。输入的 `value` 就是 `audio_input_add` 用的 `device`。要命名它们：`equipo_renombrar`。
- `video_devices`：摄像机和采集卡，来自同一份登记（操作员起的名字和技术名称）；加 `{"device": value, id o nombre}` 可查看其格式、分辨率和帧率。

## 做法：用上所有显示器
1. `monitors` 和 `dashboard_list`（已打开的窗口）。
2. 把节目放到**不是**主显示器的最大那台上（主显示器留给程序）：`monitor_asignar {"monitor": "<orden>", "cosas": ["programa"]}`（会固定下来，重启后自动恢复）。
3. 如果还有第三台，放场景或嘉宾的多画面监看（`output_window {"open": ["guest:1","guest:2"], "layout": "grid", "screen": ...}`）。
4. 报告最终效果。
