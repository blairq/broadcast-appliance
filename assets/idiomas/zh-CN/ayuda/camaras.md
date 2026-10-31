每个视频输入都有 PTZ：**数字** PTZ（在 GPU 上裁切和缩放，任何摄像机都可以），如果是电动摄像机，还有摄像机本身的 PTZ（后端 VISCA、ONVIF 或 Panasonic）。`ptz_camaras` 说明每台有什么、能做什么。

## 移动
- `ptz_mover {"entrada": ..., "pan": 0.3, "duracion_ms": 1000}`：速度 -1..1（pan + 向右，tilt + 向上，zoom + 拉近）；结束时自动停止。它移动的是其 `modo` 所指的对象（optico = 云台，digital = 裁切区域）。
- 精确的数字构图：`ptz_encuadre {"entrada": ..., "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1}`（x、y = 中心，0..1；zoom 1..8）。用 `foto_fuente` 查看结果。
- 预置位：`ptz_preset {"entrada": ..., "numero": 2}`（调用）或加 `"accion": "guardar"`。

## 命名的位置
1. 构图（ptz_mover / ptz_encuadre），然后 `ptz_posicion_crear {"entrada": ..., "nombre": "Atril", "comentario": ...}`：保存数字构图、摄像机的预置位记忆以及定义。
2. `ptz_ir_posicion {"entrada": ..., "posicion": "Atril"}`（id 或名称）把它带到那里。
3. `ptz_posiciones` 列出它们（带 `miniatura_ruta`：照片）；`ptz_posicion_foto` 显示照片；`ptz_posicion_editar`（名称、备注、`velocidad` 0..1、`segundos`、`regrabar`、`miniatura`）和 `ptz_posicion_borrar`。
4. 会场（席位网格）：`recinto_ver`、`recinto_editar {filas, columnas, auto | mover | bancas}` 和 `recinto_apuntar {banca}`（让摄像机对准席位的镜头，但不切到在播）；`enfocar {que}` 用口头方式做同样的事（席位、人或位置）。

## 配置电动摄像机
1. ONVIF：`ptz_buscar` 查找网络上的摄像机；`ptz_configurar {"entrada": ..., "backend": "onvif", "host": <direccion>, "usuario": ..., "clave": ...}`。
2. VISCA（Sony、PTZOptics、BirdDog……）：`"backend": "visca", "host": ip, "transporte": "sony"`（UDP 52381）或 `"tcp"`（5678）或 `"udp"`（1259）。
3. Panasonic AW/UE：`"backend": "panasonic", "host": ip`。
4. `ptz_camaras` 显示它是否响应（`listo`、`estado`）。同时有摄像机 PTZ 和数字 PTZ 时，`"modo": "digital"` 让摇杆移动裁切区域。

## 改善画质
`video_mejora {"activa": true}`（全局：降噪、随亮度调整的色彩、锐度；0-100），按摄像机设置用 `ptz_configurar {"entrada": ..., "mejora": "si"|"no"|"general"}`。
