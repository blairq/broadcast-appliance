## Set up (once)
1. `twitch_status`: says whether the app or the account is missing.
2. App: the person turns on Twitch two-factor authentication and registers an app at https://dev.twitch.tv/console/apps/create (redirect `http://localhost`, category **Broadcaster Suite**, client type **Public**). It is saved with the secure form `account_credentials_request {"platform": "twitch"}` (or `twitch_setup {"clientId": "..."}`: the ID of a Public client is not a secret).
3. `twitch_login`: returns a **code** and opens twitch.tv/activate; the PERSON signs in with the channel's account and confirms it (show them the code). `twitch_status` confirms the account.

## Stream
- `twitch_live_start {"title": "...", "game": "Just Chatting"}`: applies title/category, takes the channel key (memory only) and streams from the app to `rtmp://live.twitch.tv/app`.
- Verify: `status` (streaming, fps, dropped frames) and `twitch_stream_status` (live, viewers) after ~15 s.
- Stop: `twitch_live_stop`. Twitch has no "events": the channel is live while it receives a signal.

## Channel and VODs
- `twitch_channel` reads; `twitch_channel {"title", "game", "tags", "language"}` changes; `{"search": "..."}` searches categories.
- `twitch_videos`, `twitch_video_delete {"ids": [...]}`. Live streams are saved as VODs only if the channel has "Store past broadcasts" turned on.

## Chat
- `twitch_chat {"on": true}` connects to the channel chat (secure IRC); `{"send": "texto"}` writes as the channel; it returns the latest messages with color and badges.
- On screen: `chat_overlay_add {"channel": "twitch"}` (or `all` to mix platforms).