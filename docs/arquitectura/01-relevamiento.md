# Relevamiento de la arquitectura (01)

Relevamiento **descriptivo** del sistema tal como está en el código: qué hay, cómo
está conectado y con qué números. No contiene evaluaciones ni propuestas. Cada
dato lleva su `archivo:línea` (rutas relativas a la raíz del repositorio).

- Código relevado: rama `master`, commits `e002ac9b` → `53287af3` (2026-10-31). Durante el
  relevamiento entraron a `master` otros commits; las referencias `archivo:línea` de los
  archivos que cambiaron se revisaron contra `d49547c3` (el último commit, `53287af3`, no
  toca archivos citados).
- Método: lectura del código fuente (`src/`, `cmake/`, `tools/deps/`) y de los
  documentos (`docs/planes/`, `docs/funciones/`, `docs/desarrollo/`). No se corrió
  la app ni los tests. Donde un documento o un comentario de cabecera dice algo
  distinto de lo que hace el código, se anota en la sección 13.
- Unidades: ms = milisegundos; «cps» = cuadros por segundo; los valores marcados
  `ajustes:<clave>` se pueden cambiar en Configuración → avanzados (mecanismo en
  `src/core/ajustes/Ajustes.hpp:1-50`); el número que se da es el de fábrica.

## Índice

1. [Procesos y su ciclo de vida](#1-procesos-y-su-ciclo-de-vida)
2. [Hilos](#2-hilos)
3. [Comunicación entre procesos](#3-comunicación-entre-procesos)
4. [Camino del video](#4-camino-del-video)
5. [Camino del audio](#5-camino-del-audio)
6. [Relojes y sincronía](#6-relojes-y-sincronía)
7. [Disponibilidad](#7-disponibilidad)
8. [Concurrencia y bloqueos](#8-concurrencia-y-bloqueos)
9. [Organización del código](#9-organización-del-código)
10. [Plataformas](#10-plataformas)
11. [Configuración y datos](#11-configuración-y-datos)
12. [Cifras (constantes, colas, intervalos)](#12-cifras-constantes-colas-intervalos)
13. [Documentos y comentarios contra el código](#13-documentos-y-comentarios-contra-el-código)
- [Apéndice A — Datos que no se pudieron determinar sin correr el sistema](#apéndice-a--datos-que-no-se-pudieron-determinar-sin-correr-el-sistema)

---

## 1. Procesos y su ciclo de vida

### 1.1 Ejecutables

| Ejecutable | Fuente | Lo lanza | Qué hace | Cómo se comunica |
|---|---|---|---|---|
| `BroadcastAppliance` (launcher, «proceso 0») | `src/launcher/` (`CMakeLists.txt:27`) | el usuario / inicio con el sistema | instancia única, integridad, lanza y vigila A y B, cierre ordenado, actualizador | memoria compartida `BA.Health`; canal de control por socket local (`ControlChannel`); variables de entorno |
| `ba-app` (A) | `src/app/` (`CMakeLists.txt:303-305`) | launcher | interfaz Qt/QML, motor de GStreamer (`ba_motor`), salidas, MCP | `BA.Health` slot 1; memoria compartida con B; HTTP a la API de B (8788); MCP HTTP (8790) |
| `ba-gateway` (B, «la sala») | `src/gateway/` (`CMakeLists.txt:46-50`) | launcher | WebRTC (libwebrtc M123), señalización HTTP/WS, API de la sala, túneles, TURN, WireGuard en espacio de usuario | `BA.Health` slot 0; memoria compartida con A; puertos 8785-8793, 40000-40099 |
| `ba-mcp` | `src/app/mcp/bridge/ba_mcp_main.cpp` (`src/app/CMakeLists.txt:584`) | un cliente MCP externo, o la app para sus agentes | puente MCP stdio ↔ HTTP `127.0.0.1:<puerto>/mcp` | stdin/stdout JSON-RPC por línea; lee `Runtime/mcp.json` en cada mensaje (`ba_mcp_main.cpp:14-20`) |
| `ba-web` | `src/web/` (C, `CMakeLists.txt:13-16`) | gateway (`src/gateway/BaWeb.cpp:130`) | servidor HTTPS tipo nginx (master + workers) para la red local y el túnel | escuchas TCP; certificado por stdin (`src/gateway/main.cpp:986-1008`) |
| `ba-pinggy` | `src/pinggy/main.cpp` | gateway (`src/gateway/Pinggy.cpp:271`) | túnel de pinggy.io en proceso aparte (OpenSSL separado de BoringSSL de libwebrtc) | stdout: un JSON por línea (`src/pinggy/main.cpp:1-20`) |
| `cloudflared` | binario de terceros (`extern/cloudflared`) | gateway (`src/gateway/Cloudflared.cpp:79`) | túnel de Cloudflare | métricas en `127.0.0.1:9099` (`src/gateway/Config.hpp:61`) |
| `ba-grafico` | `src/grafico/` (`CMakeLists.txt:36`) | app (`src/app/graficos/GraficoProceso.*`) | dibuja gráficos QML (vistas previas y tiras de cuadros) | stdin/stdout JSON por línea; píxeles por memoria compartida `Segmento` con `"shm":true` (`src/grafico/main.cpp:1-35`, `src/app/graficos/GraficoProceso.cpp:143-153`) |
| `ba-modelo` (llama.cpp) | `src/modelo-llama/main.cpp` | app (`src/app/agent/ModelosLocales.cpp:1236`) | modelo local en Windows y Linux (Vulkan) | stdin/stdout JSON por línea (`src/modelo-llama/main.cpp:1-30`) |
| `ba-modelo` (MLX, Swift) | `src/modelo-mac/Sources/ba-modelo/main.swift` (792 líneas) | app | modelo local en la Mac | mismo protocolo que el de llama.cpp |
| `ba-control` | `src/app/escritorio/ba_control_main.cpp` (`src/app/CMakeLists.txt:594`) | app / agentes | control del escritorio (UIA, Chrome) | línea de comandos |
| Plugins de Python | `src/plugins/sdk/` + `src/app/plugins/Hijo.cpp` | app (`Hijo.cpp:71`) | extensiones del usuario | stdin/stdout (Hijo) |
| WordCraft/DeckCraft/GridCraft (`*-cli`) | `extern/craft` | app (`src/app/carrusel/Conversion.cpp:75-102`) | conversión de documentos a PDF | proceso por pedido |
| `chrome-headless-shell` | `extern/chrome-headless` | app (`src/app/net/CapturaWeb.cpp:30-52`) | captura de páginas web | proceso por pedido |
| `llama-server`, agentes (claude, opencode…) | externos | app (`src/app/agent/ServidorModeloLocal.cpp`, `ServidorOpencode.cpp`, `Despachador.cpp:303`) | asistentes | HTTP / stdio |
| `ba-app --sonda-drivers` | el mismo `ba-app` | app (`src/app/dispositivos/Sondeo.cpp:44`) | prueba drivers en un proceso aparte | proceso por pedido |

### 1.2 Árbol de procesos

```
BroadcastAppliance (launcher)
├── ba-gateway (B)            lanzado PRIMERO (Supervisor.cpp:243-247)
│   ├── cloudflared           (si tunnel)
│   ├── ba-pinggy             (si pinggyActivo)
│   └── ba-web (master)
│       └── workers (fork en POSIX; relanzamiento con --worker en Windows: src/web/main.c:1-15)
└── ba-app (A)                lanzado DESPUÉS de B
    ├── ba-grafico            (a demanda)
    ├── ba-modelo             (a demanda)
    ├── plugins Python        (a demanda)
    ├── *-cli de Craft, chrome-headless-shell, arp (a demanda)
    └── agentes / llama-server (a demanda)
```

Los hijos A y B se lanzan con `killWithParent = false`: sobreviven a una caída
del launcher (`src/launcher/ChildProcess.cpp:230`) y un launcher nuevo los
«readopta» si su PID publicado está vivo y su latido es fresco
(`src/launcher/Supervisor.cpp:249-263`).

### 1.3 El launcher: secuencia y supervisión

Secuencia de `Supervisor::run()` (`src/launcher/Supervisor.cpp:109-167`):

1. Instancia única `SingleInstance::acquire("launcher")`; con `--reiniciar` espera
   hasta 60 s al anterior (`src/launcher/main.cpp:129`, `Supervisor.cpp:112-120`).
2. `suppressCrashDialogs()` (heredado por los hijos) (`Supervisor.cpp:122`).
3. Integridad rápida (tamaño y fecha contra caché; completa si la corrida anterior
   dejó la marca de fallo) (`Supervisor.cpp:194-217`).
4. Copia sombra opcional (desactivada por defecto) (`Supervisor.cpp:140-144`, `Supervisor.hpp:58-61`).
5. Abre o crea `BA.Health` con `max(slotA, slotB)+1` = 2 slots (`Supervisor.cpp:148-157`).
6. Lanza B y luego A (`Supervisor.cpp:243-247`).
7. Hilo de fondo: integridad completa con prioridad baja (`Supervisor.cpp:162`, `219-241`).
8. `watchdogLoop()` en el hilo principal, cada 250 ms (`Supervisor.cpp:45`, `483-560`).

Constantes del supervisor:

| Parámetro | Valor de fábrica | Dónde | Configurable |
|---|---|---|---|
| Período del watchdog | 250 ms | `Supervisor.cpp:45` | no |
| Latido quieto = colgado | 5000 ms | `Supervisor.hpp:65` | `launcher.json:cuelgueS` (2-120 s) (`launcher/main.cpp:152`) |
| Gracia de arranque | 20000 ms | `Supervisor.hpp:66` | `graciaArranqueS` (5-600 s) |
| Espera del primer latido (si nunca latió) | 90 s | `Supervisor.cpp:61` | `ajustes:launcher.primerLatidoS` |
| Backoff de relanzamiento | 0, 1, 2, 5 s y después 10 s | `Supervisor.cpp:72-78` | `ajustes:launcher.relanzarMaxS` (mín. 5) |
| Cortacircuito (modo seguro de A) | más de 5 caídas de A en 120 s | `Supervisor.hpp:67-68`, `Supervisor.cpp:300-311` | `maxCaidas`, `ventanaCaidasMin` |
| Fallas que se reinician sin preguntar | 2 | `Supervisor.hpp:69`; solo pregunta por A (`Supervisor.cpp:536`) | `reiniciosSinPreguntar` |
| Reintento si el hijo sale con `kEnvironment` | 60 s | `Supervisor.cpp:63`, `375-389` | `ajustes:launcher.reintentoEntornoS` |
| Espera tras pedido de cierre por canal | 5000 ms | `Supervisor.cpp:52` | `ajustes:launcher.cierreOrdenadoMs` |
| Espera tras SIGTERM / kill | 3000 ms | `Supervisor.cpp:53` | `ajustes:launcher.terminarMs` |
| Rotación de `app.log`/`gateway.log` | 5 MB | `src/launcher/ChildOutput.hpp:44` | `ajustes:launcher.salidaHijosMB` |

Datos de comportamiento:

- Los contadores de reinicios consecutivos (`appConsecutive_`, `bConsecutive_`)
  que eligen el escalón del backoff solo se ponen en cero en `forceRestart()`
  (`Supervisor.cpp:314-316`, `474-475`); no hay otro lugar del código que los reinicie.
- A relanzado tras una caída recibe `--restore`; con el cortacircuito, además
  `--safe-mode` (`Supervisor.cpp:274-279`). En modo seguro las escenas se crean
  vacías, el codificador pasa a `x264` y no se guarda la configuración
  (`src/app/arranque/OperateMode.cpp:149-165`, `659-662`, `1810-1835`).
- B que sale con código 0 sin volcado y no dos veces en 10 s se trata como
  «reinicio pedido» y se relanza sin backoff ni conteo (`Supervisor.cpp:390-399`, `407-410`).
- A que sale con código 0 = cierre ordenado del operador: el launcher cierra todo
  (`Supervisor.cpp:495-500`).
- El cartel de recuperación corre en un hilo desprendido; el watchdog sigue
  relanzando mientras tanto (`Supervisor.cpp:435-463`).
- Cierre ordenado: primero A, después B; cada uno: pedido por canal de control
  (5 s) → SIGTERM (3 s) → kill (`Supervisor.cpp:562-598`).
- Causa de cada caída: `Runtime/ultima-caida.json` (truncado y reescrito, sin
  temporal) y `Runtime/caidas.jsonl` (agregado) (`Supervisor.cpp:349-369`).
- Volcados: `Logs/dumps/` con un `.json` al lado, últimos 10 por proceso; solo en
  Windows (`src/launcher/Volcados.hpp:1-17`).

### 1.4 El latido de A y de B

| Proceso | Quién late | Cadencia | Condición | Dónde |
|---|---|---|---|---|
| B | hilo principal de `ba-gateway` | 250 ms | siempre, en el mismo bucle que `hub.reapViewers()`, `room.tick()` y `room.setUsage(processUsage())` | `src/gateway/main.cpp:1310-1347` |
| A (arranque) | hilo que espera `gst_init` | ~100 ms | mientras GStreamer revisa plugins | `src/app/arranque/OperateMode.cpp:502-539` |
| A (arranque) | hilo `LatidoArranque` | 500 ms × 240 = hasta 120 s | hasta que arranca `LatidoAire` | `OperateMode.cpp:546-564`, `633` |
| A | `AppRuntime::tick()` en el hilo de la interfaz | 100 ms (`QTimer tick_`) | siempre | `OperateMode.cpp:766-786` (late en `773-776`); timer en `src/app/ui/StudioViewModel.cpp:2301-2302` |
| A | hilo de `LatidoAire` | 250 ms | solo si el contador de cuadros del programa avanzó en el último 1000 ms (`ajustes:supervisor.sinAvanceMs`); deja de latir si la UI está trabada más de 60 s **y** no hay transmisión ni grabación | `src/app/emision/LatidoAire.cpp:67-104`, `LatidoAire.hpp:36-40` |

El launcher considera colgado un slot cuyo `lastBeatQpc` no cambió en 5 s
(`Supervisor.cpp:492-493`). La UI se marca «trabada» a los 2000 ms
(`ajustes:supervisor.uiTrabadaMs`, `LatidoAire.cpp:87`) y lo escribe en
`Runtime/estado-ui.json` desde un hilo propio (`LatidoAire.cpp:106-120`).

### 1.5 Qué pasa si muere cada proceso

| Muere | Detección | Acción | Efecto en los demás (según el código) |
|---|---|---|---|
| launcher | — | ninguna | A y B siguen (`ChildProcess.cpp:230`); un launcher nuevo los readopta por PID + latido (`Supervisor.cpp:249-263`) |
| A | salida del proceso o latido quieto 5 s | volcado (Windows) + relanzar con backoff y `--restore` | B: los lectores de `BA.Program`/`BA.Audio.*` detectan el productor nuevo con `stale()` y se resuscriben; reintento de apertura cada 500 ms (`src/gateway/MediaPump.cpp:464-506`, `599-640`); el invitado ve la placa de B (5 cps, `MediaPump.cpp:1220-1240`) |
| B | ídem | relanzar con backoff, sin preguntar | A: `bavideoanillo`/`baanillo` se resuscriben solos (reabren tras 1 s sin datos) (`src/gst-plugins/ipc/video.cpp:1-22`, `anillo.cpp:1-20`); `ConferenceClient` reconecta el SSE con espera creciente (`src/app/ui/ConferenceClient.hpp:1-15`; `ajustes:room.reconexionMaxS`=15, `room.salaMuertaS`=40) |
| cloudflared / ba-pinggy | proceso caído o sin métricas | relanzar con backoff 1 s → 30 s (`ajustes:gateway.tunelReintentoMaxS`) | `src/gateway/Cloudflared.cpp:196-244`, `Pinggy.cpp:387-409` |
| ba-web | proceso caído | relanzar con backoff 500 ms → 8 s | `src/gateway/BaWeb.cpp:195-216` |
| ba-grafico | error en el pedido o tiempo límite | se levanta en el pedido siguiente; tope por pedido 20 s (`ajustes:graficos.limiteS`) | `src/app/graficos/GraficoProceso.hpp:1-22`, `GraficoProceso.cpp:25` |
| plugin de Python | salida | relanzar; más de 5 caídas en 60 s lo detienen | `ajustes:plugins.maxCaidas`, `plugins.ventanaCaidasS` (`src/app/plugins/Plugins.cpp:133-134`) |
| traductor/modelo | salida | relanzar en 500 ms; tope 5 caídas | `ajustes:traductor.relanzarMs`, `traductor.maxCaidas` (`src/app/agent/Traductor.cpp:84,541`) |

### 1.6 Corte de luz y retoma

- `Runtime/al-aire.json`: por salida, la escena al aire y si transmite/graba, más
  `cerrado` (true solo en el cierre ordenado). Se escribe con temporal + `rename`
  (sin `fsync`) (`src/app/arranque/OperateMode.cpp:959-981`); se reescribe desde
  `tick()` cuando cambia la firma «salida→escena» (`OperateMode.cpp:783`) y en
  cada arranque/parada de grabación o transmisión (`OperateMode.cpp:874`, `919`,
  `1098`, `1107`).
- Al arrancar: si `cerrado` es falso se vuelve a la escena de cada salida
  (`OperateMode.cpp:712-736`) y se retoman grabaciones y transmisión
  (`OperateMode.cpp:983-1009`). Con `--test` se restituye la escena pero nunca
  las salidas (`OperateMode.cpp:988`).
- La clave de la última transmisión se guarda cifrada en `Credenciales/`
  (`stream-last`) para retomar sin tipearla (`OperateMode.cpp:924-941`, `1093-1097`).
- `--restore` del launcher no cambia esta lógica: `restoreOutputs()` corre en
  cualquier arranque (`OperateMode.cpp:712-716`).
- El diario de arranque `Runtime/arranque.json` se escribe con `fsync`
  (`src/core/diag/DiarioDeArranque.cpp:115-154`).

---

## 2. Hilos

### 2.1 Launcher

| Hilo | Qué hace | Dónde |
|---|---|---|
| principal | `watchdogLoop` cada 250 ms; lanza y mata hijos | `Supervisor.cpp:483-560` |
| `fondo_` | integridad completa (hash) con prioridad baja | `Supervisor.cpp:162`, `219-241` |
| cartel de recuperación | desprendido; espera la decisión del operador | `Supervisor.cpp:457-462` |
| volcados | desprendido, escribe el volcado | `src/launcher/Volcados.cpp:234` |

### 2.2 App (A)

**Hilo principal (Qt)**. Corre: el bucle de eventos y QML; `StudioViewModel::onTick`
cada 100 ms (`StudioViewModel.cpp:2301-2302`, `2748-2750` → `AppRuntime::tick`:
canal de control, latido, `writeOnAirState` si cambió, recortes pendientes);
`FrameService::poll` cada 16 ms (`StudioViewModel.cpp:2303-2304`), que lee los
canales de invitados de memoria compartida y copia el cuadro (`src/app/ui/FrameService.hpp:27`,
`FrameService.cpp:286-315`); el servidor MCP (`QTcpServer` en `127.0.0.1`,
`src/app/mcp/McpHttp.cpp:94-100`) y la ejecución de sus herramientas
(`docs/planes/PLAN-MOTOR-SIN-ESPERAS.md:5-8`); `AlertEngine` cada 250 ms
(`src/app/agent/AlertEngine.hpp:4-8`); `ConferenceClient` (Qt Network, asíncrono);
arranque y apagado del motor (`PLAN-MOTOR-SIN-ESPERAS.md`, fila «El latido del
arranque»). Hay 67 temporizadores `start/setInterval(<n>)` en `src/app` (grep
`(\.|->)(start|setInterval)\([0-9]+\)`); los de 100 ms o menos están en la sección 12.4.

**Hilos del motor (`ba_motor`)**:

| Hilo | Período / disparo | Qué hace | Dónde |
|---|---|---|---|
| `Motor::hilo_` («bucle del motor») | 20 ms o antes si llega una orden | corre la cola de órdenes; `revisarLatencia`; `tic` de entradas, buses, composiciones, vistas; publica niveles cada 2 vueltas (~40 ms) y la foto de estado cada 10 vueltas (~200 ms) o a pedido; ajusta latencias de puentes y escucha | `src/motor/Motor.cpp:120`, `332-437` |
| `Motor::hiloCuadros_` | espera con tope 200 ms | entrega los cuadros BGRA que la app dejó en el buzón (`empujarCuadro` copia el cuadro al buzón con `memcpy` por fila) a las entradas `tipo=app` | `Motor.cpp:121`, `986-1002`, `1041-1065` |
| trabajador por `Entrada` | a demanda (`std::async`) | arma/rearma/desarma la entrada fuera del bucle | `src/motor/Entrada.cpp:244` |
| desarmes aparte | a demanda (`std::async`) | pasa pipelines a NULL fuera del bucle | `Motor.cpp:1135` |
| vigía de dispositivos | sondeo | detecta cámaras/micrófonos que aparecen/desaparecen | `src/motor/Piezas.cpp:567`, `602-605` (sondeo cada 3 s) |
| recálculo de latencia aparte | a demanda | `gst_bin_recalculate_latency` y consulta de latencia fuera del bucle | `Piezas.cpp:815`, `868` |
| origen de red | a demanda | desarma un origen RTSP compartido | `src/motor/OrigenRed.cpp:75` |
| `Escucha` | a demanda (`std::async`) | arma/desarma la escucha | `src/motor/Escucha.cpp:47` |

**Hilos de streaming de GStreamer**. Cada pipeline del motor tiene los suyos; el
motor arma un pipeline por: entrada (más uno de «placa» para las entradas de
video en vivo, `Entrada.cpp:296-330`), bus de audio (`Mezcla`), composición
(`Composicion`), vista (`Vistas`), salida (grabación, transmisión, repetición),
puente al gateway, toma de audio del programa y escucha. Por construcción:

- cada `intersrc` es una fuente con su propio hilo (los elementos `inter` de
  gst-plugins-rs, `libgstrsinter`, en `extern/gstreamer-macos/.../gstreamer-1.0/`);
- cada agregador (`audiomixer`, `compositor`, `glvideomixer`, `d3d12compositor`)
  tiene su hilo de salida;
- cada `queue` agrega un hilo.

Recuentos por construcción (sin contar hilos internos de decodificadores, GL o D3D12):

| Pipeline | Hilos de streaming | Dónde |
|---|---|---|
| Bus de audio con *E* entradas | *E* (`intersrc` sin `queue`) + 1 (`audiomixer`) | `src/motor/Mezcla.cpp:79-90`, `157-180` |
| Composición con *C* capas | *C* (`intersrc` sin `queue`) + 1 (mezclador); con DSK, +2 `queue` +1 mezclador | `src/motor/Composicion.cpp:224-292`, `598-602` |
| Vista | 1 (`intersrc`) | `src/motor/Vistas.cpp:133-135` |
| Grabación | 1 por `intersrc` (video + cada pista) + 1 `queue` por rama + colchones (`queue`) por rama | `src/motor/Salidas.cpp:342-375` |
| Transmisión | 2 `intersrc` + 3 `queue` (video, audio, colchón del parser) | `Salidas.cpp:607-640` |
| Puente al gateway / toma / escucha | `intersrc` + `queue` | `Motor.cpp:237-257`, `1215-1236`; `Escucha.cpp:73-93` |

Con 6 pistas posibles, N retornos mix-minus, el bus «directo», la «selección»,
PROGRAMA y PREVIEW por salida, el multiview y las vistas, el número total depende
de la configuración (ver Apéndice A).

**Otros hilos de la app** (no exhaustivo; 54 sitios `std::thread(` y 9
`QThreadPool::globalInstance()->start` en `src/app`, fuera de tests):

| Hilo | Período / disparo | Dónde |
|---|---|---|
| hilo de salidas de `BroadcastEngine` | espera ≤ 100 ms; corre órdenes de salidas, `ticSalidas()` (transmisiones, grabaciones, copia, repetición, ISO) y publica la foto de salidas | `src/app/emision/BroadcastEngine.cpp:420`, `1250-1268`, `1867-1885` |
| `LatidoAire` | 250 ms | `src/app/emision/LatidoAire.cpp:42`, `67-104` |
| escritor de `estado-ui.json` | a demanda | `LatidoAire.cpp:115` |
| mezcla de la Matriz | 10 ms (`sleep_until`), solo si hay buses virtuales | `src/app/audio/Matriz.cpp:171`, `809-873` |
| `Recursos` | 1 s | `src/core/diag/Recursos.cpp:783-795` |
| `Perfil` (hilo «principal» del muestreo) | — | `src/core/diag/Perfil.cpp:518` |
| salida estándar asíncrona | 2 hilos (lector de la tubería y escritor al archivo) | `src/core/diag/SalidaAsincrona.cpp:179-231` |
| `VozAlLobby` | bucle | `src/app/emision/VozAlLobby.cpp:52` |
| `GrabadorWav` | bucle | `src/app/audio/GrabadorWav.cpp:86` |
| `Calibracion` | a demanda | `src/app/audio/Calibracion.cpp:460` |
| `NdiSource` (un hilo de recepción por `bandisrc`) | continuo | `src/app/emision/NdiSource.cpp:216` |
| `GraficoSource` (pre-render) / `Emisor` | a demanda | `src/app/graficos/GraficoSource.cpp:156`, `Emisor.cpp:58` |
| `BancoSuperRes` | a demanda | `src/app/video/BancoSuperRes.cpp:207` |
| `TiemposModel` | a demanda | `src/app/tiempos/TiemposModel.cpp:54` |
| sellado de evento, descargas, HTTP asíncrono, pantallas, red, dispositivos, notificador, agentes | desprendidos, a demanda | `StudioViewModel.cpp:2498`, `net/Descargas.cpp:426,484`, `net/AsyncHttp.hpp:34`, `ui/PantallasViewModel.cpp:614-1374`, `red/Red.cpp:668`, `dispositivos/Registro.cpp:268,359`, `agent/*` |
| Qt Multimedia (`QAudioSink`/`QAudioSource`) | hilo propio de Qt | `src/app/audio/EquipoAudio.cpp:158-169` (abre en un `QThread`), `Matriz.cpp:659-661` |
| superresolución | `g_thread_new` para cargar modelos | `src/gst-plugins/superres/onnxsr.cpp:1329`, `coremlsr_mac.mm:307` |

### 2.3 Gateway (B)

| Hilo | Qué hace | Dónde |
|---|---|---|
| principal | latido 250 ms, `room.tick`, `reapViewers`, uso de CPU | `src/gateway/main.cpp:1310-1347` |
| `acceptThread_` + `adminAcceptThread_` + control LAN + puerta HTTP + túnel | `accept()` por escucha | `src/gateway/SignalingServer.cpp:272`, `333`, `349`, `362`, `378` |
| uno por conexión HTTP/WS | desprendido; incluye el SSE de la app | `SignalingServer.cpp:801-806`; tope `ajustes:room.maxConexiones`=64 (`SignalingServer.hpp:604`) |
| `statsThread_` | cada 2 s: estadísticas WebRTC, direcciones, quién mira el programa | `SignalingServer.cpp:274-296` |
| libwebrtc: `network`, `worker`, `signaling` | hilos de libwebrtc | `src/gateway/Webrtc.cpp:1106-1108` |
| `PullAdm` | cada 10 ms tira del audio recibido (NetEq) | `Webrtc.cpp:171-236` |
| `Webrtc` loop | — | `Webrtc.cpp:202` |
| `MediaPump`: retorno de audio, retorno de video (`BA.Program`), una por `BA.Escena.<k>` (4), audio del operador, voz del atendedor, una voz por canal de registro (8), video del operador, placas | fijos desde el arranque | `src/gateway/MediaPump.cpp:262-283` |
| `MediaPump`: par video+audio por cada salida no principal | bajo demanda | `MediaPump.cpp:416-419` |
| `Grillas` | compone `BA.Grilla.<k>` a 25 cps (`ajustes:grilla.cps`) | `src/gateway/Grillas.cpp:150`, `main.cpp:1295` |
| supervisores de cloudflared, pinggy, ba-web | uno cada uno | `Cloudflared.cpp:79`, `Pinggy.cpp:271`, `BaWeb.cpp:130` |
| TURN, WireGuard, pila IP (lwIP), TLS (accept + uno por conexión), proxy de nombres, ACME | uno cada uno | `turn/Servidor.cpp:926`, `net/WgDevice.cpp:202`, `net/IpStack.cpp:624`, `net/TlsTerminator.cpp:399,448`, `proxy/ProxyNombres.cpp:359`, `proxy/GestorProxy.cpp:106` |

### 2.4 Prioridades, afinidad, tiempo real

Todo pasa por `src/motor/Prioridad.cpp`. Se aplica a los hilos de streaming de
cada pipeline que pasa por `Reloj::adoptar` (manejador síncrono del bus en
`GST_STREAM_STATUS_TYPE_ENTER`, `Prioridad.cpp:253-265`, `309-316`), al bucle del
motor (`Motor.cpp:334`) y al hilo de la Matriz (`Matriz.cpp:810`).

| Clase | macOS | Windows | Linux |
|---|---|---|---|
| Crítico (hilo de un agregador; Matriz) | `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE)` (`Prioridad.cpp:287`) | `THREAD_PRIORITY_HIGHEST` + sin EcoQoS (`Prioridad.cpp:289-298`) | `setpriority(-10)` (requiere `CAP_SYS_NICE`/`RLIMIT_NICE`) (`Prioridad.cpp:301-308`) |
| Medios (otros hilos de streaming) | sin cambio | `THREAD_PRIORITY_ABOVE_NORMAL` | `setpriority(-5)` |
| Motor (bucle) | sin cambio | `THREAD_PRIORITY_NORMAL` | sin cambio |
| Proceso | sin App Nap ni temporizadores agrupados (`platform::sinAhorroDeEnergia`) | `ABOVE_NORMAL_PRIORITY_CLASS` (`Prioridad.cpp:324-327`) | — |
| Afinidad «medios juntos» | no se puede elegir núcleos (`Prioridad.cpp:218-219`) | CPU Sets blandos: un CCD (L3) o los núcleos P; el resto del proceso al otro CCD (`Prioridad.cpp:200-216`, `275-276`) | `sched_setaffinity` duro a un grupo L3 o a `cpu_core` (`Prioridad.cpp:220-240`, `277`) |

- Ajustes: `procesador.prioridad` y `procesador.mediosJuntos`, ambos `true` de fábrica (`Prioridad.cpp:321-333`).
- No hay en el código: MMCSS (`AvSetMmThreadCharacteristics`), `SCHED_FIFO`/`SCHED_RR`,
  `THREAD_TIME_CONSTRAINT_POLICY` ni `QThread::TimeCriticalPriority` (grep sobre `src/`).
- Medición citada en el código (M1 Pro, 2026-10-20): con QoS interactiva en todos
  los hilos de streaming ~23,8 mil cambios de contexto/s y CPU 62-84 %; sin ella
  ~18,9 mil y 59 % (`Prioridad.cpp:280-286`).

### 2.5 Nombres de hilos

Solo tres hilos se nombran explícitamente: «principal» (`src/core/diag/Perfil.cpp:511-516`),
«recursos» (`src/core/diag/Recursos.cpp:654-663`) y `ba-documentos` (`QThread`,
`src/app/ui/LectorDocumentos.cpp:67`); y uno de GLib, `bacoremlsr-carga`
(`src/gst-plugins/superres/coremlsr_mac.mm:307`). Los hilos de GStreamer llevan
los nombres que les pone GStreamer.

---

## 3. Comunicación entre procesos

### 3.1 Primitivas

| Primitiva | Implementación | Sincronización | Dónde |
|---|---|---|---|
| `SharedMemoryRegion` | Windows: `CreateFileMappingW`/`MapViewOfFile` en `Local\` con DACL; POSIX: `shm_open` + `mmap(MAP_SHARED)` | — | `src/core/ipc/SharedMemoryRegion.cpp:1-10`, `122-209`, `209-340` |
| `Segmento` | igual que la anterior; en macOS nombres ≤ 31 caracteres; el que abre borra el nombre | — | `src/core/platform/Segmento.hpp:1-21` |
| `HealthBlock` (`BA.Health`) | cabecera + `HealthEntry` por slot (`seq`, `state`, `pid`, `version`, `lastBeatQpc`) | seqlock por entrada, `atomic_ref` | `src/core/ipc/HealthBlock.hpp:10-40`, `76-104` |
| `SpoutSender`/`SpoutReceiver` (video) | región compartida `[cabecera | 3 buffers]`, buffers alineados a 4096 B, triple buffer + seqlock por buffer; `instanceId` para detectar productor nuevo | sin bloqueo para el escritor; el lector reintenta si el escritor dio la vuelta | `src/core/ipc/SpoutTransport.cpp:55-77`, `SpoutTransport.hpp:186-346` |
| `SharedAudioRing` (audio) | SPSC a nivel de **bloque**: 480 cuadros (10 ms a 48 kHz), F32 intercalado, 2 canales, capacidad 16 bloques (160 ms), sello QPC por bloque | `atomic_ref` acquire/release; overrun: el escritor **descarta** el bloque; underrun: el lector recibe silencio con fundido | `src/core/ipc/SharedAudioRing.hpp:1-62`, `130-175` |
| Canal de control | socket local: POSIX `<Runtime>/<nombre>.sock` (o abstracto), Windows `\\.\pipe\BroadcastAppliance.<sesión>.<nombre>`; mensajes «4 bytes LE de largo + JSON» | — | `src/core/platform/LocalSocket_posix.cpp:50-88`, `LocalSocket_win.cpp:50`, `src/core/ipc/ControlProtocol.hpp:1-20` |

El reloj de los sellos (`nowQpc`) es `QueryPerformanceCounter` en Windows y
`steady_clock` en ns en macOS/Linux (`HealthBlock.cpp:108-120`); del lado de
GStreamer se traduce leyendo los dos relojes en el mismo instante
(`src/gst-plugins/ipc/hora.hpp:1-50`).

### 3.2 Canales de memoria compartida

Tamaños calculados a 1920×1080 BGRA (el lienzo de fábrica, `src/app/emision/BroadcastEngine.hpp`, `VideoConfig`): 8 294 400 B por buffer, 24 883 200 B por canal (3 buffers) más cabecera.

| Nombre | Dirección | Contenido | Escritor → lector | Cadencia | Dónde |
|---|---|---|---|---|---|
| `BA.Health` | launcher ↔ A, B | salud (2 slots; 4 si B lo crea solo) | cada proceso su slot | 100-250 ms | `HealthBlock.hpp:56`; `gateway/main.cpp:134-135`, `471` |
| `BA.Program` | A → B | programa de la salida principal, BGRA, tamaño de salida | vista `programa@gateway` (`BroadcastEngine.cpp:194-196`) → `SpoutSender` → `videoBajadaThread` | cada cuadro; se suelta si nadie mira hace 10 s (`ajustes:video.vistaReposoS`) | `BroadcastEngine.cpp:114`, `639-720`; `StudioViewModel.cpp:2751-2759` |
| `BA.Salida.<id>` | A → B | programa de otra salida | igual | bajo demanda | `src/core/ipc/CanalesSalida.hpp:31`; `MediaPump.cpp:416` |
| `BA.Escena.<k>` (k=1..4) | A → B | escenas para pantallas de sala | `BroadcastEngine::enviarEscena` | cada cuadro | `src/core/ipc/CanalesEscena.hpp:17-19`; `BroadcastEngine.cpp:772-845` |
| `BA.Operator` | A → B | lo que ve del operador quien espera en el lobby | `OperatorFeed` | — | `src/app/emision/OperatorFeed.*` |
| `BA.Guest.<n>`, `BA.GuestShare.<n>` | B → A | cámara y pantalla de cada invitado, al tamaño del programa (letterbox) | `MediaPump::forwardVideo` → `bavideoanillo` / `FrameService` | lo que llega | `MediaPump.hpp:10-16`; `MediaPump.cpp:1063-1160` |
| `BA.Lobby.<k>`, `BA.Backstage.<k>` | B → A | previews (backstage a ~10 cps) | ídem | — | `MediaPump.cpp:223` |
| `BA.Remoto.<k>`, `BA.RemotoContenido.<k>` | B → A | integrante remoto con la palabra | ídem | — | `MediaPump.hpp` (Cfg) |
| `BA.Registro.<k>` (k=1..8) | B → A | cámara del tótem a 960×540 | ídem | — | `src/core/ipc/CanalesRegistro.hpp:19-23`; `MediaPump.hpp` (`registroW/H`) |
| `BA.Grilla.<k>` | B → A | grilla de personas compuesta por B | hilo `Grillas` | 25 cps | `src/gateway/Grillas.cpp:150`; `src/app/escenas/FuenteGrilla.cpp:25` |
| `BA.Audio.ContentForReturn` | A → B | audio del programa principal | puente `intersrc ! queue ! baanillosink` | bloques de 10 ms, `sync=true` | `Motor.cpp:237-257`; `src/gst-plugins/ipc/anillosink.cpp:1-20` |
| `BA.Audio.Salida.<id>` | A → B | audio de otra salida | ídem | ídem | `CanalesSalida.hpp:33` |
| `BA.Audio.Return.<n>` | A → B | mix-minus del invitado *n* | bus `retorno<n>` con `baanillosink` | ídem | `src/motor/Mezcla.hpp:51-54`; `Motor.hpp:22-24` |
| `BA.Audio.Operator`, `BA.Audio.Atendedor`, `BA.Audio.RegistroVoz.<k>` | A → B | voz del operador / del asistente | — | ídem | `MediaPump.hpp` (Cfg) |
| `BA.Audio.Guest.<n>`, `.Lobby.<k>`, `.Remoto.<k>`, `.Atencion`, `.Registro.<k>` | B → A | audio de cada participante | `MediaPump::publishAudio` (rebloqueo a 10 ms, remuestreo con libsamplerate si no es 48 kHz) → `baanillo` | 10 ms | `MediaPump.cpp:926-960`, `1180-1215`; `src/gst-plugins/ipc/anillo.cpp` |

Tamaño de un anillo de audio: 16 × 480 × 2 × 4 B = 61 440 B de datos (160 ms)
(`SharedAudioRing.hpp:58-61`). Cupo de lugares: gateway 1-16 invitados
(`src/gateway/Config.hpp:53-58`, `296-298`); en la app `kMaxInvitados = 64`
(`src/core/ipc/LugaresSala.hpp:18`).

### 3.3 Cadencias de sondeo entre procesos

| Lado | Qué | Intervalo | Dónde |
|---|---|---|---|
| B | ¿hay cuadro nuevo en `BA.Program`/`BA.Escena`/`BA.Salida`? | 4 ms (`kMiradaMs`) | `MediaPump.cpp:584-593`, `702` |
| B | retorno de audio: 1 `pop` por vuelta y `sleep_for(10 ms)` | ~10 ms + trabajo | `MediaPump.cpp:516-579` |
| B | audio del operador / voces | 10 ms | `MediaPump.cpp:765`, `806`, `858` |
| B | reapertura de un canal que no está | 500 ms (mix-minus: 1000 ms) | `MediaPump.cpp:480-506` |
| B | placas | 200 ms (5 cps) | `MediaPump.cpp:1220-1240` |
| A | `bavideoanillo` sin cuadro nuevo | 4 ms; 15 ms si lleva >1 s quieto | `src/gst-plugins/ipc/video.cpp:94-97`, `123` |
| A | `baanillo` sin bloque | 4 ms; 10 ms si lleva >1 s quieto | `src/gst-plugins/ipc/anillo.cpp:77-82`, `112` |
| A | `FrameService::poll` (invitados a la interfaz) | 16 ms, hilo de la UI | `StudioViewModel.cpp:2303-2304` |

### 3.4 Sockets y HTTP

| Puerto | Proceso | Escucha en | Uso | Dónde |
|---|---|---|---|---|
| 8785 | B (ba-web) | — | web pública/eventos | `src/gateway/Config.hpp:136` |
| 8786 | B | 127.0.0.1 | escucha propia del túnel de pinggy | `Config.hpp:126` |
| 8787 | B | 127.0.0.1 | señalización HTTP + WebSocket, web de invitados | `Config.hpp:60` |
| 8788 | B | 127.0.0.1 | API de la sala (Bearer; token en `Runtime/gateway-admin.json`) | `Config.hpp:86`; `src/gateway/AdminApi.cpp:1-60` |
| 8789 | B | `bindAddress` | HTTPS de la red local | `Config.hpp:100` |
| 8790 | A | 127.0.0.1 | MCP «Streamable HTTP» (Bearer; cuerpo ≤ 8 MB) | `src/app/ui/SettingsModel.cpp:274`; `src/app/mcp/McpHttp.hpp:1-20` |
| 8791 | A | localhost | redirección del login de Facebook | `src/app/net/AccountGuides.cpp:143` |
| 8792 | B | LAN | control LAN | `Config.hpp:104` |
| 8793 | B | LAN | puerta HTTP | `Config.hpp:106` |
| 9099 | cloudflared | 127.0.0.1 | métricas | `Config.hpp:61` |
| 40000-40099 UDP | B | — | WebRTC (rango máximo 150 puertos) | `Config.hpp:107-108`, `319` |
| 51820 UDP | B | — | WireGuard (si está activo) | `src/gateway/main.cpp:837`, `887` |
| 80/443 | servidor remoto | — | reenvío del túnel WireGuard | `Config.hpp:117-118` |

La API de la sala tiene 50 rutas literales en `AdminApi.cpp` (45 bajo `/api/`; grep),
entre ellas `GET /api/v1/events` (SSE con «room» y «chat»; B manda `: ping` cada
15 s, `ConferenceClient.hpp:9-11`). La app usa un `QNetworkRequest` con
`setTransferTimeout(5000)` para los POST (`src/app/ui/ConferenceClient.cpp:155-189`).

### 3.5 MCP

- Transporte: `POST /mcp` JSON-RPC, `GET /healthz`; solo `127.0.0.1`; Bearer
  obligatorio; token persistente cifrado en `Credenciales/mcp-token.cred`;
  puerto y token publicados en `Runtime/mcp.json` (`McpHttp.hpp:1-20`,
  `McpService.cpp:801-829`).
- `ba-mcp` lee `Runtime/mcp.json` en cada mensaje y reenvía; un hilo por línea
  recibida (`ba_mcp_main.cpp:274`).
- Herramientas: 738 nombres únicos `t.name = "…"` en `src/app` (52 archivos; uno
  repetido, `scene_switch`). `docs/mcp.json` (generado 2026-10-17) dice 697;
  `docs/funciones/MCP.md:30` dice 697.
- Las herramientas se ejecutan en el hilo de la interfaz (`PLAN-MOTOR-SIN-ESPERAS.md:5-8`).

### 3.6 Archivos de encuentro y de estado en `Runtime/`

`Runtime/` = `<datos>/Runtime/`, 0700 donde el sistema de archivos lo permite
(`src/core/platform/Paths.hpp:112-117`). Archivos referidos en el código:
`al-aire.json`, `playlist-aire.json`, `gateway-admin.json`, `mcp.json`,
`estado-ui.json`, `ultima-caida.json`, `caidas.jsonl`, `arranque.json`,
`placa.json`, `descargas.json`, `*.sock`, y carpetas `agente/`, `agentes/`,
`capturas/`, `craft/`, `pdf/`, `tmp/` (grep de `stateDir() / "…"` y `"Runtime" / "…"`).

### 3.7 Copias de memoria del programa hasta el invitado (macOS, GL)

Recorrido de un cuadro del programa principal hasta un invitado WebRTC, según el código:

| # | Etapa | Operación | Dónde |
|---|---|---|---|
| 1 | composición (`glvideomixer`) | textura RGBA en GLMemory → `intersink` (sin copia) | `Composicion.cpp:233-236` |
| 2 | vista del retorno: `intersrc ! glupload ! glcolorconvert ! glcolorscale ! gldownload` | GPU → CPU (RGBA, tamaño de salida) | `src/motor/Piezas.cpp:1157-1170`; `Vistas.cpp:133-135` |
| 3 | `videoconvert` RGBA → BGRA | pasada en CPU | `Piezas.cpp:1168` |
| 4 | `Vistas::alCuadro` | `assign` del buffer a `Cuadro` (bajo `Vistas::mu_`) | `src/motor/Vistas.cpp:50-62` |
| 5 | ídem | `copia = c` para el aviso fuera del candado | `Vistas.cpp:86-90` |
| 6 | `enviarRetorno` | `memcpy` por fila solo si `stride ≠ ancho×4` | `BroadcastEngine.cpp:705-720` |
| 7 | `SpoutSender::send` | `memcpy` al buffer compartido | `src/core/ipc/SpoutTransport.cpp:312` |
| 8 | B `SpoutReceiver::receive` | `memcpy` del buffer compartido | `SpoutTransport.cpp:399`; `MediaPump.cpp:651-652` |
| 9 | `scaleBgra` | `libyuv::ARGBScale` (filtro box) al tamaño de la sesión | `MediaPump.cpp:85-88`, `655` |
| 10 | por **cada sesión**: `AddFrame` | `libyuv::ARGBToI420` | `src/gateway/Webrtc.cpp:238-250`, `953-956` |
| 11 | por cada sesión | codificación (VP8/VP9/H.264/AV1) en libwebrtc | `Webrtc.cpp:1118-1131` |

En Windows (D3D12) la etapa 2 es `d3d12upload ! d3d12convert ! d3d12download`
(`Piezas.cpp:1160-1163`); en Linux (sin GPU de fábrica) es `videoconvert ! videoscale`
(`Piezas.cpp:1159`). Los espectadores de un invitado (relay) no se recodifican:
los bytes codificados se copian del receptor al emisor (`Webrtc.cpp:20-27`).

---

## 4. Camino del video

### 4.1 Estructura general

El motor (`src/motor`) no usa libobs: es un conjunto de **pipelines de GStreamer
separados** unidos por `intersink`/`intersrc` (plugin `rsinter` de gst-plugins-rs)
que comparten el mismo reloj y `base_time` (`src/motor/Reloj.hpp:1-17`). Cada
entrada publica `<id>:video` y `<id>:audio`; cada composición `video:<nombre>`;
cada bus `bus:<nombre>` (`src/motor/Entrada.hpp:1-10`, `Composicion.hpp:1-12`,
`Mezcla.hpp:1-20`). La app (`BroadcastEngine`) traduce los tipos lógicos que
guarda `app.json` (`ffmpeg_source`, `color_source_v3`, `ba_gst_fuente`…) a ajustes
del motor (`src/app/emision/BroadcastEngine.hpp:1-33`, `BroadcastEngineScenes.cpp:560-600`).

```
cámara / red / archivo / pantalla / NDI / invitado / app (gráficos, chat)
        │  pipeline de la ENTRADA  (filtros, normalización, subida a GPU)
        ▼
 intersink <id>:crudo:video ──► pipeline de PLACA (input-selector + placa «sin señal»)
                                         │
                                 intersink <id>:video
                                         │ intersrc (una por capa)
                         ┌───────────────┴──────────────┐
                 COMPOSICIÓN programa[:<salida>]    COMPOSICIÓN preview, escenas, multiview
                 (glvideomixer | d3d12compositor | compositor)
                 [+ DSK: tee → 2.º mezclador]  → intersink video:programa / video:programa:limpio
                         │ intersrc
        ┌────────────────┼─────────────────┬──────────────────────────┐
     VISTAS (BGRA)     GRABACIÓN        TRANSMISIÓN (RTMP/SRT)    REPETICIÓN / ISO
  UI, BA.Program,     splitmuxsink MKV   flvmux/mpegtsmux
  BA.Escena, medición
```

### 4.2 GPU del motor por plataforma

| Plataforma | De fábrica | Mezclador | Memoria | Dónde |
|---|---|---|---|---|
| macOS | `Gl` | `glvideomixer` | `GLMemory` | `src/motor/Piezas.cpp:1008-1023`; `Composicion.cpp:233-236` |
| Windows | `D3D12` | `d3d12compositor` | `D3D12Memory` | ídem |
| Linux | `Ninguna` (CPU) | `compositor` + `videoconvert` | sistema | `Piezas.cpp:1020-1021` |

`BA_MOTOR_GPU=gl|d3d12|cpu` o `BA_MOTOR_CPU=1` lo fuerzan; si la autoprueba falla
(`glListo()`/`d3d12Listo()`) queda en CPU (`Piezas.cpp:1026-1033`). Todos los
pipelines comparten el contexto GL o el dispositivo D3D12 (`darContextoGpu`,
`Piezas.cpp:1069-1090`), así una textura cruza un `inter` sin copia
(`Piezas.hpp`, sección «El video en la GPU»). Los convertidores D3D12 se crean de a
uno bajo un candado de todo el proceso (`serializarConvertidoresD3D12`,
`src/motor/Reloj.hpp:49-55`). Mediciones de CPU/GPU por rama: `docs/desarrollo/MAPA-GPU-CPU.md`.

### 4.3 Lienzo y formato

- Lienzo de fábrica: 1920×1080, 30/1 cps, BT.709, rango limitado, 8 bits
  (`src/motor/Piezas.hpp`, struct `Lienzo`); `VideoConfig` de la app: 1920×1080,
  30/1, `nv12`, `709` (`src/app/emision/BroadcastEngine.hpp`, struct `VideoConfig`).
- Lo que sale de una composición en GPU: textura RGBA del tamaño del lienzo con
  `framerate` del lienzo; en CPU: caps de color del lienzo (`Composicion.cpp:233-236`).
- Lo que entra al codificador: NV12 con la colorimetría del lienzo; en D3D12, si el
  codificador acepta `D3D12Memory`, sin bajar; en GL se sube/convierte y se baja a
  sistema; en CPU `videoconvert` (`Piezas.cpp:1145-1155`).

### 4.4 Entradas

| Tipo | Origen (elemento) | Detalle | Dónde |
|---|---|---|---|
| Cámara USB/integrada | macOS `avfvideosrc device-index`; Windows `mfvideosrc device-path`; Linux `v4l2src device` | elegida por id estable con `GstDeviceMonitor` | `src/motor/Piezas.cpp:383-455` |
| Cámara de red (RTSP, SRT, RTMP, HTTP) | `uridecodebin3` en un **origen compartido por URI** → `queue max-size-buffers=4` → subida a GPU → `intersink origen:<n>:video` | `latency` = máx(latencia de la entrada, 20 ms); `ntp-sync` (de fábrica sí) y `buffer-mode` synced/slave | `src/motor/OrigenRed.cpp:85-130`; `Entrada.cpp:935-941` |
| Archivo / medio | `uridecodebin3` + `identity sync=true` (marca el ritmo) y corrección de offset del pad | sin placa | `Entrada.cpp:942-960`, `924`; `Reloj.hpp:9-12` |
| Pantalla | Windows `d3d12screencapturesrc` (o d3d11); Mac `avfvideosrc capture-screen`; Linux `ximagesrc` / `pipewiresrc` | | `src/motor/Capturas.hpp:1-22` |
| Ventana | Windows `d3d12screencapturesrc window-handle`; Mac `bacapturaventana` (ScreenCaptureKit); Linux `ximagesrc xid` | reintenta cada 2 s si la ventana no está | `Capturas.hpp:14-21` |
| NDI | `bandisrc` (libNoIDea, LGPL, ingeniería inversa) → `avdec_speedhq` | un hilo de recepción por fuente | `src/app/emision/NdiSource.hpp:1-17`; `src/app/emision/InputSpecs.cpp:162-163` |
| Invitado / remoto / grilla | `bavideoanillo canal=BA.Guest.<n>` (BGRA, cps variable, latencia declarada 1/30 s, máx. 1/5 s) | hora = sello QPC del gateway llevado al reloj del pipeline | `src/gst-plugins/ipc/video.cpp:1-22`, `185`; `InputSpecs.cpp:124` |
| `tipo=app` (gráficos, chat, subtítulos, QR, PDF) | `appsrc` alimentado por el buzón del motor + `imagefreeze` | | `Entrada.hpp:28-36`; `Motor.cpp:986-1065` |
| `tipo=color/imagen/texto` | estáticas, terminan en `imagefreeze` | | `Entrada.hpp:28-34`; `Entrada.cpp:962-965` |

Cadena de video de una entrada en vivo (resumen de `Entrada.cpp:850-935`):

- En GPU: `[capssetter colorimetría] ! subida (glupload/d3d12upload) ! textura RGBA ! [desentrelazado GL/D3D12] ! [queue leaky=downstream max-size-buffers=1 ! filtros] ! [videorate a los cps del lienzo] ! intersink sync=false`.
- En CPU: `videoconvertscale ! … ! queue leaky=downstream max-size-buffers=1 ! filtros ! videoconvert ! [desentrelazado] ! videorate ! videoconvertscale ! caps del lienzo ! intersink`.
- Con nadie que la use, una sonda en `ba_origen` deja pasar 1 cuadro cada 500 ms
  (2 cps) (`Entrada.cpp:1040-1058`).
- Filtros propios (plugin `gstbafiltros`): color, croma, recorte, legal, PTZ
  digital, mejora, GL, salida, vista 360, sombra (`src/gst-plugins/CMakeLists.txt:42`);
  superresolución (`gstbasuperres`: FSR, GLSL, ONNX Runtime con CPU/DirectML,
  CoreML y VideoToolbox en Mac) (`src/gst-plugins/superres/onnxsr.cpp:11-24`);
  rostros (`gstbarostros`).

### 4.5 La placa «sin señal» (respaldo por entrada)

Pipeline aparte que no se rearma (`Entrada.cpp:296-330`):
`input-selector sync-streams=false cache-buffers=false ! intersink <id>:video`, con
`sink_0 = intersrc <id>:crudo:video max-time=2500 ms` (`ajustes:entradas.congelarMs`)
y `sink_1 = appsrc placa 640×360 BGRA ! subida ! imagefreeze is-live=true ! 1 cps`.

| Parámetro | Valor | Dónde |
|---|---|---|
| Sin cuadros → placa | 3 s (200 ms-60 s) | `Entrada.cpp:28`, `1467`, `1535` |
| Sin datos ni error → rearmar | 10 s (3-600 s) | `Entrada.cpp:29`, `1447`, `1468` |
| Reintentos de armado | 0,2 / 0,5 / 1 / 2 s, luego el doble hasta 2 s (200 ms-120 s) | `Entrada.cpp:30`, `756-760`, `1469` |
| Placa de entrada sin uso → se desarma | 120 s (5-3600 s) | `Entrada.cpp:1462-1465`, `1490-1505` |
| Hora de cámara RTSP corrida > 400 ms → se apaga `ntp-sync` | `ajustes:entradas.avisoHoraMs` | `Entrada.cpp:1424-1427` |

### 4.6 Composición (PROGRAMA, PREVIEW, escenas, multiview)

- Un pipeline por composición. Por capa: `intersrc max-buffers=3 max-time=0`
  (la cola del `intersrc` descarta lo viejo; sin `queue`) → recorte en GPU/CPU →
  [sombra] → pad del mezclador (`Composicion.cpp:540-640`).
- Mezclador: `force-live=true start-time-selection=set start-time=<grilla>`, con la
  grilla alineada a múltiplos de la duración de un cuadro desde el cero del estudio
  y 100 ms adelante (`Composicion.cpp:215-230`).
- `min-upstream-latency` del mezclador = latencia del estudio + 5 ms
  (`ajustes:video.margenComposicionMs`) (`Composicion.cpp:395-412`).
- Plazo de llegada de cada capa = latencia del mezclador + 1 cuadro (`Composicion.cpp:400-404`, `603-608`).
- DSK: `tee` → `queue` → `intersink video:programa:limpio` y `queue` → 2.º
  mezclador con las capas DSK → `intersink video:programa` (`Composicion.cpp:280-285`).
- Transiciones: corte, mezcla, empuje, cortina; el corte saca la escena vieja
  cuando la nueva dio su primer cuadro, con espera máxima 700 ms
  (`ajustes:video.esperaCorteMs`) (`Composicion.hpp:14-24`, `Composicion.cpp:964`).
- Multiview: 1280×720 de fábrica, PREVIEW y PROGRAMA arriba, hasta 8 entradas
  abajo con tally (`src/motor/Motor.hpp:216-221`).
- «Otro mezclador» (cualquier elemento de varias entradas de GStreamer) configurable
  por el grafo de video (`Composicion.hpp:38-46`, `Composicion.cpp:244-276`).

### 4.7 Vistas (imágenes para la interfaz y para el gateway)

`intersrc max-buffers=2 max-time=0 ! (convertir y escalar a BGRA) ! appsink
max-buffers=1 drop=true sync=false`, latencia del pipeline = la del productor +
40 ms (`src/motor/Vistas.cpp:23`, `127-176`). La conversión por plataforma está en
`aSistemaEscalado` (`Piezas.cpp:1157-1170`). Claves en uso: `programa@ui` (la
interfaz, `BroadcastEngine.cpp:867`), `programa@gateway` (`BA.Program`),
`programa@alarmas` 160×90 (brillo/quietud/sincronía, `BroadcastEngine.cpp:461-470`),
escenas `BA.Escena.<k>` (`BroadcastEngine.cpp:785-795`).

### 4.8 Del programa a los invitados, pantallas y espectadores

1. Vista `programa@gateway` al tamaño de salida → `SpoutSender BA.Program` (sección 3.7).
2. B: `videoBajadaThread` mira cada 4 ms, recibe, escala a la sesión y empuja a cada
   sesión que la ruta indica (invitados, pantallas, consola) (`MediaPump.cpp:599-702`).
3. Cada `PeerConnection` convierte BGRA→I420 y codifica (`Webrtc.cpp:238-250`).
   Codificadores: macOS VideoToolbox H.264 + VP8/VP9/AV1; Windows/Linux libvpx
   VP8/VP9, OpenH264, libaom AV1 (`Webrtc.cpp:1118-1131`). Tras negociar se pide
   `scalability_mode = L1T3` y `degradation_preference = BALANCED` (`Webrtc.cpp:861-882`).
4. B mide y registra cada 10 s cps, ms por cuadro y la edad A→B (`MediaPump.cpp:663-700`).
5. La vista del retorno se suelta cuando ninguna sesión mira el programa hace 10 s
   (`StudioViewModel.cpp:2751-2759`; `ajustes:video.vistaReposoS`).

### 4.9 Subida de invitados (B → A)

Navegador → libwebrtc (decodifica) → `I420ToARGB` (`Webrtc.cpp:298-306`) →
`forwardVideo` ajusta al tamaño del canal con barras (`MediaPump.cpp:1063-1160`) →
`SpoutSender BA.Guest.<n>` → A: `bavideoanillo` (mira cada 4 ms) → pipeline de la
entrada (subida a GPU, normalización) → placa → composición. En la interfaz, el
invitado también se lee directo del canal en el hilo de la UI (`FrameService`,
`src/app/ui/FrameService.hpp:8-12`).

### 4.10 Codificación, grabación y transmisión

Elección del codificador H.264 (`src/motor/Salidas.cpp:40-70`):

| Plataforma | Lista (en orden) | Respaldo |
|---|---|---|
| macOS | `vtenc_h264_hw`, `vtenc_h264` | `x264enc` |
| Windows | `nvh264enc`, `qsvh264enc`, `d3d12h264enc`, `amfh264enc`, `mfh264enc` (con D3D12, primero los que aceptan `D3D12Memory`) | `x264enc` |
| Linux | `nvh264enc`, `qsvh264enc`, `vah264enc`, `vah264lpenc` | `x264enc` |

Propiedades que se ponen si existen: `bitrate` (kbps), intervalo de clave
(`max-keyframe-interval` / `key-int-max` / `gop-size` = `gopSeg × fps`),
`realtime=TRUE`, `allow-frame-reordering=FALSE`, `zerolatency`; x264:
`tune=zerolatency speed-preset=veryfast` (`Salidas.cpp:72-98`). Valores de fábrica:
6000 kbps video, 160 kbps audio, clave cada 2 s (`BroadcastEngine.hpp`, struct `EncoderConfig`).

| Salida | Cadena | Colas | Latencia del pipeline | Dónde |
|---|---|---|---|---|
| Grabación | `intersrc video ! valve ! queue leaky=downstream max-size-time=3 s ! a NV12 ! codificador ! h264parse ! queue colchón ! splitmuxsink (matroskamux)`; cada pista: `intersrc bus ! valve ! queue leaky 3 s ! audioconvert ! avenc_aac ! aacparse ! queue colchón` | cola cruda 3 s (`ajustes:record.colaEncoderS`); colchón comprimido `colchonSeg` (app: 60 s, mín. 5; motor: 30 s) y 512 MB (`ajustes:record.colchonMB`) | máx(lat. video, lat. audio) + 300 ms (`ajustes:salidas.margenMs`) | `Salidas.cpp:330-456`; `BroadcastEngine.cpp:1641-1703` |
| Copia de la grabación | **segundo codificador** con las mismas fuentes, en otra carpeta | ídem | ídem | `BroadcastEngine.cpp:1664-1676` |
| Transmisión RTMP | `flvmux streamable=true ! rtmp2sink async-connect=true` | video: `queue leaky=downstream max-size-time=1000 ms` (`ajustes:stream.colaMs`); audio: `queue max-size-time=1000 ms` (sin leaky) | ídem | `Salidas.cpp:596-675` |
| Transmisión SRT | `mpegtsmux alignment=7 ! srtsink wait-for-connection=false` | ídem | ídem | `Salidas.cpp:607-612` |
| Repetición | grabación continua en tramos de 2 s, guarda los últimos N; «juntar» hasta 120 s | | | `Salidas.hpp` (`Repeticion`); `Salidas.cpp:230`, `285` |
| ISO (por entrada) | una grabación por entrada, tope 8 | | | `BroadcastEngine.hpp:734` |

- Tramos: `ajustes:record.tramoMin` = 0 → un solo archivo MKV por grabación
  (`BroadcastEngine.cpp:1647`). MP4/MOV se graban en MKV y se remuxan al terminar
  (`BroadcastEngine.cpp:1619-1621`; `Salidas.cpp:100-125`).
- `h264parse config-interval=-1` en transmisión (`Salidas.cpp:640`).
- `EncoderConfig::rateControl` («CBR») y `RecordConfig::calidadCrf` no se leen en
  el motor (grep: solo `EngineSelfTest.cpp:162`).

### 4.11 Gráficos

`ba-grafico` dibuja la animación entera de antemano (tira de cuadros en memoria
compartida, BGRA premultiplicado) y la app la reproduce empujando cuadros a la
entrada `tipo=app`; el último cuadro queda (`src/app/graficos/GraficoSource.hpp:1-18`,
`src/grafico/main.cpp:15-25`). Un zócalo de 900×220 a 30 cps son ~24 MB por segundo
de animación (`GraficoSource.hpp:10-13`).

---

## 5. Camino del audio

### 5.1 Formato interno

F32LE intercalado, 48 kHz, 2 canales en todo el motor (`src/motor/Entrada.cpp:188`;
`Mezcla.cpp:84-85`), en los anillos (`SharedAudioRing.hpp:58-60`) y en los buses
de la Matriz (`src/app/audio/RingEstereo.hpp:18`, planar). WebRTC: int16, 48 kHz,
2 canales, bloques de 10 ms (`src/gateway/Webrtc.cpp:166-168`).

### 5.2 Captura

| Fuente | Camino | Dónde |
|---|---|---|
| Micrófono / línea (entrada de la app) | `osxaudiosrc unique-id` (Mac) / `wasapi2src device … low-latency=true` (Windows) / `pulsesrc device` (Linux) `! queue ! [barack] ! filtros ! audioconvert ! audioresample ! F32 48k 2ch ! intersink <id>:audio sync=false` | `src/app/audio/EntradasAudio.cpp:18-64`; `src/motor/Entrada.cpp:910-915`, `962-969` |
| Canales físicos de una placa (Matriz) | `QAudioSource` (Qt Multimedia, 48 kHz float) → cola de `bavoz` → entrada del motor `ba_entrada_fisica`; se encola 20 ms adelante | `src/app/audio/EquipoAudio.cpp:20-30`, `255-283`; `EquipoAudio.hpp:12-18` |
| Invitado / remoto / lobby | libwebrtc (NetEq, tirado por `PullAdm` cada 10 ms) → int16→float → remuestreo a 48 kHz si hace falta (libsamplerate `SincMedium`) → rebloqueo 10 ms → `BA.Audio.Guest.<n>` → `baanillo` (latencia declarada 10 ms) | `Webrtc.cpp:171-236`; `MediaPump.cpp:1180-1215`; `src/gst-plugins/ipc/anillo.cpp:1-20`, `164` |
| Cámara de red / archivo | `uridecodebin3` → `audioconvert ! audioresample ! F32` | `OrigenRed.cpp:100-104`; `Entrada.cpp:939-960` |
| NDI | pad `audio` de `bandisrc` | `InputSpecs.cpp` (ajustes NDI) |
| Voces sintetizadas (TTS, teleprompter) | `bavoz voz=<nombre>`: cola con horas en el reloj de la app pasadas al del pipeline | `src/app/audio/GstVoz.hpp:1-11` |

- Los tamaños de buffer del dispositivo no se fijan en el código (no hay
  `buffer-time`/`latency-time` en `src/`): rigen los de fábrica de GStreamer.
- Hora de captura: en cada `GstAudioBaseSrc` de una entrada, una sonda pone el PTS
  contando muestras desde un ancla y se reancla si se aparta más de 50 ms del reloj
  del pipeline (`src/motor/Piezas.cpp:711-790`; aplicado en `Entrada.cpp:1103`).
  Los elementos con reloj propio se configuran `provide-clock=FALSE`
  (`Piezas.cpp:697-708`; `Entrada.cpp:1100`).

### 5.3 Cadena por entrada (rack)

- `barack fuente=<nombre>` va primero en la cadena de audio, precedido de
  `audioconvert ! audioresample ! F32 48k 2ch` (`Entrada.cpp:910-915`).
- `barack` le pasa el audio al anfitrión de la fuente (el `AudioRack` de
  `RackManager`) en bloques **planares de hasta 1024 muestras**; si el registro
  está ocupado prueba en el bloque siguiente (sin bloquear); sin anfitrión, pasa tal
  cual (`src/app/audio/GstRack.hpp:1-18`, `GstRack.cpp:85-140`).
- El rack es un grafo (CLAP/LSP, analizadores, generadores, salida a dispositivo,
  texto↔voz); la edición arma un PLAN nuevo que se intercambia sin bloquear el
  audio (`src/app/audio/AudioRack.hpp:1-40`).
- Vigía de plugins lentos: si un módulo tarda más del 50 % del bloque durante 20
  bloques seguidos, se puentea (`AudioRack.cpp:979`, `1156-1170`).
- `barack` no contesta la consulta de latencia (`docs/planes/PLAN-TIEMPOS.md:27`).
- Biblioteca DSP de LSP en C: `src/lspdsp/ba_lspdsp.{h,cpp}` (823 líneas).
- Automixer de mesa (`baautomixer`, Dugan, por grupo; sin latencia agregada)
  (`src/gst-plugins/audio/automixer.cpp:1-18`).

### 5.4 Buses (mezcla)

Un pipeline por bus (`src/motor/Mezcla.cpp:73-109`):

```
intersrc <id>:audio (max-time 1500 ms, max-buffers 0) ! volume (fader, mute) ! level ─┐  (una rama por entrada)
                                                                                      ├─► audiomixer force-live=true
                                                                                      │   start-time-selection=first
                                                                                      │   output-buffer-duration=10 ms
  ! F32 48k 2ch ! volume master ! [balimitador] ! [ebur128level interval=100 ms] ! level interval=50 ms ! intersink bus:<nombre>  (o baanillosink)
```

| Bus | Qué | Dónde |
|---|---|---|
| `programa[:<salida>]` | pista 1 de cada salida; limitador + loudness | `Motor.hpp:8-13`, `35-49` |
| `pista2`…`pista6` | grabación multipista; se arman al usarse; siguen a la salida principal | `Motor.hpp:10-11`, `50-53` |
| `retorno<n>` | mix-minus del invitado *n* → `BA.Audio.Return.<n>`; sin medidores | `Motor.hpp:12`, `22-24`; `Mezcla.hpp:51-54` |
| `directo` | la sala sin demora: latencia fija 40 ms (lo que llega más tarde no entra) | `Motor.cpp:26`; `Mezcla.hpp:55-57` |
| `seleccion` | escucha «selección» | `Motor.hpp:354` |

- `min-upstream-latency` = latencia del estudio + 30 ms (`ajustes:audio.margenMezclaMs`);
  sube enseguida y baja solo si la diferencia supera 60 ms (`Mezcla.cpp:133-147`, `Mezcla.hpp:142`).
- La ganancia de escena (entrada de escena suena solo si su escena está al aire en
  esa salida) va en el pad del mezclador con fundido (`Mezcla.hpp:22-24`).
- Medidores por rama: `level` cada 50 ms, retención de pico 300 ms
  (`ajustes:audio.medidorMs`, `audio.picoMs`) (`Mezcla.cpp:169-171`).
- Niveles para la interfaz: el bucle del motor publica cada ~40 ms sin candado
  (`Motor.hpp:250-260`, `Motor.cpp:397-400`).

### 5.5 Retornos, IFB y lo que oyen los invitados

- Modo por invitado: `programa` (oye `BA.Audio.ContentForReturn`), `mix_minus`
  (oye `BA.Audio.Return.<n>`) o `apagado` (`Motor.hpp:181-183`). Guardado en
  `Config/retornos.json` (`OperateMode.cpp:651-652`).
- En B, un solo hilo lee el programa y los mix-minus **un bloque por vuelta**; si el
  bloque del mix-minus es silencio de relleno se usa el programa entero
  (`MediaPump.cpp:514-531`).
- IFB del operador: la voz se mezcla sobre el programa con el programa a ×0,4
  (~−8 dB) (`MediaPump.cpp:533-556`).
- Prueba de eco: tono de 1 kHz a −12 dBFS con código seudoaleatorio de tramos de 250 ms;
  detección con Goertzel en ventanas de 20 ms (`MediaPump.cpp:557-575`, `926-946`).

### 5.6 Loudness y limitador

- `balimitador`: true peak, lookahead 5 ms (declarado en la consulta LATENCY),
  sobremuestreo ×4, techo −1 dBTP, liberación 100 ms; apagado sigue demorando lo
  mismo (`src/gst-plugins/audio/limitador.cpp:1-16`, `73-91`).
- `ebur128level` en el programa, después del limitador, cada 100 ms (`Mezcla.cpp:81-82`).
- Objetivo de loudness en `Config/loudness.json` (`OperateMode.cpp:630`).

### 5.7 Salidas de audio

| Salida | Camino | Latencia configurada | Dónde |
|---|---|---|---|
| Escucha del operador (auriculares) | `intersrc bus ! queue max-size-time=2 s ! audioconvert ! audioresample ! volume ! level ! osxaudiosink / wasapi2sink / pulsesink`, sin reloj propio | latencia del bus oído + 40 ms | `src/motor/Escucha.cpp:20-32`, `66-95`; `Motor.cpp:420-432` |
| Al gateway (programa, otras salidas) | `intersrc bus ! queue max-size-time=2 s ! baanillosink sync=true` | latencia de salida del bus + 30 ms | `Motor.cpp:237-257`, `406-415` |
| Mix-minus | el bus `retorno<n>` termina en `baanillosink` | — | `Mezcla.cpp:86-88` |
| Codificador (grabación/transmisión) | `avenc_aac`: 160 kbps de fábrica (grabación con calidad «archivo»: 256) | ver 4.10 | `Salidas.cpp:351-356`, `619-622` |
| Toma del programa (loudness de la app, picos, sincronía) | `intersrc ! queue 2 s ! appsink sync=true` | bus + 30 ms | `Motor.cpp:1215-1236` |
| Toma por entrada (Matriz) | `intersrc ! audioconvert ! audioresample ! appsink sync=false max-buffers=50 drop=true` | — | `Motor.cpp:1290-1305` |
| Buses virtuales de la Matriz → dispositivo | hilo cada 10 ms → `Ring` (colchón 2 bloques = 20 ms; tope 8 bloques = 80 ms) → `QAudioSink` (buffer = colchón × canales × 4 B × 2) | ~20-30 ms extra (comentario) | `src/app/audio/Matriz.cpp:809-873`, `655-661`; `RingEstereo.hpp:18-20`; `Matriz.hpp:19-24` |
| Salidas físicas (EquipoAudio) | `QAudioSink` con `AlDispositivoMulti` | — | `EquipoAudio.cpp:186-218` |
| Retorno WebRTC | `pushLocalAudio` float→int16 a cada sesión | — | `MediaPump.cpp:576`, `740`; `Webrtc.cpp:19-20` |

### 5.8 Dominios de reloj y remuestreos

| Dominio | Quién lo marca | Cómo se alinea con el estudio | Dónde |
|---|---|---|---|
| Pipelines del motor | reloj del estudio (sistema monotónico, NTP, PTP o red) y `base_time` común | — | `src/motor/Reloj.cpp:325-370` |
| Placa de audio de captura | el dispositivo | PTS por muestras + reancla a 50 ms; `audioresample` presente en la cadena; no se usa `slave-method` (comentario: «ni slave-method ni do-timestamp lo arreglan») | `Piezas.cpp:711-790` |
| Placa de audio de escucha | el dispositivo | `provide-clock=FALSE`; el sink sigue al reloj del estudio | `Escucha.cpp:91` |
| Qt Multimedia (Matriz, EquipoAudio) | el dispositivo vía Qt | anillo con colchón 20 ms y recorte al tope 80 ms (`RingEstereo.hpp:28-35`); el hilo de mezcla corre con `steady_clock` cada 10 ms | `Matriz.cpp:809-830` |
| Gateway (WebRTC) | `PullAdm` cada 10 ms con `steady_clock`; NetEq | sello QPC por bloque → `hora.hpp` | `Webrtc.cpp:220-235`; `gst-plugins/ipc/hora.hpp` |
| Invitado con otra frecuencia | navegador | libsamplerate `SincMedium` en B | `MediaPump.cpp:1185-1203` |
| Archivos | su propio ritmo (`identity sync=true`) | offset del pad = base de la cadena − base del estudio | `Reloj.hpp:9-12` |

### 5.9 Latencias configuradas (no medidas)

| Tramo | Valor | Dónde |
|---|---|---|
| Lo que pide cada entrada | máx(declarada + 15 ms, p90 de edad + (p98−p90 acotado a 3-40 ms) + 5 ms), con piso `latencia` de la entrada; mide sobre ~10 s y ≥ 30 muestras | `Entrada.cpp:1247-1290`, `1361-1384` |
| Latencia del estudio | la mayor de las que no pasan el tope; tope automático 250 ms; sube si se sostiene 2 s; baja con permiso tras 10 s (más de 20 ms por debajo); los primeros 8 s sigue al objetivo | `Reloj.hpp:85-137`; `Reloj.cpp:445-490` |
| Entrada que pasa el tope («llega tarde») | se corre su hora en múltiplos de 10 ms hasta 300 ms (`ajustes:entradas.retrasoPropioMaxMs`) con histéresis de 20 ms / 3 s | `Entrada.cpp:1385-1400` |
| Composición | estudio + 5 ms | `Composicion.cpp:395` |
| Bus de audio | estudio + 30 ms (+ lo que declaran limitador 5 ms y bloque de salida 10 ms) | `Mezcla.cpp:133-147` |
| Bus «directo» | 40 ms fijos | `Motor.cpp:26` |
| Escucha | bus + 40 ms | `Escucha.cpp:32`, `93` |
| Vista (UI, `BA.Program`) | productor + 40 ms | `Vistas.cpp:23`, `173` |
| Puente al gateway / toma | bus + 30 ms | `Motor.cpp:255`, `1235` |
| Grabación / transmisión | máx(video, audio) + 300 ms | `Salidas.cpp:56`, `454`, `674` |
| Anillo de audio A↔B | hasta 160 ms (16 bloques) | `SharedAudioRing.hpp:61` |
| `bavideoanillo` / `baanillo` | 33 ms / 10 ms declarados | `video.cpp:185`; `anillo.cpp:164` |
| Cámara de red (`rtspsrc latency`) | máx(latencia de la entrada, 20 ms); `ffmpeg_source` de red migrado → 200 ms (50 con `low_latency`) | `OrigenRed.cpp:120`; `BroadcastEngineScenes.cpp:581-582` |

### 5.10 Tests de latencia y sincronía

| Test | Qué verifica | Dónde |
|---|---|---|
| `test_latencia_estudio` | la latencia del estudio no sigue picos; sube sostenida 2 s; baja solo con permiso o a pedido; lo que pasa el tope no arrastra | `src/motor/tests/test_latencia_estudio.cpp:1-61` |
| `test_retraso_propio` | una entrada con demora propia a 2 cps no hace crecer su retraso ni pasa el tope | `src/motor/tests/test_retraso_propio.cpp:1-8` |
| `test_ring_drift` | contabilidad del anillo sin deriva a través de muchas vueltas (en lockstep, no concurrente) | `src/core/tests/test_ring_drift.cpp:1-30` |
| `test_sincronia`, `test_sincro_labios` | huellas de luma entre entradas; aplauso en el programa | `src/app/tests/` (`video/Sincronia.cpp`, `camaras/SincroLabios.cpp`) |
| `test_motor_sin_esperas` | la interfaz no toma el candado del motor y ninguna llamada pasa 50 ms (con una cámara de red que no contesta) | `docs/planes/PLAN-MOTOR-SIN-ESPERAS.md` (fila S5); `src/motor/tests/test_motor_sin_esperas.cpp` |
| `test_shared_audio_ring`, `test_spout_transport`, `test_ipc_cross_process`, `test_video_cross_process` | transportes A↔B | `src/core/tests/` |

---

## 6. Relojes y sincronía

- **Reloj del estudio** (`src/motor/Reloj.cpp:325-370`; configuración en
  `Config/video-reloj.json`, `OperateMode.cpp:592-600`):

  | Tipo | Implementación | Parámetros |
  |---|---|---|
  | `sistema` (de fábrica) | `gst_system_clock_obtain()` (monotónico) | — |
  | `ntp` | `gst_ntp_clock_new` | servidor, puerto (123) |
  | `red` | `gst_net_client_clock_new` | servidor, puerto (8554) |
  | `ptp` | `gst_ptp_init` + `gst_ptp_clock_new(dominio)`; requiere `gst-ptp-helper` con permisos en 319/320 | dominio |

  Espera de sincronía de un reloj de red: 3 s; si no, sistema (`Reloj.hpp:69`, `Reloj.cpp:355-367`).
- **Publicar el reloj**: `GstNetTimeProvider` en un puerto (`Motor.hpp:272-274`; `publicar_puerto` en `video-reloj.json`).
- **Base común**: todos los pipelines adoptan reloj y `base_time` del estudio (`Reloj::adoptar`).
- **Entradas de red**: `rtspsrc ntp-sync=true buffer-mode=synced` (hora de captura
  del emisor) o `slave`; se apaga sola si la hora está corrida > 400 ms
  (`OrigenRed.cpp:115-128`, `Entrada.cpp:996-1003`, `1424-1427`). Una fuente con
  horas propias corridas más de 1 s se realinea con offset (`Entrada.cpp:1320-1358`;
  `ajustes:entradas.realineoEntreS` 10, `realineoQuietoS` 60).
- **Invitados**: video y audio llegan por canales distintos, sellados con el QPC del
  gateway al publicar, y se traducen al reloj del pipeline leyendo ambos relojes
  juntos; sellos de > 5 s o del futuro se reemplazan por «ahora» (`hora.hpp:30-50`).
  `baanillo` cuenta por muestras y vuelve al sello si se corre > 40 ms (`anillo.cpp:13-18`).
- **A/V en las salidas**: la grilla de cuadros de los mezcladores se alinea a
  múltiplos de un cuadro desde el cero del estudio (el comentario documenta que con
  `first` el video quedaba un cuadro atrás del audio) (`Composicion.cpp:217-230`).
- **Desfases manuales** por entrada: `desfase` (audio y video), `desfase_audio`,
  `desfase_video` en ms (`Entrada.hpp:18-21`).
- **Medición**: aplauso/huellas de luma (`SincroLabios`, `Sincronia`); «edad» de
  cada cuadro y de lo que sale (`MedidorEdad`, `src/motor/Edad.hpp`); análisis por
  elemento con sondas (`src/motor/Sondas.hpp`; `Motor::analizarEmpezar/Terminar`).

---

## 7. Disponibilidad

### 7.1 Mecanismos por capa

| Capa | Mecanismo | Parámetros | Dónde |
|---|---|---|---|
| Procesos A/B | watchdog por latido + salida; backoff; cortacircuito → modo seguro; readopción | sección 1.3 | `src/launcher/Supervisor.cpp` |
| Latido de A | dos fuentes: UI (100 ms) y cuadros del motor (250 ms) | sección 1.4 | `LatidoAire.cpp`, `OperateMode.cpp:766-786` |
| Entrada | placa «sin señal» (`input-selector`), rearmado por falta de datos, reintentos crecientes, espera de dispositivo | sección 4.5 | `src/motor/Entrada.cpp` |
| Escena de respaldo | por entrada: avisar, ARMAR o CORTAR a una escena tras `ms` sin señal; una vez por caída | `Config/respaldo-entradas.json` | `src/app/escenas/RespaldoEntradas.hpp:1-8` |
| Cámara de red | un origen por URI compartido; se reintenta solo; se va unos segundos después de que lo suelta la última entrada | — | `src/motor/OrigenRed.hpp:1-14` |
| Transmisión | reconexión 1, 2, 4, 8 s y después cada 10 s (`Configuración → Transmisión`, 1-300 s); «conectada» si hubo bytes en los últimos 2 s; URL mal escrita no se reintenta | `ajustes:stream.sinDatosS` | `src/motor/Salidas.cpp:23-33`, `736-780` |
| Destinos extra (multistreaming) | cada uno es una `Transmision` aparte | — | `BroadcastEngine.cpp:1867-1879` |
| Grabación | MKV (se puede abrir lo escrito tras un corte); colchón comprimido 60 s / 512 MB contra disco trabado; **copia** simultánea en otra carpeta con un segundo codificador | sección 4.10 | `Salidas.cpp:330-456`; `BroadcastEngine.cpp:1641-1703` |
| Grabación caída | `Grabacion::tic` registra el error del bus y no rearranca (`Salidas.cpp:527-542`); alarma `record_down` (`forSec` 5) | — | `src/app/agent/AlertEngine.hpp:36` |
| Disco en apuros | si lo grabado espera > 3 s (0,5-60 s) o el registro no puede escribir 3 s: aviso y, con `priorizar_programa=auto`, pausa ISO, repetición y registro; vuelve tras 30 s bien (< 0,5 s) | `Config/disco.json` | `BroadcastEngine.cpp:1915-1922`, `2010-2060`; `414-418` |
| Codificador caído | `atenderEncoderCaido` está vacía; las salidas se atienden en su `tic` (cada 100 ms) | — | `BroadcastEngine.cpp:1863-1865` |
| FTB y placa de seguridad | por salida; se guardan en `Runtime/placa.json` y se restauran al volver | — | `src/app/emision/SeguridadAire.hpp:1-13`, `SeguridadAire.cpp:30` |
| Retoma tras corte | `Runtime/al-aire.json` | sección 1.6 | `OperateMode.cpp:712-1009` |
| Gateway → invitados | placa a 5 cps si A no publica; reapertura de canales cada 500 ms | — | `MediaPump.cpp:1220-1240`, `480-506` |
| A ↔ B (canales) | `instanceId`/generación del productor: el lector detecta reinicio y se resuscribe; anillo: descarta lo bufferizado y hace fundido de 10 ms | — | `SpoutTransport.hpp:322-334`; `SharedAudioRing.hpp:27-30` |
| Túneles y ba-web | relanzado con backoff | sección 1.5 | `Cloudflared.cpp`, `Pinggy.cpp`, `BaWeb.cpp` |
| Salida estándar | tubería en memoria de 1 MB + cola acotada; si el disco se traba se descarta y se cuenta | — | `src/core/diag/SalidaAsincrona.hpp:1-25`, `SalidaAsincrona.cpp:162`, `206` |
| Integridad del bundle | rápida antes de arrancar, completa en el fondo; marca de fallo para la próxima | — | `Supervisor.cpp:194-241` |
| Energía | mantener despierta la máquina y la pantalla (de fábrica sí) | `ajustes:energia.*` | `src/launcher/main.cpp:186-188`; `src/core/platform/Energia_*.cpp` |

### 7.2 Alarmas (AlertEngine)

Se evalúan en el hilo de la interfaz cada 250 ms; reglas en `Config/alertas.json`
(`src/app/agent/AlertEngine.hpp:1-45`). Tipos: `audio_peak`, `audio_silence`,
`media_end`, `speaker_time`, `lobby`, `hand`, `superres_tarde`, `guest_drop`,
`chat`, `guest_message`, `texto`, `stream_health` (pérdida 2 % en 30 s),
`disk` (5 GB), `program_black` (3 s, umbral 16/255), `program_frozen` (30 s),
`record_down` (5 s), `recursos`, `timer` (`AlertEngine.hpp:20-41`).

### 7.3 Recursos

Hilo propio, 1 vez por segundo: CPU (máquina y procesos propios), RAM, GPU (Mac:
IOAccelerator; Windows: contadores «GPU Engine» y DXGI; Linux: amdgpu en `/sys`),
red, disco, cps y cuadros perdidos del motor; historial de 10 minutos; aviso ≥ 80 %,
crítico ≥ 92 %, sostenidos (`src/core/diag/Recursos.hpp:1-26`, `Recursos.cpp:783-795`).

### 7.4 Persistencia: qué se guarda, dónde, cuándo, cómo

| Archivo | Escribe | Cuándo | Cómo | Dónde |
|---|---|---|---|---|
| `Config/app.json`, `gateway.json`, `launcher.json` | A (`AppRuntime::save`) | al guardar | temporal + `rename`; copia la generación anterior a `.anterior` si era JSON válido; sin `fsync`; no se guarda en modo seguro | `src/app/arranque/AppSettings.cpp:90-122`; `OperateMode.cpp:1763-1798` |
| `Runtime/al-aire.json` | A, hilo de la interfaz | al cambiar la escena al aire de alguna salida y al arrancar/parar salidas | temporal + `rename`; sin `fsync` | `OperateMode.cpp:959-981` |
| `Config/salidas.json`, `retornos.json`, `loudness.json`, `interfaz`, escenas, seguridad de sala, rótulo de traducción, `Runtime/placa.json` | A | al cambiar | `config::guardar`: `ofstream` directo (trunca), sin temporal | `src/app/arranque/ArchivoConfig.cpp:16-22`; llamadas en `OperateMode.cpp:1527-1567`, `emision/RetornosInvitados.cpp:22`, `emision/SeguridadAire.cpp:30`, `audio/ObjetivoLoudness.cpp:17`, `arranque/InterfazConfig.cpp:21`, `escenas/AjustesEscenas.cpp:19`, `sala/SeguridadSala.cpp:23`, `agent/RotuloTraduccionConfig.cpp:20` |
| `Runtime/ultima-caida.json`, `caidas.jsonl` | launcher | en cada caída | `ofstream` truncado / agregado | `Supervisor.cpp:349-369` |
| `Runtime/estado-ui.json` | A, hilo propio | con la UI trabada | último pedido | `LatidoAire.cpp:106-120` |
| `Runtime/arranque.json` | A | durante el arranque | con `fsync` | `src/core/diag/DiarioDeArranque.cpp:115-154` |
| `Credenciales/*` | A, B, launcher | al cambiar | `SecretsStore` con `fsync` | `src/core/security/SecretsStore.cpp:299` |
| Instantáneas de estado | `OperatorEngine` (modo sin interfaz) | — | `SnapshotStore`: temporal + `fsync`/`FlushFileBuffers` + `rename`, SHA-256 | `src/core/persist/SnapshotStore.cpp:1-50`, `393-450`; usado en `src/app/emision/OperatorEngine.*` |

Otros datos: 65 archivos `.cpp` de `src/app` (fuera de tests) usan `std::ofstream`;
44 líneas con sufijo `".tmp"` y 93 llamadas a `rename(` en todo `src/` (grep).

---

## 8. Concurrencia y bloqueos

### 8.1 Candados principales

| Candado | Tipo | Protege | Quién lo toma | Dónde |
|---|---|---|---|---|
| `Motor::mu_` | `recursive_mutex` con vigía (`BA_CANDADO`) | canales, salidas, buses, escenas | el bucle del motor (cada vuelta); la interfaz solo en `iniciar`/`detener` | `src/motor/Motor.hpp:346`; `Motor.cpp:345`; `Candado.hpp:1-12` |
| `Motor::colaM_` + `colaCv_` | `mutex` + cv | cola de órdenes | la interfaz encola; el bucle corre | `Motor.hpp:405-417` |
| `instantaneaM_`, `nivelesM_` | `mutex` (solo copiar un `shared_ptr`) | foto de estado y niveles | interfaz (lee), bucle (publica) | `Motor.hpp:367-371` |
| `buzonesM_` + cv | `mutex` | buzón de cuadros de la app | quien empuja, `hiloCuadros_` | `Motor.hpp:387-398` |
| `Entrada::mu_` | `timed_mutex` por entrada | su pipeline | su trabajador; `tic`/`ponerEnUso` con `try_lock` (si está ocupada se saltea) | `src/motor/Entrada.cpp:415-423`; `PLAN-MOTOR-SIN-ESPERAS.md` (S2) |
| `Mezcla::mu_`, `Composicion::mu_` | `mutex` | ramas, latencias | bucle del motor | `Mezcla.cpp:155`, `345-347`; `Composicion.cpp:1148-1150` |
| `Vistas::mu_` | `mutex` | cuadros de cada vista | **hilo de streaming en cada cuadro** (copia el cuadro bajo el candado) y lectores | `src/motor/Vistas.cpp:50-62` |
| `Salida::retornoM` | `mutex` | `SpoutSender` del retorno | hilo de streaming de la vista, en cada cuadro | `BroadcastEngine.cpp:705-707` |
| `Reloj::mu_` | `mutex` | latencias anotadas | entradas (`anotar`), mezcladores | `Reloj.hpp:126` |
| `ajustes` | `shared_mutex` global | valores avanzados | lecturas compartidas en cualquier hilo | `src/core/ajustes/Ajustes.cpp:12-41` |
| `Registro::d_->m` | `mutex` global | escritura de cada línea del registro (stderr + archivo) | cualquier hilo que registre | `src/core/diag/Registro.cpp:204-215` |
| `AppRuntime::healthMu_` | `mutex` | escribir el latido desde varios hilos | UI, `LatidoAire`, arranque | `OperateMode.cpp:511`, `559`, `638`, `774` |
| `BroadcastEngine` `salidasColaM_`, `pedidosM_`, `fotoSalidasM_` | `mutex` | cola y foto de salidas | interfaz, hilo de salidas | `BroadcastEngine.cpp:410-413`, `1250-1268`, `1311-1314` |
| `Transmision::mu_`, `Grabacion::mu_` | `mutex` | su pipeline | hilo de salidas (`tic`, `parar` con espera de EOS) | `src/motor/Salidas.hpp` |
| `RingEstereo::m` | `mutex` | anillos de la Matriz | hilo de GStreamer (toma), hilo de mezcla, hilo de audio de Qt | `src/app/audio/RingEstereo.hpp:23-52` |
| `gstrack g_mu` | `mutex` | registro de anfitriones del rack | `barack` en el hilo de audio (sin bloquear: si está ocupado, prueba en el bloque siguiente) | `src/app/audio/GstRack.cpp:26-40`, `GstRack.hpp:13-15` |
| `MediaPump`: tabla de rutas, `out.lock` por canal, `ifb->lock` | `mutex` | rutas; anillo/slot de salida | hilos de libwebrtc (`forward*`), hilos de bajada | `src/gateway/MediaPump.hpp:42-50`; `MediaPump.cpp:533-541`, `1184` |
| `HealthBlock`, `SpoutTransport`, `SharedAudioRing` | seqlock / `atomic_ref` | memoria compartida | escritor y lectores sin bloqueo | sección 3.1 |

El vigía del candado registra cuando la interfaz espera o tiene `Motor::mu_` más
de 50 ms, o cualquier hilo lo tiene más de 200 ms (un aviso por lugar cada 10 s);
con `--test`, cada toma de la interfaz es ERROR (`src/motor/Candado.hpp:1-30`,
`Candado.cpp:44-55`).

### 8.2 Trabajo con espera o E/S por hilo

| Hilo | Operación | Duración máxima configurada | Dónde |
|---|---|---|---|
| interfaz | `writeOnAirState` (archivo) | E/S de disco | `OperateMode.cpp:783`, `959-981` |
| interfaz | `AppRuntime::save` (3 JSON + copia `.anterior`) | E/S de disco | `OperateMode.cpp:1810-1845` |
| interfaz | `config::guardar` (8 sitios) | E/S de disco | sección 7.4 |
| interfaz | `FrameService::poll` copia cuadros de invitados (BGRA del tamaño del programa) desde memoria compartida | cada 16 ms si hay cuadro nuevo | `src/app/ui/FrameService.cpp:207-320` |
| interfaz | herramientas MCP | según la herramienta | `PLAN-MOTOR-SIN-ESPERAS.md:5-8` |
| interfaz | `AlertEngine` | cada 250 ms | `AlertEngine.hpp:4-8` |
| interfaz | arranque y apagado del motor | — | `PLAN-MOTOR-SIN-ESPERAS.md` (fila del latido) |
| interfaz | lectura de `Config/destinos.json` en `destinos()` | E/S de disco | `OperateMode.cpp:1113-1142` |
| hilo de salidas | parar grabación: EOS hasta 5000 ms por grabación | 5 s | `Salidas.hpp` (`Grabacion::parar(int esperaMs = 5000)`) |
| hilo de salidas | cerrar transmisión: EOS hasta 2000 ms | `ajustes:stream.cierreMs` | `Salidas.cpp:723-730` |
| hilo `reinicioSala_` | pedido de reinicio a B | ~4 s | `OperateMode.cpp:1847-1849`, `1896` |
| streaming (vista) | copia del cuadro bajo `Vistas::mu_`; `enviarRetorno` bajo `retornoM` + `memcpy` a memoria compartida | por cuadro | sección 3.7 |
| streaming (archivo) | `splitmuxsink`/`filesink` escribe detrás del colchón comprimido | colchón 60 s / 512 MB | `Salidas.cpp:345-375` |
| cualquiera | `registrar()` → `fprintf(stderr)` + `fflush` bajo candado global → tubería de 1 MB | lo que tarde la tubería | `Registro.cpp:204-215`; `SalidaAsincrona.cpp:162`, `206` |
| bucle del motor | consultas de latencia y recálculo: en hilos aparte | — | `Piezas.cpp:800-880`; `Entrada.cpp:1252-1258` |
| B principal | `room.tick`, `processUsage`, `reapViewers` entre latidos | — | `src/gateway/main.cpp:1326-1347` |
| B por conexión | un hilo por conexión HTTP; timeout de handshake 15 s | `ajustes:room.handshakeMs` | `SignalingServer.cpp:790-806`; `SignalingServer.hpp:605` |

---

## 9. Organización del código

### 9.1 Tamaño por módulo

Contado sobre `.cpp/.hpp/.h/.c/.mm/.qml/.js/.py/.rs` (incluye tests).

| Módulo | Archivos | Líneas | Producto |
|---|---|---|---|
| `src/app` | 887 | 263 056 | `ba-app`, `ba-mcp`, `ba-control` |
| `src/gateway` | 109 | 35 144 | `ba-gateway` |
| `src/core` | 167 | 30 628 | `ba_core` (estática) |
| `src/gst-plugins` | 47 | 18 950 | 7 módulos de GStreamer |
| `src/motor` | 51 | 16 909 | `ba_motor` (estática) |
| `src/web` | 10 | 5 752 | `ba-web` (C) |
| `src/launcher` | 30 | 4 969 | `BroadcastAppliance` |
| `src/plugins` | 9 | 2 901 | SDK de plugins (Python) |
| `src/grafico` | 11 | 2 365 | `ba-grafico` |
| `src/lspdsp` | 2 | 823 | DSP de LSP en C |
| `src/pinggy` | 1 | 818 | `ba-pinggy` |
| `src/modelo-llama` | 1 | 741 | `ba-modelo` (llama.cpp) |
| `src/modelo-mac` | 1 (`.swift`) | 792 | `ba-modelo` (MLX) |
| **Total `src/`** | **1 329** | **384 136** | |

Subdirectorios de `src/app` (archivos / líneas, con `.hpp`): `qml` 159 / 51 548; `mcp` 76 / 37 015;
`ui` 96 / 34 245; `tests` 95 / 20 575; `agent` 58 / 16 176; `audio` 59 / 15 631;
`emision` 34 / 9 468; `video` 22 / 9 040; `evento` 24 / 7 542; `control` 20 / 6 430;
`escritorio` 7 / 6 372; `arranque` 26 / 5 701; `asamblea` 25 / 5 596; `ptz` 24 / 4 310;
`registro` 8 / 3 729; `dispositivos` 17 / 3 110; `graficos` 14 / 2 893; `plugins` 4 / 2 849;
`net` 12 / 2 788; `pantallas` 12 / 2 647; `sala` 34 / 2 473; `escenas` 14 / 2 046;
`red` 8 / 1 948; `term` 7 / 1 629; `tiempos` 4 / 1 519; `youtube` 2 / 1 070; resto < 1 000.

Subdirectorios de `src/core`: `diag` 21 / 4 523; `platform` 35 / 4 002; `ipc` 17 / 3 242;
`dsp` 11 / 2 081; `security` 6 / 1 709; `persist` 6 / 1 506; `documentos` 8 / 1 377;
`hw` 9 / 1 040; `rostros` 4 / 798; `qr` 4 / 720; `i18n` 2 / 639; `tema` 2 / 463;
resto (`crypto`, `fault`, `ajustes`) < 400; `tests` 34 / 7 856.

Web estática: `assets/web/` 91 archivos; 13 693 líneas de `.js/.html/.css`.

### 9.2 Archivos más grandes

| Líneas | Archivo |
|---|---|
| 5 168 | `src/app/ui/StudioViewModel.cpp` |
| 4 073 | `src/app/mcp/McpService.cpp` |
| 3 117 | `src/gateway/SignalingServer.cpp` |
| 3 056 | `src/app/video/VideoGrafos.cpp` |
| 2 644 | `src/app/ui/EventoViewModel.cpp` |
| 2 460 | `src/web/http.c` |
| 2 408 | `src/app/emision/BroadcastEngine.cpp` |
| 2 385 | `src/app/agent/AgentViewModel.cpp` |
| 2 182 | `src/motor/Entrada.cpp` |
| 2 146 | `src/app/qml/pages/SettingsPage.qml` |
| 2 142 | `src/app/control/Botonera.cpp` |
| 2 100 | `src/app/mcp/McpEventoTools.cpp` |
| 2 095 | `src/app/mcp/McpVideoTools.cpp` |
| 2 075 | `src/app/mcp/McpEtiquetas.cpp` |
| 2 073 | `src/app/plugins/Plugins.cpp` |
| 2 046 | `src/app/ui/TotemsViewModel.cpp` |
| 1 972 | `src/motor/Motor.cpp` |
| 1 951 | `src/app/arranque/OperateMode.cpp` |
| 1 949 | `src/gst-plugins/filtros/vista360_d3d12.h` |
| 1 889 | `src/app/registro/Conversacion.cpp` |

### 9.3 Bibliotecas y dependencias entre módulos

| Objetivo | Enlaza | Dónde |
|---|---|---|
| `ba_core` (estática) | mbedTLS, nlohmann-json, libsamplerate, argon2, ONNX Runtime (si está), IOKit/Foundation (Mac), advapi32 (Win) | `src/core/CMakeLists.txt:76-140` |
| `ba_motor` (estática; 18 `.cpp`) | `ba_core`, GStreamer (`ba::gstreamer`), AVFoundation/CoreGraphics (Mac), dwmapi (Win) | `src/motor/CMakeLists.txt:9-18` |
| `ba-app` | `ba_core`, `ba_motor`, Qt6 (Gui, Quick, Qml, QuickControls2, Network, Svg, Pdf, Multimedia), mbedTLS, `ba_codigos`, `ba_mdns`, `ba_noidea`, `ba_vterm`, sherpa-onnx, CLAP, CoreAudio (Mac), dxgi/ole32 (Win), X11 (Linux) | `src/app/CMakeLists.txt:324-516` |
| `ba-gateway` | `ba_core`, mbedcrypto, `ba_lwip`, libwebrtc (Mac: `ba_webrtc_mac`; Win: `ba_webrtc_win`, Shiguredo; Linux: ExternalProject con clang + libc++ si hay SDK; si no, backend NULL) | `src/gateway/CMakeLists.txt:53-215`, `340-360` |
| `BroadcastAppliance` | `ba_core` | `src/launcher/CMakeLists.txt:53` |
| `ba-web` | libdeflate, mbedTLS | `src/web/CMakeLists.txt:20-22` |
| `ba-grafico` | Qt6 (Gui, OpenGL, Qml, Quick), `ba_core` | `src/grafico/CMakeLists.txt:56` |
| Módulos GStreamer | `gstbacaptura` (Mac), `gstbafiltros`, `gstbaaudio`, `gstbaipc`, `gstbaplugins`, `gstbasuperres`, `gstbarostros` | `src/gst-plugins/CMakeLists.txt:35-163` |

Includes cruzados (cuenta aproximada de `#include "…"` resueltos por carpeta,
fuera de tests): `app` → `core` 461, → `motor` 30; `motor` → `core` 38;
`gateway` → `core` 85; `launcher` → `core` 26; `gst-plugins` → `core` 3;
`grafico` → `core` 9. `gateway`, `launcher` y `motor` no incluyen nada de `app`
(los «`net/…`» del gateway son `src/gateway/net/`).

Dependencias vendoreadas (`extern/`): argon2, cacert, chrome-headless, clap,
cloudflared, craft, dxc, fonts, fuentes-texto, gst-python-mac, gstreamer-macos
(1.28.7), herramientas, libdeflate, libmicrodns, libnoidea, libsamplerate,
libvterm, llamacpp-mac, lsp, lwip, mbedtls, mlx-swift-lm, nlohmann-json, pinggy,
python, qt6-host, rostros, sherpa-onnx, toolchain, webrtc. GStreamer Linux 1.28.7
(`tools/deps/gstreamer-linux.sh:37`); Windows 1.26.11 (`tools/deps/gstreamer-win.sh:63`).

### 9.4 Herramientas MCP

738 nombres únicos de herramienta en `src/app` (sección 3.5). Inventario generado:
`docs/mcp.json` (697, 52 categorías, 8 roles, 33 temas de ayuda; generado 2026-10-17).

### 9.5 Tests

| Carpeta | Archivos `test_*` | Líneas |
|---|---|---|
| `src/app/tests` | 95 | 20 575 |
| `src/core/tests` | 34 | 7 856 |
| `src/gateway/tests` | 26 | 7 498 |
| `src/motor/tests` | 13 | 2 978 |
| `src/gst-plugins/tests` | 6 | 943 |
| `src/launcher/tests` | 5 | 718 |
| `src/web/tests` | 1 (`test_ba_web.c`) + `ataques.py` | — |
| `tests/` (Python: docs, i18n, manifiesto, empaquetado, temporales, reloj de gráficos, tests C++) | 8 + `conftest.py` | — |
| `cmake/tests/qml_syntax.cmake` | sintaxis QML | — |

Cobertura por tema (nombres): motor (`test_motor`, `test_motor_salidas`,
`test_motor_retornos`, `test_motor_sin_esperas`, `test_composicion`, `test_salidas`,
`test_latencia_estudio`, `test_retraso_propio`, `test_capturas`, `test_sondas`,
`test_modos`, `test_d3d12_de_a_uno`, `test_motor_app`); IPC (`test_shared_audio_ring`,
`test_spout_transport`, `test_ipc_cross_process`, `test_video_cross_process`,
`test_health_block`, `test_ring_drift`, `test_control_protocol`); persistencia
(`test_snapshot_store`, `test_restore`, `test_config_persistida`); DSP
(`test_automixer`, `test_limitador_tp`, `test_resampler`, `test_audio_dsp`,
`test_lspdsp`, `test_loudness`, `test_matrix_mixer`, `test_audio_rack`, `test_clap_host`,
`test_calibracion`); gateway (`test_webrtc_loopback`, `test_gateway_integration`,
`test_room`, `test_turn`, `test_wireguard`, `test_ipstack`, `test_cupo_caliente`,
`test_prueba_eco`, `test_gateway_salidas`); launcher (`test_shadow_copy`,
`test_updater`, `test_datos`, `test_child_output`, `test_sha256`).

---

## 10. Plataformas

| Aspecto | macOS | Windows | Linux |
|---|---|---|---|
| GPU del motor | OpenGL (`glvideomixer`, `GLMemory`) | D3D12 (`d3d12compositor`, `D3D12Memory`) | ninguna (CPU, `compositor`) |
| Cámaras | `avfvideosrc` | `mfvideosrc` | `v4l2src` |
| Pantalla / ventana | `avfvideosrc capture-screen` / `bacapturaventana` (ScreenCaptureKit) | `d3d12screencapturesrc` (WGC para ventana) | `ximagesrc` / `pipewiresrc` |
| Decodificación H.264 de red | `vtdec` (GL sin copia) | D3D12/otros según GStreamer | VA/otros según GStreamer |
| Codificador H.264 | `vtenc_h264_hw`, `vtenc_h264` | `nvh264enc`, `qsvh264enc`, `d3d12h264enc`, `amfh264enc`, `mfh264enc` | `nvh264enc`, `qsvh264enc`, `vah264enc`, `vah264lpenc` |
| Respaldo | `x264enc` | `x264enc` | `x264enc` |
| Audio de entrada (motor) | `osxaudiosrc unique-id` | `wasapi2src device low-latency=true` (ASIO y DirectShow se excluyen del registro de GStreamer) | `pulsesrc device` |
| Audio de salida (escucha) | `osxaudiosink` | `wasapi2sink` | `pulsesink` |
| Audio de la Matriz | Qt Multimedia (CoreAudio; canales físicos por CoreAudio) | Qt Multimedia | Qt Multimedia |
| Memoria compartida | `shm_open` + `mmap` (nombres ≤ 31 en `Segmento`) | `CreateFileMappingW` en `Local\` | `shm_open` + `mmap` |
| Canal de control | socket UNIX en `Runtime/` | named pipe | socket UNIX (o abstracto) |
| Reloj `nowQpc` | `steady_clock` | `QueryPerformanceCounter` | `steady_clock` |
| Prioridad de hilos | QoS interactiva solo en hilos «críticos»; sin App Nap | `THREAD_PRIORITY_*`, sin EcoQoS, CPU Sets, `ABOVE_NORMAL_PRIORITY_CLASS` | `setpriority` (requiere permisos), `sched_setaffinity` |
| WebRTC (gateway) | libwebrtc + VideoToolbox H.264 | libwebrtc (Shiguredo, `/MT`) | libwebrtc por ExternalProject (clang + libc++) o backend NULL |
| Superresolución | CoreML, VideoToolbox, GLSL/FSR | ONNX Runtime + DirectML, ncnn/Vulkan, CUDA si hay | ONNX (CPU) |
| Modelo local | MLX (`ba-modelo` Swift) | llama.cpp (Vulkan) | llama.cpp (Vulkan) |
| GStreamer | 1.28.7 (framework) | 1.26.11 (MSVC) | 1.28.7 (compilado desde fuente) |
| Volcados | — (pila al registro por señal) | minivolcados por el launcher | — |
| Datos | carpeta «`<X> Datos`» al lado de la `.app` | junto al bundle | junto al bundle |

Fuentes: `src/motor/Piezas.cpp:383-455`, `1008-1023`, `1145-1170`; `src/motor/Capturas.hpp:1-22`;
`src/motor/Salidas.cpp:40-50`; `src/app/audio/EntradasAudio.cpp:18-30`;
`src/core/platform/GstEntorno.hpp:31-33`, `GstEntorno.cpp:119-124`; `src/motor/Escucha.cpp:20-30`;
`src/app/audio/EquipoAudio.cpp:28-45`; `src/core/platform/Segmento.hpp`; `LocalSocket_*.cpp`;
`src/core/ipc/HealthBlock.cpp:108-120`; `src/motor/Prioridad.cpp`; `src/gateway/CMakeLists.txt:139-215`,
`340-360`; `src/gateway/Webrtc.cpp:1118-1131`; `src/gst-plugins/CMakeLists.txt:103-160`;
`src/gst-plugins/superres/onnxsr.cpp:11-24`; `src/modelo-llama/CMakeLists.txt:1-10`;
`src/launcher/Volcados.hpp:15-16`; `src/core/platform/Paths.hpp:53-71`.

---

## 11. Configuración y datos

### 11.1 Raíces

| Raíz | Qué es | Dónde se resuelve |
|---|---|---|
| bundle (`bundleRoot`) | binarios (`bin/64bit`, plugins, `data/`); con copia sombra, la copia | `src/core/platform/Paths.hpp:47-51` |
| datos (`dataRoot`, `BA_DATA_ROOT`) | Windows/Linux: la carpeta del bundle; macOS: `<dir>/<X> Datos/` al lado de la `.app` | `Paths.hpp:53-71` |
| configuración activa (`configRoot`, `BA_CONFIG_ROOT`) | `Configuraciones/<nombre>/`; cambiar de configuración = reiniciar | `Paths.hpp:84-100`; `src/launcher/main.cpp:63-64` |
| `Runtime/` | estado de ejecución y archivos de encuentro (0700) | `Paths.hpp:112-117` |
| `Cache/`, `Credenciales/` | cachés; credenciales cifradas (de la máquina; `--update` no las toca) | `Paths.hpp:119-126` |

Carpetas **por configuración**: `Config`, `Media`, `Graficos`, `Plugins`, `Fotos`,
`Miniaturas`, `Eventos`, `Recordings`, `Shares`, `Exportado`, `Extensiones`
(`Paths.hpp:86-89`). De la **máquina**: `Modelos/`, `Herramientas/`, `Logs/`,
`Runtime/`, `Credenciales/`, `Cache/`, `launcher.json` (`Paths.hpp:90-92`;
`OperateMode.cpp:221-224`). Las grabaciones de un evento van a
`Eventos/<evento>/Grabaciones` y las ISO a `…/entradas` (`OperateMode.cpp:827-830`, `884`).

### 11.2 Archivos de `Config/` referidos en el código

(grep de `"Config" / "…"`): `app.json`, `gateway.json`, `launcher.json` (en los datos
desde la mudanza), `salidas.json`, `destinos.json`, `retornos.json`, `loudness.json`,
`disco.json`, `supervisor.json`, `video-reloj.json`, `video-grafo.json`,
`video-grafos.json`, `superres-banco.json`, `audio-racks.json`, `audio-matriz.json`,
`audio-escucha.json`, `alertas.json`, `alarmas.json`, `respaldo-entradas.json`,
`escenas.json`, `transiciones.json`, `dsk.json`, `multiview.json`, `camaras.json`,
`dispositivos.json`, `dispositivos2.json`, `drivers.json`, `ptz-interlock.json`,
`plugins.json`, `plugins-aprobados.json`, `pantallas.json`, `monitores.json`,
`ventanas.json`, `botonera*.json`, `control-web.json`, `control-sistema.json`,
`control-windows.json`, `conferencia.json`, `sesion-remota.json`, `seguridad.json`,
`tls/`, `wireguard.key`, `cuerpo.json`, `quorum.json`, `debate.json`,
`votaciones-oficiales.json`, `orden-del-dia.json`, `personas.json`, `personas/`,
`atencion.json`, `roles.json`, `asistente.json`, `reconocedores.json`,
`vocabulario.json`, `traducciones.json`, `glosario-traduccion.json`,
`rotulo-traduccion.json`, `correccion-subtitulos.json`, `cc.json`,
`voz-teleprompter.json`, `interfaz.json`, `ui.json`, `atajos.json`, `riel.json`,
`playlist.json`, `qr.json`, `historico.json`, `informes`, `recetas.json`,
`modo-vivo.json`, `mezclador-vista.json`, `mcp-aire.json`, `modelo-propio.json`,
`notificadores.json`, `tiempos-analisis.json`, `seguir-microfono.json`,
`youtube-token.json`, `twitch-token.json`, `user.ini`, `plugin_config/`, `.restaurar/`.

Quién escribe y lee los tres principales: A escribe `app.json`, `gateway.json` y
`launcher.json` (`OperateMode.cpp:1836-1840`); B lee `gateway.json` al arrancar
(se reinicia B para aplicar, `OperateMode.cpp:1847-1849`, `Supervisor.cpp:390-399`);
el launcher lee `launcher.json` al arrancar (`src/launcher/main.cpp:136-163`). Cada
proceso carga sus `ajustes` de su JSON (`src/core/ajustes/Ajustes.hpp:4-9`).

---

## 12. Cifras (constantes, colas, intervalos)

### 12.1 Constantes fijas en el código (no son `ajustes`)

| Constante | Valor | Dónde |
|---|---|---|
| Watchdog del launcher | 250 ms | `src/launcher/Supervisor.cpp:45` |
| Tick de la interfaz (`AppRuntime::tick`) | 100 ms | `src/app/ui/StudioViewModel.cpp:2302`; `OperateMode.cpp:72` (modo consola) |
| Cuadros a la interfaz (`FrameService::poll`) | 16 ms | `StudioViewModel.cpp:2304` |
| Bucle del motor | 20 ms | `src/motor/Motor.cpp:435-436` |
| Niveles / foto del motor | cada 2 / 10 vueltas (~40 / ~200 ms) | `Motor.cpp:341`, `397-404` |
| Latido de B | 250 ms | `src/gateway/main.cpp:1312` |
| Latido de `LatidoAire` | 250 ms | `LatidoAire.cpp:72` |
| Bloque de audio IPC | 480 cuadros (10 ms) | `SharedAudioRing.hpp:59` |
| Capacidad del anillo | 16 bloques (160 ms) | `SharedAudioRing.hpp:61` |
| Buffers por canal de video | 3, alineados a 4096 B | `SpoutTransport.cpp:75-77` |
| Sondeo de B sobre `BA.Program` | 4 ms | `MediaPump.cpp:593` |
| Sondeo de A sobre canales (video/audio) | 4 ms (15/10 ms si quieto) | `gst-plugins/ipc/video.cpp:97`; `anillo.cpp:82` |
| `PullAdm` | 10 ms | `src/gateway/Webrtc.cpp:220-235` |
| Bloque del rack (`barack`) | ≤ 1024 muestras | `src/app/audio/GstRack.cpp:85` |
| Bloque de la Matriz | 480 (10 ms); colchón 960; tope 3840 | `src/app/audio/RingEstereo.hpp:18-20` |
| Salida del `audiomixer` | 10 ms | `src/motor/Mezcla.cpp:84` |
| Lookahead del limitador | 5 ms | `src/gst-plugins/audio/limitador.cpp:8` |
| Margen de vistas y escucha | 40 ms | `Vistas.cpp:23`; `Escucha.cpp:32` |
| Bus «directo» | 40 ms | `Motor.cpp:26` |
| Tope automático de la latencia del estudio | 250 ms | `src/motor/Reloj.hpp:135` |
| Sostener subida / bajada de latencia | 2 s / 10 s | `Reloj.hpp:113-114` |
| Seguir al objetivo al arrancar | 8 s | `Reloj.cpp:453-456` |
| Espera de sincronía de reloj de red | 3 s | `Reloj.hpp:69` |
| Reanclaje de la hora de audio de captura | 50 ms | `Piezas.cpp:718` |
| Cola de una capa (`intersrc`) | 3 buffers, sin tope de tiempo | `Composicion.cpp:602` |
| Cola de una vista (`intersrc`) | 2 buffers | `Vistas.cpp:134` |
| Cola de origen de red | 4 buffers | `OrigenRed.cpp:101` |
| Cola delante de filtros de video | 1 buffer, `leaky=downstream` | `Entrada.cpp:859` |
| Placa | 640×360, 1 cps | `Entrada.cpp:46`, `296-309` |
| Entrada sin uso | 2 cps (1 cuadro / 500 ms) | `Entrada.cpp:1050-1053` |
| Reintentos de una entrada | 0,2 / 0,5 / 1 / 2 s | `Entrada.cpp:757` |
| Grabaciones ISO | tope 8 | `src/app/emision/BroadcastEngine.hpp:734` |
| Invitados (gateway / app) | 16 / 64 | `src/gateway/Config.hpp:53`; `src/core/ipc/LugaresSala.hpp:18` |
| Canales de escena / registro | 4 / 8 | `CanalesEscena.hpp:17`; `CanalesRegistro.hpp:19` |
| Cuerpo máximo MCP | 8 MB | `McpHttp.hpp:19` |
| Tubería de stdout/stderr | 1 MB | `SalidaAsincrona.cpp:162`, `206` |
| Disco en apuros / bien / tiempo bien | 3 s / 0,5 s / 30 s | `BroadcastEngine.cpp:1915-1917` |

### 12.2 `ajustes` relevantes para latencia y disponibilidad

Hay 210 claves `ajustes::*("…", <valor de fábrica>)` distintas en `src/` (grep). Las
de tiempos, colas y reintentos:

| Clave | Fábrica | Dónde |
|---|---|---|
| `audio.margenMezclaMs` | 30 ms | `src/motor/Mezcla.hpp:142` |
| `audio.colaFuenteMs` | 1500 ms | `Mezcla.cpp:168` |
| `audio.colaEscuchaS` | 2 s | `Escucha.cpp:75`; `Motor.cpp:242`, `1223` |
| `audio.medidorMs` / `audio.picoMs` | 50 / 300 ms | `Mezcla.cpp:170-171` |
| `audio.compensacionMaxMs` | 2000 ms | `src/app/audio/AudioRack.cpp:874` |
| `audio.rescanEquiposMs` | 3000 ms | `Matriz.cpp:170` |
| `video.margenComposicionMs` | 5 ms | `BroadcastEngine.cpp:583` (y `Composicion.cpp:395`) |
| `video.esperaCorteMs` | 700 ms | `Composicion.cpp:964` |
| `video.esperaCapturaMs` | 700 ms | `BroadcastEngine.cpp:1158` |
| `video.vistaReposoS` | 10 s | `BroadcastEngine.cpp:1133` |
| `entradas.congelarMs` | 2500 ms | `Entrada.cpp:318` |
| `entradas.latenciaMinimaMs` | 20 ms | `Entrada.cpp:996` |
| `entradas.retrasoPropioMaxMs` | 300 ms | `Entrada.cpp:1396` |
| `entradas.avisoHoraMs` | 400 ms | `Entrada.cpp:1424` |
| `entradas.realineoEntreS` / `realineoQuietoS` | 10 / 60 s | `Entrada.cpp:1324-1327` |
| `entradas.buscarDispositivoS` / `buscarVentanaMs` | 15 s / 2000 ms | `Entrada.cpp:1552-1553` |
| `entradas.redModo` / `entradas.rtspTransporte` | «low» / «tcp» | `src/app/emision/InputSpecs.cpp:213-216` |
| `salidas.margenMs` | 300 ms | `Salidas.cpp:56` |
| `stream.colaMs` | 1000 ms | `Salidas.cpp:617`, `621` |
| `stream.cierreMs` | 2000 ms | `Salidas.cpp:727` |
| `stream.sinDatosS` | 2 s | `Salidas.cpp:741` |
| `record.colaEncoderS` | 3 s | `Salidas.cpp:351` |
| `record.colchonMB` | 512 MB | `Salidas.cpp:348` |
| `record.tramoMin` | 0 (un archivo) | `BroadcastEngine.cpp:1647` |
| `record.replayTramoS` / `record.replayJuntarS` | 2 s / 120 s | `Salidas.cpp:285`, `230` |
| `record.colchonIsoS` | 10 s | `BroadcastEngine.cpp:2159` |
| `supervisor.sinAvanceMs` / `supervisor.uiTrabadaMs` | 1000 / 2000 ms | `LatidoAire.cpp:83`, `87` |
| `launcher.*` | ver 1.3 | `Supervisor.cpp:52-77` |
| `procesador.prioridad` / `procesador.mediosJuntos` | sí / sí | `Prioridad.cpp:321`, `333` |
| `room.maxConexiones` / `room.handshakeMs` | 64 / 15000 ms | `src/gateway/SignalingServer.hpp:604-605` |
| `room.maxVisores` | 48 | `src/gateway/Webrtc.hpp:253` |
| `room.visorCaidoS` / `room.visorNoConectaS` | 10 / 20 s | `Webrtc.cpp:1350-1351` |
| `room.reconexionMaxS` / `room.salaMuertaS` | 15 / 40 s | `src/app/ui/ConferenceClient.cpp:21-22` |
| `room.sondeoMs` | 5000 ms | `src/app/ui/ConferenceViewModel.cpp:183` |
| `gateway.tunelReintentoMaxS` | 30 s | `src/gateway/Pinggy.cpp:404` |
| `grilla.cps` | 25 | `src/gateway/main.cpp:1295` |
| `pantallas.max` / `pantallas.ttlHoras` | 32 / 12 h | `SignalingServer.hpp:506-507` |
| `turn.maxAsignaciones` / `turn.maxConexiones` / `turn.colaTcpKB` | 64 / 128 / 1024 KB | `src/gateway/turn/Servidor.cpp:877-882` |
| `superres.lentaMs` / `superres.tranquilaMin` | 2000 ms / 10 min | `src/app/video/VideoGrafos.cpp:2169-2170` |
| `graficos.limiteS` | 20 s | `src/app/graficos/GraficoProceso.cpp:25` |
| `plugins.arranqueS` / `plugins.pedidoMs` | 10 s / 10000 ms | `src/app/plugins/Hijo.cpp:97`; `Plugins.cpp:1447` |
| `ptz.sinRespuestaMs` / `ptz.reconexionMs` | 1500 / 3000 ms | `src/app/ptz/PtzVisca.cpp:19-23` |
| `red.httpTimeoutS` | 30 s | `src/app/net/HttpsClient.hpp:41` |

El relevamiento previo de valores fijos está en `docs/planes/VALORES-FIJOS.md` (226 líneas).

### 12.3 Ajustes de Configuración que no son `ajustes::`

| Valor | Fábrica | Rango | Dónde |
|---|---|---|---|
| Sin cuadros → placa | 3000 ms | 200-60000 | `Entrada.cpp:28`, `1467` |
| Rearmar sin datos | 10 s | 3-600 | `Entrada.cpp:29`, `1468` |
| Reintento máximo de una entrada | 2000 ms | 200-120000 | `Entrada.cpp:30`, `1469` |
| Placa sin uso | 120 s | 5-3600 | `Entrada.cpp:1462-1465` |
| Reconexión máxima de transmisión | 10 s | 1-300 | `Salidas.cpp:23`, `33` |
| Colchón de grabación | 60 s (`RecordConfig`), mín. 5 | — | `BroadcastEngine.hpp` (`RecordConfig::colchonSeg`); `BroadcastEngine.cpp:1653` |
| Disco en apuros | 3 s | 0,5-60 | `BroadcastEngine.cpp:1921` |
| Política de UI trabada | 60 s; relanzar si no hay aire | 10-3600 s | `LatidoAire.hpp:36-40`; `Config/supervisor.json` |

### 12.4 Temporizadores de la app de 100 ms o menos

`term/Pty_posix.cpp:101` 4 ms; `term/TerminalSession.cpp:157` 12 ms;
`ui/StudioViewModel.cpp:2304` 16 ms; `control/Botonera.cpp:254` 33 ms;
`escenas/ControlGrillas.cpp:50` 40 ms; `ptz/PtzVisca.cpp:16` 40 ms;
`agent/EstadoIA.cpp:27` 50 ms; `audio/Matriz.cpp:881` 50 ms; `control/Botonera.cpp:230` 50 ms;
`ui/PuestoCorreccion.cpp:18` 80 ms; `asamblea/SeguirMicrofono.cpp:35`, `plugins/Plugins.cpp:1137`,
`ptz/PtzOnvif.cpp:29`, `tiempos/TiemposModel.cpp:34`, `ui/StudioViewModel.cpp:2302` 100 ms
(todas rutas bajo `src/app/`; algunos solo corren con su vista abierta).

---

## 13. Documentos y comentarios contra el código

| Dice | Hace el código |
|---|---|
| `SpoutTransport.hpp:1-25`: en Windows el backend real es `spoutDX` (texturas DX11, misma GPU) | No hay código D3D/Spout2: el único `#if defined(_WIN32)` de `SpoutTransport.cpp` (`42-50`) es un comentario; el transporte es memoria compartida por CPU con triple buffer en las tres plataformas (`SpoutTransport.cpp:55-77`) |
| `platform/Segmento.hpp:9-11`: `ipc::SharedMemoryRegion` en Linux/macOS «vive en un registro del propio proceso y no cruza a otro» | `SharedMemoryRegion.cpp:209-225` usa `shm_open` + `mmap(MAP_SHARED)` |
| `Entrada.hpp:41-45`: la placa usa `fallbackswitch` | `input-selector` con un vigilante en `tic` (`Entrada.cpp:310-317`) |
| `Mezcla.hpp:4-8`: `intersrc ! queue ! volume` | sin `queue`: la cola es la del `intersrc` (`Mezcla.cpp:163-168`) |
| `Composicion.hpp:6-12`: `intersrc ! queue ! videocrop` y `start-time-selection=first` | sin `queue` (`Composicion.cpp:598-602`); `start-time-selection=set` con grilla de cuadros (`Composicion.cpp:217-230`) |
| `gst-plugins/ipc/video.cpp:14`: se mira cada 2 ms | 4 ms (15 ms quieto) (`video.cpp:94-97`, `123`) |
| `Salidas.hpp:16-17`: grabación en tramos, «un corte pierde a lo sumo el último» | `record.tramoMin` = 0 → un archivo por grabación (`BroadcastEngine.cpp:1647`) |
| `Salidas.hpp:12-13`: lista de codificadores `vtenc_h264_hw, nvh264enc, qsvh264enc, d3d12h264enc, vtenc_h264` | por plataforma, con `amfh264enc`, `mfh264enc`, `vah264enc`, `vah264lpenc` (`Salidas.cpp:40-50`) |
| `OperateMode.cpp:780`: «un codificador que se cayó en el aire se reemplaza y se rearranca» | `atenderEncoderCaido` está vacía (`BroadcastEngine.cpp:1863-1865`); `Transmision::armar` elige con la misma lista cada vez (`Salidas.cpp:596-640`) |
| `PLAN-MOTOR-SIN-ESPERAS.md` (S4): «una transmisión caída se rearma probando codificadores» | reintenta con `elegirCodificador(cfg_.codificadores)`, que devuelve el primero disponible de la misma lista (`Salidas.cpp:52-70`, `626-628`) |
| `EncoderConfig::rateControl = "CBR"`, `RecordConfig::calidadCrf` | no se usan en el motor (solo `EngineSelfTest.cpp:162`) |
| `docs/funciones/MCP.md:30` y `docs/mcp.json`: 697 herramientas | 738 nombres únicos `t.name` en `src/app` |
| `GraficoProceso.hpp:18-21`: componer en vivo «va a necesitar memoria compartida» | ya pide `"shm": true` y abre el `Segmento` (`GraficoProceso.cpp:143-153`) |
| `AudioRack.hpp:30-32`: «El audio (OBS) llama process()» | lo llama `barack` desde el hilo de GStreamer (`GstRack.hpp:1-18`) |
| `HealthBlock.hpp:31-32`, `Supervisor.hpp:138-140`, `ChildOutput.hpp:5-8`, `FrameService.hpp:9-11`, `Emisor.hpp:13-15`: referencias a libobs | `ba_motor` no usa libobs (`BroadcastEngine.hpp:9-13`) |
| `Motor.hpp:477-478`: «las demás van a 2 cuadros/s» | coincide (`Entrada.cpp:1040-1058`) |
| `InputSpecs.cpp:208-230`: la entrada de red guarda `ffmpeg_options` (`rtsp_transport`, `fflags=nobuffer`) | la traducción a ajustes del motor pasa solo `uri`, `nombre`, `audio` y `latencia` 200 ms (50 con `low_latency`, que `networkSpec` no escribe) (`BroadcastEngineScenes.cpp:573-583`) |
| `SharedAudioRing.hpp:18-19`: objetivo de latencia del anillo 20-40 ms | capacidad de fábrica 16 bloques (160 ms) en `baanillosink` (`anillosink.cpp:94-95`); el nivel real depende del lector (Apéndice A) |

---

## Apéndice A — Datos que no se pudieron determinar sin correr el sistema

1. **Latencia de punta a punta medida** (cámara → programa → codificador → servidor
   RTMP/SRT; micrófono → auriculares; invitado → programa → invitado). Solo hay
   latencias **configuradas** (sección 5.9) y medidores en vivo (`edad`, `Tiempos`).
2. **Latencia del estudio efectiva** en una instalación concreta: depende de lo que
   pidan las entradas (p90 de edades medidas en vivo, `Entrada.cpp:1361-1384`).
3. **Latencia declarada por `audiomixer`, `compositor`/`glvideomixer`/`d3d12compositor`,
   decodificadores y codificadores** que GStreamer suma en cada pipeline (se consulta
   en vivo: `Mezcla.cpp:261-266`, `Composicion.cpp:1148`).
4. **Tamaños de buffer reales de los dispositivos de audio** (`osxaudiosrc`,
   `wasapi2src low-latency=true`, `pulsesrc`, sinks, `QAudioSink`/`QAudioSource`):
   el código no fija `buffer-time`/`latency-time`; el valor depende de GStreamer, Qt
   y el controlador.
5. **Nivel de llenado real del anillo de retorno A→B**: el escritor (`baanillosink
   sync=true`) produce un bloque cada 10 ms; el lector de B saca un bloque por vuelta
   y duerme 10 ms (`MediaPump.cpp:516-579`); la ocupación en régimen (entre 0 y 160 ms)
   y la cantidad de overruns (`overrunCount`) dependen de la planificación del sistema.
6. **Cantidad total de hilos** por proceso con una configuración dada (hay fórmulas
   por pipeline en la sección 2.2, más los internos de GStreamer, GL/D3D12, Qt y libwebrtc).
7. **Uso de CPU/GPU y memoria** por pipeline y por proceso (lo mide `Recursos` y
   `motor_mapa` en vivo; hay mediciones históricas en `docs/desarrollo/MAPA-GPU-CPU.md`).
8. **Copias GPU↔CPU efectivas por rama**: dependen de los elementos que negocie
   GStreamer en cada máquina (lo informa `motor_mapa`, `src/motor/Mapa.cpp`).
9. **Codificador elegido** en cada máquina (`elegirCodificador` depende de los
   elementos disponibles y de la placa) y su latencia interna.
10. **Deriva real entre el reloj de las placas de audio y el del estudio** y cada
    cuánto se reancla la hora (umbral 50 ms, `Piezas.cpp:718`).
11. **Comportamiento de NetEq/jitter buffer** del lado de los navegadores y de B
    (latencia de los invitados).
12. **Tiempos de arranque** (GStreamer, motor, escenas): se registran en vivo las
    etapas de más de 150 ms (`OperateMode.cpp:578-589`).
13. **Duración real de las esperas en el hilo de la interfaz** (E/S de `al-aire.json`,
    `app.json`, copias de `FrameService`, herramientas MCP): el vigía solo cubre el
    candado del motor (`Candado.hpp`) y la traba de la UI (`LatidoAire`).
14. **Si las escrituras sin `fsync` (`al-aire.json`, `app.json`) sobreviven a un
    corte de luz** en cada sistema de archivos (FAT/exFAT, APFS, NTFS, ext4).
15. **Tiempo de readopción de A/B por un launcher nuevo** y comportamiento ante un PID
    reutilizado (el código lo condiciona al latido fresco, `Supervisor.cpp:252-261`).
16. **Resultado actual de la suite de tests** (no se corrieron).

