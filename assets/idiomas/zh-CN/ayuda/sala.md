## 人员
`room_status` 列出所有人：`state` 等待中（等候室）或已准入，`slot`（在节目中的位置，0 = 无），`lobbySlot`，`speaker`（发言人模式），麦克风/摄像头/屏幕/举手，连接状况，信息和联系方式。

## 等候室
- 私密：等待的人只能看到你（你用 `operator_feed` 选择的画面），永远看不到其他人。
- `participant_admit`、`participant_admit_all`、`participant_reject`。
- `participant_talk {"on": true}`：私下对话（只有那个人能听到你；你通过监听听到他）。

## 已进入
- 其摄像头就是来源 `Invitado N`（N = slot）。`guest_source_add` 把它放进场景。
- `participant_request {"what": "mute-mic"|"mute-cam"|"unmute-mic"|"unmute-cam"|"stop-screen"|"lower-hand"}`（unmute 会重新打开之前关掉的设备）、`participant_kick`。
- `chat_send`（发给所有人或某一个人）、`invitados_chat_leer`。
- 他们分享的文件：`share_list`、`compartida_revisar`（查看图片）、`share_take`（放到场景）、`share_delete`。

## 加入
- `room_link {"target": "local"|"remote", "withPin"}` 给出链接及其二维码（图片）。`room_pin` 修改 PIN。`room_set {"lobby", "locked"}`。
- `network_status`：HTTPS、WireGuard 隧道、链接。
- 让助手**自行接待**来访者（通过聊天或**通过说话**：他们说的话会转成文字，并用语音回答他们），在“人员”中自动登记，以及接待员的声音：`atencion_configurar` / `atencion_estado`（也适用于大会的远程会议：开启 `asambleaHablando` 时，用 `chat_send {"to": <id del integrante>}` 对等待的人说话，用 `chat_wait {"participant": <id>}` 听他说；一次一个，放其进入后结束）。被识别后可自行进入的人员，由活动名册决定（`padron_ver {"solos": true}`）。
- 要让他们**从互联网**加入（pinggy、Cloudflare、自有隧道）：`help {"topic": "acceso_remoto"}`。
