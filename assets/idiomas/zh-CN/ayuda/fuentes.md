## 类型
`source_types` 列出本安装中的类型。最常用的：
| type | 常用 settings |
|---|---|
| `image_source` | `file`（路径） |
| `ffmpeg_source` | `local_file`、`looping`、`restart_on_activate` |
| `color_source_v3` | `color`（ABGR 整数：0xAABBGGRR，例如不透明红色 = 4278190335）、`width`、`height` |
| `text_ft2_source_v2` | `text`、`font` {face, size}、`color1`、`color2`、`outline`、`drop_shadow` |
| `v4l2_input`（Linux） | 摄像机：`device_id`、分辨率（见 source_properties） |
| `macos-avcapture`（macOS） | 摄像机：`device`（见 source_properties） |
| `coreaudio_output_capture`（macOS） | 系统音频：`device_id` |
| 麦克风（所有系统） | 通过 GStreamer 输入：`audio_input_add`（不是 `crear_fuente`）；`ba_gst_fuente` 配合 `pipeline_audio` 和 `microfono`。滤镜：`entrada_filtros_audio`；圆桌讨论：`automixer` |
| `screen_capture` | 一个屏幕：`monitor`（它的名称：选项见 source_properties；留空 = 主屏幕）、`show_cursor` |
| `ba_qr` | 场景中的二维码：`qr` = \"aparato:publico\"（观众页面，带其密码）、\"aparato:sala\"、\"aparato:prompter\"、\"aparato:rotulos\" 或某个已保存二维码的 id（qr_listar）；留空 = `texto`。`fondo`（白色边距）、`tamano`。会自动保持更新 |
| `window_capture` | 某个应用的窗口，即使被遮挡：`ventana` = "aplicacion\ntitulo"（已打开的窗口见 source_properties）、`show_cursor`。会记住应用：关闭再打开后自动继续。在 Mac 上需要屏幕录制权限 |
| `dshow_input`（Windows） | 摄像机 |
| `ba_guest_video` | 嘉宾：`slot`（1..N），`channel` 为 guest（摄像头）或 share（共享屏幕） |
| `ba_grilla` | 人员网格：一个随会议中已准入的人和大会中已准许的成员自动增长的拼贴画面。`origen`（sala/asamblea/ambas）、`orden`（llegada/habla/prioridad）、`tope`（带“+N”）、`grupo`（多个网格分配人员而不重复；`reparto` en_orden/parejo）。已在另一个在播网格中出现的人不会重复。见 grilla_personas_estado 和 grilla_personas_ajustar；手动：grilla_personas_fijar / _sacar / _mover / _devolver |
| `slideshow` | `files`、`slide_time` |
| `ba_chat_overlay` | 叠加在视频上的直播聊天（YouTube、Twitch、Facebook 和/或会议）：`show_youtube`、`show_twitch`、`show_facebook`、`show_room`（或 `channel` all/youtube/twitch/facebook/room）、`max_messages`、`font_size`、`width`、`height`、`bg_opacity`、`show_names`、`fade_sec` |

要查看某个来源的**所有**选项：`source_properties {"name"}`；要修改其中一个：`source_set_property`。`source_update {"settings": {...}}` 会合并键（不删除其他键）。

## 位置（`layout`）
画布的比例：`{"x":0.1,"y":0.1,"w":0.8,"h":0.8}` 或预设：
`full`、`left`、`right`、`top`、`bottom`、`center`、`top-left`、`top-right`、`bottom-left`、`bottom-right`、`pip-tl`、`pip-tr`、`pip-bl`、`pip-br`（该角落的小窗口）、`lower-third`（字幕条）、`third-1..3`、`grid-<n>-<i>`（n 格网格中的第 i 格：`grid-4-1` .. `grid-4-4`）。
来源会在矩形内按原比例缩放。

## 顺序和可见性
`source_update {"name": ..., "order": "top"|"bottom"|"up"|"down"|n}`、`{"visible": false}`（隐藏但不删除：适合提前准备好，在恰当时刻显示）。

## 嘉宾
`guest_source_add {"slot": 1, "kind": "camera"|"screen", "layout": "left"}` 把位置 1 的嘉宾（不论是谁）放进场景。
