## Ouvrir une émission
1. `status`, `foto_todas_escenas`.
2. `scene_switch` vers la scène d'ouverture ; `foto_programa` pour confirmer.
3. `record_start` (secours) et `stream_start`.

## Interview avec deux invités
1. `room_status` — qu'ils soient admis (slot 1 et 2).
2. `scene_define` avec `ba_guest_video` slot 1 `left` et slot 2 `right`, et un synthé.
3. `foto_fuente` de la scène -> `scene_switch`.

## Synthé avec le nom de la personne qui parle
`source_update {"name": "Zocalo", "settings": {"text": "Ana Perez — Vecina"}, "visible": true}` et vérifier avec `foto_programa`.

## Tour de parole
`speakers_config {"enabled": true, "defaultTime": "2:00"}` ; admettre ; `speaker_next` ; surveiller `remotos_fila_ver` ; quand `remainingSec` < 0, `chat_send` à l'intervenant ou `speaker_next`.

## Terminer
`scene_switch` vers celle de clôture, `stream_stop`, `record_stop`.
