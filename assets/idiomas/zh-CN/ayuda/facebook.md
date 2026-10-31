## 范围
Facebook API 只能推流和管理**公共主页**（Page）的视频（不能用于个人资料）。账号必须至少管理一个公共主页。

## 设置（只需一次）
1. `facebook_status`：说明缺的是应用、账号还是公共主页，以及重定向 URI。
2. Meta 应用：在 https://developers.facebook.com/apps/ 创建一个应用（类型 **Business**），添加 **Facebook Login**，并把 `facebook_status` 给出的 `redirectUri` 填进 “Valid OAuth Redirect URIs”（默认 `http://localhost:8791/`）。开发模式就够了（只有其管理员使用）。ID 和密钥在 Settings -> Basic：用安全表单 `account_credentials_request {"platform": "facebook"}` 保存（密钥不能经过聊天）。
3. `facebook_login`：打开浏览器；由**本人**选择账号、勾选公共主页并接受。然后 `facebook_pages`（如果有多个，再用 `{"select": "..."}`）。

## 直播
- `facebook_live_create {"title": "...", "test": true}` 创建**未发布**的直播视频（只有公共主页在 Live Producer 中能看到），并从程序推流（RTMPS，服务器和密钥仅存内存）。用 `foto_programa` 和 `facebook_live_list {"id"}` 检查；没问题后，`facebook_live_publish {"id"}` 会在不中断的情况下将其公开。
- `test: false`：立即公开。
- 结束：`facebook_live_end`（不带 `id`：程序正在推流的那个直播视频）。结束后会保留为视频。

## 视频
`facebook_videos`、`facebook_video_update {"id", "title", "description"}`（或 `"delete": true`）、`facebook_video_upload`（不带 `path`：最近一次录制）。

## 评论
- 推流时会自动选定该直播视频的评论；`facebook_chat {"live": id}` 选择另一个，`{"send": "texto"}` 以公共主页身份评论。
- 在画面上显示：`chat_overlay_add {"channel": "facebook"}`（或 `all`）。
