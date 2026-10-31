# ba-web: el servidor web público

`ba-web` (src/web, C11) es el servidor que atiende lo que entra por el túnel de pinggy.
Va adelante del gateway: sirve el sitio del evento y los archivos de la sala desde memoria,
comprimidos, guarda un microcaché de la API del evento y le pasa todo lo demás al gateway.
Es otro proceso a propósito: si los visitantes lo tapan o se cae, la sala y la emisión siguen.

```
visitante ──https──> pinggy ──> ba-web 127.0.0.1:8785 ──────> gateway 127.0.0.1:8786 (escucha del túnel)
red local ──https──────────────> ba-web 0.0.0.0:8789 (TLS) ──> gateway 127.0.0.1:8787 (escucha local)
```

## HTTPS de la red local

ba-web atiende también el HTTPS de la red local (el enlace `https://<IP>:8789` de la sala), con
mbedTLS y sin bloquear: así la gente de la sala también tiene gzip, caché y límites, y el gateway
no cifra nada por cada visitante.

- **El certificado es el mismo de siempre** (el propio de `Credenciales/tls-propio`, o el
  autofirmado): misma huella, los navegadores que ya lo aceptaron no vuelven a avisar. El gateway
  lo saca de `Credenciales/` (cifrado) y se lo pasa a ba-web **por la entrada estándar** (un pipe
  que se cierra): nunca toca el disco en claro, ni la línea de comandos, ni el entorno.
  `ProcessOptions::stdinData` (Process_posix/win) es lo que lo permite.
- Cada worker arma su TLS con su propio generador al azar (si lo heredaran del master con
  `fork()`, sacarían los mismos números). TLS 1.2 (ECDHE + AES-GCM/ChaCha20), como el terminador
  del gateway; 1.0 y 1.1 se rechazan.
- Cada escucha tiene su destino (`listen … ssl upstream=127.0.0.1:8787`): la red local va a la
  escucha local del gateway (ICE con el TURN local, como siempre); el túnel, a la del túnel.
- **En Windows, de fábrica, no** (`web.lan: false`): el firewall de Windows pregunta por cada
  programa que escucha en la red, y permitir pide ser administrador. `ba-gateway` ya está
  permitido; `ba-web.exe` sería otro aviso, y si nadie lo acepta el firewall crea una regla
  «Block» y la red local deja de entrar (pasó en la PC de pruebas). Ahí el HTTPS de la red local
  lo sigue atendiendo el gateway, como antes; ba-web atiende el túnel (por 127.0.0.1, sin aviso).
  Con `web {lan: true}` en la configuración del gateway lo atiende ba-web (hay que permitirlo en
  el firewall).
- Con WireGuard (cuyo puente manda la línea PROXY antes del TLS) sigue el terminador del gateway.
  Si ba-web no arranca, el terminador escucha en el mismo puerto: las URLs no cambian.
- Probado: 20 000 pedidos por HTTPS con pipelining en 50 conexiones (~34 000/s), archivo de 5 MB
  íntegro (SHA-256), handshake roto / HTTP en claro / conexión muda (se cierra a los 3 s) / TLS
  1.0 y 1.1 rechazados, y 5 000 pedidos rotos cifrados con ASan, sin errores. Con la app: misma
  huella, un invitado entra por la red local y su WebSocket anda; matado el master, el gateway lo
  relanza con el certificado.

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

## Seguridad

Lo que puede intentar alguien desde internet y qué hace ba-web:

| Ataque | Defensa |
|---|---|
| Slowloris (cabecera de a un byte) | el plazo de la cabecera corre desde el primer byte y no se estira (`client_header_timeout`, 30 s) |
| Conexiones mudas por miles | límite por IP desde la línea PROXY; casi lleno, se desalojan las ociosas más viejas |
| Pipelining en cascada (miles de pedidos en un paquete) | una cola: un pedido por conexión y por vuelta del bucle; `keepalive_requests` 1000 |
| Contrabando de pedidos (request smuggling) | CRLF estricto (ni CR ni LF sueltos, ni controles), CL+TE o dos CL distintos → 400; al gateway, una conexión nueva por pedido (nunca se mezclan dos visitantes) |
| IP falsa (para saltear el límite de intentos de PIN del gateway) | la línea PROXY se acepta solo si pinggy la manda (sin TURN), desde loopback y con una IP válida; la IP real es la última de `X-Forwarded-For` (la pone pinggy); al gateway ba-web le manda su propia línea PROXY, nunca la del visitante |
| Tapar al gateway (hilo por conexión) | `upstream_max_conns` por destino (503; también el microcaché), pedidos al gateway por IP con balde de fichas (`limit_upstream_rate`, 429), túneles por IP (`limit_tunnels_per_ip`). Los dos topes los escribe el gateway desde SUS cupos (`room.maxConexiones` por escucha, `room.maxConexionesPorIp`) repartidos entre los workers: ba-web nunca le pide más de lo que acepta |
| Que el gateway entienda otra cosa que ba-web (contrabando, cabeceras raras) | al gateway le llega una cabecera ARMADA por ba-web con lo que validó (línea de pedido y «Nombre: valor» recortados), sin `X-Forwarded-For`/`Forwarded`/`X-Real-IP` del visitante; la IP va siempre en la línea PROXY de ba-web; un cuerpo `chunked` (el gateway lee solo Content-Length) se corta con 411 |
| Envenenar el microcaché | el pedido del caché no lleva nada del visitante (ni su `Host`) |
| Inflar el «N mirando» | pocos ids de mirada por IP y por ventana (`ba_vistos_per_ip`) |
| Rutas | `..`, `%2e%2e`, `%2f`, `%00`, `\`, controles codificados → 400; enlaces que salen de la raíz, archivos ocultos (`.env`, `.git`), `::$DATA`, `CON`/`NUL`/`COM1`, nombres 8.3 (`SECRET~1`), `nombre.` → 404 sin preguntarle al gateway |
| Inyectar cabeceras (`%0d%0a`) | prohibido en la ruta; la redirección del directorio va codificada y relativa |
| TRACE, CONNECT | 405 |
| Sin descriptores (EMFILE) | se sube el tope al arrancar (o se achica `worker_connections`); si igual falta, se pausa aceptar 100 ms en vez de girar al 100 % |
| Memoria | cabecera hasta 16 KiB, 64 KiB por sentido en túneles, caché de archivos con tope, sin multipart en `Range` |

**Probado** con `src/web/tests/ataques.py` (34 ataques y un fuzz de pedidos rotos) contra una versión
compilada con AddressSanitizer y UBSan, y con `--gateway` contra el gateway DE VERDAD detrás de
ba-web (inundación de 300 conexiones lentas, contrabando, cuerpos rotos, WebSocket basura y el
fuzz entero: el gateway sigue vivo y atendiendo, 0 caídas):

```
clang -std=c11 -g -O1 -fsanitize=address,undefined -Iextern/libdeflate src/web/*.c \
      build/_deps/libdeflate-build/libdeflate.a -o /tmp/ba-web-asan
python3 src/web/tests/ataques.py /tmp/ba-web-asan --fuzz 20000 --gateway build/src/gateway/ba-gateway
```

29 de 29 y 20 000 pedidos rotos sin errores de memoria. Con 3 000 conexiones mudas encima
(más del doble de la capacidad) la gente real sigue entrando.

## Registro (que no trabe ni llene el disco)

- Los workers **no escriben archivos**: mandan cada línea al master por un pipe no bloqueante.
  Si el pipe está lleno (el disco anda lento, o un ataque genera mucho), la línea se tira y
  se cuenta («se perdieron N líneas»); el worker nunca espera. Probado: con el master
  congelado (nadie vacía el pipe) siguió a ~81 000 pedidos/s sin un error.
- Escribe el master (que no atiende a nadie) y **rota**: `Logs/ba-web.log` a los 5 MiB,
  el registro de accesos (apagado de fábrica) a los 20 MiB; quedan el actual, `.1` y `.2`.
  Probado: 900 000 pedidos con el registro de accesos prendido → tres archivos, ~49 MB.
- Nada se anota por conexión ni por pedido (salvo el registro de accesos, si se prende).
  Lo que se repite se resume: un worker que se cae seguido deja una línea cada 10 s; «sin
  descriptores», una cada 10 s.
- La salida cruda (`Logs/ba-web.log.salida`: errores antes de abrir el log) se vacía al
  relanzar si pasa de 1 MiB.
- pinggy: la línea de uso va al log solo si cambió la cantidad de conexiones o una vez por
  minuto (antes, cada 5 s: ~4 MB por día), y el gateway vacía `pinggy.log` al pasar los 4 MiB
  (después de leerlo; ba-pinggy escribe en modo agregar y sigue al final).

## Configuración

La genera el gateway; la sintaxis es un subconjunto de la de nginx (ver `src/web/conf.c`):
`worker_processes`, `listen … [proxy_protocol] [real_ip_xff]`, `upstream`, `limit_conn_per_ip`, `limit_tunnels_per_ip`, `limit_upstream_rate`, `upstream_max_conns`, `gzip*`, `location [=] /ruta { root |
alias | alias_file | proxy_pass | return; index; try_files $uri @upstream; proxy_cache_valid;
proxy_cache_key; add_header }`. `ba-web -t -c archivo` la revisa.

En la configuración del gateway: `web {activo: true, puerto: 8785, procesos: 0}`.

## Medido (Mac M-series, 2 workers, vivo.js comprimido)

`ab -k -c 100`: ~78 600 pedidos/s; sin keep-alive ~27 000/s; 0 errores.

## Prueba

`build/src/web/test_ba_web`: arranca ba-web con un gateway de mentira y le pega como un
navegador (gzip, condicionales, rangos, pipelining, proxy, microcaché, PROXY v1, WebSocket,
POST, SSE, pedidos mal formados). Lo comprimido se descomprime y se compara con el original.
