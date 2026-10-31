## 设置（只需一次）
1. `twitch_status`：说明缺的是应用还是账号。
2. 应用：由**本人**开启 Twitch 的两步验证，并在 https://dev.twitch.tv/console/apps/create 注册一个应用（重定向 `http://localhost`，类别 **Broadcaster Suite**，客户端类型 **Public**）。用安全表单 `account_credentials_request {"platform": "twitch"}` 保存（或 `twitch_setup {"clientId": "..."}`：Public 客户端的 ID 不是机密）。
3. `twitch_login`：返回一个**代码**并打开 twitch.tv/activate；由**本人**用频道账号登录并确认（把代码给他看）。`twitch_status` 确认账号。

## 直播
- `twitch_live_start {"title": "...", "game": "Just Chatting"}`：设置标题/分类，获取频道密钥（仅存内存），并从程序推流到 `rtmp://live.twitch.tv/app`。
- 确认：约 15 秒后看 `status`（推流、fps、丢帧）和 `twitch_stream_status`（直播中、观众数）。
- 停止：`twitch_live_stop`。Twitch 没有“活动”的概念：频道只要收到信号就处于直播状态。

## 频道和录像（VOD）
- `twitch_channel` 读取；`twitch_channel {"title", "game", "tags", "language"}` 修改；`{"search": "..."}` 搜索分类。
- `twitch_videos`、`twitch_video_delete {"ids": [...]}`。只有频道开启了“保存过往直播”，直播才会保存为 VOD。

## 聊天
- `twitch_chat {"on": true}` 连接频道聊天（安全 IRC）；`{"send": "texto"}` 以频道身份发言；返回最新消息，带颜色和徽章。
- 在画面上显示：`chat_overlay_add {"channel": "twitch"}`（或 `all` 混合多个平台）。
