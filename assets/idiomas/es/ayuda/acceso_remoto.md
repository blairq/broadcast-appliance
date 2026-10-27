Tres caminos, **uno por vez** (Configuración → «Acceso desde internet»): **Pinggy** (el de siempre), **Cloudflare** (dominio de la organización en Cloudflare) y **Túnel propio (WireGuard)** (un servidor con IP pública). Cualquiera reinicia la conferencia al cambiarlo: **nunca con el evento en vivo** (las herramientas lo rechazan).

## Pinggy: con la API key, todo solo
La cuenta Pro de pinggy tiene **dos claves distintas**: la **API key** (maneja la cuenta) y el **token del túnel** (abre el túnel). **El operador solo tiene que pasarte la API key**; el token lo trae la app.
1. `acceso_remoto_clave {"tipo": "api_key", "clave": "…"}`: la guarda cifrada (nunca se devuelve ni se escribe en el registro; no la repitas en el chat).
2. `acceso_remoto_configurar_con_api_key {}`: le pide a la cuenta el token (lo guarda cifrado), su **subdominio fijo** (algo.a.pinggy.link) y su **puerto TCP fijo** (para el video de los de afuera), y prende el túnel con eso. Con `{"subdominio": "sala-concejo"}` primero **cambia el subdominio en la cuenta** (queda sala-concejo.a.pinggy.link); si pinggy no lo da (ocupado o inválido) lo dice: ofrecé otro nombre.
3. `acceso_remoto_estado`: esperá `pinggy.estado` = `listo` y pasale al operador `pinggy.urlPublica` y los `enlaces` (invitados, sesión remota, público). Para que lo escanee: `qr_mostrar`.
- `acceso_remoto_panel`: qué túneles tiene activos la cuenta, visto desde pinggy. `acceso_remoto_cortar_tuneles`: los corta todos (p. ej. otro equipo que quedó con el mismo token: pinggy deja **un túnel por token**).
- **No pegues la API key como token** (`tipo: "token"`): no abre el túnel (pinggy contesta «no autenticado») y la app avisa que son iguales. El token a mano (`tipo: "token"`) es solo si no hay API key.
- `acceso_remoto_configurar` cambia lo demás (región, dominio propio con `host` y `cname_destino`, TURN, lista blanca). Dominio de la organización: `acceso_remoto_dns_instrucciones` arma el texto para el que maneja el DNS y `acceso_remoto_dns_verificar` controla el CNAME.
- Sin plan Pro (sin token): anda igual en modo gratis, 60 minutos, dirección al azar y con un cartel de pinggy que los invitados tienen que aceptar.

## Cloudflare
Hace falta el **token del túnel de Cloudflare** (Zero Trust → Networks → Tunnels): `acceso_remoto_clave {"tipo": "cloudflare", "clave": "…"}`, después `settings_set {"values": {"gateway.tunnel": true, "gateway.tunnelHost": "sala.midominio.com"}}`. Sin token queda apagado.

## Túnel propio (WireGuard)
Un servidor propio con IP pública: `settings_set` con `wg.enabled`, `wg.endpoint` (host:puerto), `wg.serverKey`, `wg.address`, `wg.publicAddress`. Es el mejor camino para el video, pero alguien tiene que haber armado el servidor.