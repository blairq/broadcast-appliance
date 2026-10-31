## Открыть программу
1. `status`, `foto_todas_escenas`.
2. `scene_switch` на сцену открытия; `foto_programa` для подтверждения.
3. `record_start` (резервная копия) и `stream_start`.

## Интервью с двумя гостями
1. `room_status` — убедиться, что они впущены (слот 1 и 2).
2. `scene_define` с `ba_guest_video` слот 1 `left` и слот 2 `right`, и титр.
3. `foto_fuente` сцены -> `scene_switch`.

## Титр с именем выступающего
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}` и проверить через `foto_programa`.

## Очередь выступающих
`speakers_config {"enabled": true, "defaultTime": "2:00"}`; впустить; `speaker_next`; следить за `remotos_fila_ver`; когда `remainingSec` < 0 — `chat_send` выступающему или `speaker_next`.

## Закрыть
`scene_switch` на сцену закрытия, `stream_stop`, `record_stop`.
