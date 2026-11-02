# PC Ryzen 9 5900X + Radeon RX 5700 XT (Windows 11)

La PC de pruebas de Windows (`192.168.1.177`, `ssh windows`). Medida el **2026-11-02** con el
paquete de Windows de master `87f4d0d8` (`BroadcastAppliance-2.1.0-windows-x64.zip`):
la app corre en la PC y **hinchada genera los invitados desde la Mac por la LAN**
(`tools/carga/medir-sala.py --remoto windows`). Las mediciones crudas están en
`~/Proyectos/mediciones-carga/2026-11-02-pc177/carga-{10,20,30,40}` (fuera del repo).

## Ficha

| | |
|---|---|
| Placa madre | ASUS TUF GAMING X570-PRO (WI-FI), BIOS 4802 (2023-06-14) |
| CPU | AMD Ryzen 9 5900X: 12 núcleos / 24 hilos, 3,7 GHz base, L2 6 MB, L3 64 MB |
| RAM | 32 GB DDR4 (2 × 16 GB Crucial Ballistix BL16G36C16U4RL) **a 2666 MT/s** (el kit es de 3600: el XMP/DOCP está apagado) |
| GPU | AMD Radeon RX 5700 XT (Navi 10, 8 GB GDDR6), driver 32.0.21045.16010 (2026-10-15) |
| Discos | NVMe Kingston SNV3S 2 TB (C:, sistema y `C:\ba-pruebas`) + NVMe Kingston SNV2S 1 TB (D:) |
| Sistema | Windows 11 IoT Enterprise 26H2, build 26300.9711, 64 bits; plan de energía «Equilibrado» |
| Red | Intel I225-V 2.5GbE, enlazada a **1 Gbps** por cable (perfil de red Privada); Wi-Fi AX200 sin usar |
| Monitores | DELL U2913WM 2560×1080 a 60 Hz (principal) + DELL 2208WFP 1680×1050 en vertical (1050×1680) |

### Qué usa la app de esta máquina

- **Motor de video (ba-app)**: GStreamer con **D3D12** en la Radeon (`d3d12upload ! d3d12convert`
  y la composición en `D3D12Memory`). La GPU va al 8-10 % del motor 3D con 10 a 40 invitados
  y ~0,5 GB de memoria dedicada: no es lo que limita.
- **Video de los invitados (ba-gateway)**: libwebrtc decodifica y codifica **por software**
  (libvpx VP8/VP9, OpenH264, dav1d): en Windows el gateway no usa la GPU ni el códec por
  hardware (VCN) de la placa; en la Mac usa VideoToolbox. Por eso el gateway gasta acá
  2-3 veces más CPU que en la Mac (ver la comparación).
- Los motores `video_codec`/`video_decode` de la Radeon quedaron en 0 % durante toda la prueba
  (sin grabación ni transmisión: los codificadores por hardware de salida no se midieron acá).
- La red de la prueba: la Mac está por **Wi-Fi 5** (802.11ac, 5 GHz/80 MHz, 866 Mbps de enlace,
  -41 dBm) y la PC por cable a 1 Gbps; ping ~5 ms.

## Cómo se comporta (lo esperado en este hardware)

Sala de prueba «prueba linea L0» (la misma de las mediciones de la Mac): grilla de personas al
aire con `alta_hasta` 10 (hasta 10 personas, todas en alta 720p30; con más, miniaturas en baja
360p15 salvo el orador), hinchada `--modo grabado --hablan 1`, 60 s de régimen y 20 s de
cuadros por celda. CPU en **% de un núcleo** (como `ps`: 100 % = un hilo entero); la máquina
entera en % de sus 24 hilos.

| Invitados | 10 | 20 | 30 | 40 |
|---|---|---|---|---|
| Todos con video / estable (s) | 2 / 8 | 3 / 25 | 4 / 13 | 5 / 21 |
| ba-app CPU p50 (p95) | 99 % (110) | 69 % (74) | 91 % (97) | 107 % (116) |
| ba-app hilos (máx) | 213 | 246 | 274 | 285 |
| ba-app memoria: working set / privada máx | 1,7 / 1,8 GB | 1,8 / 2,0 GB | 1,8 / 2,4 GB | 1,7 / 2,8 GB |
| ba-gateway CPU p50 (p95) | **320 % (363)** | 115 % (127) | 161 % (176) | 201 % (226) |
| ba-gateway hilos (máx) | 126 | 206 | 285 | 366 |
| ba-gateway memoria (working set) | 0,8 GB | 1,2 GB | 1,4 GB | 1,9 GB |
| Cambios de contexto/s: app / gateway | 5,7k / 11,6k | 5,6k / 17,6k | 5,8k / 25,6k | 5,9k / 32,9k |
| Máquina entera (24 hilos), CPU p50 | 31 % | 17 % | 23 % | 19 % |
| GPU (motor 3D de ba-*) / memoria dedicada | 8 % / 0,4 GB | 8 % / 0,5 GB | 8 % / 0,5 GB | 8 % / 0,5 GB |
| Grilla en el programa: mosaico | 30,0 cps | 30,0 cps | 30,0 cps | 30,0 cps |
| — celdas en baja (p50) | — | 15,0 cps | 15,0 cps | 15,0 cps |
| — celdas en alta (p50 / p5) | 29,9 / 29,8 | 30,0 / 30,0 | 30,0 / 30,0 | 30,0 / 29,9 |
| — cuadros perdidos | 0 | 0 | 0 | 0 |
| Edad del audio de los invitados en la mezcla (p50) | 11,6 ms | 11,9 ms | 14,4 ms | 14,5 ms |
| Faltantes del retorno de audio (máx en 10 s) | 0 | 0 | 0 | 0 |
| Red de la PC: sale / entra (Mbps) | 17,6 / 16,6 | 35,6 / 6,2 | 51,4 / 8,6 | 69,6 / 10,7 |
| Conectar: offer→medios p95 / desde el lanzamiento p95 | 20 ms / 1,2 s | 45 ms / 2,1 s | 23 ms / 3,1 s | 26 ms / 4,0 s |

Cómo leerla:

- **10 invitados es el caso caro para el gateway**: los diez van en alta (720p30), y el gateway
  decodifica diez 720p por software para la grilla (3,2 núcleos). Con 11 o más pasan a baja
  (360p15) y el gateway baja a ~1,2 núcleos con 20 y sube ~0,4 núcleos cada 10 invitados.
- La salida crece ~1,7 Mbps por invitado (el retorno 640×360 de cada uno); la entrada, ~0,2 Mbps
  por invitado en baja más ~1,5 Mbps por cada uno en alta.
- ba-app crece poco con la gente (~3 hilos por invitado); el gateway, ~8 hilos por invitado
  (en la Mac, ~1,3).
- La grilla queda **clavada a 30 cps** en el mosaico y en cada capa, sin perdidos, de 10 a 40.

### Prueba de sala desde la app (generador externo)

Con la **Prueba de sala** de la página Conferencia (`prueba_sala_arrancar` con generador
externo y hinchada en la Mac usando la credencial de prueba), 40 de 40 con video, 0 fallidos y
0 cortes; video al entrar p50 545 ms (p95 2,8 s); **HD al dar la palabra p50 102 ms (p95 284)**;
subida pico 9,6 Mbps, bajada pico 67,5 Mbps; memoria disponible mínima 21,4 GB.

## Límites y recomendaciones

- **Hasta 40 invitados con mucho margen**: la máquina entera no pasa del 31 % (p50) en
  ningún escalón, sobran ~21 GB de memoria y la grilla y el audio no se degradan. No se
  probó más de 40 (la Prueba de sala llega a 64); por la pendiente medida, 60 andarían en
  ~3 núcleos de gateway y ~105 Mbps de salida, pero hay que medirlo antes de prometerlo.
- **La red manda antes que la CPU**: con 40, 70 Mbps de salida. Por cable a 1 Gbps sobra; por
  Wi-Fi o por un túnel/servidor propio, medir el enlace (tarjeta «Enlace») antes del evento.
- **Ojo con `alta_hasta`**: subirlo (más gente en 720p30) cuesta ~0,3 núcleos de gateway por
  invitado en alta en esta PC (decodificación por software).
- **Tope de 32 conexiones por dirección IP** (`room.maxConexionesPorIp`, de fábrica 32): con un
  solo generador en otra máquina, el 33º invitado se rechaza («esa IP ya tiene 32») y reintenta
  sin parar. Para medir más de 32 desde una máquina, subirlo en el evento de prueba (se usó 64).
  **En un evento real con muchos invitados detrás de la misma IP pública (NAT de un edificio, una
  red de celular), pasa lo mismo**: revisar ese ajuste.
- RAM a 2666: el kit es de 3600 y el XMP/DOCP está apagado en el BIOS; no limita estas pruebas,
  pero prenderlo es gratis para lo que corre en CPU (gateway).

## Comparación con la Mac

MacBook Pro M1 Pro (8 núcleos), 32 GB, macOS 26, misma sala y mismo hinchada, pero **la app y
hinchada en la misma Mac** (`mediciones-carga/2026-11-02-al-final`, la serie después de los arreglos de la grilla; los
números de la Mac incluyen la carga del generador en la máquina).

| Invitados | 10 | 20 | 30 | 40 |
|---|---|---|---|---|
| ba-app CPU p50 — PC / Mac | 99 / 53 % | 69 / 60 % | 91 / 73 % | 107 / 86 % |
| ba-gateway CPU p50 — PC / Mac | 320 / 93 % | 115 / 71 % | 161 / 88 % | 201 / 108 % |
| ba-app memoria — PC (working set) / Mac (RSS) | 1,7 / 3,3 GB | 1,8 / 3,5 GB | 1,8 / 3,8 GB | 1,7 / 4,1 GB |
| ba-gateway hilos — PC / Mac | 126 / 66 | 206 / 71 | 285 / 82 | 366 / 93 |
| Máquina entera p50 — PC (24 hilos, sin generador) / Mac (8 núcleos, con hinchada) | 31 / 40 % | 17 / 37 % | 23 / 43 % | 19 / 50 % |
| Celdas en alta p50 — PC / Mac | 29,9 / 28,4 cps | 30,0 / 29,8 | 30,0 / 28,9 | 30,0 / 30,1 |
| Audio, edad p50 — PC / Mac | 11,6 / 10,9 ms | 11,9 / 10,4 | 14,4 / 12,9 | 14,5 / 14,1 |
| Faltantes del retorno (máx 10 s) — PC / Mac | 0 / 0 | 0 / 10 | 0 / 8 | 0 / 25 |
| Salida de red (Mbps) — PC / Mac | 17,6 / 16,5 | 35,6 / 32,5 | 51,4 / 50,4 | 69,6 / 67,9 |

En resumen: la PC gasta más CPU por proceso (el gateway sin códecs por hardware, el motor
con más trabajo en CPU) pero tiene 3 veces más hilos de hardware y queda mucho más holgada
como máquina; usa la mitad de memoria; la grilla y el audio salen igual o mejor.

## Capturas

La Conferencia con 10 invitados (cada uno con su número y su video):

![Conferencia con 10 invitados](img/pc177-sala-10.jpg)

La grilla de personas al aire con 10 (todos en alta) y con 40 (miniaturas en baja, el orador
resaltado):

![Grilla con 10](img/pc177-programa-grilla-10.jpg)

![Grilla con 40](img/pc177-programa-grilla-40.jpg)

La Conferencia con 40 y la Prueba de sala corriendo (columna derecha), y el panel de cerca:

![Conferencia con 40 y la Prueba de sala](img/pc177-sala-40.jpg)

![Panel de la Prueba de sala con 40](img/pc177-panel-prueba-sala-40.jpg)

> **Defecto visto en la captura de 40**: la lista «En la conferencia» encoge las filas hasta
> dejarlas como rayas vacías en lugar de desplazarse (con 10 se ven bien). Anotado para
> arreglar; no afecta la medición.

## Problemas encontrados en esta medición

- Al cambiar de evento por MCP (`evento_cambiar`) justo después del primer arranque del paquete
  nuevo, el launcher dio a la app por **colgada** («dejó de latir») a los 10 s, sacó un volcado
  (`Logs/dumps/A-20261102-114932-colgado.dmp`) y la relanzó; el evento nuevo abrió bien. La
  pila: el hilo que cierra el motor queda esperando en `gst_element_set_state` →
  `gst_pad_stop_task` (el candado del flujo de un pad que tiene otro hilo), un abrazo mortal de
  GStreamer al bajar una cadena. El segundo cambio (de vuelta a «Principal») cerró limpio.
- Con el generador externo, la Prueba de sala muestra **«fallidos 40»** apenas arranca (antes de
  que entre nadie); se corrige solo cuando entran.
- Bajo Wine (el armado en Docker) fallaron 16 tests, casi todos rc 133/3 del entorno; además de
  los conocidos: test_cupos, test_gateway_http, test_mirada, test_sse_epoch y test_tiempo_real
  (sin revisar en la PC).

## Cómo repetir la medición

1. Paquete de Windows (`make dist-win-docker`) con hinchada de Windows en `extern/hinchada`
   (`make deps-hinchada`); desplegar en `C:\ba-pruebas\BroadcastAppliance` y arrancar en la
   sesión del usuario (`C:\ba-pruebas\app.ps1 -Que arrancar`).
2. Evento de prueba con el gateway en la LAN (`gateway.bind` 0.0.0.0) y, para más de 32 desde
   una sola máquina, `room.maxConexionesPorIp` 64. Comprobar con `evento_en_curso`.
3. Desde la Mac, con la app de la Mac **cerrada**, colima apagada y sin compilaciones:

   ```sh
   ssh -f -N -L 18790:127.0.0.1:8790 windows
   export BA_MCP_URL=http://127.0.0.1:18790/mcp
   export BA_MCP_TOKEN=$(ssh windows 'C:\ba-pruebas\BroadcastAppliance\bin\64bit\ba-mcp.exe --token' | tr -d '\r\n')
   for n in 10 20 30 40; do
     tools/carga/medir-sala.py --ba-mcp dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/ba-mcp \
       --remoto windows --url http://192.168.1.177:8787 --participantes $n --cupo --cupo-final 2 \
       --regimen 60 --medir-grilla-s 20 --salida ~/Proyectos/mediciones-carga/<fecha>-pc177/carga-$n \
       -- --modo grabado --hablan 1
   done
   ```

4. Al terminar: volver al evento que estaba abierto, `app_quit`, que no quede `ba-*.exe` ni
   tareas `\ba-*` (`schtasks /Query`), y cerrar el túnel.
