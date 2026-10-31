## Configurer (une fois)
1. `twitch_status` : dit s'il manque l'application ou le compte.
2. Application : la personne active la validation en deux étapes de Twitch et enregistre une application sur https://dev.twitch.tv/console/apps/create (redirection `http://localhost`, catégorie **Broadcaster Suite**, type de client **Public**). Elle s'enregistre avec le formulaire sécurisé `account_credentials_request {"platform": "twitch"}` (ou `twitch_setup {"clientId": "..."}` : l'ID d'un client Public n'est pas secret).
3. `twitch_login` : renvoie un **code** et ouvre twitch.tv/activate ; la PERSONNE se connecte avec le compte de la chaîne et confirme (montrez-lui le code). `twitch_status` confirme le compte.

## Diffuser
- `twitch_live_start {"title": "...", "game": "Just Chatting"}` : applique le titre/la catégorie, prend la clé de la chaîne (seulement en mémoire) et diffuse depuis l'application vers `rtmp://live.twitch.tv/app`.
- Vérifier : `status` (streaming, fps, images perdues) et `twitch_stream_status` (live, spectateurs) au bout de ~15 s.
- Couper : `twitch_live_stop`. Sur Twitch il n'y a pas d'« événements » : la chaîne est en direct tant qu'elle reçoit un signal.

## Chaîne et VOD
- `twitch_channel` lit ; `twitch_channel {"title", "game", "tags", "language"}` modifie ; `{"search": "..."}` cherche des catégories.
- `twitch_videos`, `twitch_video_delete {"ids": [...]}`. Les directs ne sont gardés en VOD que si la chaîne a activé « Stocker les anciennes diffusions ».

## Chat
- `twitch_chat {"on": true}` se connecte au chat de la chaîne (IRC sécurisé) ; `{"send": "texto"}` écrit au nom de la chaîne ; renvoie les derniers messages avec couleur et badges.
- À l'écran : `chat_overlay_add {"channel": "twitch"}` (ou `all` pour mélanger les plateformes).
