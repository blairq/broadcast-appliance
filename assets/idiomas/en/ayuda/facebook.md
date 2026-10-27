## Scope
The Facebook API only streams and manages videos of **Pages** (not the personal profile). The account has to manage at least one Page.

## Set up (once)
1. `facebook_status`: says whether the app, the account or the Page is missing, and the redirect URI.
2. Meta app: at https://developers.facebook.com/apps/ create an app (type **Business**), add **Facebook Login** and enter the `redirectUri` from `facebook_status` in "Valid OAuth Redirect URIs" (by default `http://localhost:8791/`). Development mode is enough (only its admins use it). ID and secret key under Settings -> Basic: they are saved with the secure form `account_credentials_request {"platform": "facebook"}` (the key must not go through the chat).
3. `facebook_login`: opens the browser; the PERSON chooses the account, checks the Pages and accepts. Then `facebook_pages` (and `{"select": "..."}` if there are several).

## Stream
- `facebook_live_create {"title": "...", "test": true}` creates the live video **unpublished** (only the Page sees it in Live Producer) and streams from the app (RTMPS, server and key memory only). Check with `foto_programa` and `facebook_live_list {"id"}`; when it's right, `facebook_live_publish {"id"}` makes it public without cutting.
- `test: false`: goes public immediately.
- End: `facebook_live_end` (without `id`: the live video the app is streaming to). It remains as a video.

## Videos
`facebook_videos`, `facebook_video_update {"id", "title", "description"}` (or `"delete": true`), `facebook_video_upload` (without `path`: the latest recording).

## Comments
- When streaming, the live video's comments are chosen automatically; `facebook_chat {"live": id}` chooses another, `{"send": "texto"}` comments as the Page.
- On screen: `chat_overlay_add {"channel": "facebook"}` (or `all`).