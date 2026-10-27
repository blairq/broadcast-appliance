## HTTP (Streamable HTTP)
- URL: `http://127.0.0.1:8790/mcp` (port: setting `mcp.port`; it only listens on this machine).
- Header: `Authorization: Bearer <token>`; the token is created once and stays encrypted in `Credenciales/`; `bin/64bit/ba-mcp --token` shows it.
- Claude Code: `claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio (clients that launch a process)
The bridge `bin/64bit/ba-mcp` reads port and token from `Runtime/mcp.json (inside the bundle)` (the app publishes it on startup) and forwards. Example (Claude Desktop):
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
The app has to be open; if it isn't, the bridge answers with an explanatory error.

## Security
Localhost only, token required, and requests with an Origin from another page are rejected (defense against DNS rebinding). Whoever has the token controls EVERYTHING: going on air, kicking people out, changing the PIN, closing the app.