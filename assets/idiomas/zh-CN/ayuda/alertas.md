## 用途
与其时不时去查询（mixer_list、remotos_fila_ver……），不如让服务器来盯着：它每 250 ms 检查一次，一旦发生情况就**在聊天中通知你**（“助手”选项卡）和/或显示在屏幕上，还可以**自行执行操作**（操作 = MCP 工具）。

## 创建
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`。类型和参数：`alert_types`。

| 类型 | 监视什么 |
|---|---|
| `audio_peak` | 某个通道（或全部，`source: "*"`）超过阈值（`thresholdDb`，-1） |
| `audio_silence` | 某个通道静音超过 `forSec`（仅在播/录制时） |
| `media_end` | 视频/曲目播完（`beforeSec` 提前通知） |
| `speaker_time` | 发言人还剩 `remainingSec`（0 = 时间到） |
| `lobby` / `hand` / `guest_drop` | 有人在等候室等待 / 举手 / 断开连接 |
| `chat` | 收到一条消息（平台、`contains` 或 `regex`） |
| `guest_message` | 嘉宾在会议聊天中发言（`privateOnly`） |
| `stream_health` | 推流丢帧或中断 |
| `disk` | 录制时磁盘空间不足 |
| `timer` | `in` 秒后、在 `at` "HH:MM" 时，或每隔 `every` 秒 |

## 示例
- 防止削波：`{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}`，收到通知后用 `audio_set` 调低 3 dB。
- 音乐播完后切回摄像机：`{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`。
- 提醒发言人：`{"type":"speaker_time","params":{"remainingSec":30}}`，然后给发言人 `chat_send`。
- 提醒：`{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`。

## 管理
`alert_list`、`alert_update {"id","enabled":false}`、`alert_delete`、`alert_events`（历史）、`alert_wait`（给无法在聊天中收到通知的客户端用：一有通知就返回）。触发器会被保存（Config/alertas.json）。
