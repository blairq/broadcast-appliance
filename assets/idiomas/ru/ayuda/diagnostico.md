| Симптом | Что смотреть |
|---|---|
| Программа чёрная | `foto_programa`; `escena_fuentes_ver` (источник скрыт или за пределами холста); `foto_fuente` каждого источника |
| Гостя не видно | `room_status` (connected, mediaUp, cam), `foto_invitado`, `participant_request` не подходит; что его источник есть в сцене (`guest_source_add`) |
| Нет звука / перегрузка | `mixer_list` (muted, peakDb) |
| Обрывы трансляции | `status` (droppedFrames, fps, kbps), уменьшить `encoder.bitrate` |
| Конференция не отвечает | `status.gatewayAlive`, `log_leer {"modulo": "gateway"}`, `gateway_restart` |
| Изображение застыло | `capture_sequence` (одинаковые кадры) |
Журналы: `log_leer {"minimo": "aviso"}` — с него ВСЕГДА начинают: он выдаёт три
процесса вперемешку по времени, с уровнем и модулем. Если ничего не видно, понизьте уровень через
`log_nivel`, повторите проблему и посмотрите снова. `logs_tail` — чтобы
посмотреть сырой файл (cloudflared.log).
