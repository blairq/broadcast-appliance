Three ways, **one at a time** (Settings → “Access from the internet”): **Pinggy** (the usual one), **Cloudflare** (the organization's domain on Cloudflare) and **Own tunnel (WireGuard)** (a server with a public IP). Changing any of them restarts the conference: **never with the event live** (the tools refuse it).

## Pinggy: with the API key, everything on its own
The pinggy Pro account has **two different keys**: the **API key** (manages the account) and the **tunnel token** (opens the tunnel). **The operator only has to give you the API key**; the app fetches the token.
1. `acceso_remoto_clave {"tipo": "api_key", "clave": "…"}`: stores it encrypted (it is never returned or written to the log; don't repeat it in the chat).
2. `acceso_remoto_configurar_con_api_key {}`: asks the account for the token (stores it encrypted), its **fixed subdomain** (something.a.pinggy.link) and its **fixed TCP port** (for the video of those outside), and turns on the tunnel with that. With `{"subdominio": "sala-concejo"}` it first **changes the subdomain in the account** (it becomes sala-concejo.a.pinggy.link); if pinggy doesn't grant it (taken or invalid) it says so: offer another name.
3. `acceso_remoto_estado`: wait for `pinggy.estado` = `listo` and give the operator `pinggy.urlPublica` and the `enlaces` (guests, remote session, audience). For them to scan it: `qr_mostrar`.
- `acceso_remoto_panel`: which tunnels the account has active, as pinggy sees it. `acceso_remoto_cortar_tuneles`: cuts them all (e.g. another machine left with the same token: pinggy allows **one tunnel per token**).
- **Don't paste the API key as the token** (`tipo: "token"`): it doesn't open the tunnel (pinggy answers “not authenticated”) and the app warns that they are the same. The token by hand (`tipo: "token"`) is only for when there is no API key.
- `acceso_remoto_configurar` changes the rest (region, own domain with `host` and `cname_destino`, TURN, allowlist). Organization's domain: `acceso_remoto_dns_instrucciones` writes the text for whoever manages the DNS and `acceso_remoto_dns_verificar` checks the CNAME.
- Without a Pro plan (no token): it still works in free mode, 60 minutes, random address and with a pinggy banner the guests have to accept.

## Cloudflare
You need the **Cloudflare tunnel token** (Zero Trust → Networks → Tunnels): `acceso_remoto_clave {"tipo": "cloudflare", "clave": "…"}`, then `settings_set {"values": {"gateway.tunnel": true, "gateway.tunnelHost": "sala.midominio.com"}}`. Without a token it stays off.

## Own tunnel (WireGuard)
Your own server with a public IP: `settings_set` with `wg.enabled`, `wg.endpoint` (host:port), `wg.serverKey`, `wg.address`, `wg.publicAddress`. It's the best path for video, but someone has to have set up the server.
