## HTTP (Streamable HTTP)
- URL : `http://127.0.0.1:8790/mcp` (port : réglage `mcp.port` ; il n'écoute que sur cette machine).
- En-tête : `Authorization: Bearer <token>` ; le jeton est créé une fois et reste chiffré dans `Credenciales/` ; `bin/64bit/ba-mcp --token` l'affiche.
- Claude Code : `claude mcp add --transport http broadcast http://127.0.0.1:8790/mcp --header "Authorization: Bearer $(<bundle>/bin/64bit/ba-mcp --token)"`

## stdio (clients qui lancent un processus)
Le pont `bin/64bit/ba-mcp` lit le port et le jeton dans `Runtime/mcp.json (dentro del bundle)` (l'application le publie au démarrage) et relaie. Exemple (Claude Desktop) :
```json
{"mcpServers": {"broadcast": {"command": "<bundle>/bin/64bit/ba-mcp"}}}
```
L'application doit être ouverte ; sinon, le pont répond par une erreur explicative.

## Sécurité
Seulement localhost, jeton obligatoire, et les requêtes avec un Origin d'une autre page sont refusées (défense contre le DNS rebinding). Qui a le jeton pilote TOUT : passer à l'antenne, expulser, changer le PIN, fermer l'application.
