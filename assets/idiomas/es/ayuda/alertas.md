## Para que
En vez de consultar cada tanto (mixer_list, remotos_fila_ver...), pedile al servidor que vigile: evalua cada 250 ms y cuando pasa algo te **avisa en el chat** (pestaña Asistente) y/o en pantalla, y puede **actuar solo** (acciones = herramientas MCP).

## Crear
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`. Tipos y parametros: `alert_types`.

| Tipo | Vigila |
|---|---|
| `audio_peak` | un canal (o todos, `source: "*"`) pasa el umbral (`thresholdDb`, -1) |
| `audio_silence` | silencio en un canal mas de `forSec` (solo al aire/grabando) |
| `media_end` | termina un video/tema (`beforeSec` para avisar antes) |
| `speaker_time` | al orador le quedan `remainingSec` (0 = se cumplio) |
| `lobby` / `hand` / `guest_drop` | alguien espera en el lobby / levanta la mano / se desconecta |
| `chat` | llega un mensaje (plataforma, `contains` o `regex`) |
| `guest_message` | un invitado escribe en el chat de la conferencia (`privateOnly`) |
| `stream_health` | la transmision pierde cuadros o se corta |
| `disk` | queda poco disco grabando |
| `timer` | en `in` segundos, a las `at` "HH:MM", o cada `every` segundos |

## Ejemplos
- No saturar: `{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}` y, al recibir el aviso, bajar 3 dB con `audio_set`.
- Musica que termina y vuelve a camara: `{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`.
- Avisar al orador: `{"type":"speaker_time","params":{"remainingSec":30}}` y `chat_send` al orador.
- Recordatorio: `{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`.

## Manejo
`alert_list`, `alert_update {"id","enabled":false}`, `alert_delete`, `alert_events` (historial), `alert_wait` (para clientes que no reciben avisos en el chat: vuelve apenas hay uno). Los disparadores quedan guardados (Config/alertas.json).