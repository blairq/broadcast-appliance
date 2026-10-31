| Symptôme | Quoi regarder |
|---|---|
| Programme noir | `foto_programa` ; `escena_fuentes_ver` (source masquée ou hors du canevas) ; `foto_fuente` de chaque source |
| Un invité n'apparaît pas | `room_status` (connected, mediaUp, cam), `foto_invitado`, `participant_request` ne s'applique pas ; que sa source soit dans la scène (`guest_source_add`) |
| Pas de son / saturé | `mixer_list` (muted, peakDb) |
| Coupures dans la diffusion | `status` (droppedFrames, fps, kbps), baisser `encoder.bitrate` |
| La conférence ne répond pas | `status.gatewayAlive`, `log_leer {"modulo": "gateway"}`, `gateway_restart` |
| Image figée | `capture_sequence` (images identiques) |
Journaux : `log_leer {"minimo": "aviso"}` est TOUJOURS le point de départ : il apporte les trois
processus entremêlés par heure, avec niveau et module. Si rien n'apparaît, baisser le niveau avec
`log_nivel`, faire en sorte que le problème se reproduise et regarder de nouveau. `logs_tail` sert
à voir un fichier brut (cloudflared.log).
