## Matériel (branché sur la machine)
1. `video_devices` -> choisir `device` ; `video_devices {"device": ...}` affiche formats/résolutions/fps.
2. `video_input_add {"device": ..., "scene": ..., "layout": "full", "resolution": ..., "framerate": ...}`.
3. Vérifier avec `foto_fuente`. Une même caméra ne s'ouvre pas deux fois : si elle est déjà une source dans une autre scène, ajoutez-la avec `crear_fuente` en utilisant son nom (elle reste partagée).

## NDI
- `ndi_sources` cherche les émetteurs NDI sur le réseau (mDNS) et `ndi_input_add {"ndiName": ...}` (ou `host` + `port`) les ajoute.
- Réception OUVERTE avec libNoIDea (VideoLAN, expérimental) : NDI complet (SpeedHQ, avec audio) ; NDI|HX (H.264/H.265) non. Si l'image n'arrive pas, `input_status` dit pourquoi (introuvable, ne se connecte pas, coupé) et réessaie tout seul.

## Réseau
- Caméra IP : `network_input_add {"url": "rtsp://usuario:clave@192.168.1.50:554/stream1"}` (RTSP sur TCP ; `rtspTransport: "udp"` si la caméra l'exige).
- Autres : `rtmp://`, `srt://ip:puerto?mode=caller`, `https://.../playlist.m3u8` (HLS), `udp://@:5000`.
- Indique si l'image arrive et de quelle taille. `input_status` affiche l'état de toutes les entrées (sans les clés).
- Faible latence par défaut ; `latency: "normal"` pour les signaux instables (plus de tampon). Se reconnecte tout seul.
