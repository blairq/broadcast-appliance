## HTTP (Streamable HTTP)
- URL: `http://127.0.0.1:8790/mcp` (порт: настройка `mcp.port`; слушает только на этой машине).
- Заголовок: `Authorization: Bearer <token>`; токен создаётся один раз и хранится зашифрованным в `Credenciales/`; `bin/64bit/ba-mcp --token` его показывает.
- Claude Code: `claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio (клиенты, которые запускают процесс)
Мост `bin/64bit/ba-mcp` читает порт и токен из `Runtime/mcp.json (dentro del bundle)` (приложение публикует его при запуске) и пересылает запросы. Пример (Claude Desktop):
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
Приложение должно быть открыто; иначе мост отвечает понятной ошибкой.

## Безопасность
Только localhost, токен обязателен, запросы с Origin другой страницы отклоняются (защита от DNS rebinding). У кого есть токен, тот управляет ВСЕМ: выход в эфир, удаление гостей, смена PIN, закрытие приложения.
