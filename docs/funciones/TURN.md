# TURN de la caja

Para qué: que un navegador de afuera (entrando por pinggy, ver
`docs/planes/PINGGY.md`) mande y reciba el medio de WebRTC sin abrir puertos en el
router. pinggy expone **un** puerto TCP/TLS: con un forwarding `tlstcp` termina el
TLS en su borde y entrega TCP plano a `127.0.0.1:3478`. Opcionalmente, un
forwarding UDP a `127.0.0.1:3478`.

Código: `src/gateway/turn/` (`Stun.*` mensajes, `Servidor.*` el servidor, `Ice.*`
credenciales y `/api/ice`), navegador en `assets/web/comun/ice.js`, prueba en
`src/gateway/tests/test_turn.cpp`.

## Diseño

- TURN mínimo propio (RFC 5766/8656, sin RFC 6062). No se usa `cricket::TurnServer`
  (el `libwebrtc.a` de la Mac no lo trae compilado) ni coturn (portable: nada del
  sistema).
- Transporte cliente↔servidor: **TCP** (marcos STUN y ChannelData; sobre TCP el
  ChannelData va rellenado a múltiplo de 4, en los dos sentidos) y, si se pide,
  **UDP**. El relay es siempre UDP (`REQUESTED-TRANSPORT` 17; otro → 442).
- Métodos: Binding (sin credencial), Allocate, Refresh, CreatePermission,
  ChannelBind, indicaciones Send/Data y ChannelData. Atributos que exigen
  entenderse y no se entienden → 420. IPv6 → 440. `RESERVATION-TOKEN` → 508;
  `EVEN-PORT` se respeta.
- Una asignación por conexión TCP (o por dirección UDP). Cerrar la conexión TCP
  borra la asignación. Una conexión TCP sin asignación se corta a los 30 s.
- Vida: la pedida, entre 600 s y `turn.vidaMaxS`; permisos 300 s; canales 600 s.
- Un hilo propio con `poll()` (WSAPoll en Windows), sockets no bloqueantes.
  Hacia el cliente TCP: cola por conexión; si se llena (`turn.colaTcpKB`) el
  **medio** se descarta (como en UDP), el control no.

## Credenciales

- Credencial larga (RFC 5389 §10.2): `MESSAGE-INTEGRITY` = HMAC-SHA1 con
  `MD5(usuario:realm:clave)`; toda respuesta autenticada va firmada y con
  `FINGERPRINT`. Un `FINGERPRINT` entrante mal → se descarta.
- Usuarios efímeros estilo «TURN REST»: `usuario = "<vence_unix>:<id>"`,
  `clave = base64(HMAC-SHA1(secreto, usuario))`. El `secreto` es al azar en cada
  arranque (`turn::secretoAleatorio()`): reiniciar el gateway invalida todo.
- Nonce sin estado: `hex(vence) + hex(HMAC(secretoNonce, hex(vence)))[0..16]`,
  vale `turn.nonceS`; vencido o trucho → 438 con uno nuevo.
- El vencimiento del usuario cuenta para asignaciones **nuevas** (401). Una
  asignación viva sigue refrescándose con su usuario aunque haya vencido (así la
  credencial puede ser corta sin cortar una llamada larga). Otro usuario sobre una
  asignación ajena → 441.

## Política de seguridad (no es un relay abierto)

Solo se relaya hacia:

1. una IP de `peersPermitidos` (las de la propia máquina, con 127.0.0.1) **y** un
   puerto de `[rtcMin, rtcMax]` (los de WebRTC del gateway, `rtcPortMin/Max`), o
2. el relay de **otra asignación de este mismo servidor** (la P2P
   navegador↔navegador de la sesión remota: los dos de afuera se encuentran en la
   caja).

CreatePermission hacia una IP que no es de la máquina → 403 (no se instala
ninguna). ChannelBind hacia un peer fuera de la política → 403. Send/ChannelData
hacia afuera de la política, y todo lo que llegue al relay desde un origen sin
permiso o fuera de la política → descartado (se cuenta en `rechazosPeer`).
Topes: `turn.maxAsignaciones`, `turn.maxConexiones` (486 / se corta la conexión).
El forwarding de pinggy hacia el TURN va **sin PROXY protocol** (el TURN no lo
entiende y cierra la conexión).

## Ajustes (ba::ajustes, gateway.json → "ajustes")

| clave | respaldo | qué |
|---|---|---|
| `turn.maxAsignaciones` | 64 | asignaciones vivas a la vez |
| `turn.maxConexiones` | 128 | conexiones TCP a la vez |
| `turn.vidaMaxS` | 3600 | vida máxima de una asignación |
| `turn.nonceS` | 600 | vida de un nonce |
| `turn.colaTcpKB` | 1024 | cola hacia un cliente TCP antes de descartar medio |
| `turn.credencialS` | 3600 | vida de la credencial que da `/api/ice` |

Faltan en la página de Configuración (SettingsModel), si se quieren a mano.

## `GET /api/ice` (contrato)

Respuesta (`application/json`, `Cache-Control: no-store`):

```json
{"iceServers":[{"urls":["turns:sala.ejemplo.com:443?transport=tcp",
                        "turn:x.a.pinggy.link:41234?transport=tcp",
                        "turn:u.a.pinggy.link:40000?transport=udp"],
                "username":"1791234567:9f2c01a4b3de","credential":"…"}],
 "politica":"relay","ttl":3600}
```

- **Desde afuera** (la conexión entró por la escucha del túnel, `externo`):
  los extremos públicos de pinggy y `politica: "relay"`. Por qué relay: por pinggy
  el gateway no tiene IP pública ni STUN, así que los candidatos host/srflx del
  navegador no le sirven a nadie; con relay el navegador no prueba contra IPs
  privadas del gateway que pueden coincidir con la red de su casa, no anuncia sus
  propias IP privadas y conecta antes. Sin extremos públicos → `[]` y `"all"`.
- **Desde la LAN**: `[]` y `"all"` (lo de siempre: directo). Si el TURN escucha
  también en la LAN (`Config::direccion` 0.0.0.0 o la IP de la LAN) se puede dar
  `turn:<ip-lan>:3478` con `"all"`: los candidatos host siguen ganando por
  prioridad, y un navegador de la sala consigue un candidato relay en la caja, el
  único destino que uno de afuera puede usar (consola de la presidencia en la
  sala ↔ integrante de afuera).
- **TURN apagado**: `[]`, `"all"`, `ttl` 0.
- **Sin autenticación**, a propósito: las páginas se autentican con tokens de un
  solo uso por el WebSocket (invitado, pantalla) o con el fragmento `#t=` que
  nunca viaja al servidor (sesión, presidencia), así que no hay una credencial
  común que mandar en un GET. Lo que se entrega solo sirve para llegar a los
  puertos WebRTC de la propia caja, que sin el ufrag/clave ICE de la señalización
  no aceptan nada; los topes de asignaciones acotan el abuso. Conviene el mismo
  rate-limit por IP que `/api/join`.
- El cuerpo lo arma `turn::respuestaIce(cfg, turnActivo, extremos, externa)`.

## Navegador (`assets/web/comun/ice.js`)

`window.BAIce.config()` (sincrónico, lo último que llegó) y `await BAIce.listo()`
(espera la respuesta, con tope de 4 s). Pide `/api/ice` al cargar y renueva a
mitad del `ttl`. Si falla, 404 o gateway viejo → `{iceServers: []}`. Lo usan:
`guest`, `sesion` (las tres conexiones), `presidencia`, `board` y `cast`. La
consola del operador (`admin`, solo 127.0.0.1) no.
