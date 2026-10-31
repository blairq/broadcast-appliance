## Abrir um programa
1. `status`, `foto_todas_escenas`.
2. `scene_switch` para a cena de abertura; `foto_programa` para confirmar.
3. `record_start` (backup) e `stream_start`.

## Entrevista com dois convidados
1. `room_status` — que estejam admitidos (slot 1 e 2).
2. `scene_define` com `ba_guest_video` slot 1 `left` e slot 2 `right`, e um GC.
3. `foto_fuente` da cena -> `scene_switch`.

## GC com o nome de quem fala
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}` e verificar com `foto_programa`.

## Rodada de oradores
`speakers_config {"enabled": true, "defaultTime": "2:00"}`; admitir; `speaker_next`; acompanhar `remotos_fila_ver`; quando `remainingSec` < 0, `chat_send` ao orador ou `speaker_next`.

## Encerrar
`scene_switch` para a de encerramento, `stream_stop`, `record_stop`.