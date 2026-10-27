| Symptom | What to check |
|---|---|
| Black program | `foto_programa`; `escena_fuentes_ver` (source hidden or off the canvas); `foto_fuente` of each source |
| A guest isn't visible | `room_status` (connected, mediaUp, cam), `foto_invitado`, `participant_request` doesn't apply; that their source is in the scene (`guest_source_add`) |
| No audio / clipping | `mixer_list` (muted, peakDb) |
| Stream drops | `status` (droppedFrames, fps, kbps), lower `encoder.bitrate` |
| The conference doesn't respond | `status.gatewayAlive`, `log_leer {"modulo": "gateway"}`, `gateway_restart` |
| Frozen picture | `capture_sequence` (identical frames) |
Logs: `log_leer {"minimo": "aviso"}` is where you ALWAYS start: it brings the three
processes interleaved by time, with level and module. If nothing shows up, lower it with
`log_nivel`, make the problem happen again and look again. `logs_tail` is for
viewing a raw file (cloudflared.log).