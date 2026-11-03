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

Campaña del 2026-11-02 en la .177 con H.264 por la placa (master `b792325d`, ver «Lo que dijo
la campaña»): con 40 el gateway gasta 220 % directo y 231-242 % por TURN, con **647 hilos** y
**45-47 mil cambios de contexto/s**; con 10 en 720p, 405 %.

**Lo que todavía no medimos (antes de la campaña): el camino de producción.** Los invitados entran por los
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

Pases/s y tamaños son **estimaciones para 40 invitados**. La última columna es lo **medido** en
la .177 (campaña del 2026-11-02, master `b792325d`, 40 por TURN mezcla:10 salvo que diga otra
cosa; p50 del régimen de los contadores de F0: pases/s, despertadas/s, µs de trabajo por pase,
% de un núcleo y espera). La Mac quedó sin medir por TURN (campaña pausada). «Hoy» dice cuántos
hilos y pases por el kernel hay en esa frontera.

### Entrada por pinggy y el TURN (producción)

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace | Medido PC (40) |
|---|---|---|---|---|---|---|---|---|
| 1 | pinggy UDP → TURN | 1 hilo `turn` (`poll`) | ~4 mil | ~1,2 KB | µs (ChannelData) | **V** + lote | recibir de a tandas; el TURN corre en el hilo de red de su grupo (abajo) | UDP 40: 4,0k/s, 6 pases por despertada, 12 µs, 5 % · mezcla 40: 3,6k/s, 4 % |
| 2 | pinggy TCP → TURN | mismo hilo `turn` | ~4 mil | ~1,2 KB | µs (marcos STUN sobre TCP) | **V** | cada conexión TCP se entrega una sola vez al hilo de red de su grupo (como `accept` en nginx) | TCP 40: 4,0k/s, 1,1 pases por despertada (3,6k desp/s), 14 µs, 5,4 % · mezcla: 380/s |
| 3 | TURN → red de WebRTC | `sendto` a `127.0.0.1:400xx`: **por el kernel**, otro hilo | ~4 mil | ~1,2 KB | µs | **V** | entrega en memoria: un `rtc::PacketSocketFactory` propio; el TURN y la red de WebRTC en el mismo hilo | 4,0k/s, 10 µs, 4 % |
| 4 | red → trabajo de WebRTC (RTP) | `ba-rtc-net` → `ba-rtc-work` | ~4 mil | ~1,2 KB | µs (descifrar SRTP, ordenar) | **H** + lote (o **V** si F0 lo muestra liviano) | libwebrtc separa red y trabajo; se mantiene la separación pero con grupos por capa y pase en lote | sin contador; hilos con 40: `ba-rtc-net` 50 %, `ba-rtc-work` 25 % y 9,7k cambios/s |
| 5 | trabajo → decodificador (cuadro codificado) | 1 cola de libwebrtc por invitado + hilos internos de libvpx/OpenH264 | ~700 | 1–30 KB | **ms** (720p por software: 3–5 ms) | **H** | pozo de decodificación de D hilos, repartido por costo (píxeles/s); cada decodificador con 1 hilo interno | sin contador; con 40: `IncomingVideoStream` ×80 16 %, `DecodingQueue` ×80 15 %, `(sin nombre)` ×320 49 %; con 10 en 720p: 107 %, 48 % y 170 % |
| 6 | decodificador → celda de la grilla | hilo `grillas` achica todo | ~700 | 43 KB–1,4 MB | decenas de µs (achicar) | **V** | achicar en el hilo que decodificó; a `grillas` solo le llega la celda chica | 40: 620/s, 66-78 µs, 4-5 % · 10 en 720p: 300/s, 1,07 ms, 33 % |
| 7 | celdas → mosaico | `grillas` | 30 | 40 celdas chicas | copiar | **H** (ya está) | un hilo al ritmo del lienzo: una despertada por cuadro | 40: 30/s, 3,3-4,3 ms, 10-13 % · 10: 7,3 ms, 22 % |
| 8 | decodificador → ba-app (memoria compartida) | desde el hilo que decodifica | ~700 | el cuadro | copia | **V** (ya está) | otro proceso: inevitable; ya va directo | 40: 620/s, 0,41-0,47 ms, 25-29 %, espera 35 ms · 10 en 720p: 3,5 ms, **105 %**, espera 71 ms (VP8: 2,1 ms) |
| 9 | audio: paquete → NetEq | hilo de trabajo | ~4 mil | ~100 B | µs | **V** (ya está) | queda en el hilo de trabajo del grupo | sin contador |
| 10 | audio: sacar a todos cada 10 ms | `webrtc-playout` | 100 | 40 × 10 ms | decodificar Opus | **H** (ya está) | una despertada por vuelta; uno por grupo al mismo reloj si F0 lo pide | 100/s, 1,4-1,5 ms, 14-15 % (10: 0,8 ms) |

### Salida hacia los invitados (retornos)

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace | Medido PC (40) |
|---|---|---|---|---|---|---|---|---|
| 11 | ba-app → codificar retorno | `ba-retorno-N` por flujo | 30 por flujo | cuadro | **ms** (codificar) | **H** (ya está) | se mantiene; en Windows con la placa (F3) | 30/s, 0,9 ms, 2,8 % (placa AMD) |
| 12 | retorno codificado → cada oyente | uno por oyente al hilo de red | 30 × oyentes | KB | µs | **H** + lote | un pase por cuadro y por grupo de red, no uno por oyente | 1,2k/s, 32 µs, 4 %, espera 5,8 ms |
| 13 | Opus compartido → cada oyente | igual | 50 × oyentes | ~100 B | µs | **H** + lote | ídem | a oyentes 1,95k/s · Opus 100/s, 55 µs, 0,6 % · `AudioEncoder` ×40: **11,6k cambios/s** |
| 14 | red de WebRTC → TURN → pinggy | `sendto` por el kernel + hilo `turn` | ~4 mil | ~1,2 KB | µs | **V** | lo inverso de 3: en memoria, mismo hilo | 11,3k/s, 6,1k despertadas/s, 10 µs, 11 % |

### Entrada por WireGuard (cuando se use)

Hoy (`net/WgDevice.cpp`, `net/IpStack.cpp`):

```
VPS ─UDP─► [wireguard] select + descifra ChaCha20 ─(cola + despertar por UDP local)─►
  [ipstack] lwIP ─(un socket local por invitado, sendto: por el kernel)─► [ba-rtc-net]
vuelta: [ba-rtc-net] ─sendto─► socket del invitado ─► [ipstack] select ─► lwIP ─► cifra ─► VPS
```

| # | Frontera | Hoy | Pases/s | Tamaño | Trabajo por pase | V/H | Qué se hace | Medido PC (40) |
|---|---|---|---|---|---|---|---|---|
| 15 | VPS → `wireguard` (socket) | 1 hilo, `select`, vacía hasta 256 por vuelta | ~4 mil | ~1,3 KB | µs | **V** + lote | ya va en lote; pasar a `recvmmsg` en Linux | sin medir (WireGuard) |
| 16 | descifrar ChaCha20-Poly1305 | en `wireguard`, de a uno | ~4 mil | ~1,3 KB | ~1–2 µs | **H** + lote (solo si F0 lo muestra cuello) | con un solo par (el VPS) todo cae en un hilo; si satura, descifrar tandas en paralelo y reordenar por el contador de WireGuard | sin medir (WireGuard) |
| 17 | `wireguard` → `ipstack` | cola con candado + despertar por un UDP local (por el kernel); despierta solo si la cola estaba vacía | ~4 mil paquetes, menos despertadas | ~1,3 KB | µs | **V** | lwIP corre en el mismo hilo que descifra; sin cola ni despertar. Además, hoy copia cada paquete a un `std::vector` nuevo: reusar buffers | sin medir (WireGuard) |
| 18 | `ipstack` → red de WebRTC | un socket local por invitado, `sendto`: **por el kernel** | ~4 mil | ~1,2 KB | µs | **V** | entrega en memoria con el mismo `PacketSocketFactory` del TURN (#3), en el hilo de red del grupo del invitado | sin medir (WireGuard) |
| 19 | red de WebRTC → `ipstack` (vuelta) | `sendto` al socket del invitado; `ipstack` con `select` sobre todos | ~4 mil | ~1,2 KB | µs | **V** | lo inverso de 18, en memoria. `select` con un socket por invitado se cae del todo (en Windows el límite de `select` es chico): desaparece | sin medir (WireGuard) |
| 20 | `ipstack` → cifrar → VPS | en `ipstack`, `wg.send` de a uno | ~4 mil | ~1,3 KB | ~1–2 µs | **V** + lote | cifrar y mandar en tanda por vuelta | sin medir (WireGuard) |

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

## Lo que dijo la campaña (2026-11-02, .177)

Mediciones en `~/Proyectos/mediciones-carga/2026-11-02-campana/pc177/` y la ficha
[pc-ryzen9-5900x-rx5700xt.md](../hardware/pc-ryzen9-5900x-rx5700xt.md). La Mac quedó pausada con
solo directo 10-40 (sin TURN): sus columnas quedan para cuando se retome.

**Qué hilo pesa más, por camino (40 invitados, % de un núcleo; cambios de contexto/s):**

| Camino | 1.º | 2.º | 3.º | 4.º | Más cambios de contexto |
|---|---|---|---|---|---|
| directo | `(sin nombre)` ×322 51 % | `ba-rtc-net` 50 % | `ba-rtc-work` 25 % | `AudioEncoder` ×40 17 % | `AudioEncoder` 11,6k · `(sin nombre)` 9,9k · `ba-rtc-work` 9,7k · `EncoderQueue` 6,5k |
| TURN UDP | `ba-rtc-net` 51 % | `(sin nombre)` 50 % | `ba-rtc-work` 26 % | **`turn` 24 %** | igual (+ `turn` 1,0k) |
| TURN TCP | `ba-rtc-net` 53 % | `(sin nombre)` 49 % | **`turn` 29 %** | `ba-rtc-work` 27 % | igual |
| TURN mezcla:10 | `ba-rtc-net` 49 % | `(sin nombre)` 49 % | `turn` 24 % | `ba-rtc-work` 24 % | igual |
| 10 en 720p, directo | `(sin nombre)` ×261 170 % | **`IncomingVideoStream` ×20 107 %** | `DecodingQueue` ×20 48 % | `ba-rtc-net` 22 % | `(sin nombre)` 6,3k · `ba-rtc-work` 4,0k |
| 60, TURN mezcla | `(sin nombre)` ×362 87 % | `ba-rtc-net` 77 % | `ba-rtc-work` 43 % | `turn` 41 % | `AudioEncoder` 17,7k · `(sin nombre)` 14,4k |

Conclusiones para el rewrite:

1. **Ningún hilo satura con 40**: el más cargado (`ba-rtc-net`) va a medio núcleo; con 60 llega
   a 77 %. El primero en llegar a un núcleo entero será `ba-rtc-net` (~70 invitados): la capa
   RED con R≥2 (F2) es necesaria antes de pasar de 60, no para 40.
2. **El TURN es barato y es V**: #1/#3/#14 trabajan 10-14 µs por pase; el hilo `turn` entero
   cuesta 24-29 % con 40. Por TCP casi cada paquete es una despertada (1,1 pases por
   despertada contra 6 por UDP): justo lo que F1 saca metiendo los sockets del TURN en el hilo de
   red. Meta realista de F1: ahorrar el `turn` (≈25 %) y los ~6k despertadas/s de #14, no más.
   El costo de una frontera por el kernel acá es chico frente al de los hilos por invitado (3).
3. **Los cambios de contexto no vienen de las fronteras de la tabla sino de las colas de
   libwebrtc por invitado**: `AudioEncoder` (una por invitado, 11,6k/s con 40, 17,7k/s con 60),
   `EncoderQueue` (6,5k/s), `DecodingQueue` (×2 por invitado, 4,1k/s) e `IncomingVideoStream`
   (×2 por invitado, 2,7k/s). Son ~25k de los 46k cambios/s. Lo que más rinde: un
   `TaskQueueFactory` propio que ponga esas colas sobre un pozo chico de hilos (H en lote, F3
   punto 1 generalizado a audio y codificación), no solo la decodificación.
4. **`IncomingVideoStream`** (el hilo por flujo entrante que entrega el cuadro decodificado al
   sumidero, con su suavizado de render de libwebrtc): 107 % con 10 en 720p (los 20 hilos
   juntos, ~5 % cada uno) y 16 % con 40. Ese «suavizado» (`IncomingVideoStream` + `VideoRenderFrames`)
   espera la hora de render y despierta por cuadro; para la grilla no sirve (la grilla tiene su
   reloj de 30 cps). **Evaluar apagarlo**: entregar el cuadro directo en el hilo que decodifica
   (#6 y #8 en V). En el libwebrtc que usamos es una opción de la conexión:
   `RTCConfiguration::set_prerenderer_smoothing(false)` (`media_config.video.enable_prerenderer_smoothing`,
   `api/peer_connection_interface.h`); sin él no se crea `IncomingVideoStream`. Probarlo primero
   (cambio de una línea) y medir la grilla: si sigue a 30 cps sin tirones, va. Ahorro esperado: el 107 % de 10
   en 720p y 80 hilos con 40.
5. **La placa no ahorró CPU en Windows** (B): con 10 en 720p el gateway gasta 405 % con
   H.264 por DXVA contra 305 % con VP8 por software; el cuadro decodificado → BGRA cuesta 3,5 ms
   (#8) contra 2,1 ms, y aparecen ~260 hilos `(sin nombre)` de Media Foundation/D3D11 (170 %).
   Para F3: decodificar a una superficie compartida y achicar en la GPU (o leer NV12 una sola
   vez y achicar en el mismo hilo, #6 V), y un solo dispositivo D3D11/MF para todos los
   decodificadores en vez de uno por flujo.
6. **Lo de en medio (#6 achicar, #8 copiar a ba-app) es V y ya pesa poco con 40** (0,07 y
   0,4 ms por cuadro, 4 % y 25 %); con 10 en 720p pesa (1,1 y 3,5 ms). Achicar en el hilo que
   decodificó (F3 punto 2) es la ganancia grande del caso de 10.
7. **El audio de todos en un hilo (#10) aguanta**: 1,4-1,5 ms cada 10 ms con 40 (15 % de un
   núcleo), 2,3 ms con 60 y 2,6 ms con 68 (26 %). Sigue H como está. #8 con 60 sube a 71 % de
   un núcleo (repartido entre los hilos que decodifican) y #14 a 17k pases/s con 8k despertadas.
8. **Techo de pinggy (gratis): ~7-8 Mbps**, unos 5 invitados con video; desde 10 el retorno
   baja a 180p. El cuello es el túnel, no la CPU ni la casa (42,8 Mbps de subida). Para el
   camino de producción con más gente: cuenta paga (medir su techo), WireGuard (F5) o un
   servidor propio. Ningún cambio de hilos del gateway mueve ese techo.
9. **Ráfaga por el TURN**: 40 a la vez → ~la mitad no conecta (directo entran todos). Hay que
   mirarlo antes de F1/F2 (cola de asignaciones, permisos, `rechazosPeer`): es correctitud, no
   rendimiento, y F1 toca justo ese código.
10. **Núcleos (la pregunta del usuario)**: con 40 no hay dos núcleos saturados; el gateway queda
    en el primer CCD (0-11) y ba-app en el segundo (14-23). Las interrupciones de la placa de
    red (I225-V, sin RSS) van todas a los núcleos 0 y 1 (5 % de DPC en el 1). Los «dos núcleos
    casi llenos» del Administrador de tareas eran de la medición (`WmiPrvSE` en los núcleos
    lógicos 12-13 con el muestreo viejo por WMI). Para F4: dejar los núcleos 0-1 libres de hilos
    calientes del gateway (son los de la placa de red) antes de fijar afinidad.

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

**Herramientas (hechas, 2026-11-02):**

- `src/core/diag/Frontera.hpp`: contadores por frontera (atómicos relajados) en #1 #2 #3 #14
  (TURN), #6 #7 (grilla), #8 (decodificado: la espera es desde que llegó el primer paquete
  del cuadro), #10 (playout), #11 #12 #13 (retorno video y audio) y #17-#20 (WireGuard/
  ipstack). #4 (red → trabajo de libwebrtc), #5, #9, #15 y #16 quedan adentro de libwebrtc o
  de WgDevice sin enganche propio. Se ven en `gateway_estado` (MCP, GET /api/v1/gateway).
- hinchada `--turn udp|tcp|mezcla:PCT` (y `--turn-host`); del retorno: cps, congelamientos,
  tamaño, RTT, jitter, pérdida, NACK/PLI que pide, jitter buffer, primer cuadro, edad de lo que
  se reproduce y el camino (`relay/udp`, `relay/tcp`…).
- `tools/carga/medir-sala.py`: `--camino`, `--duracion`, `--rafaga`, `--palabra K`,
  `--grabar-transmitir`, `--xctrace S [--contadores]` (Mac, `tools/carga/xctrace.py`),
  `--etw S` (Windows, `tools/carga/etw-win.ps1` + `ba-nucleos.wprp`); `muestrear-win.ps1` da
  los hilos por nombre, cada núcleo (ocupado, DPC, interrupciones, MHz) y la placa de red.
- Lo que no hay: en la Mac, interrupciones por núcleo (piden sudo: powermetrics); IPC y
  fallos de caché por evento (Mac: plantilla propia de Instruments; Windows: xperf del ADK o
  AMD uProf, sin instalar); cambios de contexto por núcleo en Windows (tracerpt no decodifica
  CSwitch: por hilo salen de WMI).

### F1 — Lo vertical de la red: TURN y red de WebRTC en el mismo hilo, sin el kernel

1. `rtc::PacketSocketFactory` propio (`src/gateway/rtc/SocketsEnMemoria.*`): el puerto
   de relay del TURN se entrega en memoria a la red de WebRTC (#3, #14).
2. Los sockets del TURN (TCP y UDP) en el bucle del hilo de red
   (`rtc::PhysicalSocketServer`), no en un `poll` aparte (#1, #2).
3. Por ahora R=T=1 (una fábrica, como hoy).

Criterio: menos cambios de contexto/s del gateway con 40 por TURN (meta −30 %), misma
edad del audio y mismo mosaico.

Escrito (2026-11-02, noche; **sin compilar ni medir**: por pedido del usuario se compila,
depura y mide todo junto con F3-F5):

- `rtc/Conmutador.{hpp,cpp}` (sin libwebrtc): la entrega en memoria. Cada extremo (un socket
  UDP de WebRTC, el relay de una asignación) se anota con su dirección y su hilo de red. En
  el mismo hilo se entrega ahí mismo, salvo adentro de un `Ambito` (un manejador del TURN u
  otra entrega): entonces se encola en el hilo y sale al terminar, así nunca se anida WebRTC
  adentro de WebRTC ni el TURN adentro del TURN. A otro hilo (F2, o el túnel): una cola SPSC
  sin candado por par (origen, destino), y se despierta al destino solo si no estaba avisado.
  Fronteras `memoria.cruce` y `memoria.tunel` (#18).
- `rtc/SocketsEnMemoria.{hpp,cpp}`: `FabricaEnMemoria` (el `rtc::PacketSocketFactory` de las
  PeerConnection): cada UDP es uno de verdad (los directos siguen por el kernel) envuelto en
  un `SocketHibrido` que recibe en memoria lo que le da el TURN (desde la dirección del
  relay, la del candidato señalado) y le manda en memoria al relay (#14). Avisa
  `SignalSentPacket` igual que `AsyncUDPSocket` (lo usa el control de congestión). Solo los
  enlazados a una IP concreta (los permisos del TURN son por IP).
- `turn/Bucle.hpp`, `rtc/BucleRtc.{hpp,cpp}`: el TURN sin `poll()` propio. Sus sockets crudos
  se anotan como `rtc::Dispatcher` en el `PhysicalSocketServer` del hilo de red (select en
  macOS, epoll en Linux, WSAEventSelect en Windows); `turn/BuclePoll.{hpp,cpp}` es el de los
  tests y del gateway sin libwebrtc.
- `turn/Servidor.cpp`: en fragmentos (uno por hilo de red; con R = 1, uno solo en
  `ba-rtc-net`). `gateway.turnEnMemoria` = false vuelve al `sendto` (para comparar).
- Pruebas: `test_sockets_en_memoria` (mismo hilo sin anidar, ida y vuelta, 20 000 a otro hilo
  en orden y en lote, cerrado, túnel en tandas), `test_turn` (la ráfaga de 40 también con el
  peer en memoria) y `test_webrtc_capas` (navegadores de libwebrtc que entran solo por relay,
  UDP y TCP, contra el hub: conectan y el TURN cuenta paquetes en memoria en los dos sentidos).

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

Escrito (2026-11-02, noche; **sin compilar ni medir**, como F1):

- `rtc/CapasHilos.{hpp,cpp}` (sin libwebrtc, `test_capas_rtc`): los tamaños
  (`gateway.hilosRed`, `gateway.hilosTrabajo`; 0 = automático: red = núcleos de un bloque /3
  entre 1 y 4, trabajo = (núcleos+1)/2 entre 1 y 6: 2 y 3 en el 5900X, 1 y 2 en la M1 Pro) y
  el `Reparto`: cada sesión (invitado o espectador) al hilo de red y al de trabajo menos
  cargados al entrar; no se muda; el que sube 540p o más pesa 4 (`kPesoAlta`) para las que
  vienen después. `gateway.capasCruzadas` = false: cada red con su trabajo (T = R), el
  respaldo si varias fábricas no pudieran compartir hilos.
- `Webrtc.cpp`: R hilos `ba-rtc-net-i`, T `ba-rtc-work-j` (sin sufijo con 1), un
  `ba-rtc-sig`; una fábrica por par (R×T) compartiendo esos hilos, todas con las MISMAS colas
  compartidas (`ColasDePaso` sobre una sola `colas::crearDeLosAjustes()`) y el mismo reloj de
  audio (`RelojPlayout`: un solo `webrtc-playout` tira de los ADM de todas las fábricas cada
  10 ms, #10). Cada capa de red con su `BasicNetworkManager`, su `FabricaEnMemoria` y su
  `BucleRtc` (el TURN pone un fragmento en cada una). `test_webrtc_capas` comprueba que en M123
  4 fábricas comparten 2 + 2 hilos, conectan y reciben el audio.
- TURN (`turn/Servidor.cpp`): el fragmento 0 acepta y le entrega cada conexión TCP UNA vez al
  fragmento menos cargado (el descriptor pasa crudo al otro hilo); el UDP de pinggy lo vacía
  de a tandas (hasta 256) y reparte por origen (ruta fija por dirección), una tarea por
  fragmento y por tanda (`turn.reparto.udp`, `turn.entrega.tcp`). Los topes son de todo el
  servidor. Lo que va del relay a una sesión de otro hilo de red cruza por el conmutador en
  lote (no hay forma de saber al aceptar de qué sesión es una conexión).
- Opus y retorno compartidos: el `Grupo` de Opus codifica sin candado y publica junto; los
  codificadores de paso leen con candado compartido (no se esperan entre ellos ni esperan a
  Opus); el resultado de cada emisión del retorno, igual.
- #4 (red → trabajo) y #12/#13 (retorno → oyentes) quedan como los arma libwebrtc: el pase
  red → trabajo es un `PostTask` de libwebrtc (despierta solo si el hilo dormía) y los
  codificadores de paso corren en las colas compartidas «rtc» (ya en lote: 733 despertadas
  para 7,6 mil tareas/s con 40). Medir antes de tocar más. (La #4 se midió en la .177 y se
  hizo en lote: ver «La frontera #4» abajo.)
- `gateway_estado` → `capas`: cada hilo de red y de trabajo con su carga y sus sesiones, las
  fábricas y el conmutador (directos, encolados, cruzados, despertadas, descartes); `turn` →
  `fragmentos`.
- WireGuard en memoria (F5, con HL): `rtc/TunelEnMemoria.{hpp,cpp}` es el `SumideroRtc` de la
  pila del túnel: cada tanda del hilo de WireGuard va al hilo de red de cada socket por el
  conmutador (una despertada por hilo y por tanda) y la vuelta es `IpStack::enviarDesdeRtc`;
  `WebrtcHub::conectarTunel` lo enchufa (main, al arrancar la pila y antes de pararla).
- Afinidad (F4, de HL): los hilos de red, de trabajo, de las colas, el reloj de audio y los
  del TURN sin libwebrtc llaman `nucleos::alGrupoGateway()` al empezar.

#### La frontera #4 (red → trabajo) en lote

Escrito (2026-11-02, noche; **sin compilar ni medir**). Lo que lo pidió: en la .177 con 40,
29 mil cambios de contexto/s en el gateway y el primero es `ba-rtc-work-#` (×3): 13,5 mil/s,
20-26 % de un núcleo cada uno. En libwebrtc cada paquete que llega al hilo de red pasa al de
trabajo con un `PostTask` (`WebRtcVideoReceiveChannel` / `WebRtcVoiceReceiveChannel::OnPacketReceived`,
siempre, aunque los dos hilos sean el mismo), y `rtc::Thread::PostTaskImpl` encola y llama
SIEMPRE a `WakeUp()` del socket server del destino: el de trabajo (más rápido que el de red)
atiende el paquete en µs, se vuelve a dormir y el siguiente lo despierta otra vez. Una
despertada por paquete (~4 mil/s con 40).

Sin tocar libwebrtc, con lo que M123 deja enganchar (`rtc_base/thread.h`, `socket_server.h`:
el socket server de cada hilo, `Run()`, `PostTaskImpl`/`PostDelayedTaskImpl`/`BlockingCallImpl`
virtuales; `Get()`/`Dispatch()` son privados):

- `rtc/LoteTrabajo.{hpp,cpp}` (sin libwebrtc, `test_lote_trabajo`): un `Destino` (la espera
  del hilo de trabajo: mutex + condvar, con «aviso pendiente» para no perder ninguno) y un
  `Remitente` por hilo de red (en un `thread_local`). El aviso que hace un hilo de red no
  despierta: anota al destino. Se suelta (una despertada por destino y por tanda): al entrar
  a esperar (antes de dormirse, siempre), al terminar cada tarea o vuelta de E/S si la primera
  anotada ya esperó `gateway.loteTrabajoUs` (500 µs de fábrica; 0 = al terminar cada una),
  antes de un `BlockingCall` a otro hilo y al cerrar. Desde cualquier otro hilo (señalización,
  colas, `webrtc-playout`, el cierre) el aviso es en el acto, como siempre; el propio hilo de
  trabajo avisándose a sí mismo, nada (está corriendo y mira su cola antes de dormir).
- `rtc/HilosLote.{hpp,cpp}`: `HiloTrabajo` (un `rtc::Thread` con `EsperaTrabajo`, socket
  server sin sockets como `NullSocketServer`; un `BlockingCall` hacia él es **urgente**: el
  que llama queda esperando y no puede esperar al lote) y `HiloRed` (un `rtc::Thread` con
  `ServidorRed`, que ES un `PhysicalSocketServer`: BucleRtc y las fábricas lo usan igual).
  Las tareas del hilo de red van envueltas (al terminar: `soltarSiVencio`); si anota algo
  procesando E/S ADENTRO de `PhysicalSocketServer::Wait` (que no sale hasta que lo
  despierten), se despierta a sí mismo (sin cambio de contexto) para soltar al terminar esa
  vuelta. Además, un `PostTask` del hilo de red a sí mismo fuera de la espera no llama al
  `WakeUp` de verdad (pipe/evento de Windows): `rtc::Thread::Get` mira la cola antes de
  volver a esperar.
- **Por qué no se pierde precisión ni orden:** los `PostTask` ya están en la cola del de
  trabajo (libwebrtc encola antes de avisar): la tanda corre en el orden de llegada. Si el de
  trabajo está despierto, anotar no cambia nada. Las diferidas: el de trabajo duerme con el
  plazo de la próxima (lo calcula `rtc::Thread` en ms, igual que antes). El control de
  congestión no lo ve: la hora de llegada de cada paquete la toma el hilo de red al leerlo,
  antes de la frontera. El audio (NetEq) recibe el paquete a lo sumo `loteTrabajoUs` + lo que
  dure la tarea en curso del de red más tarde, y nunca más tarde de cuando el de red se
  desocupa.
- `gateway.redYTrabajo` (reinicia el gateway): `lote` (de fábrica), `juntos` (cada hilo de
  red es también el de trabajo de su grupo: tantos como el mayor de `hilosRed`/`hilosTrabajo`,
  sin cruzar; la frontera desaparece como despertada, aunque libwebrtc sigue haciendo el
  `PostTask` a sí mismo, que ahora no escribe en el pipe; de yapa, el envío
  `MediaChannelUtil::TransportForMediaChannels::DoSendPacket` de M123 manda directo, sin
  `PostTask`, cuando ya está en el hilo de red — eso lo dice el código de M123, no los headers:
  verificarlo con `rtc.red.propias` y los cambios de contexto) o `separados` (como venía:
  una despertada por paquete; usa la misma espera, solo cuenta, para comparar).
- Contadores: fronteras `rtc.red-trabajo[-j]` (#4: pases/s = `PostTask` del de red al de
  trabajo j, despertadas/s, pases por despertada, espera = desde la primera anotada hasta la
  despertada, o sea la latencia agregada) y `rtc.red.propias` (#4 con `juntos`: los `PostTask`
  a sí mismo sin despertar). `gateway_estado` → `capas.redYTrabajo`: el modo, `loteUs` y por
  hilo de trabajo los avisos, los de red, anotados, urgentes, despertadas y `despierto_pct`.
- Tests: `test_lote_trabajo` (orden, una despertada por tanda, separados, urgente, diferidas,
  el dueño, el tope del lote, cierre, la frontera) y `test_webrtc_lote` (con `rtc::Thread` de
  verdad: orden y tandas lote contra separados, `BlockingCall` del de red con una tanda
  anotada, diferidas, E/S por sockets UDP del `PhysicalSocketServer`, `juntos`, cierre).

Qué mirar al medir (40 por TURN, `lote` contra `separados` y `juntos`): cambios de
contexto/s de `ba-rtc-work*` (la meta: de 13,5 mil a ≤ 4 mil), pases por despertada de
`rtc.red-trabajo*` (≥ 4), `espera_us` (≤ 500, máx. ≤ 1000), CPU de `ba-rtc-net*` (no debería
subir más que unos puntos: envolver cada tarea y el autodespertar de la E/S), edad del audio
p50 y lo que recibe el invitado (sin cambios). Con `juntos`: CPU y cambios de contexto de
`ba-rtc-net*` y si la red de un grupo se atrasa (ráfaga de 40 entrando: los DTLS y los
`SetRemoteDescription` en el mismo hilo que la red).

Medición (pendiente: se hace con todo junto; `medir-sala.py`, Mac, `--camino turn-mezcla:10`
con `--turn-prueba` y `--camino directo`, 10 y 40, después 80; grabado, habla 1, decodificar
todo):

| | master (antes) | F1 (R = T = 1) | F2 (automático) |
|---|---|---|---|
| CPU del gateway p50, 40 por TURN | | | |
| Cambios de contexto/s, 40 por TURN | | | |
| Hilos | | | |
| `ba-rtc-net*` / `ba-rtc-work*` / `turn` (% de un núcleo) | | | |
| Edad del audio p50 | | | |
| Mosaico / celdas (cps) | | | |
| Lo que recibe el invitado (cps, congelamientos) | | | |

### F3 — Decodificación: pozo compartido y la placa en Windows

1. Cada decodificador por software con 1 hilo interno; la decodificación en el pozo de D
   hilos (#5) vía un `TaskQueueFactory` propio para las colas de decodificación.
2. Achicar la celda en el mismo hilo que decodificó (#6).
3. Windows con la placa: decodificar con D3D11/D3D12 y codificar los retornos con
   AMF/D3D12 en la RX 5700 XT (`src/gateway/CodecsWin.*`, como `CodecsMac.mm`), con caída
   a software.

Criterio: en la .177, menos de 120 hilos con 40 y la mitad de CPU con 10.

Hecho (2026-11-02, adelantado por pedido del usuario: «en Windows quiero H.264 ya»):

- `src/gateway/CodecsVideo.{hpp,cpp}`: las fábricas por plataforma. Todos los
  decodificadores van envueltos: los de software con `gateway.hilosPorDecodificador`
  hilos (1 por omisión; libwebrtc les pasaba todos los núcleos) y cada uno anotado (log
  y `uso.codecsVideo` en `room_status`). «auto» de `retorno.codec` = H.264 en macOS y
  Windows (42e01f, packetization-mode=1 primero), VP8 en Linux.
- macOS: VideoToolbox para subir y bajar (ya estaba; ahora también lo elige «auto» con
  hinchada, que manda H.264 por omisión).
- Windows (`src/gateway/CodecsWin.{hpp,cpp}`, Media Foundation): decodifica con el
  decodificador H.264 de Windows + D3D11 (DXVA, motor «Video Decode»), NV12→I420 con
  libyuv; codifica el retorno con el MFT por hardware de la placa (asincrónico, CBR, sin
  B, clave y bitrate en caliente). Sin placa o si no arranca: el H.264 por software de
  Windows (el libwebrtc de Shiguredo no trae OpenH264), avisado en el log y en
  `codecsVideo.h264.caidas_*`. Sin ningún H.264 (Windows N): VP8 como antes.
- Cada sesión dice en sus estadísticas `entrada` (códec, decodificador, hardware) y
  `salida.codec`, y una línea en el log cuando se sabe o cambia.
- Faltaba: el pozo de decodificación de D hilos (punto 1, segunda mitad: hecho abajo, colas
  compartidas) y achicar en el mismo hilo (punto 2: escrito abajo, sin compilar).

Hecho (2026-11-02, tarde): decodificar directo con la API de video de D3D11, sin el MFT.

- Qué eran los hilos (hilos del gateway por módulo donde arrancan, .177, 10 invitados 720p
  con el MFT): **msmpeg2vdec.dll 40 hilos, 87 % de un núcleo** (dos por decodificador, aun
  con `CODECAPI_AVDecNumWorkerThreads`=1; con 40 invitados, 94 hilos) y **amdxx64.dll 121
  hilos** (el driver de AMD: ~25 por dispositivo D3D11, y el MFT abría 4 más el suyo). Los
  decodificadores H.264 que libwebrtc arma de antemano gastaban ~100 % aunque el invitado
  mandara VP8.
- `CodecsWin.cpp` `DecodificadorD3d11` + `H264Sintaxis.{hpp,cpp}`: el gateway lee SPS/PPS
  y la cabecera del slice, lleva POC y referencias (ventana deslizante y MMCO), y le pasa a
  la placa los parámetros DXVA en formato «corto» (ID3D11VideoDecoder) en el mismo hilo
  que decodifica; copia a una textura de paso propia y espera con una cerca (ID3D11Fence)
  en vez de Lock2D; un solo dispositivo para todos (`gateway.dispositivosPlaca`). Caída al
  MFT con DXVA si la placa no tiene formato corto, el flujo es entrelazado/FMO o falla 5
  veces; `gateway.decodificadorPlaca` = "mft" para comparar. Probado: test_webrtc_codecs
  (PSNR del peor cuadro 34,0 dB, igual que el MFT) y test_h264_sintaxis (x264 con B, MMCO,
  pesos, listas de escalado).
- Sin la placa no se recibe H.264: el MFT por software de Windows no entregaba ningún
  cuadro (ProcessOutput E_FAIL; 0 cps en la grilla) y abría ~24 hilos por flujo. Ahora el
  H.264 no se ofrece para subir y todo va en VP8 (`decodificaConPlaca()`).
- Medido en la .177 (`medir-sala.py --camino directo`, grabado, 2 min; gateway p50 e hilos
  al final, en /Users/usuario/Proyectos/mediciones-carga/2026-11-02-d3d11):

  | | 10 invitados (720p30) | 40 invitados (capa baja) |
  |---|---|---|
  | H.264 por el MFT (antes) | 260 %, 296 hilos | 211 %, 591 hilos |
  | VP8 por software | 337 %, 247 hilos | 198 %, 393 hilos |
  | H.264 por D3D11 (ahora) | **117 %, 181 hilos** | **151 %, 421 hilos** |

  Con 40, los hilos que quedan son de libwebrtc (2 por flujo de video, 1 por audio, 1
  rtc_event_log por sesión) y 49 del driver de AMD (25 de nuestro dispositivo, 24 del
  codificador del retorno). Darle al codificador el mismo dispositivo (MFT_MESSAGE_SET_D3D_MANAGER)
  ahorra 24 hilos pero sube la CPU con 10 de 117 % a 165 %: se descartó.
- El banco del test (BA_BANCO_DECODIFICADORES=40): la placa entrega ~16 cps por flujo con
  40 a la vez en 720p tanto con D3D11 como con el MFT: es el tope del motor de video de la
  RX 5700 XT (~650 cuadros 720p por segundo), no del gateway.

Hecho (2026-11-02, por la campaña de la .177: con 40, 647 hilos y 45 mil cambios de
contexto/s; más de la mitad de las colas de libwebrtc de un hilo cada una):

- **Sin suavizado previo al render** (`set_prerenderer_smoothing(false)` en la config de
  cada PeerConnection): se va `IncomingVideoStream` (un hilo por stream; con 10 en 720p,
  107 % de un núcleo en la .177). El cuadro sale del decodificador derecho al sink; la
  grilla ya tiene su cola de 4 cuadros.
- **Colas compartidas** (`src/gateway/rtc/ColasCompartidas.*`, el pozo del punto 1): un
  `TaskQueueFactory` propio en `PeerConnectionFactoryDependencies` (la fábrica se arma con
  `EnableMedia` + `CreateModularPeerConnectionFactory`). Dos grupos: «dec» para las
  `DecodingQueue` (`gateway.hilosColas`; 0 = núcleos de un bloque de caché, entre 2 y 6:
  3 en esta M1 Pro, 6 en el 5900X) y «rtc» con 2 hilos para lo liviano (`AudioEncoder`,
  `EncoderQueue`: µs por tarea). Una cola programada no despierta a nadie; diferidas con un
  hilo vigía. `gateway.hilosColas = -1` vuelve a un hilo por cola. Contadores
  `cola.<nombre>` y `colas.<grupo>` en las fronteras. `test_colas_compartidas`.
- Medido en la Mac (`mediciones-carga/2026-11-02-colas/`, directo, grabado, habla 1,
  decodificar todo, régimen 45 s). En macOS las colas de fábrica ya eran de GCD (hilos
  compartidos), así que acá la ganancia es chica; la grande se espera en Windows, donde
  cada cola era un hilo:

  | | 10 antes | 10 después | 40 antes | 40 después |
  |---|---|---|---|---|
  | CPU del gateway p50 | 80 % | 81 % | 103 % | 99 % |
  | Hilos (máx.) | 68 | 63 | 108 | 100 |
  | Cambios de contexto/s p50 | 11,9 mil | 10,6 mil | 25,9 mil | 24,3 mil |
  | Mosaico / celdas (cps p50) | 29,8 / 29,1 | 30,0 / 29,3 | 29,8 / 15 (baja) | 29,9 / 15 (baja) |
  | Perdidos | 0 | 0 | 0 | 0 |
  | Edad del audio p50 | 13,1 ms | 12,3 ms | 11,3 ms | 11,3 ms |

  Con 40: «rtc» corre 7,6 mil tareas/s con 733 despertadas/s (antes, una por tarea) y
  espera media de 0,1-0,2 ms; «dec», 617 decodificaciones/s (0,45 ms c/u, con el sink),
  espera media 0,28 ms, máx. 13,6 ms. Falta medir en la .177.

Escrito (2026-11-02, noche, sin compilar todavía: se compila y se mide todo junto con F1/F2):
**achicar la celda en el mismo hilo que decodifica (#6) y el BGRA de ba-app solo si hace falta (#8)**.

- Qué pasaba: el sink de cada cámara (`RemoteVideoSink`, Webrtc.cpp) convertía SIEMPRE el
  cuadro entero a BGRA (3,5 ms un 720p en la .177, #8) aunque su ruta no lo publicara (con 40
  en la grilla casi ninguno va a ba-app), y en Windows además bajaba el NV12 entero de la placa
  y lo pasaba a I420 antes. La grilla ya achicaba en ese hilo, pero desde lo grande.
- `src/gateway/DestinoCamara.{hpp,cpp}`: adónde va la cámara de una sesión. `quiereEntero()`
  (`MediaPump::quiereCamara`: lo mismo que mira `forwardVideo`/`publishVideo`, con el tope de
  cps de las miniaturas) y `pedirCelda`/`guardarCelda` (Grillas). Lo arma SignalingServer
  (sala y asamblea) en `cb.camara`.
- `Grillas::pedirCelda/guardarCelda/soltarCelda`: el hilo que decodificó recibe el búfer de la
  celda (del tamaño del lugar más grande que ocupa la persona, cubriéndolo, nunca más que el
  cuadro), lo llena como le convenga y lo devuelve: a `grillas` solo le llega lo chico. Se
  mantiene la cola de 4 cuadros por persona, el balde y `elegir` (el ritmo).
- `codecs::entregarCamara` (CodecsVideo.cpp): primero pregunta; si nadie lo usa, no convierte
  nada (`uso.codecsVideo.entrega.salteados`). Solo la grilla: la celda del cuadro en la placa
  (Windows), del I420 tal cual (software) o del buffer nativo achicado primero (`Scale()`: en la
  Mac, `ObjCFrameBuffer::CropAndScale` achica el CVPixelBuffer NV12 con libyuv al pedir el
  I420: nunca se arma el BGRA entero). ba-app: el BGRA entero y la celda de ahí.
- Windows (`CodecsWin.cpp`, `DecodificadorD3d11`): el cuadro ya no se baja al decodificar; sale
  un `BufferPlaca` (kNative) que lo baja recién si alguien pide `ToI420()`, y el decodificador
  se anuncia como `CuadroEnPlaca` (thread_local, sin RTTI) mientras llama a `Decoded()`. La
  celda se achica con `ID3D11VideoProcessor` (NV12 → BGRA del tamaño de la celda, BT.601
  limitado → RGB completo, como libyuv) y se baja solo eso; el entero va de NV12 a BGRA directo
  (sin el I420 del medio). Si alguien se quedó con el buffer después de `Decoded()`, se baja ahí
  (la superficie se reusa en el próximo `Decode`). Sin procesador que sirva: avisa una vez y se
  achica en el procesador. `gateway.celdaEnPlaca` (de fábrica sí) para comparar;
  `codecsVideo.h264.celdas_en_placa` y `achica_en_placa` en el estado.
- **Enganche en Webrtc.cpp (lo hace GW, F1/F2):** `SessionCallbacks::camara`
  (`std::shared_ptr<DestinoCamara>`) y en `RemoteVideoSink::OnFrame`, antes de `ToI420`, si
  sale y hay `camara`: `codecs::entregarCamara(frame, *camara_, bgra_); return;`.
- Cómo se prueba: `test_grillas` 16 (pedido, tamaño, balde, soltar, DestinoCamara sin ba-app);
  `test_webrtc_codecs` y `test_webrtc_loopback` siguen pasando (el loopback no usa `camara`);
  en la .177 con 10 en 720p y 40 en la grilla: `uso.codecsVideo.entrega` (salteados, celdas
  en placa/cpu, enteros), las fronteras #6/#8 (µs por pase y % de núcleo) y el mosaico (30 cps,
  colores iguales a los de antes: comparar una captura con `gateway.celdaEnPlaca` apagado).

Linux con VA-API (sin hacer: no hay placa para probar). Sería igual que Windows con
otras piezas: un `CodecsLinux.cpp` con decodificador y codificador H.264 sobre libva
(`vaCreateConfig` con `VAProfileH264ConstrainedBaseline`/`VAEntrypointVLD` para
decodificar y `VAEntrypointEncSlice`/`EncSliceLP` para codificar), superficies NV12
leídas con `vaDeriveImage`/`vaMapBuffer` y pasadas a I420 con libyuv, el parseo de
SPS/PPS/slices del lado nuestro (VA-API no parsea: se puede tomar el de
`common_video/h264` de libwebrtc) y caída a VP8 si `vaInitialize` falla (Mesa sin
`radeonsi`/`iHD`, o en una VM). libva y los drivers van en el bundle (portable), no del
sistema; el driver se elige con `LIBVA_DRIVERS_PATH` al lado de `bin/64bit`. Más simple
pero con otra dependencia grande: GStreamer `vah264dec`/`vah264enc` (ya viaja en el
bundle de ba-app) en un hilo del gateway.

### F4 — Afinidad

1. Llevar la topología de `src/motor/Prioridad.cpp` a `src/core/platform/Nucleos.*`.
2. El gateway pone sus capas en el bloque de caché que no usan los medios de ba-app
   (CPU Sets en Windows, `sched_setaffinity` en Linux; macOS nada). Línea en el log como
   la de ba-app.

Criterio: menos cambios de contexto y la misma latencia en la .177; si no mejora, queda
apagado por omisión (`gateway.afinidad`).

Escrito (2026-11-02, noche, sin compilar todavía):

- `src/core/platform/Nucleos.{hpp,cpp}`: la topología (`topologia`, `resumen`) y el reparto
  puro (`repartir`: medios = el L3 más grande de los núcleos rápidos, el primero si son
  iguales, como siempre; gateway = el más grande de los OTROS L3, sin los lógicos a evitar si
  quedan 2 o más; en un híbrido o con un solo L3, el gateway nada). ba-app sigue igual
  (`motor/Prioridad.cpp` llama a `armarGrupoMedios`/`alGrupoMedios`; los textos siguen en el
  dominio «motor»).
- Gateway: `main.cpp` arma el grupo antes de arrancar nada (`armarGrupoGateway`: Windows
  `SetProcessDefaultCpuSets`, así los hilos de libwebrtc que no creamos también caen ahí;
  Linux `sched_setaffinity` en el principal, que heredan los que se crean después) y escribe
  «procesador: …; hilos del gateway: …». Los hilos calientes además llaman a
  `ba::platform::nucleos::alGrupoGateway()`: grillas, retornos (`ba-retorno-N`), bajadas y audio
  de retorno de MediaPump, WireGuard; **GW lo llama en sus hilos de red, trabajo, TURN y en
  las colas compartidas**.
- Las interrupciones (`gateway.afinidadEvitar`, «auto»): Linux suma `/proc/interrupts` de las
  placas de red y evita las CPU con 20 % o más; Windows evita el 0 y el 1 (sin RSS, ahí caen
  las DPC de la placa). «» = ninguno; «0,1», «0-3» = esos.
- `gateway.afinidad` (de fábrica sí, para comparar apagado). En la .177 (dos CCD de 12): medios
  de ba-app en uno, gateway en el otro.
- Cómo se prueba: `test_nucleos` (5900X, evitar, un L3, híbrido, CCD desparejos, /proc/interrupts);
  en la .177 la línea del log y, con `muestrear-win.ps1`, en qué núcleos corre cada hilo del
  gateway; medir con `gateway.afinidad` prendido y apagado (cambios de contexto, edad del
  audio, mosaico).

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

Escrito (2026-11-02, noche, sin compilar todavía):

```
VPS ─UDP─► [wireguard] poll → recvmmsg (tanda) → descifra la tanda → lwIP (mismo hilo)
             ─► SumideroRtc::entregar(tanda) ─► sockets en memoria de GW (hilo de red Ri)
vuelta: GW SendTo ─► IpStack::enviarDesdeRtc (cola, despierta si estaba vacía) ─► lwIP ─►
             cifra en tanda ─► sendmmsg ─► VPS
```

- `WgDevice`: `cederBucle()` (la pila IP corre el bucle con `servir()`; `retomarBucle()` al
  pararla), recibe en tanda (`recvmmsg` en Linux, `recvfrom` hasta vaciar en los demás), los
  datos de la tanda se descifran juntos y se entregan en orden (los mensajes de handshake en
  el medio cortan la tanda: el orden se respeta), cifra en tanda (`empezarTanda`/
  `terminarTanda`, `sendmmsg` en Linux). Sin `std::vector` por paquete (búferes del
  dispositivo). #16 preparado: `DescifradorParalelo` (cada `DescifradoWg` es independiente;
  el anti-replay se marca al entregar), sin implementación.
- `IpStack`: lwIP en el hilo de WireGuard (se llama «wireguard»): sin la cola `inbound` ni el
  despertar por UDP local (#17); `poll`/`WSAPoll` en vez de `select`; despertador por eventfd
  (Linux), pipe (macOS) o UDP local (Windows) solo para las tareas y la vuelta de WebRTC, y
  solo si su cola estaba vacía.
- En memoria (#18/#19): `SumideroRtc` + `IpStack::setSumidero` + `IpStack::enviarDesdeRtc`
  (IpStack.hpp). **Lo enchufa GW** con su `PacketSocketFactory` en memoria (F1): una llamada
  por tanda con todos los datagramas para los puertos de WebRTC; GW los reparte a los hilos de
  red de cada grupo (en lote). Sin sumidero, o con `gateway.wgEnMemoria` apagado, el camino
  de siempre (un socket local por invitado y `sendto`), que es lo que queda hasta que GW lo
  enchufe. `IpStack::Stats` dice `enMemoria`, `aRtc` y `desdeRtc`.
- Cómo se prueba: `test_ipstack` 6 (un sumidero de eco: ida y vuelta en memoria, 20 seguidos
  con su respuesta, una llamada por tanda) y los 1-5 de siempre por el respaldo;
  `test_wireguard` (el dispositivo con su hilo propio, ahora en tanda). Con carga:
  `hinchada --wireguard` y las fronteras #17-#20.

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
