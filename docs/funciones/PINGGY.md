# ba-pinggy: el túnel de pinggy.io

`ba-pinggy` levanta el túnel de [pinggy.io](https://pinggy.io) con la biblioteca oficial
(libpinggy, C++ con API en C, Apache-2.0) en un **proceso propio**, que el gateway lanza y
supervisa como a cloudflared. Va aparte porque libpinggy trae su OpenSSL y el gateway el BoringSSL
de libwebrtc (en un mismo proceso chocarían), y para que una caída del túnel no tire abajo al
gateway. El análisis y el plan están en [docs/planes/PINGGY.md](../planes/PINGGY.md).

## Dónde está

| Qué | Dónde |
|---|---|
| Fuente | `src/pinggy/main.cpp`, `src/pinggy/CMakeLists.txt` |
| Biblioteca | `extern/pinggy/<plataforma>/` (`make deps-pinggy`, `make deps-pinggy-win`; `tools/deps/pinggy.sh`) |
| Versión | libpinggy **0.3.1**, binario publicado, fijado por SHA-256 |
| Bundle macOS/Linux | `bin/64bit/ba-pinggy` + `bin/64bit/pinggy/` (libpinggy, libssl.3, libcrypto.3) |
| Bundle Windows | `bin/64bit/ba-pinggy.exe` + `bin/64bit/pinggy.dll` (variante MT: OpenSSL y CRT adentro) |

Las bibliotecas de macOS/Linux van en una subcarpeta porque GStreamer ya trae un `libssl.3` y un
`libcrypto.3` con el mismo nombre. El ejecutable las busca con su rpath (`@executable_path/pinggy`,
`$ORIGIN/pinggy`). Pasa `tools/deps/check-portable-macos.sh`.

Compilar: `make deps-pinggy` y después el target `ba-pinggy` (si `extern/pinggy` no está, el
target no existe y el resto compila igual).

## Contrato

**Token**: por la variable de entorno `BA_PINGGY_TOKEN`; vacía o ausente = túnel gratis y
anónimo (sirve para probar). Nunca por la línea de comandos (`--token` da error). El proceso la
borra de su entorno al arrancar.

**Opciones**:

| Opción | Qué hace |
|---|---|
| `--forward <tipo>=<host:puerto>[@<binding>]` | Repetible. `tipo`: `http`, `tcp`, `tls`, `tlstcp`, `udp`. `binding`: dominio (o `dominio:puerto`) público pedido |
| `--region auto\|us\|eu\|ap` | Servidor `a.pinggy.io` (auto) o `us.`/`eu.`/`ap.a.pinggy.io`, siempre al 443 |
| `--host <dominio>` | Binding del primer forwarding `http` que no tenga `@binding` (subdominio persistente o dominio propio, Pro) |
| `--listablanca <ip,...>` | IP o CIDR (v4 y v6) que pueden entrar; el resto ni llega |
| `--haproxy v1\|v2\|no` | PROXY protocol. Por defecto `v1` si hay algún `tcp`/`tls`/`tlstcp`, si no `no` |
| `--xff` | Pide `X-Forwarded-For` (en los hechos ya viene siempre, ver abajo) |
| `--reconectar` | Reconexión automática sin límite (libpinggy cada 5 s y, si la sesión se cae entera, otra desde cero con espera creciente hasta 60 s) |

Rechaza (sale con 2 y un `error`) cualquier forward a los puertos **8788** (API de control) y
**8790** (MCP), con el host que sea.

**Stdout**: un objeto JSON por línea, con flush:

```
{"ev":"conectando"}
{"ev":"listo","urls":[...],"forwardings":[{"tipo":"http","local":"127.0.0.1:8786","publico":"https://...","publicos":["https://...","https://..."]}]}
{"ev":"reconectando"}
{"ev":"reconectado","urls":[...],"forwardings":[...]}
{"ev":"error","error":"...","recuperable":true|false}
{"ev":"uso","elapsedTime":..,"numLiveConnections":..,"numTotalConnections":..,"numTotalReqBytes":..,"numTotalResBytes":..,"numTotalTxBytes":..}
{"ev":"aviso","texto":"..."}
```

- `publico` es la URL principal de ese forwarding (la `https://` si hay); `publicos`, todas las
  que le dio pinggy. Para `tcp`/`udp` viene como `tcp://host:puerto` / `udp://host:puerto`; para
  `tls`, `tls://host` (puerto 443). El reparto sale del mapa de forwardings de pinggy (si no
  llega en 1,5 s, se reparte la lista de URLs por esquema).
- `uso`: como mucho uno cada 5 s, con los campos tal cual los da pinggy (trae un `elaspedTime`
  mal escrito además de `elapsedTime`).
- `aviso`: los mensajes de bienvenida del servidor (p. ej. que el túnel gratis vence a los 60 min)
  y los forwardings rechazados.
- Stderr: texto libre (servidor elegido, mapa de forwardings, reintentos). El log propio de
  libpinggy va a stdout y puede tener el token: está apagado; `BA_PINGGY_LOG=<archivo>` lo prende.

**Salida**: SIGTERM/SIGINT (o Ctrl+C/cierre en Windows) corta prolijo y sale con **0** (medido:
0,14 s). Si una llamada de libpinggy está bloqueada (conectando), a los 4 s sale igual con 0. Error
fatal: **1**; opciones inválidas: **2**. Si el proceso padre desaparece (macOS no tiene
PDEATHSIG), cierra el túnel y sale.

## Lo que hace por su cuenta

- **Elige la IP del servidor**: libpinggy se conecta a la primera dirección que resuelve y, si no
  contesta, espera el timeout entero de TCP sin probar otra (visto: IPv6 colgado en SYN_SENT más de
  un minuto). ba-pinggy prueba antes las IPv4 (4 s cada una) y le pasa la que contesta; si ninguna,
  le deja el nombre.
- **SNI siempre `a.pinggy.io`**, también en `us.`/`eu.`/`ap.`: el certificado de todos es de
  `a.pinggy.io` y con otro SNI cortan la conexión (`SSL_ERROR_SYSCALL: Connection reset`).
- **`force`** siempre: un túnel por token, y si quedó colgada una sesión vieja (el gateway se
  reinició), la nueva la desplaza.
- **Token rechazado**: pinggy NO falla con un token inválido, levanta un túnel gratis y anónimo y lo
  dice solo en el saludo («You are not authenticated»). ba-pinggy lo detecta, no anuncia `listo`,
  emite `error` (`recuperable:false`) y sale con 1, aun con `--reconectar`.
- **OpenSSL aislado**: apunta `OPENSSL_CONF`/`OPENSSL_MODULES`/`OPENSSL_ENGINES` a
  `bin/64bit/pinggy/` (sin archivos de configuración), para no leer nada del sistema. En Windows
  la DLL se carga diferida (`/DELAYLOAD`) para que su CRT vea esas variables.

## P0: lo que se verificó (2026-10-11, túnel gratis, desde la Mac en Argentina)

Con un servidor de prueba local (HTTP + WebSocket, TCP eco, UDP eco) y curl/clientes propios.

| Prueba | Resultado |
|---|---|
| HTTP por túnel `http` | Anda. 200 en todos los pedidos |
| **WebSocket** por túnel `http` | **Anda**: `101 Switching Protocols`, 30 ecos de texto, 200 KB binario ida y vuelta, cierre limpio. El servidor de pinggy es un proxy en Go: el pedido llega con `User-Agent: Go-http-client/1.1` y los nombres de cabecera normalizados (hay que leerlas sin distinguir mayúsculas) |
| Cabeceras en `http` | Siempre (modo reverse proxy de fábrica): `X-Forwarded-For`, `Forwarded`, `X-Forwarded-Host`, `X-Forwarded-Proto: https`. La conexión local llega desde 127.0.0.1 |
| **PROXY en `http`** | **Es global de la sesión**: con `--haproxy v1` (o el defecto, si hay un `tcp`) el túnel `http` TAMBIÉN manda `PROXY TCP6 ...` al principio de cada conexión, además de las cabeceras. Sin `tcp` y sin `--haproxy` no lo manda |
| PROXY en `tcp` | v1: `PROXY TCP6 <ip cliente> <ip servidor> <puerto> <puerto>` (TCP6 si el cliente vino por IPv6). v2 también anda (firma binaria, `src=` correcto) |
| `tls` (passthrough) | Anda: llega el ClientHello crudo (el TLS lo termina la caja) |
| `tlstcp` | **No permitido en el plan gratis** («Forwarding type `tlstcp` not allowed») y tira abajo toda la sesión. Con Pro: sin probar |
| **Varios forwardings** | http + tcp + udp en una sola sesión gratis: **anda**, cada uno con su URL (`tcp://host:35127`, `udp://host:44845`). Pero **dos forwardings al 443 (http + tls) chocan**: «Requested domain ... is already bound» (en el gratis el host es uno solo por sesión; con Pro hay que darle a cada uno su binding) |
| **UDP** | **Anda**: 10/10 ecos, sin cabecera agregada |
| Lista blanca | Anda (CIDR v4 y v6): fuera de la lista la conexión se corta antes de llegar |
| Región `auto` | Sale por **Brasil** (`br.a.pinggy.io` → `lin.br.*.a.pinggy.click`, Linode São Paulo). TCP al servidor: ~55 ms |
| Latencia (ida y vuelta por el túnel, cliente y caja en la misma Mac) | auto/Brasil: WebSocket 212–276 ms (mediana 223), TCP 198–288 (mediana 227), UDP mediana 224. us: ~1000 ms, eu: ~990 ms, ap: ~1670 ms. HTTPS de punta a punta: conectar 55 ms, TLS 120 ms, primer byte ~420 ms |
| Token inválido | Túnel gratis anónimo sin error (ver arriba: ba-pinggy lo corta) |
| `--reconectar` | La primera conexión llega por el callback de reconexión (ba-pinggy la anuncia como `listo`). La caída real y la vuelta no se pudieron provocar en la prueba |

**Ojo con el gratis**: el subdominio al azar lleva **la IP pública del cliente** adentro
(`xxxxx-143-105-137-116.free.pinggy.net`, o la IPv6 si conectó por IPv6) y vence a los 60 min. Sirve
para probar, no para dar enlaces.

## Lo que tiene que saber el resto

- El puerto del túnel en el gateway tiene que aceptar **PROXY v1 (TCP4 y TCP6)** si se usa
  `--haproxy v1` o si hay algún forwarding tcp/tls en la misma sesión (es global). Si el puerto
  `http` no lo quiere, pasar `--haproxy no` y tomar la IP de `X-Forwarded-For` (que viene siempre;
  como solo escucha en 127.0.0.1 y lo único que conecta es ba-pinggy, se puede confiar).
- Para el TURN: `tlstcp` no está en el gratis; `tcp` (puerto al azar, fijo con Pro) y `udp` sí.
  `tls` comparte el 443 con el `http`: con Pro, darle su propio binding (`@turn.<dominio>`).
- `forwardings[].publico` puede ser `null` si pinggy no devolvió URL para ese forwarding.
- Después de un `listo` puede venir un `error`: tratarlo como túnel caído.
