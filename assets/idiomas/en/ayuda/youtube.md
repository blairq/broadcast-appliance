## Set up (once)
1. `youtube_status`: says whether the credential or the account is missing.
2. Credential: the person creates an OAuth client of type **Desktop app** in Google Cloud (the app's YouTube tab has the steps with links: project, enable YouTube Data API v3, consent screen, test user, client) and it's saved with `youtube_setup {"fromDownloads": true}` (imports the downloaded JSON) or with the secure form `account_credentials_request {"platform": "youtube"}` (see `help {"topic": "cuentas"}`).
3. `youtube_login`: opens this machine's browser; the PERSON chooses the account and accepts. `youtube_status` confirms the channel.

## Stream live
- All in one: `youtube_live_create {"title": "...", "privacy": "private", "start": true}` creates the event, takes server and key (memory only, never to disk) and starts streaming. With auto-start, the event goes LIVE when YouTube receives the signal (~10-30 s).
- To an existing event: `youtube_live_list` -> `youtube_live_start {"id"}`.
- Verify: `status` (streaming, fps, dropped frames) and `youtube_live_health {"id"}` (active / good).
- End: `youtube_live_end {"id"}` (closes the event and stops the app). Edit/change status/delete: `youtube_live_update`.
- New channels: YouTube takes up to 24 h to enable live streaming the first time (error "liveStreamingNotEnabled").

## Videos
`youtube_videos`, `youtube_video_update` (title, description, privacy, tags, thumbnail), `youtube_video_delete`, `youtube_video_upload` (without `path`: the app's latest recording).

## Chat
- `youtube_chat {"broadcast": id}` chooses the event; `{"send": "texto"}` writes; it returns the latest messages.
- On screen: `chat_overlay_add {"channel": "all"|"youtube"|"twitch"|"facebook"|"room"}` adds the **Live chat** source (type `ba_chat_overlay`) to a scene; with `all` it mixes all connected platforms (each message with its logo). It's adjusted with `source_update` (settings: show_youtube, show_twitch, show_facebook, show_room, max_messages, font_size, width, height, bg_opacity, show_names, fade_sec). Verify with `foto_fuente`.

## Quota
10,000 units per day per project: listing and editing cost little; uploading a video ~1,600; reading the chat ~5 per read (the app reads every 6 s or more, only with an event chosen).