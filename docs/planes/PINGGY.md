# Pinggy para las conexiones remotas

Análisis del 2026-10-11 (pedido: «implementar pinggy.io para conexiones remotas de
clientes: todos los servicios por ahí, DNS, ver si hace falta un proxy local para una
URL de todos los servicios web, y para WebRTC configurar bien ICE»).

## Estado (2026-10-11): implementado y probado de punta a punta en la Mac con un túnel gratis

- `ba-pinggy` (src/pinggy, libpinggy 0.3.1 fijada por sha256: `make deps-pinggy`), supervisado
  por la sala (src/gateway/Pinggy.*); TURN propio (src/gateway/turn/*); `/api/ice`;
  Configuración → Pinggy (token y API key en la bóveda, dirección pública:
  al azar / subdominio fijo / dominio propio con instrucciones de DNS y «Verificar DNS»);
  MCP `acceso_remoto_estado`. Detalle: docs/funciones/PINGGY.md y docs/funciones/TURN.md.
- Probado (túnel gratis, Argentina → región automática São Paulo, ~220 ms ida y vuelta):
  páginas por la URL pública (invitados, sesión, público), botonera rechazada desde afuera
  (403), `/api/ice` con TURN `turn:<host>:<puerto>?transport=tcp` y política «relay», TURN
  por pinggy (asigna con la credencial efímera, rechaza una clave mala), y un invitado de
  afuera con Chrome (cámara falsa) admitido al programa: 1280×720 entrando a ~670 kbps y el
  retorno saliendo, todo por el TURN.
- Hallazgos: en el plan GRATIS pinggy muestra a los navegadores un aviso intermedio
  («Caution – you are about to visit…») que hay que aceptar; con Pro no. El forwarding
  `tlstcp` no se permite en el gratis y tira la sesión: el TURN va por TCP (y UDP opcional).
  El WebSocket pasa por el túnel HTTP. La cabecera PROXY es de toda la sesión: con el TURN
  prendido va sin PROXY y la IP real sale de `X-Forwarded-For`.
- Falta: probar con la cuenta Pro (token, subdominio fijo/dominio propio, puertos fijos),
  Windows y Linux (libpinggy ya está bajada para las tres plataformas), y un corte de red
  con reconexión.

## Lo que ofrece pinggy

- **libpinggy** (oficial, C++ con API en C, Apache-2.0): https://github.com/Pinggy-io/libpinggy.
  No usa SSH: protocolo propio sobre TLS (OpenSSL 3), con certificado raíz embebido.
  Releases precompilados (v0.3.1) para Windows x64 (`pinggy.dll`, ~11 MB), macOS
  universal (dylib + libssl/libcrypto) y Linux x64/arm64. API: `pinggy_create_config`,
  `pinggy_config_set_token`, `pinggy_config_add_forwarding(config, tipo, binding_url,
  forward_to)`, reconexión automática, lista blanca de IP, Basic/Bearer, cabeceras,
  PROXY protocol v1/v2, callbacks (establecido, reconectando, falló, uso).
- **Paquete de PyPI `pinggy`** (https://pypi.org/project/pinggy/): envoltorio de la misma
  biblioteca. No suma nada. El CLI actual pide Node.js: tampoco.
- **Tipos de túnel**: HTTP(S) (pinggy termina TLS), TCP, TLS (passthrough), TLSTCP (pinggy
  envuelve en TLS un TCP), UDP (un puerto público; fijo con Pro). Varios forwardings por
  sesión, ruteados por nombre de host (hace falta dominio comodín).
  https://pinggy.io/docs/http_tunnels/multi_port_forwarding/
- **Dominios** (Pro): subdominio persistente asignado al token, o dominio propio con
  `CNAME host → <id>.a.pinggy.link.` (sin el proxy naranja si el DNS está en Cloudflare),
  comodín incluido; certificado Let's Encrypt de pinggy. Sin API para crearlos: desde el
  dashboard. https://pinggy.io/docs/custom_domain/
- **API REST**: la documentada es solo monitoreo (`GET /session/active`, historial) con
  `Authorization: Bearer <API key>`. https://pinggy.io/docs/api/api/
- **El backend del panel acepta la misma API key** (verificado 2026-10-12 con la cuenta Pro,
  base `https://dashboard.pinggy.io/backend/api/v1`, rutas sacadas del JS del panel):
  - `GET /token/active` → `[{token, enabled, subscription, preferences:[{subdomain,
    customdomain, tcp_persistent_port, subdomain_id}]}]`: **el token del túnel** y su dirección.
    Con ese token ba-pinggy abre el túnel Pro autenticado («You are authenticated as …»).
  - `GET /subdomain/list`, `POST /subdomain/update {subdomain_id, subdomain_substring}` (cambia
    el subdominio; si es el mismo: 403 «already set»), `POST /subdomain/randomize {subdomain_id}`.
  - `GET /session/active` → `[{id, tokenId, subdomains, ports, forwardings…}]`;
    `POST /session/kill {"session-id": id}` → `{"status": true}` (corta el túnel: probado).
  - `GET /plan/active`, `/user/limits`, `/configuration`, `/device-token/list`.
  - No hay ruta para crear o regenerar el token (el plan Pro trae uno).
  Así la app arma todo con **solo la API key**: `AccesoRemoto::configurarConApiKey`,
  MCP `acceso_remoto_configurar_con_api_key` y `acceso_remoto_cortar_tuneles`. Es una API sin
  documentar: si pinggy la cambia, queda el camino manual (pegar el token).
- **Un túnel por token** (`force` corta el otro). Free: 60 min y URL aleatoria. Pro:
  subdominios, dominios, puertos TCP/UDP fijos. https://pinggy.io/#prices
- **Sin confirmar**: que el WebSocket pase por un túnel HTTP; el hostname de la región
  Sudamérica; si UDP anda (la documentación se contradice).

## Cómo encaja con lo nuestro

- **Web: no hace falta un proxy local nuevo.** El 8787 ya sirve todas las páginas por ruta
  (8789 es TLS delante de 8787). Un forwarding HTTP al gateway da una URL para todo
  (`https://<sub>/guest/`, `/sesion/`, `/publico/`…). Cambio recomendado: un puerto de
  escucha propio del túnel (127.0.0.1:8786) que marque cada conexión como externa, con
  PROXY v1 para la IP real.
- **Problema de seguridad a resolver primero**: lo que entra por el túnel llega desde
  127.0.0.1; hoy «de afuera» se decide por cabeceras de Cloudflare
  (`Botonera.cpp:96`, `Sesion.cpp:225`) y lo demás se tomaría como red local. Nunca
  reenviar la API de control (8788) ni el MCP (8790).
- **WebRTC**: hoy sin STUN/TURN (`Webrtc.cpp:822`), un puerto UDP por conexión del rango
  40000-40099 y `iceServers: []` en los navegadores (`guest/app.js:522`,
  `sesion/app.js:227/258/396`, `presidencia/consola.js:466`). Por pinggy eso no pasa.
  La forma limpia: **un TURN propio dentro de la caja** (127.0.0.1:3478) que relaya solo
  hacia la propia máquina, expuesto por un solo forwarding TLSTCP (`turns:<dominio>:443`,
  atraviesa firewalls) o TCP con puerto fijo (y UDP opcional). El gateway manda
  `iceServers` con credenciales efímeras en la señalización. De dónde sacar el TURN:
  `p2p/base/turn_server.cc` de libwebrtc M123 compilado en el gateway (el `.a` de la Mac
  no lo trae compilado), un TURN mínimo propio, o pion/turn aparte. coturn no.
  De paso arregla la P2P navegador↔navegador de la sesión remota, que hoy falla entre NATs.
- **Mecanismo**: libpinggy en un **proceso hijo propio** (`ba-pinggy`), supervisado como
  `Cloudflared.cpp`: aísla OpenSSL de BoringSSL (que trae libwebrtc) y las caídas. Token
  en Credenciales (cifrado), por variable de entorno.
- **Costo**: el medio va por los servidores de pinggy (latencia de la región) y del lado
  del gateway sobre TCP. Si hay VPS de WireGuard, sigue siendo mejor para el medio.

## Plan

- **P0 — verificar con la cuenta del dueño**: que haya token de túnel (no solo API key);
  WebSocket por túnel HTTP; PROXY v1 en HTTP; región y latencia desde Argentina; cuántos
  forwardings y puertos fijos da el plan; qué devuelve `/session/active`.
- **P1**: `extern/pinggy/` + `make deps-pinggy` (versión y SHA fijados);
  `src/gateway/pinggy/main.cpp` (ba-pinggy) y `src/gateway/Pinggy.{hpp,cpp}`; sección en
  `Config.hpp` y en Configuración → Acceso remoto; estado en la API de control.
- **P2 — web**: puerto del túnel marcado como externo; URL pública en enlaces y QR
  (`main.cpp` 864/913); botonera, sesión remota y `soloLan` sin depender de Cloudflare.
- **P3 — TURN**: servidor TURN local con política de peers, credenciales efímeras,
  `iceServers` en la señalización y en `assets/web/*`, forwarding TLSTCP/TCP (+UDP).
- **P4 — opcional**: nombres por servicio con comodín, TLS de punta a punta (passthrough
  hacia ProxyNombres), monitoreo con la API key.
