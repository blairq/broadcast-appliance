## Open a show
1. `status`, `foto_todas_escenas`.
2. `scene_switch` to the opening scene; `foto_programa` to confirm.
3. `record_start` (backup) and `stream_start`.

## Interview with two guests
1. `room_status` — make sure they are admitted (slot 1 and 2).
2. `scene_define` with `ba_guest_video` slot 1 `left` and slot 2 `right`, plus a lower third.
3. `foto_fuente` of the scene -> `scene_switch`.

## Lower third with the speaker's name
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}` and check with `foto_programa`.

## Round of speakers
`speakers_config {"enabled": true, "defaultTime": "2:00"}`; admit; `speaker_next`; watch `remotos_fila_ver`; when `remainingSec` < 0, `chat_send` to the speaker or `speaker_next`.

## Close
`scene_switch` to the closing scene, `stream_stop`, `record_stop`.