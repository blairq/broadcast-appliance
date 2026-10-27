## Abrir un programa
1. `status`, `foto_todas_escenas`.
2. `scene_switch` a la escena de apertura; `foto_programa` para confirmar.
3. `record_start` (respaldo) y `stream_start`.

## Entrevista con dos invitados
1. `room_status` — que esten admitidos (slot 1 y 2).
2. `scene_define` con `ba_guest_video` slot 1 `left` y slot 2 `right`, y un zocalo.
3. `foto_fuente` de la escena -> `scene_switch`.

## Zocalo con el nombre de quien habla
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}` y verificar con `foto_programa`.

## Ronda de oradores
`speakers_config {"enabled": true, "defaultTime": "2:00"}`; admitir; `speaker_next`; vigilar `remotos_fila_ver`; cuando `remainingSec` < 0, `chat_send` al orador o `speaker_next`.

## Cerrar
`scene_switch` a la de cierre, `stream_stop`, `record_stop`.