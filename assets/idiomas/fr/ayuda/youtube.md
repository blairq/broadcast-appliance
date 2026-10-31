## Configurer (une fois)
1. `youtube_status` : dit s'il manque l'identifiant ou le compte.
2. Identifiant : la personne crée un client OAuth de type **Application de bureau** dans Google Cloud (l'onglet YouTube de l'application donne les étapes avec des liens : projet, activer YouTube Data API v3, écran de consentement, utilisateur test, client) et il s'enregistre avec `youtube_setup {"fromDownloads": true}` (importe le JSON téléchargé) ou avec le formulaire sécurisé `account_credentials_request {"platform": "youtube"}` (voir `help {"topic": "cuentas"}`).
3. `youtube_login` : ouvre le navigateur de cette machine ; la PERSONNE choisit le compte et accepte. `youtube_status` confirme la chaîne.

## Diffuser en direct
- Tout en un : `youtube_live_create {"title": "...", "privacy": "private", "start": true}` crée l'événement, récupère le serveur et la clé (seulement en mémoire, jamais sur disque) et commence à diffuser. Avec le démarrage automatique, l'événement passe EN DIRECT quand YouTube reçoit le signal (~10-30 s).
- Vers un événement existant : `youtube_live_list` -> `youtube_live_start {"id"}`.
- Vérifier : `status` (streaming, fps, images perdues) et `youtube_live_health {"id"}` (active / good).
- Terminer : `youtube_live_end {"id"}` (clôt l'événement et coupe l'application). Modifier/changer d'état/supprimer : `youtube_live_update`.
- Nouvelles chaînes : YouTube met jusqu'à 24 h à activer le direct la première fois (erreur "liveStreamingNotEnabled").

## Vidéos
`youtube_videos`, `youtube_video_update` (titre, description, confidentialité, tags, miniature), `youtube_video_delete`, `youtube_video_upload` (sans `path` : le dernier enregistrement de l'application).

## Chat
- `youtube_chat {"broadcast": id}` choisit l'événement ; `{"send": "texto"}` écrit ; renvoie les derniers messages.
- À l'écran : `chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` ajoute la source **Chat en direct** (type `ba_chat_overlay`) à une scène ; avec `all`, elle mélange toutes les plateformes connectées (chaque message avec son logo). On l'ajuste avec `source_update` (settings : show_youtube, show_twitch, show_facebook, show_room, max_messages, font_size, width, height, bg_opacity, show_names, fade_sec). Vérifier avec `foto_fuente`.

## Quota
10 000 unités par jour et par projet : lister et modifier coûte peu ; mettre en ligne une vidéo ~1 600 ; lire le chat ~5 par lecture (l'application lit toutes les 6 s ou plus, seulement avec un événement choisi).
