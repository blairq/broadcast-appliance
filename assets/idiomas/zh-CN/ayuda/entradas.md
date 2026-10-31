## 硬件（连接在本机上）
1. `video_devices` -> 选择 `device`；`video_devices {"device": ...}` 显示格式/分辨率/fps。
2. `video_input_add {"device": ..., "scene": ..., "layout": "full", "resolution": ..., "framerate": ...}`。
3. 用 `foto_fuente` 检查。同一台摄像机不会被打开两次：如果它已经是另一个场景中的来源，就用 `crear_fuente` 按名称添加（变成共享的）。

## NDI
- `ndi_sources` 在网络上查找 NDI 发送端（mDNS），`ndi_input_add {"ndiName": ...}`（或 `host` + `port`）添加它们。
- 用 libNoIDea（VideoLAN，实验性）进行**开源**接收：完整 NDI（SpeedHQ，含音频）；不支持 NDI|HX（H.264/H.265）。如果没有画面，`input_status` 会说明原因（未找到、无法连接、已断开），并自动重试。

## 网络
- 网络摄像机：`network_input_add {"url": "rtsp://usuario:clave@192.168.1.50:554/stream1"}`（RTSP 走 TCP；摄像机需要时用 `rtspTransport: "udp"`）。
- 其他：`rtmp://`、`srt://ip:puerto?mode=caller`、`https://.../playlist.m3u8`（HLS）、`udp://@:5000`。
- 返回是否有画面及其尺寸。`input_status` 显示所有输入的状态（不含密钥）。
- 默认低延迟；信号不稳定时用 `latency: "normal"`（更多缓冲）。会自动重连。
