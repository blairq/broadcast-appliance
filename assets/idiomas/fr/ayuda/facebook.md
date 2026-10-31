## Portée
L'API de Facebook ne diffuse et ne gère que les vidéos des **Pages** (pas du profil personnel). Le compte doit administrer au moins une Page.

## Configurer (une fois)
1. `facebook_status` : dit s'il manque l'application, le compte ou la Page, et l'URI de redirection.
2. Application Meta : sur https://developers.facebook.com/apps/ créer une application (type **Entreprise**), ajouter **Facebook Login** et saisir dans « URI de redirection OAuth valides » la `redirectUri` de `facebook_status` (par défaut `http://localhost:8791/`). Le mode développement suffit (seuls ses administrateurs l'utilisent). ID et clé secrète dans Paramètres -> Général : ils s'enregistrent avec le formulaire sécurisé `account_credentials_request {"platform": "facebook"}` (la clé ne doit pas passer par le chat).
3. `facebook_login` : ouvre le navigateur ; la PERSONNE choisit le compte, coche les Pages et accepte. Ensuite `facebook_pages` (et `{"select": "..."}` s'il y en a plusieurs).

## Diffuser
- `facebook_live_create {"title": "...", "test": true}` crée le direct **sans le publier** (seule la Page le voit dans Live Producer) et diffuse depuis l'application (RTMPS, serveur et clé seulement en mémoire). Vérifier avec `foto_programa` et `facebook_live_list {"id"}` ; quand tout va bien, `facebook_live_publish {"id"}` le rend public sans couper.
- `test: false` : il est public immédiatement.
- Terminer : `facebook_live_end` (sans `id` : le direct vers lequel diffuse l'application). Il reste en vidéo.

## Vidéos
`facebook_videos`, `facebook_video_update {"id", "title", "description"}` (ou `"delete": true`), `facebook_video_upload` (sans `path` : le dernier enregistrement).

## Commentaires
- Pendant la diffusion, ceux du direct sont choisis automatiquement ; `facebook_chat {"live": id}` en choisit un autre, `{"send": "texto"}` commente au nom de la Page.
- À l'écran : `chat_overlay_add {"channel": "facebook"}` (ou `all`).
