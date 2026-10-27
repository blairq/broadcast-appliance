| Sintoma | Que mirar |
|---|---|
| Programa negro | `foto_programa`; `escena_fuentes_ver` (fuente oculta o fuera del lienzo); `foto_fuente` de cada fuente |
| Un invitado no se ve | `room_status` (connected, mediaUp, cam), `foto_invitado`, `participant_request` no aplica; que su fuente este en la escena (`guest_source_add`) |
| Sin audio / saturado | `mixer_list` (muted, peakDb) |
| Cortes en la transmision | `status` (droppedFrames, fps, kbps), bajar `encoder.bitrate` |
| La conferencia no responde | `status.gatewayAlive`, `log_leer {"modulo": "gateway"}`, `gateway_restart` |
| Imagen congelada | `capture_sequence` (cuadros iguales) |
Registros: `log_leer {"minimo": "aviso"}` es por donde se empieza SIEMPRE: trae los tres
procesos entrelazados por hora, con nivel y modulo. Si no aparece nada, bajar con
`log_nivel`, hacer que el problema pase otra vez y volver a mirar. `logs_tail` es para
ver un archivo crudo (cloudflared.log).