# ba-web: el servidor web público

`ba-web` (src/web, C11) es el servidor que atiende lo que entra por el túnel de pinggy.
Va adelante del gateway: sirve el sitio del evento y los archivos de la sala desde memoria,
comprimidos, guarda un microcaché de la API del evento y le pasa todo lo demás al gateway.
Es otro proceso a propósito: si los visitantes lo tapan o se cae, la sala y la emisión siguen.

```
visitante ──https──> pinggy ──> ba-web 127.0.0.1:8785 ──> gateway 127.0.0.1:8786 (escucha del túnel)
```

## Arquitectura (la de nginx)

- **Master**: no atiende a nadie. Lee la configuración, abre la escucha, lanza los workers y
  los vuelve a lanzar si se caen. Si el gateway muere (`--padre`), se cierra.
- **Workers** (los núcleos, hasta 4): cada uno con su bucle de eventos — epoll (Linux),
  kqueue (macOS), WSAPoll (Windows) —, de un hilo y no bloqueante: miles de conexiones por
  worker sin un hilo por conexión. En POSIX son `fork()` del master; en Windows el master se
  relanza con `--worker` y les pasa la escucha con `WSADuplicateSocket`.
- El **gateway** (BaWeb.cpp) escribe la configuración (`Runtime/ba-web.conf`), lo lanza, y si
  el master se cae lo relanza (con backoff). Si falta el binario, pinggy va directo al gateway.

## Qué hace

| | |
|---|---|
| Estáticos | `/invitados/`, `/evento/_/` (vivo.js, marco…) y `/evento/` (la carpeta Web/ del evento en vivo, que el gateway escribe en `Runtime/evento-web.dir`). Caché en memoria que revalida con `stat` como mucho cada 1 s; los archivos grandes van por `sendfile`. |
| HTTP | HTTP/1.1 y 1.0, keep-alive, pipelining, HEAD, `ETag`/`Last-Modified` y 304, `Range` (206/416, `If-Range`), pedidos mal formados rechazados (CL+TE, dos Host, obs-fold…), rutas normalizadas (sin `..`, `%00` ni salirse de la raíz por un enlace). |
| gzip | RFC 9110: `Accept-Encoding` con valores q (`gzip;q=0` es no), `x-gzip`, `*`; `Content-Encoding: gzip`, `Vary: Accept-Encoding`, un ETag propio para lo comprimido, sin `Range` sobre lo comprimido. Lo comprimido se guarda: no se comprime en cada pedido. |
| Proxy | Los GET/HEAD van al gateway con buffer y se comprimen si conviene (html, css, js, json, svg…); WebSocket, POST, subidas y SSE pasan como túnel crudo. La línea PROXY v1 de pinggy se le pasa al gateway (la IP real no se pierde). |
| Microcaché | `/`, `/api/v1/evento` y `/api/v1/evento/vivos`: una respuesta del gateway sirve a todos por `evento.cacheMs`; vencida, se sirve la vieja mientras se trae la nueva y nunca hay dos pedidos iguales en vuelo. Los ids de mirada (`?v=`) los junta y se los manda al gateway (`X-BA-Vistos`, con la clave `BA_WEB_CLAVE`): el «N mirando» cuenta también a los que atendió el caché. |

## Configuración

La genera el gateway; la sintaxis es un subconjunto de la de nginx (ver `src/web/conf.c`):
`worker_processes`, `listen … proxy_protocol`, `upstream`, `gzip*`, `location [=] /ruta { root |
alias | alias_file | proxy_pass | return; index; try_files $uri @upstream; proxy_cache_valid;
proxy_cache_key; add_header }`. `ba-web -t -c archivo` la revisa.

En la configuración del gateway: `web {activo: true, puerto: 8785, procesos: 0}`.

## Medido (Mac M-series, 2 workers, vivo.js comprimido)

`ab -k -c 100`: ~78 600 pedidos/s; sin keep-alive ~27 000/s; 0 errores.

## Prueba

`build/src/web/test_ba_web`: arranca ba-web con un gateway de mentira y le pega como un
navegador (gzip, condicionales, rangos, pipelining, proxy, microcaché, PROXY v1, WebSocket,
POST, SSE, pedidos mal formados). Lo comprimido se descomprime y se compara con el original.
