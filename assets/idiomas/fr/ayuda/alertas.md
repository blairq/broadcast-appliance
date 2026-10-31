## À quoi ça sert
Au lieu de consulter de temps en temps (mixer_list, remotos_fila_ver...), demandez au serveur de surveiller : il évalue toutes les 250 ms et, quand quelque chose se passe, il vous **prévient dans le chat** (onglet Assistant) et/ou à l'écran, et il peut **agir tout seul** (actions = outils MCP).

## Créer
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`. Types et paramètres : `alert_types`.

| Type | Surveille |
|---|---|
| `audio_peak` | un canal (ou tous, `source: "*"`) dépasse le seuil (`thresholdDb`, -1) |
| `audio_silence` | silence sur un canal pendant plus de `forSec` (seulement à l'antenne/en enregistrement) |
| `media_end` | une vidéo/un morceau se termine (`beforeSec` pour prévenir avant) |
| `speaker_time` | il reste `remainingSec` à l'intervenant (0 = temps écoulé) |
| `lobby` / `hand` / `guest_drop` | quelqu'un attend dans la salle d'attente / lève la main / se déconnecte |
| `chat` | un message arrive (plateforme, `contains` ou `regex`) |
| `guest_message` | un invité écrit dans le chat de la conférence (`privateOnly`) |
| `stream_health` | la diffusion perd des images ou se coupe |
| `disk` | il reste peu de disque pendant l'enregistrement |
| `timer` | dans `in` secondes, à `at` "HH:MM", ou toutes les `every` secondes |

## Exemples
- Ne pas saturer : `{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}` et, à la réception de l'alerte, baisser de 3 dB avec `audio_set`.
- Musique qui se termine et retour caméra : `{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`.
- Prévenir l'intervenant : `{"type":"speaker_time","params":{"remainingSec":30}}` et `chat_send` à l'intervenant.
- Rappel : `{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`.

## Gestion
`alert_list`, `alert_update {"id","enabled":false}`, `alert_delete`, `alert_events` (historique), `alert_wait` (pour les clients qui ne reçoivent pas d'alertes dans le chat : rend la main dès qu'il y en a une). Les déclencheurs restent enregistrés (Config/alertas.json).
