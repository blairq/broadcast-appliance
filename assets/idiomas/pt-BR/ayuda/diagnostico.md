| Sintoma | O que olhar |
|---|---|
| Programa preto | `foto_programa`; `escena_fuentes_ver` (fonte oculta ou fora do canvas); `foto_fuente` de cada fonte |
| Um convidado não aparece | `room_status` (connected, mediaUp, cam), `foto_invitado`, `participant_request` não se aplica; que a fonte dele esteja na cena (`guest_source_add`) |
| Sem áudio / saturado | `mixer_list` (muted, peakDb) |
| Cortes na transmissão | `status` (droppedFrames, fps, kbps), baixar `encoder.bitrate` |
| A conferência não responde | `status.gatewayAlive`, `log_leer {"modulo": "gateway"}`, `gateway_restart` |
| Imagem congelada | `capture_sequence` (quadros iguais) |
Logs: `log_leer {"minimo": "aviso"}` é por onde se começa SEMPRE: traz os três
processos intercalados por hora, com nível e módulo. Se não aparecer nada, baixe com
`log_nivel`, faça o problema acontecer de novo e olhe outra vez. `logs_tail` é para
ver um arquivo bruto (cloudflared.log).