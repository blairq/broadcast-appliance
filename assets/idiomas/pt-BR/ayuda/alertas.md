## Para quê
Em vez de consultar de vez em quando (mixer_list, remotos_fila_ver...), peça ao servidor que vigie: ele avalia a cada 250 ms e, quando algo acontece, **avisa você no chat** (aba Assistente) e/ou na tela, e pode **agir sozinho** (ações = ferramentas MCP).

## Criar
`alert_create {"type": ..., "params": {...}, "message": "...", "notify": ["llm","ui"], "actions": [{"tool": ..., "arguments": {...}}], "once": false, "cooldownSec": N}`. Tipos e parâmetros: `alert_types`.

| Tipo | Vigia |
|---|---|
| `audio_peak` | um canal (ou todos, `source: "*"`) passa do limite (`thresholdDb`, -1) |
| `audio_silence` | silêncio num canal por mais de `forSec` (só no ar/gravando) |
| `media_end` | termina um vídeo/música (`beforeSec` para avisar antes) |
| `speaker_time` | restam `remainingSec` ao orador (0 = acabou o tempo) |
| `lobby` / `hand` / `guest_drop` | alguém espera na sala de espera / levanta a mão / se desconecta |
| `chat` | chega uma mensagem (plataforma, `contains` ou `regex`) |
| `guest_message` | um convidado escreve no chat da conferência (`privateOnly`) |
| `stream_health` | a transmissão perde quadros ou cai |
| `disk` | resta pouco disco durante a gravação |
| `timer` | em `in` segundos, às `at` "HH:MM", ou a cada `every` segundos |

## Exemplos
- Não saturar: `{"type":"audio_peak","params":{"source":"*","thresholdDb":-1}}` e, ao receber o aviso, baixar 3 dB com `audio_set`.
- Música que termina e volta para a câmera: `{"type":"media_end","params":{"source":"Musica"},"actions":[{"tool":"scene_switch","arguments":{"scene":"Principal"}}]}`.
- Avisar o orador: `{"type":"speaker_time","params":{"remainingSec":30}}` e `chat_send` ao orador.
- Lembrete: `{"type":"timer","name":"Cierre","params":{"at":"21:55"}}`.

## Gerenciamento
`alert_list`, `alert_update {"id","enabled":false}`, `alert_delete`, `alert_events` (histórico), `alert_wait` (para clientes que não recebem avisos no chat: volta assim que houver um). Os gatilhos ficam salvos (Config/alertas.json).