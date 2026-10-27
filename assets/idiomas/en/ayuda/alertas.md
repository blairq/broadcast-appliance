## What for
Instead of checking every so often (mixer_list, remotos_fila_ver...), ask the server to watch: it evaluates every 250 ms and when something happens it **notifies you in the chat** (Assistant tab) and/or on screen, and it can **act on its own** (actions = MCP tools).

## Create
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`. Types and parameters: `alert_types`.

| Type | Watches |
|---|---|
| `audio_peak` | a channel (or all, `source: "*"`) goes over the threshold (`thresholdDb`, -1) |
| `audio_silence` | silence on a channel for more than `forSec` (only on air/recording) |
| `media_end` | a video/track ends (`beforeSec` to warn earlier) |
| `speaker_time` | the speaker has `remainingSec` left (0 = time is up) |
| `lobby` / `hand` / `guest_drop` | someone waits in the lobby / raises their hand / disconnects |
| `chat` | a message arrives (platform, `contains` or `regex`) |
| `guest_message` | a guest writes in the conference chat (`privateOnly`) |
| `stream_health` | the stream drops frames or cuts out |
| `disk` | little disk space left while recording |
| `timer` | in `in` seconds, at `at` "HH:MM", or every `every` seconds |

## Examples
- Avoid clipping: `{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}` and, when the alert arrives, lower 3 dB with `audio_set`.
- Music that ends and goes back to camera: `{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`.
- Warn the speaker: `{"type":"speaker_time","params":{"remainingSec":30}}` and `chat_send` to the speaker.
- Reminder: `{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`.

## Management
`alert_list`, `alert_update {"id","enabled":false}`, `alert_delete`, `alert_events` (history), `alert_wait` (for clients that don't receive alerts in the chat: returns as soon as there is one). Triggers are saved (Config/alertas.json).