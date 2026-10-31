用于“有哪些摄像机？”、“把院子里的摄像机加进来”、“连上 DVR”、“我有一台 Axis/Hikvision/EZVIZ……”。

## 做法（始终如此）
1. `red_escanear`（约 10 秒）：每台设备的 IP、MAC 和厂商、品牌（以及识别依据）、端口、已测试的常见 RTSP URL（`se ve` / `pide usuario y clave` / `no existe`）、该品牌的 `notas` 以及 `siguiente_paso`。**按 `siguiente_paso` 去做。** 用几行话告诉操作员你发现了什么（几台摄像机、什么品牌、哪些已经能用、哪些需要密码），不要把 JSON 甩给他。
<!-- «[OPERADOR]»: no se traduce (es una marca fija que pone o lee el programa). -->
2. 如果需要密码：`camara_credenciales {"ips": [...], "pedir": true}`（所有需要密码的摄像机共用**一个**表单；如果已经有一个打开着，就加到那个里面。它会立即返回：操作员填完后，聊天中会收到“[OPERADOR] guardo…”；在此期间，继续做别的事）。这是底部栏上的安全表单：**绝不要通过聊天向他要密码**。事先告诉他用户名是什么（`usuario_de_fabrica`），以及密码从哪里来（见 `notas`：EZVIZ = 标签上的验证码，并且需要在其应用中启用 RTSP；Hikvision = 录像机的密码；Axis = 某个 ONVIF 用户的密码）。
3. `camara_ficha {"ip"}`：完整的 ONVIF 信息（带确切 URL 的配置文件、PTZ、图像）+ GStreamer 对流的分析 + `plan`。用两行话把计划读给操作员听。
4. `camara_agregar {"ip", "escena", "nombre"}`，使用 `plan.argumentos`（如果已连接，`reusar: true` 会把它加到另一个场景而不另开一路流；如果 ONVIF 密码后来才拿到，`solo_ptz: true` + `entrada` 会为其配置 PTZ）：该输入会被同步（工作室的缓冲和时钟），只在需要时做标准化，如有 ONVIF PTZ 也会一并配置。如果没人告诉你放到哪个场景，就问一下（不问就别放到在播的场景里）。它会等待连接并返回 `estado.diagnostico`：按其建议操作（例如摄像机时钟不准时用 `video_entrada_red {"nombre", "ntp_sync": false}`；对已有输入执行 `video_entrada_red` 会更新它）。
5. 几秒后 `video_estado {"entrada"}`：实际分辨率和诊断。如果还有问题，诊断会给出修复方法。

## 须知
- **录像机**（DVR/NVR）每个通道一台摄像机：`/Streaming/Channels/101` 是通道 1（主码流），`102` 是子码流，`201` 是通道 2……每个通道单独添加（`camara_agregar` 带该通道的 `url`）。
- 如果品牌只是根据 MAC 识别的，可信度较低：如果所有 URL 都不行，就向操作员要型号或 URL。
- EZVIZ 使用 Hikvision 的 MAC：以 ONVIF 名称（CS-…）为准。
- `camara_urls {"ip"}` 重新测试 URL（在保存密码或启用 RTSP 之后）。
- 没有 RTSP 也没有 ONVIF 的设备（路由器、电脑）不是摄像机：不要推荐它们。
- 如果别人给了你 URL，也可以用 `video_entrada_red` 手动添加网络摄像机。
