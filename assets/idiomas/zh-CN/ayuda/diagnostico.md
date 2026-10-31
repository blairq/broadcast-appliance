| 症状 | 查看什么 |
|---|---|
| 节目黑屏 | `foto_programa`；`escena_fuentes_ver`（来源被隐藏或在画布外）；对每个来源 `foto_fuente` |
| 某位嘉宾看不到 | `room_status`（connected、mediaUp、cam）、`foto_invitado`，`participant_request` 不适用；确认其来源在场景中（`guest_source_add`） |
| 没有声音 / 削波 | `mixer_list`（muted、peakDb） |
| 推流卡顿中断 | `status`（droppedFrames、fps、kbps），调低 `encoder.bitrate` |
| 会议没有响应 | `status.gatewayAlive`、`log_leer {"modulo": "gateway"}`、`gateway_restart` |
| 画面冻结 | `capture_sequence`（帧完全相同） |
日志：**始终**从 `log_leer {"minimo": "aviso"}` 开始：它按时间交错列出三个进程的记录，带级别和模块。
如果什么都没有，用 `log_nivel` 调低级别，让问题再出现一次，再看。`logs_tail` 用于查看原始文件（cloudflared.log）。
