## 哪些能自动化，哪些不能
在 Google Cloud、Twitch 或 Meta 创建应用/凭据必须由**本人**完成（用自己的账号，接受各平台的条款）：你一步一步地引导。其余的一切都由你来做。

## 流程
1. `accounts_status`：每个平台缺什么，以及下一步（`next`）。
2. `account_setup_guide {"platform"}`：步骤、要粘贴的**确切值**（`paste`）以及表单字段。每次只讲**一步**，说得简短、通俗。
3. `account_open {"platform", "step"}` 在浏览器中打开该步骤的页面。等对方说完成了（或用 `operator_ask` 问他），再进行下一步。
4. 凭据：
   - YouTube：如果已经下载了 Google 的 JSON，`youtube_setup {"fromDownloads": true}` 会自动从“下载”文件夹导入。
   - 否则（或在 Twitch/Facebook）：`account_credentials_request {"platform"}` 在底部栏显示一个表单；对方把数据粘贴到程序中，加密保存。**绝不要让对方把机密粘贴到聊天里**：聊天会经过 AI 服务商。
5. 登录：`youtube_login`、`twitch_login`（显示一个代码：告诉对方在 twitch.tv/activate 确认）或 `facebook_login`（然后用 `facebook_pages` 选择公共主页）。
6. `account_check {"platform"}`：真实测试（频道、直播已启用、权限、公共主页）。如果有 `problem`，解释清楚并帮助解决。

## 常见问题
- YouTube “未启用直播功能”：在 youtube.com/features 启用，第一次最多需要 24 小时。
- YouTube 每 7 天要求重新连接：Google 应用处于测试模式；在 “Público” -> “Publicar app”（发布应用）后就不会过期。
- 连接时 YouTube 提示“应用未经验证”：这是正常的（应用是对方自己的）：“高级” -> “转至 ...”。
- Twitch 不让注册应用：缺少两步验证。
- Facebook “URL 被阻止”/重定向问题：Meta 应用的重定向 URI 必须与 `paste.uriDeRedireccion` **完全**一致。
- Facebook 没有公共主页：API 推流的目标是对方管理的**公共主页**，而不是个人资料。
