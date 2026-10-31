## 开始一档节目
1. `status`、`foto_todas_escenas`。
2. `scene_switch` 到开场场景；用 `foto_programa` 确认。
3. `record_start`（备份）和 `stream_start`。

## 两位嘉宾的访谈
1. `room_status` —— 确认他们已准入（slot 1 和 2）。
2. `scene_define`，用 `ba_guest_video` slot 1 `left` 和 slot 2 `right`，再加一个字幕条。
3. 对该场景执行 `foto_fuente` -> `scene_switch`。

## 显示发言人姓名的字幕条
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}`，然后用 `foto_programa` 检查。

## 发言人轮次
`speakers_config {"enabled": true, "defaultTime": "2:00"}`；准入；`speaker_next`；关注 `remotos_fila_ver`；当 `remainingSec` < 0 时，给发言人 `chat_send` 或 `speaker_next`。

## 结束
`scene_switch` 到结束场景，`stream_stop`、`record_stop`。
