Trois voies, **une à la fois** (Paramètres → « Accès depuis internet ») : **Pinggy** (celui d'habitude), **Cloudflare** (domaine de l'organisation chez Cloudflare) et **Tunnel propre (WireGuard)** (un serveur avec une IP publique). Changer de voie redémarre la conférence : **jamais avec l'événement en direct** (les outils le refusent).

## Pinggy : avec la clé d'API, tout se fait seul
Le compte Pro de pinggy a **deux clés différentes** : la **clé d'API** (gère le compte) et le **jeton du tunnel** (ouvre le tunnel). **L'opérateur n'a qu'à vous donner la clé d'API** ; le jeton, l'application le récupère.
1. `acceso_remoto_clave {"tipo": "api_key", "clave": "…"}` : l'enregistre chiffrée (elle n'est jamais renvoyée ni écrite dans le journal ; ne la répétez pas dans le chat).
2. `acceso_remoto_configurar_con_api_key {}` : demande au compte le jeton (l'enregistre chiffré), son **sous-domaine fixe** (quelquechose.a.pinggy.link) et son **port TCP fixe** (pour la vidéo des personnes de l'extérieur), et ouvre le tunnel avec cela. Avec `{"subdominio": "salaconcejo"}` (seulement des minuscules et des chiffres : pinggy n'accepte pas les tirets), il **change d'abord le sous-domaine dans le compte** (cela donne salaconcejo.a.pinggy.link) ; si pinggy ne le donne pas (pris ou invalide), il le dit : proposez un autre nom.
3. `acceso_remoto_estado` : attendez `pinggy.estado` = `listo` et donnez à l'opérateur `pinggy.urlPublica` et les `enlaces` (invités, séance à distance, public). Pour qu'il le scanne : `qr_mostrar`.
- `acceso_remoto_panel` : quels tunnels le compte a actifs, vu depuis pinggy. `acceso_remoto_cortar_tuneles` : les coupe tous (par ex. un autre appareil resté avec le même jeton : pinggy autorise **un tunnel par jeton**).
- **Ne collez pas la clé d'API comme jeton** (`tipo: "token"`) : cela n'ouvre pas le tunnel (pinggy répond « non authentifié ») et l'application signale qu'elles sont identiques. Le jeton à la main (`tipo: "token"`), c'est seulement s'il n'y a pas de clé d'API.
- `acceso_remoto_configurar` modifie le reste (région, domaine propre avec `host` et `cname_destino`, TURN, liste blanche). Domaine de l'organisation : `acceso_remoto_dns_instrucciones` rédige le texte pour la personne qui gère le DNS et `acceso_remoto_dns_verificar` contrôle le CNAME.
- Sans offre Pro (sans jeton) : cela marche quand même en mode gratuit, 60 minutes, adresse aléatoire et avec un avertissement pinggy que les invités doivent accepter.

## Cloudflare
Il faut le **jeton du tunnel Cloudflare** (Zero Trust → Networks → Tunnels) : `acceso_remoto_clave {"tipo": "cloudflare", "clave": "…"}`, puis `settings_set {"values": {"gateway.tunnel": true, "gateway.tunnelHost": "sala.midominio.com"}}`. Sans jeton, il reste éteint.

## Tunnel propre (WireGuard)
Un serveur à vous avec une IP publique : `settings_set` avec `wg.enabled`, `wg.endpoint` (host:port), `wg.serverKey`, `wg.address`, `wg.publicAddress`. C'est la meilleure voie pour la vidéo, mais quelqu'un doit avoir monté le serveur.
