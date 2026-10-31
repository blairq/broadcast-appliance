## HTTP (Streamable HTTP)
- URL: `http://127.0.0.1:8790/mcp` (porta: ajuste `mcp.port`; só escuta nesta máquina).
- Cabeçalho: `Authorization: Bearer <token>`; o token é criado uma vez e fica criptografado em `Credenciales/`; `bin/64bit/ba-mcp --token` o mostra.
- Claude Code: `claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio (clientes que lançam um processo)
A ponte `bin/64bit/ba-mcp` lê porta e token de `Runtime/mcp.json (dentro del bundle)` (o app o publica ao iniciar) e repassa. Exemplo (Claude Desktop):
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
O app tem que estar aberto; se não estiver, a ponte responde com um erro explicativo.

## Segurança
Só localhost, token obrigatório, e são rejeitados pedidos com Origin de outra página (defesa contra DNS rebinding). Quem tiver o token controla TUDO: entrar no ar, expulsar, mudar o PIN, fechar o app.