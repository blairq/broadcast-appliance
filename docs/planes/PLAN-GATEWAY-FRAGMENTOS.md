# Plan: el gateway en muchos núcleos, sin ahogarse en cambios de contexto

Pedido del usuario (2026-11-02): que el gateway use muchos núcleos sin caer en «un hilo
por tema», que multiplica los cambios de contexto. «¿No pueden compartir? ¿Cómo hace
nginx?» Y después: «horizontal o vertical cuando convenga, algo mixto… ver qué cambios
usan poca cantidad y qué cambios de contexto mucha».

## Dónde estamos (medido)

| | Mac M1 Pro, 40 invitados | PC Ryzen 9 5900X (.177), 40 invitados |
|---|---|---|
| CPU del gateway | 108 % de un núcleo | 201 % (320 % con 10, todos en 720p) |
| Hilos del gateway | 93 | 366 |
| Cambios de contexto/s del gateway | ~16 mil | sin medir por hilo |

Mediciones: `mediciones-carga/2026-11-02-final-am-sr/`, `2026-11-02-al-final/`,
`2026-11-02-pc177/`; ficha de la PC en [docs/hardware](../hardware/pc-ryzen9-5900x-rx5700xt.md).

**Lo que todavía no medimos: el camino de producción.** Los invitados entran por los
puertos TCP y UDP que abre pinggy, que llegan al TURN propio (`127.0.0.1:3478`,
[TURN.md](../funciones/TURN.md)). hinchada entra directo a WebRTC por UDP y se saltea el
TURN. Más adelante también se va a usar WireGuard ([CONFERENCIA.md](../funciones/CONFERENCIA.md)
«Túnel WireGuard»), que tampoco medimos con carga.

## Qué cuesta pasar algo de un hilo a otro

1. **Despertar al otro hilo** (costo fijo, ~3–10 µs, más en Windows): llamada al sistema,
   planificador, cambio de contexto. No depende del tamaño.
2. **El costo escondido:** el hilo que despierta encuentra su caché pisada y tarda en
   tomar ritmo. Suele ser mayor que el cambio en sí.
3. **Candados:** si los dos se pelean una cola, se suman esperas (lo de la mezcla de
   audio: 60 % del tiempo esperando candados).
4. **Mover los datos** (sí depende del tamaño): lo que un núcleo escribió, otro lo trae.
   Barato en el mismo bloque de caché; varias veces más caro entre los dos CCD del Ryzen.
   Un cuadro 720p (1,4 MB) no entra en la caché de un núcleo y ya se fue a la L3 o a la
   memoria: moverlo agrega poco. Lo **mediano** (tandas de paquetes, cuadros 180p de
   ~43 KB, bloques de audio) es lo que más pierde.

**Costo de una frontera ≈ pases/s × (costo fijo + datos tocados × distancia).** Casi
siempre manda la frecuencia.

## La regla: vertical u horizontal, según la frontera

- **V — vertical (mismo hilo):** muchos pases por segundo, poco dato y poco trabajo por
  pase (µs). Pasarlo a otro hilo cuesta más que hacerlo.
- **H — horizontal (otra capa, un grupo de hilos por capa, cada uno con N conexiones):**
  trabajo pesado por pase (más de ~20 veces el costo del pase, o sea más de ~100 µs).
  El pase sale gratis y aísla: lo pesado no frena a lo liviano.
- **Dato grande → llevar el trabajo al dato** (V) y pasar solo el resultado chico.
- **Toda frontera H va en lote:** una despertada por tanda, nunca una por paquete.
- **Todo en el mismo bloque de caché** (el que no usan los medios de ba-app).

## Las fronteras, una por una

Pases/s y tamaños son **estimaciones para 40 invitados**; los reales salen de los
contadores de F0. «Hoy» dice cuántos hilos y pases por el kernel hay en esa frontera.

### Entrada por pinggy y el TURN (producción)

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace |
|---|---|---|---|---|---|---|---|
| 1 | pinggy UDP → TURN | 1 hilo `turn` (`poll`) | ~4 mil | ~1,2 KB | µs (ChannelData) | **V** + lote | recibir de a tandas; el TURN corre en el hilo de red de su grupo (abajo) |
| 2 | pinggy TCP → TURN | mismo hilo `turn` | ~4 mil | ~1,2 KB | µs (marcos STUN sobre TCP) | **V** | cada conexión TCP se entrega una sola vez al hilo de red de su grupo (como `accept` en nginx) |
| 3 | TURN → red de WebRTC | `sendto` a `127.0.0.1:400xx`: **por el kernel**, otro hilo | ~4 mil | ~1,2 KB | µs | **V** | entrega en memoria: un `rtc::PacketSocketFactory` propio; el TURN y la red de WebRTC en el mismo hilo |
| 4 | red → trabajo de WebRTC (RTP) | `ba-rtc-net` → `ba-rtc-work` | ~4 mil | ~1,2 KB | µs (descifrar SRTP, ordenar) | **H** + lote (o **V** si F0 lo muestra liviano) | libwebrtc separa red y trabajo; se mantiene la separación pero con grupos por capa y pase en lote |
| 5 | trabajo → decodificador (cuadro codificado) | 1 cola de libwebrtc por invitado + hilos internos de libvpx/OpenH264 | ~700 | 1–30 KB | **ms** (720p por software: 3–5 ms) | **H** | pozo de decodificación de D hilos, repartido por costo (píxeles/s); cada decodificador con 1 hilo interno |
| 6 | decodificador → celda de la grilla | hilo `grillas` achica todo | ~700 | 43 KB–1,4 MB | decenas de µs (achicar) | **V** | achicar en el hilo que decodificó; a `grillas` solo le llega la celda chica |
| 7 | celdas → mosaico | `grillas` | 30 | 40 celdas chicas | copiar | **H** (ya está) | un hilo al ritmo del lienzo: una despertada por cuadro |
| 8 | decodificador → ba-app (memoria compartida) | desde el hilo que decodifica | ~700 | el cuadro | copia | **V** (ya está) | otro proceso: inevitable; ya va directo |
| 9 | audio: paquete → NetEq | hilo de trabajo | ~4 mil | ~100 B | µs | **V** (ya está) | queda en el hilo de trabajo del grupo |
| 10 | audio: sacar a todos cada 10 ms | `webrtc-playout` | 100 | 40 × 10 ms | decodificar Opus | **H** (ya está) | una despertada por vuelta; uno por grupo al mismo reloj si F0 lo pide |

### Salida hacia los invitados (retornos)

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace |
|---|---|---|---|---|---|---|---|
| 11 | ba-app → codificar retorno | `ba-retorno-N` por flujo | 30 por flujo | cuadro | **ms** (codificar) | **H** (ya está) | se mantiene; en Windows con la placa (F3) |
| 12 | retorno codificado → cada oyente | uno por oyente al hilo de red | 30 × oyentes | KB | µs | **H** + lote | un pase por cuadro y por grupo de red, no uno por oyente |
| 13 | Opus compartido → cada oyente | igual | 50 × oyentes | ~100 B | µs | **H** + lote | ídem |
| 14 | red de WebRTC → TURN → pinggy | `sendto` por el kernel + hilo `turn` | ~4 mil | ~1,2 KB | µs | **V** | lo inverso de 3: en memoria, mismo hilo |

### Entrada por WireGuard (cuando se use)

Hoy (`net/WgDevice.cpp`, `net/IpStack.cpp`):

```
VPS ─UDP─► [wireguard] select + descifra ChaCha20 ─(cola + despertar por UDP local)─►
  [ipstack] lwIP ─(un socket local por invitado, sendto: por el kernel)─► [ba-rtc-net]
vuelta: [ba-rtc-net] ─sendto─► socket del invitado ─► [ipstack] select ─► lwIP ─► cifra ─► VPS
```

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace |
|---|---|---|---|---|---|---|---|
| 15 | VPS → `wireguard` (socket) | 1 hilo, `select`, vacía hasta 256 por vuelta | ~4 mil | ~1,3 KB | µs | **V** + lote | ya va en lote; pasar a `recvmmsg` en Linux |
| 16 | descifrar ChaCha20-Poly1305 | en `wireguard`, de a uno | ~4 mil | ~1,3 KB | ~1–2 µs | **H** + lote (solo si F0 lo muestra cuello) | con un solo par (el VPS) todo cae en un hilo; si satura, descifrar tandas en paralelo y reordenar por el contador de WireGuard |
| 17 | `wireguard` → `ipstack` | cola con candado + despertar por un UDP local (por el kernel); despierta solo si la cola estaba vacía | ~4 mil paquetes, menos despertadas | ~1,3 KB | µs | **V** | lwIP corre en el mismo hilo que descifra; sin cola ni despertar. Además, hoy copia cada paquete a un `std::vector` nuevo: reusar buffers |
| 18 | `ipstack` → red de WebRTC | un socket local por invitado, `sendto`: **por el kernel** | ~4 mil | ~1,2 KB | µs | **V** | entrega en memoria con el mismo `PacketSocketFactory` del TURN (#3), en el hilo de red del grupo del invitado |
| 19 | red de WebRTC → `ipstack` (vuelta) | `sendto` al socket del invitado; `ipstack` con `select` sobre todos | ~4 mil | ~1,2 KB | µs | **V** | lo inverso de 18, en memoria. `select` con un socket por invitado se cae del todo (en Windows el límite de `select` es chico): desaparece |
| 20 | `ipstack` → cifrar → VPS | en `ipstack`, `wg.send` de a uno | ~4 mil | ~1,3 KB | ~1–2 µs | **V** + lote | cifrar y mandar en tanda por vuelta |

Con un solo par todo llega por un socket: la capa WireGuard es un hilo **V** (recibir,
descifrar, lwIP, entregar en memoria al grupo del invitado) y solo si satura se agrega
una capa **H** para descifrar en paralelo (#16).

## Cómo queda (mixto)

```
                ┌─ capa RED (H: R hilos, N conexiones c/u; adentro todo V) ─────────────┐
pinggy UDP ─►   │ hilo R1: socket(s) → TURN → ICE/DTLS/SRTP (red de WebRTC)            │
pinggy TCP ─►   │ hilo R2: ...                                                         │
WireGuard  ─►   │ hilo WG (V): recibir → descifrar → lwIP → entrega en memoria al Ri   │
                └──────────────────────────── en lote ──────────────────────────────────┘
                ┌─ capa TRABAJO (H: T hilos, M conexiones c/u) ──────────────────────────┐
                │ RTP, armar cuadros, NetEq (audio), control de congestión              │
                └──────────────────────────── en lote ──────────────────────────────────┘
                ┌─ capa DECODIFICACIÓN (H: pozo de D hilos, por costo) ──────────────────┐
                │ decodificar (V) → achicar la celda (V) → memoria compartida a ba-app  │
                └───────────────────────────────────────────────────────────────────────┘
ritmo propio (H, ya están): audio cada 10 ms · mosaico 30 cps · retornos (codificar)
señalización: 1 hilo para toda la sala
```

- **Cómo se arma con libwebrtc:** una fábrica de WebRTC por par (hilo de red Ri, hilo de
  trabajo Tj). Varias fábricas comparten hilos, así que R y T son independientes. Cada
  invitado va al par menos cargado al entrar y no se muda.
- **R, T y D son ajustes** (`gateway.hilosRed`, `gateway.hilosTrabajo`,
  `gateway.hilosDecodificar`, `0` = automático según la máquina). Automático de partida:
  en el 5900X R=2, T=3, D=4; en la M1 Pro R=1, T=2, D=2 (decodifica la placa). Con
  R=T=1 queda casi como hoy pero sin el kernel en el medio: sirve para comparar.

## Etapas

Cada etapa termina con su medición en la Mac y en la .177, antes y después, y se
integra sola. Si no mejora lo que promete, se frena y se revisa.

### F0 — Medir el camino real y cada frontera (sin cambiar el producto)

1. **hinchada entra por TURN** (`../hinchada`, sin remoto): `--turn udp|tcp|mezcla:<pct_tcp>`
   con los servidores de `/api/ice` y `politica: relay`, como un navegador que viene por
   pinggy (`assets/web/comun/ice.js`). Y `--wireguard` para cuando se mida ese camino
   (contra el VPS o un WireGuard local en Docker).
2. **Contadores por frontera** (`src/core/Frontera.hpp`, atómicos, casi gratis): en cada
   cola entre hilos de la tabla, pases/s, despertadas/s, bytes/s, trabajo por pase
   (µs) y espera en la cola (µs). Se ven en `gateway_estado` (MCP) y en el informe.
3. **Nombres de hilo en las mediciones:** Mac con `xctrace` (pasar el script de AP a
   `tools/carga/`; nunca `sample`); Windows con `GetThreadDescription` en
   `tools/carga/muestrear-win.ps1`. El informe lista los 10 hilos que más CPU y más
   cambios de contexto tienen.
4. **Serie base** 10/20/30/40 con `--turn udp` y `--turn mezcla:10`, Mac y .177.

Criterio: la tabla de fronteras con números medidos en vez de estimados.

### F1 — Lo vertical de la red: TURN y red de WebRTC en el mismo hilo, sin el kernel

1. `rtc::PacketSocketFactory` propio (`src/gateway/rtc/SocketsEnMemoria.*`): el puerto
   de relay del TURN se entrega en memoria a la red de WebRTC (#3, #14).
2. Los sockets del TURN (TCP y UDP) en el bucle del hilo de red
   (`rtc::PhysicalSocketServer`), no en un `poll` aparte (#1, #2).
3. Por ahora R=T=1 (una fábrica, como hoy).

Criterio: menos cambios de contexto/s del gateway con 40 por TURN (meta −30 %), misma
edad del audio y mismo mosaico.

### F2 — Lo horizontal: capas con R, T y D hilos

1. `src/gateway/rtc/Capas.{hpp,cpp}`: crea R hilos de red, T de trabajo, la señalización,
   y las fábricas por par; reparto por carga al entrar (el de 720p cuenta como 4).
2. TCP de pinggy: el socket se pasa al hilo de red de su grupo una vez. UDP de pinggy
   (un solo puerto): un hilo lo vacía de a tandas y reparte por dirección de origen,
   una entrega por grupo y por tanda.
3. Pases en lote entre capas (#4, #12, #13): cola sin candado de un productor y un
   consumidor, despertar solo si estaba vacía.
4. Opus y retorno compartidos: revisar sus candados para que varios hilos de red lean a
   la vez sin esperarse.
5. `gateway_estado` muestra las capas con su carga.

Criterio: con 40, ningún hilo por encima del 60 % de un núcleo; cambios de contexto/s
no peores que F1; la .177 aguanta igual o mejor.

### F3 — Decodificación: pozo compartido y la placa en Windows

1. Cada decodificador por software con 1 hilo interno; la decodificación en el pozo de D
   hilos (#5) vía un `TaskQueueFactory` propio para las colas de decodificación.
2. Achicar la celda en el mismo hilo que decodificó (#6).
3. Windows con la placa: decodificar con D3D11/D3D12 y codificar los retornos con
   AMF/D3D12 en la RX 5700 XT (`src/gateway/CodecsWin.*`, como `CodecsMac.mm`), con caída
   a software.

Criterio: en la .177, menos de 120 hilos con 40 y la mitad de CPU con 10.

### F4 — Afinidad

1. Llevar la topología de `src/motor/Prioridad.cpp` a `src/core/platform/Nucleos.*`.
2. El gateway pone sus capas en el bloque de caché que no usan los medios de ba-app
   (CPU Sets en Windows, `sched_setaffinity` en Linux; macOS nada). Línea en el log como
   la de ba-app.

Criterio: menos cambios de contexto y la misma latencia en la .177; si no mejora, queda
apagado por omisión (`gateway.afinidad`).

### F5 — WireGuard

1. lwIP en el mismo hilo que recibe y descifra (#17): sin cola ni despertar por UDP
   local; buffers reusados en vez de un `std::vector` por paquete.
2. Entrega en memoria al hilo de red del grupo de cada invitado, ida y vuelta (#18,
   #19), con el mismo `PacketSocketFactory` de F1. Se cae el `select` con un socket por
   invitado.
3. Recibir y cifrar en tanda (#15, #20); `recvmmsg`/`sendmmsg` en Linux.
4. Solo si F0 muestra el descifrado como cuello: tandas en paralelo y reorden por el
   contador (#16).
5. El Enlace (`net/EnlaceMedidor`) mide por el camino que se use (pinggy o WireGuard), y
   la tarjeta «Enlace» de la Sala dice cuál.

Criterio (con `hinchada --wireguard`): con 40, el camino WireGuard cuesta como mucho un
20 % más de CPU que el de pinggy, sin pérdidas.

## Cómo se mide (cada etapa)

- `tools/carga/medir-sala.py` (con `--remoto 192.168.1.177` para la PC), `--modo grabado
  --hablan 1`, `--turn udp` y `--turn mezcla:10` (y `--wireguard` en F5), régimen 60 s,
  grilla 20 s, serie 10/20/30/40.
- Por proceso: CPU p50/p95, hilos, cambios de contexto/s, memoria. Por hilo con nombre.
  Por frontera: los contadores de F0.
- Medios: mosaico y celdas (cps, perdidos), edad del audio, faltantes del retorno, Mbps.
- Máquina quieta, nada compilando, en «prueba linea L0»; al final el evento del usuario
  y la app cerrada. Los resultados van a la ficha de cada máquina en `docs/hardware/`.

## Riesgos

| Riesgo | Qué se hace |
|---|---|
| Varias fábricas de WebRTC compartiendo hilos (M123) | prueba chica al empezar F2; si no se puede, una fábrica por grupo con sus dos hilos |
| El `PacketSocketFactory` propio choca con ICE (candidatos, puertos) | F1 empieza por el relay del TURN solamente; `test_webrtc_loopback` y `test_turn` |
| Un hilo de red se satura con varios en 720p | reparto por carga; R configurable; medir el peor caso (10 en 720p) |
| Opus/retorno compartidos con candados pensados para un hilo | revisar en F2 con `test_retorno*` y `test_audio_compartido*` bajo carga |
| El UDP de pinggy entra por un solo puerto | va en tanda; si pesa, pedirle a pinggy un puerto UDP por grupo |
| Placa de video en Windows con drivers viejos | caída automática a software, avisada en el log y en `gateway_estado` |
| Cambia la latencia | cada etapa mide la edad del audio y la llegada del mosaico; si sube, no se integra |

## Pruebas nuevas

- `test_frontera`: los contadores cuentan bien con varios hilos.
- `test_sockets_en_memoria`: entrega TURN↔WebRTC sin `sendto`, en orden, ida y vuelta.
- `test_capas_rtc`: reparto por carga, una sesión no se muda, R/T = 1 y R/T = 3 dan el
  mismo resultado.
- `test_turn` ampliado: TCP pasado al hilo de su grupo, UDP en tanda.
- `test_ipstack` ampliado: lwIP en el hilo de WireGuard, entrega en memoria, sin `select`.
- `test_webrtc_loopback` con R/T = 1 y 3.

## Otros arreglos pendientes (fuera de este plan)

| | Qué | Por qué |
|---|---|---|
| C13 | Tope de 32 conexiones por IP (`room.maxConexionesPorIp`) | con mucha gente detrás de un NAT quedan afuera; que dependa del cupo |
| C14 | «En la conferencia» con 40 se ve como rayas vacías | desplazarse o pasar a lista; `docs/hardware/img/pc177-sala-40.jpg` |
| C15 | Cuelgue al cambiar de evento tras el primer arranque | GStreamer trabado en `gst_pad_stop_task`; volcado en la .177 |
| C16 | 5 tests nuevos fallan bajo Wine | test_cupos, test_gateway_http, test_mirada, test_sse_epoch, test_tiempo_real |
| C17 | Detalles | «fallidos 40» antes de que entre nadie; `analizar.py --pc` con acentos; script de hinchada con bash 3.2; la tarjeta «Enlace» habla de WireGuard |
| C9 | Faltantes del retorno al entrar la gente | quedan algunos |
| C10 | Hilos de las placas de cámara tras TAKE | ~5,5 por invitado, se sueltan a los 2 min |
| C11 | 13 pedidos de capa alta tras TAKE | sin explicar |
| C12 | Costura de 10 ms fila↔aire; IFB hasta 40 ms | |

## Orden

F0 → F1 → F2 → F3 → F4, de a una, integrando y midiendo cada una. F5 cuando se decida
usar WireGuard (F0 ya deja la medición lista). C14 y C13 en paralelo desde el principio;
el resto de los C después, de a uno, sin tocar los archivos de la etapa en curso.

## Decisiones del usuario que esperan

- DTX en los invitados (`usedtx=1`).
- Retorno liviano para Starlink.
- Dónde se publican los paquetes 2.1.
- Eco del VPS en Python o binario.
- Grilla: esperar 2 vueltas (29 cps con 10) o 3 (29,4 cps, +33 ms).
