## HTTP（Streamable HTTP）
- URL：`http://127.0.0.1:8790/mcp`（端口：设置 `mcp.port`；只在本机监听）。
- 请求头：`Authorization: Bearer <token>`；token 只创建一次，加密保存在 `Credenciales/`；`bin/64bit/ba-mcp --token` 可显示它。
- Claude Code：`claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio（会启动进程的客户端）
桥接程序 `bin/64bit/ba-mcp` 从 `Runtime/mcp.json (dentro del bundle)`（程序启动时发布）读取端口和 token 并转发。示例（Claude Desktop）：
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
程序必须处于打开状态；否则桥接程序会返回一个说明原因的错误。

## 安全
只限 localhost，必须有 token，并拒绝来自其他页面 Origin 的请求（防御 DNS rebinding）。持有 token 的人可以控制**一切**：开始播出、踢人、修改 PIN、关闭程序。
