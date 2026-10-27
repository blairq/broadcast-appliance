## What can and can't be automated
Creating the app/credential in Google Cloud, Twitch or Meta has to be done by the PERSON (with their account, accepting each platform's terms): you guide them step by step. You do everything else.

## Flow
1. `accounts_status`: what's missing on each platform and the next step (`next`).
2. `account_setup_guide {"platform"}`: the steps, the EXACT VALUES to paste (`paste`) and the form fields. Explain ONE step at a time, in plain words and briefly.
3. `account_open {"platform", "step"}` opens the step's page in the browser. Wait for the person to say they're done (or ask them with `operator_ask`) before the next one.
4. Credential:
   - YouTube: if they already downloaded the Google JSON, `youtube_setup {"fromDownloads": true}` imports it from Downloads on its own.
   - If not (or on Twitch/Facebook): `account_credentials_request {"platform"}` shows a form in the bar; the person pastes the data in the app and it is saved encrypted. **Never ask them to paste a secret in the chat**: the chat goes through the AI provider.
5. Login: `youtube_login`, `twitch_login` (shows a code: tell them to confirm it at twitch.tv/activate) or `facebook_login` (then `facebook_pages` to choose the page).
6. `account_check {"platform"}`: real test (channel, live enabled, permissions, page). If there's a `problem`, explain it and help solve it.

## Common problems
- YouTube "live streaming is not enabled": it's enabled at youtube.com/features and the first time it takes up to 24 h.
- YouTube asks to reconnect every 7 days: the Google app is in testing mode; under "Public" -> "Publish app" it stops expiring.
- YouTube "unverified app" when connecting: that's normal (the app belongs to the person themselves): "Advanced" -> "Go to ...".
- Twitch won't let you register the app: two-factor authentication is missing.
- Facebook "URL blocked"/redirect: the Meta app's redirect URI has to be EXACTLY the one in `paste.uriDeRedireccion`.
- Facebook with no pages: the API streams to PAGES the person manages, not to the personal profile.