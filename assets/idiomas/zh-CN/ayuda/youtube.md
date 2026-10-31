## 设置（只需一次）
1. `youtube_status`：说明缺的是凭据还是账号。
2. 凭据：由本人在 Google Cloud 创建一个 **桌面应用** 类型的 OAuth 客户端（程序的 YouTube 选项卡里有带链接的步骤：项目、启用 YouTube Data API v3、同意屏幕、测试用户、客户端），然后用 `youtube_setup {"fromDownloads": true}`（导入下载的 JSON）或安全表单 `account_credentials_request {"platform": "youtube"}` 保存（见 `help {"topic": "cuentas"}`）。
3. `youtube_login`：在本机打开浏览器；由**本人**选择账号并接受。`youtube_status` 确认频道。

## 直播
- 一步到位：`youtube_live_create {"title": "...", "privacy": "private", "start": true}` 创建活动，获取服务器和密钥（仅存内存，绝不写入磁盘），并开始推流。设置了自动开始时，YouTube 收到信号后（约 10-30 秒）活动会变为“直播中”。
- 推到已有的活动：`youtube_live_list` -> `youtube_live_start {"id"}`。
- 确认：`status`（推流、fps、丢帧）和 `youtube_live_health {"id"}`（active / good）。
- 结束：`youtube_live_end {"id"}`（关闭活动并停止程序推流）。编辑/改变状态/删除：`youtube_live_update`。
- 新频道：YouTube 第一次启用直播最多需要 24 小时（错误 "liveStreamingNotEnabled"）。

## 视频
`youtube_videos`、`youtube_video_update`（标题、描述、隐私、标签、缩略图）、`youtube_video_delete`、`youtube_video_upload`（不带 `path`：程序最近一次录制）。

## 聊天
- `youtube_chat {"broadcast": id}` 选择活动；`{"send": "texto"}` 发言；返回最新消息。
- 在画面上显示：`chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` 向场景添加 **Chat en vivo** 来源（类型 `ba_chat_overlay`）；用 `all` 会混合所有已连接的平台（每条消息带平台标记）。用 `source_update` 调整（settings：show_youtube、show_twitch、show_facebook、show_room、max_messages、font_size、width、height、bg_opacity、show_names、fade_sec）。用 `foto_fuente` 确认。

## 配额
每个项目每天 10,000 单位：列出和编辑消耗很少；上传一个视频约 1,600；读取聊天每次约 5（程序每 6 秒或更久读一次，且仅在选定了活动时）。
