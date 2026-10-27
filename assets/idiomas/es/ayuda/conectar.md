## HTTP (Streamable HTTP)
- URL: `http://127.0.0.1:8790/mcp` (puerto: ajuste `mcp.port`; solo escucha en esta maquina).
- Cabecera: `Authorization: Bearer <token>`; el token se crea una vez y queda cifrado en `Credenciales/`; `bin/64bit/ba-mcp --token` lo muestra.
- Claude Code: `claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio (clientes que lanzan un proceso)
El puente `bin/64bit/ba-mcp` lee puerto y token de `Runtime/mcp.json (dentro del bundle)` (lo publica la app al arrancar) y reenvia. Ejemplo (Claude Desktop):
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
La app tiene que estar abierta; si no, el puente contesta con un error explicativo.

## Seguridad
Solo localhost, token obligatorio, y se rechazan pedidos con Origin de otra pagina (defensa contra DNS rebinding). Quien tenga el token maneja TODO: salir al aire, expulsar, cambiar el PIN, cerrar la app.