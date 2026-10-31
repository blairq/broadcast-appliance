## 截取什么
| 我想看…… | 工具 |
|---|---|
| 在播的内容 | `foto_programa` |
| 某个场景（在播或不在播）或来源 | `foto_fuente {"name"}` |
| 一眼看全所有场景 | `foto_todas_escenas` |
| 会议中的某个人 | `foto_invitado {"participant": "Ana"}`（加 `includeScreen` 可看其屏幕） |
| 会议中的所有人 | `foto_todos_invitados` |
| 原始视频（guest:N、share:N、lobby:K、backstage:K、operator） | `capture_feed {"key"}`（`feeds_list` 会列出） |
| 程序本身 | `capture_ui {"window": "main"|"all"}` |
| **动作 / 视频** | `capture_sequence {"target": "program", "frames": 6, "intervalMs": 400}` |

## 提示
- 尺寸：`maxWidth`（640 足以辨认人脸和大字；要读小字用 1280 以上）。默认 JPEG；清晰的图形用 `format: "png"`。
- 对某个人（`participant`）使用 `capture_sequence`，可以看出他是否在说话（嘴部）、是否在场、是否举手、画面是否冻结（帧完全相同）或黑屏。
- 每次视觉上的改动之后，都要截图确认。直播时，在播场景的改动会立即生效。
- 发言人队列中排在前几位之后的人不会向程序发送视频：`foto_invitado` 返回的是他们最近的照片（文字中会注明）。
