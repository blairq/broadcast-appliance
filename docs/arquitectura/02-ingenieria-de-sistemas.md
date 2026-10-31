# Opinión de ingeniería de sistemas (02)

Lectura **valorativa** de la arquitectura, hecha sobre el relevamiento descriptivo
[`01-relevamiento.md`](01-relevamiento.md) (citado como «§sección») y verificada en
el código donde hizo falta (citado como `archivo:línea`, contra `master` del
2026-10-31). Cada problema lleva una propuesta concreta, un costo (S = días,
M = 1-3 semanas, L = más de un mes), el riesgo de hacerla y una métrica para saber
si mejoró.

Las decisiones del dueño del proyecto se toman como **restricciones**, no se
discuten: todo portable y vendoreado; todo sobre GStreamer (sin libobs); MVC con la
vista enterándose por el modelo; N salidas = N operadores completos; corte de luz =
todo vuelve solo (la línea de tiempo en HOLD preguntando «¿qué está pasando?»); una
dependencia faltante es un error de configuración.

## Índice

0. [Resumen](#0-resumen)
1. [Diagnóstico general](#1-diagnóstico-general)
2. [Procesos, dominios de falla y supervisión](#2-procesos-dominios-de-falla-y-supervisión)
3. [Concurrencia](#3-concurrencia)
4. [IPC y contratos entre procesos](#4-ipc-y-contratos-entre-procesos)
5. [Estado y persistencia](#5-estado-y-persistencia)
6. [Modularidad, deuda y testabilidad](#6-modularidad-deuda-y-testabilidad)
7. [Observabilidad y operación](#7-observabilidad-y-operación)
8. [Seguridad que afecta la disponibilidad](#8-seguridad-que-afecta-la-disponibilidad)
9. [Tabla consolidada de propuestas](#9-tabla-consolidada-de-propuestas)
10. [Top 10 priorizado](#10-top-10-priorizado)
- [Apéndice — Qué medir primero](#apéndice--qué-medir-primero)

---

## 0. Resumen

El sistema tiene **muy buen diseño en las capas bajas** (transportes sin bloqueo
con versión e `instanceId`, motor con cola de órdenes y foto de estado, placa por
entrada, reloj único del estudio, supervisor con readopción) y **un problema
estructural dominante en las capas altas**: el camino del aire (motor + salidas)
vive en el mismo proceso que la interfaz QML, el MCP, los agentes, los plugins
CLAP, la superresolución por ONNX/DirectML y el receptor NDI por ingeniería inversa
(§1.1, §9.1: 263 mil líneas en `src/app`). Cualquier caída de cualquiera de esas
piezas corta la transmisión y la grabación. Lo segundo en importancia es que el
**hilo de la interfaz es también el plano de control** (MCP, alarmas, persistencia
del estado de aire, latido): si se traba, el aire sigue pero nadie lo puede operar
ni enterarse. Lo tercero es la **persistencia heterogénea**: tres maneras de
escribir archivos, solo una durable, en un producto que promete «corte de luz = todo
vuelve solo».

---

## 1. Diagnóstico general

### 1.1 Fortalezas reales (no tocar, aprovechar)

| # | Fortaleza | Evidencia | Por qué importa |
|---|---|---|---|
| F1 | Separación sala (B) / estudio (A) por proceso, con medios por memoria compartida | §1.1, §3.2 | Un problema de WebRTC no tumba el aire; los dos sobreviven al launcher (`ChildProcess.cpp:230`) |
| F2 | Transportes IPC sin bloqueo, con `magic`/`version` y detección de productor nuevo | §3.1; `SharedAudioRing.hpp:56-57`, `SpoutTransport.hpp:322-334`, `ControlProtocol.hpp:116-122` | Reinicio de un lado sin coordinación; base sólida para cualquier separación futura |
| F3 | Motor como actor: cola de órdenes + foto de estado + niveles publicados sin candado | §2.2, §8.1; `Motor.cpp:332-437`, `Motor.hpp:367-417` | Es exactamente la costura que permite sacar el motor del hilo de la UI y hasta del proceso |
| F4 | Resiliencia por entrada: placa «sin señal», rearmado, reintentos crecientes, origen de red compartido | §4.5, §7.1 | Una cámara muerta no arrastra la composición |
| F5 | Un solo reloj y `base_time` para todos los pipelines, grilla de cuadros alineada | §6; `Composicion.cpp:215-230` | La sincronía es por construcción, no por parche |
| F6 | Latido de A atado al avance de cuadros, no solo a la UI | §1.4; `LatidoAire.cpp:67-104` | Distingue «UI trabada» de «aire muerto»: no mata el aire por una UI lenta |
| F7 | Grabación en MKV con colchón comprimido y copia en otra carpeta; «disco en apuros» que prioriza programa | §4.10, §7.1 | Protege contra disco lento, el modo de falla más común en vivo |
| F8 | Vigía del candado del motor y test `test_motor_sin_esperas` | §8.1; `Candado.hpp:1-30`; §5.10 | Convierte una regla de diseño en algo verificable |
| F9 | Procesos aparte para lo más riesgoso de afuera: `ba-web` (C, workers), `ba-pinggy` (OpenSSL separado), `ba-grafico`, `ba-modelo`, plugins Python | §1.1, §1.5 | Ya existe la cultura de aislar; falta aplicarla a lo que más pesa |
| F10 | Volumen de tests de IPC, DSP, motor y gateway | §9.5 | Hay red para refactorizar |

### 1.2 Problemas estructurales, priorizados por impacto en el aire

| Prioridad | Problema | Evidencia | Impacto |
|---|---|---|---|
| 1 | **Dominio de falla de A demasiado grande**: aire + UI + MCP + agentes + CLAP + superres + NDI + Qt Multimedia en un proceso | §1.1, §2.2, §5.3 (`barack` llama al rack con CLAP en el hilo de GStreamer, `GstRack.cpp:85-140`, `AudioRack.cpp:88`, `392`), §4.4 (NDI `bandisrc`), §10 (superres ONNX/DML) | Cualquier caída corta transmisión y grabación; el historial de caídas (heap en d3d12, ONNX/DML) cae justamente acá |
| 2 | **El hilo de la UI es el plano de control**: MCP, alarmas, estado de aire, guardado, arranque del motor, cuadros de invitados | §2.2, §3.5, §7.2, §8.2; `StudioViewModel.cpp:2301-2304` | UI trabada = sin control remoto, sin alarmas, sin `al-aire.json` actualizado; con aire, el launcher nunca la mata (`LatidoAire.cpp:96-101`): puede quedar inoperable indefinidamente |
| 3 | **Recuperación de salidas incompleta**: codificador caído no se reemplaza; grabación caída no se rearranca | §7.1, §13; `BroadcastEngine.cpp:1863-1865`, `Salidas.cpp:527-542`, `52-70` | Una falla de un codificador por hardware deja la transmisión en bucle de reintento con el mismo codificador; una grabación caída queda caída hasta que alguien mire |
| 4 | **Persistencia no durable** del estado que promete «todo vuelve» | §1.6, §7.4; `ArchivoConfig.cpp:16-22`, `OperateMode.cpp:959-981` | Tras un corte, `al-aire.json` o un `Config/*.json` pueden quedar vacíos/viejos; `restoreOutputs` con JSON vacío no retoma nada y no avisa (`OperateMode.cpp:983-986`) |
| 5 | **Lector del retorno de audio en B que no puede seguir al escritor** | §3.3, §5.5, Apéndice A.5; `MediaPump.cpp:516-579` | Un `pop` por vuelta + `sleep_for(10 ms)`: el período real es > 10 ms; el anillo se llena hasta 160 ms y el escritor descarta bloques. Ver §3.4 |
| 6 | **Sondeo a 4 ms en decenas de hilos** a ambos lados de la memoria compartida | §3.3, §12.1; CPU ociosa ~88 % de un núcleo (memoria del proyecto) | CPU y cambios de contexto que se le roban al mezclador; calor en equipos chicos |
| 7 | **Gateway con un solo cupo de conexiones** para público, túnel, LAN y la API que usa A | §2.3, §12.2; `SignalingServer.cpp:272-378`, `782` | Un flujo de conexiones externas deja a la app sin su SSE → «sala muerta» a los 40 s |
| 8 | **Observabilidad sesgada a logs**: sin métricas de cola/anillo, sin nombres de hilo, volcados solo en Windows | §2.5, §1.3, Apéndice A.5/A.13 | Los problemas 5 y 6 existen hoy y nadie los ve en vivo |
| 9 | **Acoplamiento ViewModel ↔ runtime** y archivos gigantes | §9.2; `StudioViewModel.cpp` 5 168 líneas, el `tick` del runtime lo maneja su `QTimer` (`StudioViewModel.cpp:2301-2302`) | Contradice el MVC elegido; dificulta sacar el runtime de la UI (problema 2) |
| 10 | **Superficie MCP de 738 herramientas sin fuente única** | §3.5, §9.4, §13 | Deriva de documentación (697 vs 738), costo de pruebas, riesgo de que un agente opere el aire por error |

---

## 2. Procesos, dominios de falla y supervisión

### 2.1 Mapa de dominios de falla (qué cae con qué)

| Si falla… | Cae con eso | Sobrevive | Fuente |
|---|---|---|---|
| launcher | nada | A, B (readopción por PID + latido) | §1.2, §1.5 |
| B (`ba-gateway`) | invitados, pantallas de sala, túneles (hijos de B), TURN, WireGuard, API de sala | aire (A sigue; los anillos se reabren solos) | §1.5; `video.cpp:1-22` |
| A (`ba-app`) | **transmisión, grabación, copia, ISO, repetición, escucha, UI, MCP, agentes, alarmas**, mix-minus a invitados | B (muestra placa a 5 cps) | §1.5, §7.1 |
| un plugin CLAP en el rack | **A entero** (corre en el hilo de streaming de GStreamer de A) | — | §5.3 |
| superres ONNX/DML/CoreML | **A entero** | — | §4.4, §10 |
| `bandisrc` (NDI) | **A entero** | — | §4.4 |
| `ba-grafico`, `ba-modelo`, plugin Python, Craft, Chrome | solo ese hijo | A | §1.5 |

Lectura de ingeniería: la arquitectura aisló bien lo **barato de aislar** (hijos
con stdin/stdout) y dejó adentro de A lo **caro de aislar** (código nativo de
terceros que toca medios en tiempo real). Es justo lo que más se cae.

### 2.2 Tiempos de detección y recuperación de A (RTO)

Armado con los datos de §1.3/§1.4 (no medido: Apéndice A.12):

| Etapa | Valor | Fuente |
|---|---|---|
| Detección por caída | inmediata (salida del proceso) | §1.5 |
| Detección por cuelgue del motor | 1 s sin avance de cuadros + 5 s de latido quieto + ≤ 250 ms del watchdog ≈ **6,25 s** | `LatidoAire.cpp:83`, `Supervisor.hpp:65`, `Supervisor.cpp:45` |
| Backoff | 0, 1, 2, 5 s, después **10 s para siempre** en esa sesión del launcher | §1.3; `Supervisor.cpp:72-78`, `314-316`, `474-475` |
| Arranque de A hasta el primer cuadro al aire | desconocido; tolerado hasta 90 s | `Supervisor.cpp:61`; Apéndice A.12 |
| Reconexión RTMP | 1, 2, 4 s… al armarse la salida | `Salidas.cpp:23-33` |

Observaciones:

1. **El backoff no se olvida nunca** (verificado: solo `forceRestart()` pone en cero
   `appConsecutive_`/`bConsecutive_`, `Supervisor.cpp:474-475`). En un evento de 8 h,
   la quinta caída aislada (aunque la anterior haya sido 3 h antes) espera 10 s
   antes de relanzar: 10 s más de negro en la plataforma por nada.
2. **Cortacircuito binario**: > 5 caídas en 120 s → modo seguro con escenas vacías,
   `x264` y sin guardar (`OperateMode.cpp:149-165`). Para un operador eso es «el aire
   se fue a negro»; no hay escalones intermedios (desactivar superres, CLAP, NDI,
   agentes) que preserven el programa.
3. **Política de UI trabada con aire**: nunca se relanza (`LatidoAire.cpp:96-101`).
   Es la decisión correcta para no cortar el aire, pero sin un plano de control
   fuera de la UI (§3.2) deja al aparato **en vuelo sin mandos**.
4. **Readopción sin chequeo de versión** (`Supervisor.cpp:249-262`): tras una
   actualización con hijos vivos, el launcher nuevo adopta un A/B viejo. Los
   transportes tienen versión (F2), pero el launcher no compara `HealthEntry.version`.
5. **`BA.Health` con 2 o 4 slots** según quién lo cree (§3.2). Inofensivo hoy, pero es
   un contrato con dos verdades.

### 2.3 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **S1** | Caída de A corta transmisión y grabación | §2.1; §1.5 | **Proceso emisor aparte (`ba-emisor`)**, uno por salida o uno con N salidas: pipeline de GStreamer `bavideoanillo canal=BA.Salida.<id>` + `baanillo canal=BA.Audio.Salida.<id>` → `fallbackswitch` a una placa «volvemos enseguida» + silencio → codificador → `flvmux/rtmp2sink`, `mpegtsmux/srtsink`, `splitmuxsink`. Todas las piezas existen (canales §3.2, elementos `gstbaipc`, `hora.hpp` para A/V). A deja de codificar; si A se cae, la sesión RTMP/SRT **no se corta** y la grabación sigue (con placa) en el mismo archivo. Lo lanza y vigila el launcher como un tercer hijo con su slot de `BA.Health`. La vista `programa@gateway` deja de soltarse por reposo mientras haya salida activa (`BroadcastEngine.cpp:1133`). | L (M para un prototipo con la salida principal) | Medio: +1 copia CPU y +~40-50 ms de latencia (vista + anillo, §5.9); en Windows se pierde la entrada `D3D12Memory` al codificador (§4.3) — compensable con `d3d12upload` en el emisor | Prueba de caos: `kill -9` de A con transmisión a un servidor RTMP local; hoy: desconexión; objetivo: **0 desconexiones** y hueco de programa = RTO de A |
| **S2** | Código nativo de terceros en el proceso del aire | §2.1 | **Clasificar los módulos de A por riesgo** y sacar primero los que más se caen: (a) superres a un proceso «banco» con texturas compartidas (IOSurface en Mac, handle compartido D3D12 en Windows; Linux ya es CPU); (b) CLAP: anfitrión fuera de proceso por bus o por rack (bloques de 10 ms por un `SharedAudioRing` ida y vuelta, +10-20 ms) como opción **por plugin** («aislado» de fábrica para plugins nuevos, «en proceso» para los de LSP ya probados); (c) NDI: `bandisrc` en un hijo que publica `BA.Ndi.<k>` por los mismos canales que un invitado. | L (cada una M) | Medio: latencia y copias extra en lo aislado | Caídas de A por mes atribuidas (volcado → módulo) antes/después |
| **S3** | Modo seguro binario | §1.3; `OperateMode.cpp:149-165` | **Modo seguro escalonado**: el launcher pasa `--safe-level=N`; nivel 1 = sin superres, sin CLAP de terceros, sin plugins Python, sin agentes ni modelos; nivel 2 = además sin NDI ni capturas de ventana; nivel 3 = el actual. Se sube un nivel por cada disparo del cortacircuito; la causa de la última caída (`Runtime/ultima-caida.json`, volcado) puede elegir directo el módulo a apagar. Las escenas se restauran en los niveles 1-2. | M | Bajo | Programa restaurado (no negro) en ≥ 90 % de los disparos del cortacircuito en pruebas de inyección |
| **S4** | Backoff que no se olvida | `Supervisor.cpp:314-316`, `474-475` | Poner en cero `appConsecutive_`/`bConsecutive_` cuando el hijo lleva **estable** `ajustes:launcher.estableS` (p. ej. 120 s) con latido fresco; en `watchdogLoop`. | S | Muy bajo | Test de launcher: 5 caídas espaciadas 10 min → cada relanzamiento con espera 0 s |
| **S5** | Readopción de una versión distinta | `Supervisor.cpp:249-262` | Comparar `HealthEntry.version` con la del launcher; si difiere, cierre ordenado y relanzamiento (salvo con aire: posponer hasta que no haya salidas, con aviso). Unificar la cantidad de slots en una constante de `ba_core`. | S | Bajo | Test: launcher v2 + A v1 vivo → relanzado |
| **S6** | RTO desconocido | Apéndice A.12, A.15 | **Banco de caos** en CI/noche: con `FaultInjector` (`src/core/fault/`) y `kill -9`/`SIGSTOP` de A, B, ba-web, cloudflared, mide: tiempo hasta primer cuadro en `BA.Program`, hasta reconexión RTMP, hueco en el MKV, reapertura de anillos. Publica los números en `Runtime/` y en el informe. | M | Bajo | RTO de A p50/p95 y su tendencia por versión |

---

## 3. Concurrencia

### 3.1 Lo que está bien

El plan «la interfaz nunca espera al motor» (`PLAN-MOTOR-SIN-ESPERAS.md`) atacó el
problema correcto: el hilo de la UI tomando `Motor::mu_`. La combinación cola de
órdenes + foto (`Motor.hpp:367-417`), `try_lock` en `Entrada` (§8.1) y el vigía del
candado es sólida. El `recursive_mutex` del motor (§8.1) es un resto: oculta
reentradas; conviene que el motor no lo necesite (propuesta C6).

### 3.2 El hilo de la UI hace demasiado

§2.2 y §8.2 listan en el hilo principal: MCP completo, `AlertEngine` (250 ms),
`writeOnAirState` (E/S), `AppRuntime::save` (3 JSON + copia), 11 llamadas a
`config::guardar` (verificado con grep), lectura de `destinos.json`,
`FrameService::poll` copiando cuadros del tamaño del programa cada 16 ms, arranque y
apagado del motor, y el `tick` del runtime. Además en macOS GL necesita el hilo
principal para soltar buffers (`PLAN-MOTOR-SIN-ESPERAS.md`, «El problema de diseño»).

Consecuencias de sistema:

- Las **alarmas se apagan cuando más se necesitan** (UI trabada = `AlertEngine` parado).
- El **control remoto** (MCP, y todo lo que entre por el bus de control hacia
  viewmodels) depende de la UI.
- La E/S de disco en el hilo de la UI convierte un disco USB lento (caso real del
  proyecto) en una UI trabada, y eso en «sin mandos».

### 3.3 Copias en el camino caliente

§3.7 cuenta **11 etapas** del programa al invitado con 4 copias de CPU completas en A
(etapas 3, 4, 5 y 7) más la de B (8), y una conversión BGRA→I420 **por sesión**
(etapa 10). A 1080p BGRA son 8,3 MB por copia: 5 copias × 30 cps ≈ 1,2 GB/s de
ancho de banda de memoria solo para eso. Además `Vistas::alCuadro` copia bajo
`Vistas::mu_` en el hilo de streaming (§8.1) y `enviarRetorno` hace `memcpy` bajo
`retornoM` (`BroadcastEngine.cpp:705-707`): dos candados en el camino de cada cuadro.

### 3.4 Colas, contrapresión y el retorno de audio

- **Retorno de audio A→B** (verificado en `MediaPump.cpp:516-579`): el hilo saca **un**
  bloque de 10 ms por vuelta y después duerme 10 ms; el trabajo (mezcla IFB, prueba de
  eco, un `pushLocalAudio` por sesión) se suma al período. Si la vuelta dura 11 ms, el
  lector consume 91 bloques/s contra 100 del escritor: el anillo se llena hasta su tope
  de 160 ms (§3.2) y desde ahí el escritor **descarta** un bloque de cada ~11. En
  Windows, `sleep_for(10 ms)` sin resolución de temporizador de 1 ms (no hay
  `timeBeginPeriod` en `src/`, verificado) puede dormir ~15,6 ms: perdería ~1/3 del
  audio de retorno, salvo que alguna biblioteca (libwebrtc) suba la resolución del
  proceso — **hay que medirlo** (`overrunCount` existe en la cabecera,
  `SharedAudioRing.hpp:86`, pero nadie lo lee fuera de los tests). Esto explicaría
  además retornos con mucha latencia (hasta 160 ms + NetEq). Va contra el objetivo
  documentado de 20-40 ms (§13, `SharedAudioRing.hpp:18-19`).
- **Contrapresión de video**: correcta (`leaky=downstream`, `max-buffers` chicos,
  `appsink drop=true`; §4.4, §4.7, §12.1). La de audio a codificador no es leaky
  (`Salidas.cpp:617-621`): está bien para no perder audio, pero un `rtmp2sink` trabado
  bloquea el audio 1 s antes de notarse; se cubre con `stream.sinDatosS`.
- **Registro**: `registrar()` hace `fprintf+fflush` bajo un candado global desde
  cualquier hilo, incluidos los de streaming (§8.2; `Registro.cpp:204-215`). Con la
  tubería llena (1 MB) el hilo de un agregador puede quedar esperando: inversión de
  prioridad clásica.

### 3.5 Hilos y sondeo

Por construcción hay un hilo por `intersrc` (§2.2), más los de sondeo a 4 ms en ambos
lados (§3.3). Con 16 invitados (video + audio) y los canales fijos de B (programa,
4 escenas, operador, voces), son del orden de **40-60 hilos despertando 100-250
veces por segundo** solo para preguntar «¿hay algo nuevo?». La medición citada
(~19-24 mil cambios de contexto/s, §2.4) es coherente con eso.

### 3.6 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **C1** | Retorno de audio de B que se atrasa | `MediaPump.cpp:516-579`; §3.4 | Bucle con **plazo absoluto** (`sleep_until(t0 + k·10 ms)`) y, en cada vuelta, **drenar hasta un nivel objetivo** (p. ej. 2-3 bloques = 20-30 ms) descartando lo viejo con fundido; leer `overrunCount`/`underrunCount` y publicarlos. En Windows, temporizador de alta resolución (`CreateWaitableTimerExW` con `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, API del sistema operativo, no una dependencia). | S | Bajo | Ocupación media del anillo ≤ 30 ms y `overrunCount` = 0 en 1 h con 8 invitados; latencia de retorno medida con la prueba de eco (§5.5) |
| **C2** | Plano de control en el hilo de la UI | §3.2 | **Hilo (o `QThread`) de control** dueño de `AppRuntime`: su propio `tick`, `AlertEngine`, escritura de estado, guardado; recibe órdenes por cola y publica fotos (mismo patrón que el motor, F3). La UI y el MCP son clientes. El MCP: despachar cada herramienta según su metadato (`lectura` → foto, en un hilo del pool; `escritura` → cola del control; `ui` → hilo de la UI con tope de tiempo). | L (por etapas: alarmas S, persistencia S, MCP M, runtime M) | Medio: carreras al mover objetos `QObject` de hilo; mitigable porque los modelos ya publican señales | Histograma de bloqueos del hilo de la UI (ver O3); alarmas y MCP que siguen respondiendo con la UI trabada a propósito (test con `SIGSTOP` simulado: bucle ocupado en QML) |
| **C3** | E/S de disco en el hilo de la UI | §8.2 | **Escritor de persistencia** en un hilo propio con fusión por archivo (último gana) y prioridad para `al-aire.json`; usa el escritor atómico de P1. | S | Bajo | 0 llamadas a `ofstream`/`rename` desde el hilo principal (contador en modo `--test`, igual que el vigía del candado) |
| **C4** | Copias del programa al invitado | §3.7 | (a) Bajar ya en **BGRA desde la GPU** (`glcolorconvert`/`d3d12convert` a BGRA) y eliminar `videoconvert` en CPU (etapa 3); (b) en `Vistas`, guardar una referencia al `GstBuffer` (`gst_buffer_ref`) en vez de copiar, y soltar el candado antes de avisar (etapas 4-5); (c) en B, **una conversión I420 por resolución**, compartida por las sesiones (`VideoFrameBuffer` de libwebrtc es inmutable y con conteo de referencias) (etapa 10); (d) a futuro, transportar NV12/I420 en el canal (1,5 B/píxel en vez de 4). | M | Bajo-medio: cuidar el `stride` y la vida del buffer | Copias por cuadro (de 5 a 2); CPU de A y B con 8 invitados; `motor_mapa` |
| **C5** | `FrameService` copiando cuadros completos en la UI | §2.2; `FrameService.cpp:286-315` | Que B publique además una versión **chica** por invitado para la UI (ya lo hace para backstage a ~10 cps, §3.2) o que el motor dé una vista escalada; la copia y la conversión a `QImage`/textura en un hilo aparte (`QQuickImageProvider` asíncrono o `QSGTexture` desde un hilo de render). | M | Bajo | Tiempo de `poll` por tick (p99) < 1 ms |
| **C6** | `recursive_mutex` en el motor | §8.1; `Motor.hpp:346` | Cambiar a `mutex` común: la reentrada se vuelve un abrazo mortal **detectable** en tests en vez de silenciosa; aprovechar que todas las mutaciones pasan por la cola. | S-M | Medio: aparecen reentradas escondidas (eso es lo buscado) | Suite de motor verde con `mutex`; 0 tomas de la UI con `--test` |
| **C7** | Registro con candado global en hilos de tiempo real | `Registro.cpp:204-215` | Cola **por hilo** sin bloqueo (o una MPSC acotada) hacia un único hilo escritor que hace `fprintf/fflush`; en hilos de streaming, si la cola está llena, se descarta y se cuenta (igual que `SalidaAsincrona`). | S-M | Bajo | Tiempo máximo de `registrar()` en un hilo de agregador < 50 µs |
| **C8** | Sondeo a 4 ms | §3.3, §3.5 | **Timbre** por canal: el escritor incrementa un contador de generación en la cabecera y despierta al lector con un objeto del sistema (Windows: evento con nombre; Linux: `futex` sobre la palabra compartida; macOS: `os_sync_wait_on_address` donde exista o un `pipe`/socket local por canal como respaldo). Mientras tanto, como paso S: dormir hasta **el próximo cuadro esperado** (1/cps del productor, que está en la cabecera) en vez de 4 ms fijos. | M (S el paso intermedio) | Medio en macOS (primitiva entre procesos) | Despertares/s de A y B ociosos con 8 invitados (`sample`/ETW); CPU ociosa (la memoria del proyecto da ~88 % de un núcleo) |
| **C9** | Hilos sin nombre | §2.5 | Nombrar todo hilo propio (`pthread_setname_np`/`SetThreadDescription`) y, en el manejador `STREAM_STATUS_ENTER` que ya existe (`Prioridad.cpp:253-265`), nombrar los de GStreamer como `<pipeline>/<elemento>`. | S | Nulo | 100 % de hilos con nombre en `sample` y en volcados |

---

## 4. IPC y contratos entre procesos

### 4.1 Opinión

Los contratos **de datos** (memoria compartida) son de lo mejor del sistema: cabecera
con `magic`/`version`, `instanceId`, seqlock, sin bloqueo para el escritor (§3.1). Los
contratos **de control** son más débiles y más dispersos:

- Hay al menos **cinco protocolos de control** distintos: socket local con largo +
  JSON y `protocolVersion` (`ControlProtocol.hpp`), HTTP REST + SSE de la sala
  (50 rutas, `/api/v1` solo en parte, §3.4), MCP HTTP, JSON por línea por stdin/stdout
  (`ba-grafico`, `ba-modelo`, plugins, `ba-pinggy`), y archivos de encuentro en
  `Runtime/` (§3.6).
- **Configuración como IPC**: A escribe `gateway.json` y `launcher.json` y reinicia B
  para aplicarlo (§11.2; `OperateMode.cpp:1836-1849`). Reiniciar B corta **todas** las
  sesiones de invitados para cambiar un ajuste.
- **Documentación desalineada con el transporte**: `SpoutTransport.hpp:1-25` promete
  texturas DX11 en Windows y `Segmento.hpp:9-11` dice que la región no cruza procesos
  (§13). Son comentarios de cabecera, que es lo primero que lee quien toca el IPC.
- **Sin observabilidad del transporte**: `overrunCount`/`underrunCount` y la edad A→B
  existen (§3.1, §4.8 punto 4) pero solo la edad llega al registro, cada 10 s.

### 4.2 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **I1** | B se reinicia para aplicar configuración | §11.2; `OperateMode.cpp:1847-1849` | Cada proceso **dueño de su configuración**; A pide cambios a B por la API (`PUT /api/v1/config/<sección>`) y B los aplica en caliente cuando no son estructurales (puertos, certificados sí requieren reinicio, y entonces se avisa con las sesiones activas). | M | Medio: cada clave tiene que declarar si es «en caliente» | Reinicios de B por cambio de ajuste (hoy: todos; objetivo: solo puertos/TLS) |
| **I2** | Protocolos de control sin catálogo ni versión común | §1.1, §3.4 | Un **documento de contratos** generado (rutas de la API de sala, mensajes del canal de control, JSON por línea de cada hijo) con `version` en el saludo de **cada** protocolo por línea; tests de contrato que arrancan el hijo real y validan el saludo. | M | Bajo | Todos los protocolos con versión en el saludo; tests de contrato en CI |
| **I3** | Contadores del transporte invisibles | `SharedAudioRing.hpp:86`; §4.1 | Publicar por canal: ocupación, `overrun`, `underrun`, cuadros perdidos/repetidos del triple buffer, edad p50/p99, en la foto de B y de A y en las métricas (O1). Alarma si `overrun` crece. | S | Nulo | Panel con los contadores; alarma probada |
| **I4** | Comentarios de cabecera falsos sobre el IPC | §13 | Corregir `SpoutTransport.hpp`, `Segmento.hpp`, `video.cpp:14`, `SharedAudioRing.hpp:18-19` (y los de libobs: `HealthBlock.hpp:31-32`, `Supervisor.hpp:138-140`, `ChildOutput.hpp:5-8`, `FrameService.hpp:9-11`, `Emisor.hpp:13-15`). | S | Nulo | 0 filas de §13 que toquen IPC |
| **I5** | Archivos de encuentro escritos sin atomicidad uniforme | §3.6, §7.4 | Todo archivo de `Runtime/` leído por otro proceso (`mcp.json`, `gateway-admin.json`, `estado-ui.json`, `ultima-caida.json`) por el escritor atómico de P1; el lector tolera JSON inválido con reintento corto. `ba-mcp` puede cachear `mcp.json` y releerlo solo si falla la conexión (hoy lo lee en cada mensaje, §1.1). | S | Bajo | 0 lecturas de JSON truncado en tests de concurrencia escritor/lector |

---

## 5. Estado y persistencia

### 5.1 Opinión

Hay **tres calidades de escritura** conviviendo (§7.4):

1. Durable: `SnapshotStore` (temporal + `fsync`/`FlushFileBuffers` + `rename` + SHA-256),
   `SecretsStore`, `DiarioDeArranque`. **El `SnapshotStore` solo lo usa
   `OperatorEngine`**, un resto de la época de libobs que solo corre con
   `--headless-selftest` (`src/app/main.cpp:7`, `92`).
2. Atómica pero no durable: `app.json` y `al-aire.json` (temporal + `rename`, sin `fsync`
   ni del archivo ni del directorio).
3. Ni atómica ni durable: `config::guardar` (`ofstream` que trunca, verificado en
   `ArchivoConfig.cpp:16-22`), usado en 11 sitios, más 67 archivos de `src/app` con
   `ofstream` (§7.4).

Para un aparato cuya promesa es «se corta la luz y todo vuelve solo», el estado del
aire (`al-aire.json`, `placa.json`, `salidas.json`, `retornos.json`) y la corrida de la
línea de tiempo tienen que estar en la categoría 1. La carpeta de datos va **al lado del
bundle**, muchas veces en un disco USB con exFAT (§11.1; memoria del proyecto): en
exFAT/FAT no hay diario y `rename` no garantiza atomicidad tras un corte, así que hace
falta además **dos generaciones con suma de verificación**.

Sobre la lectura: `config::leerObjeto` devuelve `{}` ante un JSON inválido
(`ArchivoConfig.cpp:10-13`) y `restoreOutputs` con `{}` no restaura nada
(`OperateMode.cpp:983-986`): un archivo dañado se confunde silenciosamente con «no había
nada al aire». Es el peor modo de falla para T47.

**Fuentes de verdad duplicadas**:

- Qué está al aire por salida: `engine_` (motor), `al-aire.json`, `placa.json`, y ahora
  la `Corrida` de la línea de tiempo (`src/app/evento/Corrida.hpp:22-25`, HOLD tras corte).
  Hoy se reconcilian «al arrancar» con lógica en `OperateMode` y en `StudioViewModel`
  (`QTimer::singleShot(arranque.restaurarPlacaMs)` en `StudioViewModel.cpp:2305-2306`).
- Configuración de B: la escribe A, la lee B, la aplica reiniciando (§11.2).
- `RecordConfig::colchonSeg` 60 s en la app contra 30 s del motor (§4.10); `rateControl`
  y `calidadCrf` que no se leen (§13).
- Recuento de herramientas MCP (697 contra 738, §13).

### 5.2 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **P1** | Tres calidades de escritura | §7.4; `ArchivoConfig.cpp:16-22` | **Un solo escritor en `ba_core`**: `persist::escribir(ruta, bytes, Durabilidad{Normal, Critica})`. Normal = temporal + `rename`. Crítica = temporal + `fsync` del archivo + `rename` + `fsync` del directorio (POSIX); en Windows `FlushFileBuffers` + `MoveFileExW(MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)`. `config::guardar` y `AppSettings` pasan a usarlo. Un test de `tests/` (Python, como los de temporales) que **prohíba** `std::ofstream` sobre rutas de `Config/` y `Runtime/` fuera del escritor. | S-M | Bajo; el `fsync` cuesta ms, por eso va en el hilo de C3 | 0 escrituras directas (test); prueba de corte simulado (matar el proceso en medio de 10 000 escrituras): 0 archivos vacíos |
| **P2** | Estado del aire indistinguible de «vacío» tras daño | `OperateMode.cpp:983-986`; `ArchivoConfig.cpp:10-13` | Estado crítico en **dos generaciones** (`al-aire.a.json`/`al-aire.b.json`) con `secuencia` y `sha256` (el formato de `SnapshotStore`); al leer, la más nueva válida; si ninguna es válida, **no** asumir «nada al aire»: entrar en HOLD de todas las salidas y preguntar, igual que la línea de tiempo (T47). Registrar y alarmar el daño. | S | Bajo | Test de daño: truncar/corromper cada generación → restaura la otra o queda en HOLD con aviso |
| **P3** | Varias verdades sobre «qué está al aire» | §1.6, §7.1; `Corrida.hpp:22-25`; `StudioViewModel.cpp:2305` | Un **documento de estado de aire** único por salida (escena, placa/FTB, salidas, paso de la corrida, HOLD) que escribe un solo dueño (el hilo de control de C2), versionado; `placa.json` y la parte de aire de la corrida pasan a ser secciones de ese documento. La restauración deja de estar repartida entre `OperateMode` y un `singleShot` del ViewModel. | M | Medio: migración de archivos (la lectura vieja se conserva una versión) | 1 archivo de aire; test de corte en cada transición (escena, placa, grabar, transmitir, paso de corrida) → vuelve igual |
| **P4** | Esquemas de configuración sin versión | §11.2 (≈ 80 archivos) | Campo `esquema` en cada JSON de `Config/` y migraciones explícitas por archivo; `ajustes` (210 claves, §12.2) con un registro central (clave, tipo, rango, «en caliente» o no) que también sirva a I1. | M | Bajo | 100 % de archivos con `esquema`; test de migración desde la versión anterior |
| **P5** | Grabación en un solo archivo y copia con segundo codificador | §4.10; `BroadcastEngine.cpp:1647`, `1664-1676`; §13 | (a) `record.tramoMin` con un valor de fábrica distinto de 0 (p. ej. 15 min) para acotar lo que se arriesga si el MKV queda sin índice; (b) **copia por `tee` del flujo ya codificado** a un segundo `splitmuxsink` como opción de fábrica (protege del disco, que es la falla frecuente) y el segundo codificador como opción «independiente» (protege del codificador). La copia actual duplica la carga del codificador. | S | Bajo | Archivo recuperable tras `kill -9` (abrir con `gst-discoverer`); CPU/GPU del codificador con copia |

---

## 6. Modularidad, deuda y testabilidad

### 6.1 Opinión

- **Las fronteras de módulo son buenas donde hay biblioteca** (`ba_core`, `ba_motor`:
  `gateway`, `launcher` y `motor` no incluyen nada de `app`, §9.3) y **malas dentro de
  `src/app`**: 263 mil líneas en un ejecutable, con `StudioViewModel.cpp` (5 168 líneas)
  que además de ViewModel es el **reloj del runtime** (`tick_` y `frameTick_` en
  `StudioViewModel.cpp:2301-2304`) y orquesta la restauración. Eso invierte el MVC
  elegido: el modelo depende de que exista la vista.
- **`AppRuntime`** (en `OperateMode.cpp`, 1 951 líneas) concentra arranque, guardado,
  aire, salidas, canal de control y latido; **`BroadcastEngine`** (2 408 + escenas)
  traduce tipos lógicos de la época anterior (`ffmpeg_source`, `color_source_v3`,
  §4.1) al motor. Esa capa de traducción es deuda de migración: cada tipo lógico
  pasa por dos modelos.
- **MCP**: 738 herramientas en 52 archivos (§3.5), 37 mil líneas en `src/app/mcp`
  (§9.1). Para un LLM, el tamaño del catálogo baja la precisión de elección de
  herramienta; para el sistema, es una superficie de control enorme que corre en el
  hilo de la UI. El inventario (`docs/mcp.json`) se genera aparte y ya está atrasado.
- **Código muerto o de la época libobs**: `OperatorEngine` y `--headless-selftest`
  (`OperatorEngine.hpp:1-40` habla de «callback de audio de libobs»), referencias a
  libobs en comentarios (§13), ajustes que no se leen (`rateControl`, `calidadCrf`).
  Va contra la decisión «sin libobs, sin vuelta atrás».
- **Divergencias doc↔código** (§13): 16 filas. Las graves son las que prometen
  disponibilidad que no existe (`OperateMode.cpp:780` «el codificador se reemplaza»,
  `Salidas.hpp:16-17` «en tramos», S4 del plan de motor). Un operador o un agente que
  lee la documentación cree que el sistema se cura solo cuando no.
- **Testabilidad**: buena en capas bajas (§9.5); hay tests de cruce de procesos de
  transporte, pero no de **escenarios de sistema** (caer A con aire, corte con
  corrida en curso, B reiniciado con 16 invitados). La suite no se corrió en el
  relevamiento (Apéndice A.16).

### 6.2 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **M1** | Codificador caído que no se reemplaza; grabación caída que no vuelve | `BroadcastEngine.cpp:1863-1865`; `Salidas.cpp:52-70`, `527-542`; §13 | Implementar lo que la documentación ya promete: por salida, **lista negra** de codificadores que fallaron en esta sesión; `Transmision::armar` elige el siguiente y, agotados, `x264enc`. `Grabacion::tic`, ante error del bus: cerrar el archivo, abrir **uno nuevo** (sufijo `-parte-N`) con backoff 1-10 s, mantener la alarma `record_down` hasta que vuelva. | S-M | Bajo | Inyección: hacer fallar `vtenc_h264_hw`/`nvh264enc` en el aire → la transmisión vuelve con otro codificador en < 5 s; error de disco simulado → nueva parte en < 10 s |
| **M2** | El ViewModel es el reloj del runtime | `StudioViewModel.cpp:2301-2306`; §9.2 | `AppRuntime` dueño de su temporizador (y luego de su hilo, C2); `StudioViewModel` se suscribe a sus señales/propiedades. Partir `StudioViewModel` por áreas (aire, salidas, invitados, medios) con un ViewModel por área, cada uno ligado a su modelo (regla «la vista se entera por el modelo»). | M (incremental) | Medio: QML enlaza a propiedades del ViewModel grande | `StudioViewModel.cpp` < 1 500 líneas; el runtime corre en un test sin `QQmlEngine` |
| **M3** | Traducción de tipos lógicos viejos en `BroadcastEngine` | §4.1; `BroadcastEngineScenes.cpp:560-600`; §13 (`ffmpeg_options` que se pierden) | Migrar `app.json` una vez a los tipos del motor (con `esquema`, P4) y borrar la traducción; mientras tanto, test que verifique que **toda** opción guardada llega al motor o se rechaza con aviso (hoy `rtsp_transport`/`fflags` se pierden, §13). | M | Medio | 0 claves guardadas sin efecto (test de ida y vuelta) |
| **M4** | Superficie MCP sin fuente única | §3.5, §9.4, §13 | **Registro declarativo** de herramientas (nombre, esquema, rol, clase `lectura/escritura/aire/ui`, idempotencia, hilo) del que se generan en el build `docs/mcp.json`, `MCP.md` y el despacho de C2; el build **falla** si hay deriva (como «faltantes frenan el build»). Test genérico por herramienta: esquema válido, rechaza parámetros inválidos, las de `lectura` no cambian la foto. Consolidar familias CRUD en herramientas con `accion` donde ya hay 4-6 casi iguales (reducir lo que ve el modelo, no la funcionalidad). | M-L | Bajo (genera) / medio (consolidar cambia nombres: hay que reentrenar, ver memoria «entrenamiento de modelos») | Deriva doc↔código = 0; nº de herramientas visibles por rol; precisión de elección de herramienta en la evaluación de modelos |
| **M5** | Código muerto de la época libobs | `OperatorEngine.*`; `src/app/main.cpp:7`, `92`; §13 | Borrar `OperatorEngine` y `--headless-selftest`, o reconvertirlo en el autotest del motor nuevo; borrar `rateControl`/`calidadCrf` o hacerlos efectivos (CBR sí importa para RTMP); limpiar comentarios. | S | Nulo | 0 apariciones de «libobs» en `src/` fuera de historia |
| **M6** | Doc↔código | §13 | Test de documentación (ya existen tests de docs en `tests/`) que verifique los números citados (herramientas, puertos, valores de `ajustes`) contra el registro (P4, M4). | S | Nulo | Filas de §13 = 0 en el próximo relevamiento |
| **M7** | Sin tests de escenario de sistema | §9.5; Apéndice A.16 | Suite «de sistema» (nocturna) sobre S6: arranca launcher+A+B con fuentes sintéticas (`videotestsrc`, invitados con Chrome de medios falsos, como ya se hace a mano) y verifica las promesas: corte (kill de todo) → vuelve en HOLD; caída de A → salida sigue (con S1); caída de B → invitados reconectan. | M | Bajo | Escenarios verdes por versión |

---

## 7. Observabilidad y operación

### 7.1 Opinión

Hay buen material crudo: `Recursos` cada 1 s con historial (§7.3), `AlertEngine` con
18 tipos (§7.2), `MedidorEdad`, sondas, `motor_mapa` (§6), `caidas.jsonl` y el diario
de arranque (§1.3, §1.6), registro asíncrono a disco (§7.1). Lo que falta es
**estructura**:

- No hay **métricas** con nombre y tipo (contador/medidor/histograma): lo que no es
  un recurso del sistema o una alarma se ve solo como línea de log.
- Los **volcados solo existen en Windows** (§1.3, §10); en Mac y Linux, una pila al
  registro por señal.
- No hay **correlación** entre procesos (una orden de MCP → cambio en el motor →
  efecto en B no comparten id).
- Las alarmas **dependen del hilo de la UI** (§7.2).
- El estado de UI trabada va a `estado-ui.json` (§1.4), pero no a los notificadores.

### 7.2 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **O1** | Sin métricas estructuradas | §7.1-7.3 | Registro de métricas en `ba_core` (contadores/medidores/histogramas sin bloqueo); cada proceso expone `GET /metrics` en su puerto local ya existente (A: el de MCP con su Bearer; B: la API de sala; launcher: archivo en `Runtime/` cada 5 s). Primeras métricas (SLI): edad A→B p99, ocupación y overrun de cada anillo (I3), cuadros perdidos por pipeline, bloqueo del hilo de la UI (O3), latencia por herramienta MCP, espera del candado del motor, reconexiones RTMP, bytes escritos vs. colchón de grabación. | M | Bajo | Panel en la página de diagnóstico; las alarmas leen métricas, no logs |
| **O2** | Volcados solo en Windows | §1.3; `Volcados.hpp:1-17` | En macOS/Linux: volcado mínimo propio en el manejador de señales (pilas de todos los hilos con `backtrace` seguro + registros) o un `crashpad` vendoreado (cumple «todo vendoreado»); `tools/caidas/analizar.py` ya existe para procesarlos. Con C9, los volcados nombran los hilos. | M | Bajo | % de caídas con causa atribuida a un módulo (hoy, en Mac/Linux, bajo) |
| **O3** | Esperas del hilo de la UI no medidas | Apéndice A.13 | Vigía de **latencia del bucle de eventos** (un temporizador de 50 ms que mide su propio retraso) con histograma y «quién estaba corriendo» (último evento/herramienta MCP despachado); un aviso por lugar cada 10 s, igual que el vigía del candado. | S | Nulo | p99 del bucle de eventos < 50 ms en uso normal; lista de los 10 peores culpables |
| **O4** | Sin correlación entre procesos | §3.4, §3.5 | Id de correlación en cada orden (MCP, botonera, web) que viaja en el canal de control y en las llamadas a la API de B y aparece en el registro de los tres procesos. | S-M | Nulo | Reconstrucción de una orden de punta a punta con un grep |
| **O5** | Alarmas en el hilo de la UI y sin aviso de UI trabada | §7.2; `LatidoAire.cpp:106-120` | `AlertEngine` en el hilo de control (C2) leyendo métricas (O1); nueva alarma «interfaz trabada con aire» que sale por los notificadores y el control web (el operador puede tener el control web abierto en una tablet). | S (si C2 está) | Bajo | Alarma probada con un bucle ocupado inyectado en QML |
| **O6** | Actualizaciones | §1.1 (actualizador del launcher), `test_updater` | Regla dura: el actualizador **no reemplaza binarios con salidas activas** (lee `al-aire.json`/métricas) y verifica integridad completa antes de lanzar (§7.1 «integridad del bundle»); con S5, nunca readopta una versión distinta. | S | Bajo | Test: actualización pedida con aire → se pospone |

---

## 8. Seguridad que afecta la disponibilidad

### 8.1 Opinión

- **Cupo único de conexiones en B** (verificado): los cinco `acceptLoop` (público,
  admin, control LAN, puerta HTTP, túnel; `SignalingServer.cpp:272-378`) comparten
  `activeConns_` con tope 64 (`SignalingServer.cpp:782`). Un flujo de conexiones desde
  el túnel (Internet) o la LAN deja sin lugar a la conexión SSE de la app
  (`ConferenceClient`), y a los 40 s A da la sala por muerta (§3.4, §12.2). Además, un
  hilo por conexión con 15 s de handshake (§8.2) es el patrón más fácil de agotar con
  conexiones lentas.
- **B junta demasiadas superficies de red** en un proceso: WebRTC, señalización,
  API de sala, TURN, WireGuard + lwIP, TLS, proxy de nombres, ACME (§2.3). Un error de
  análisis en cualquiera tira a todos los invitados. `ba-web` ya existe como frente
  endurecido en proceso aparte (§1.1, tiene `ataques.py`, §9.5).
- **MCP**: bien encerrado (127.0.0.1, Bearer, Origin, 8 MB; §3.5). El riesgo para la
  disponibilidad es otro: cualquier agente con el token puede ejecutar herramientas
  de aire **en el hilo de la UI**. `Config/mcp-aire.json` existe (§11.2); conviene
  que la clase «aire» del registro (M4) lo haga cumplir en el despacho, no en cada
  herramienta.
- `cloudflared` y la escucha del túnel exponen el aparato a Internet: correcto que vayan
  por procesos aparte con backoff (§1.5), pero su tráfico termina en el mismo cupo de B.

### 8.2 Propuestas

| Id | Problema | Evidencia | Propuesta | Costo | Riesgo | Métrica |
|---|---|---|---|---|---|---|
| **N1** | Un cupo para todos en B | `SignalingServer.cpp:272-378`, `782` | **Cupos por escucha** (admin con lugares reservados que nada externo puede usar; túnel y LAN con su tope) y **por IP**; handshake más corto para lo externo; la conexión SSE de A por el puerto admin nunca rechazada. | S | Bajo | Prueba de inundación (1 000 conexiones lentas por el túnel) → la app sigue conectada, 0 «sala muerta» |
| **N2** | Hilo por conexión | §2.3, §8.2 | Mover las conexiones HTTP/WS a un bucle de eventos con un pool fijo (o delegar todo lo HTTP externo en `ba-web`, que ya tiene workers, y dejar a B solo con el loopback autenticado). | M-L | Medio | Hilos de B estables bajo carga; memoria acotada |
| **N3** | Superficie de red de B en un solo proceso | §2.3 | Terminar **todo** el tráfico externo (túnel, LAN HTTPS) en `ba-web`, que reenvía a B por loopback solo lo autenticado; a mediano plazo, TURN y WireGuard en un hijo propio de B (como `ba-pinggy`). | M | Medio | Caídas de B atribuidas a red externa = 0 en pruebas de fuzzing (`ataques.py` ampliado) |
| **N4** | Herramientas de aire al alcance de cualquier agente | §3.5; `Config/mcp-aire.json` | Política aplicada en el despacho por clase (M4): las de clase `aire` requieren rol y, en vivo, confirmación o una ventana de «modo aire permitido»; límite de frecuencia por sesión MCP. | S (con M4) | Bajo | 0 herramientas de aire sin clase; test de política |

---

## 9. Tabla consolidada de propuestas

| Id | Área | Propuesta (corta) | Costo | Riesgo | Métrica principal |
|---|---|---|---|---|---|
| S1 | Procesos | `ba-emisor`: codificación/grabación/transmisión fuera de A, con placa de respaldo | L | Medio | 0 desconexiones RTMP al matar A |
| S2 | Procesos | Superres, CLAP y NDI aislables fuera de A | L | Medio | Caídas de A por módulo |
| S3 | Procesos | Modo seguro escalonado | M | Bajo | Programa restaurado en el cortacircuito |
| S4 | Procesos | Olvidar el backoff tras 120 s estable | S | Muy bajo | Espera 0 s en caídas espaciadas |
| S5 | Procesos | No readoptar otra versión; slots de salud únicos | S | Bajo | Test de readopción |
| S6 | Procesos | Banco de caos y RTO medido | M | Bajo | RTO p50/p95 |
| C1 | Concurrencia | Retorno de audio de B con plazo absoluto y nivel objetivo | S | Bajo | Ocupación ≤ 30 ms, 0 overrun |
| C2 | Concurrencia | Hilo de control dueño del runtime; MCP despachado por clase | L | Medio | Control y alarmas con UI trabada |
| C3 | Concurrencia | Escritor de persistencia fuera de la UI | S | Bajo | 0 E/S en el hilo principal |
| C4 | Concurrencia | Menos copias al invitado (BGRA en GPU, ref de buffer, I420 por resolución) | M | Bajo-medio | Copias por cuadro 5 → 2 |
| C5 | Concurrencia | `FrameService` chico y fuera de la UI | M | Bajo | `poll` p99 < 1 ms |
| C6 | Concurrencia | `mutex` común en el motor | S-M | Medio | Suite verde |
| C7 | Concurrencia | Registro sin candado global en hilos de tiempo real | S-M | Bajo | `registrar()` < 50 µs |
| C8 | Concurrencia | Timbre entre procesos en vez de sondeo a 4 ms | M | Medio | Despertares/s, CPU ociosa |
| C9 | Concurrencia | Nombres para todos los hilos | S | Nulo | 100 % con nombre |
| I1 | IPC | Configuración de B en caliente por la API | M | Medio | Reinicios de B por ajuste |
| I2 | IPC | Catálogo y versión de todos los protocolos | M | Bajo | Tests de contrato |
| I3 | IPC | Contadores de anillos y canales publicados | S | Nulo | Alarma de overrun |
| I4 | IPC | Corregir cabeceras falsas | S | Nulo | §13 sin filas de IPC |
| I5 | IPC | Archivos de encuentro atómicos | S | Bajo | 0 JSON truncados |
| P1 | Persistencia | Escritor único con durabilidad «crítica» + test que prohíbe `ofstream` | S-M | Bajo | 0 archivos vacíos tras cortes simulados |
| P2 | Persistencia | Estado de aire en dos generaciones; daño → HOLD | S | Bajo | Test de corrupción |
| P3 | Persistencia | Un documento de estado de aire por salida | M | Medio | Corte en cada transición → vuelve igual |
| P4 | Persistencia | `esquema` y migraciones; registro de `ajustes` | M | Bajo | 100 % versionado |
| P5 | Persistencia | Tramos de grabación y copia por `tee` | S | Bajo | MKV recuperable tras `kill -9` |
| M1 | Deuda | Failover real de codificador y rearranque de grabación | S-M | Bajo | Vuelta < 5 s / < 10 s |
| M2 | Deuda | Runtime fuera de `StudioViewModel`; ViewModels por área | M | Medio | < 1 500 líneas; runtime sin QML |
| M3 | Deuda | Migrar tipos lógicos viejos y borrar la traducción | M | Medio | 0 claves sin efecto |
| M4 | Deuda | Registro declarativo MCP, docs generadas, build que falla con deriva | M-L | Bajo/medio | Deriva 0 |
| M5 | Deuda | Borrar `OperatorEngine` y ajustes muertos | S | Nulo | 0 «libobs» |
| M6 | Deuda | Test de documentación contra el registro | S | Nulo | §13 vacía |
| M7 | Deuda | Suite de escenarios de sistema | M | Bajo | Escenarios verdes |
| O1 | Observabilidad | Métricas estructuradas y `/metrics` | M | Bajo | SLIs en panel |
| O2 | Observabilidad | Volcados en macOS/Linux | M | Bajo | % de caídas atribuidas |
| O3 | Observabilidad | Vigía del bucle de eventos de la UI | S | Nulo | p99 < 50 ms |
| O4 | Observabilidad | Id de correlación entre procesos | S-M | Nulo | Traza de punta a punta |
| O5 | Observabilidad | Alarmas fuera de la UI; alarma «UI trabada con aire» | S | Bajo | Alarma probada |
| O6 | Operación | No actualizar con aire | S | Bajo | Test del actualizador |
| N1 | Seguridad | Cupos por escucha y por IP en B | S | Bajo | 0 «sala muerta» bajo inundación |
| N2 | Seguridad | Bucle de eventos/pool en vez de hilo por conexión | M-L | Medio | Hilos estables |
| N3 | Seguridad | Todo lo externo terminado en `ba-web` | M | Medio | Fuzzing sin caídas de B |
| N4 | Seguridad | Política de herramientas de aire en el despacho MCP | S | Bajo | 0 herramientas sin clase |

---

## 10. Top 10 priorizado

Criterio: (impacto en el aire × probabilidad) / costo, con preferencia por lo que
habilita lo siguiente. Los cinco primeros son baratos y cierran agujeros concretos;
del 6 en adelante son los cambios de estructura.

| # | Id | Qué | Por qué primero | Costo |
|---|---|---|---|---|
| 1 | **C1 + I3** | Arreglar el bucle del retorno de audio en B y publicar los contadores del anillo | Es probablemente una pérdida de audio/latencia **activa hoy** (`MediaPump.cpp:516-579`); se confirma leyendo `overrunCount` y se arregla en días | S |
| 2 | **M1** | Failover real de codificador y rearranque automático de grabación | La documentación ya lo promete y no existe (§13); es la falla de salida más común | S-M |
| 3 | **P1 + P2 + C3** | Escritor atómico y durable único, estado de aire en dos generaciones con HOLD ante daño, fuera del hilo de la UI | Es la promesa T47; hoy un archivo dañado equivale a «nada al aire» en silencio | S-M |
| 4 | **S4 + S5** | Backoff que se olvida y readopción con versión | Dos líneas de lógica que quitan 10 s de negro por caída en eventos largos | S |
| 5 | **N1** | Cupos de conexión por escucha/IP en B, admin reservado | Cierra un camino barato a «sala muerta» desde Internet | S |
| 6 | **O1 + O3 + C9** | Métricas, vigía del bucle de la UI y nombres de hilos | Sin esto no se pueden validar ni priorizar los cambios grandes (Apéndice A entero) | M |
| 7 | **S1** | `ba-emisor`: la salida sobrevive a la caída de A | El mayor salto de disponibilidad posible; las piezas (canales, elementos IPC, reloj QPC) ya existen | L |
| 8 | **C2 + O5** | Plano de control y alarmas fuera del hilo de la UI | Saca al aparato del estado «en vuelo sin mandos» | L |
| 9 | **S3 + S2** | Modo seguro escalonado, después aislar superres/CLAP/NDI | Ataca la causa de las caídas de A en vez de su síntoma | M → L |
| 10 | **M4 + M2** | Registro MCP declarativo (con despacho por clase) y runtime fuera de `StudioViewModel` | Habilita C2 y N4, y alinea el código con el MVC y con la documentación | M-L |

Secuencia sugerida: 1-5 en paralelo (son independientes y chicos); 6 antes de 7-8
para tener la línea de base; 7 y 8 pueden ir en paralelo porque tocan capas distintas
(salidas contra control); 9 y 10 se intercalan con el resto del trabajo.

---

## Apéndice — Qué medir primero

Del Apéndice A del relevamiento, los datos que deciden prioridades (con la herramienta
que ya existe o la que propone este documento):

| Dato | Por qué decide | Cómo |
|---|---|---|
| Ocupación y `overrunCount` del anillo de retorno A→B (A.5) | Confirma o descarta el problema 1 del top | Leer la cabecera desde B (I3); 1 h con 8 invitados en Windows y Mac |
| RTO de A (A.12, A.15) | Dimensiona el valor de S1 y S4 | Banco de caos (S6) |
| Esperas del hilo de la UI (A.13) | Dimensiona C2/C3/C5 | Vigía O3 |
| Despertares y hilos por proceso (A.6, A.7) | Dimensiona C8 | `sample`/ETW con C9 |
| Supervivencia de `al-aire.json` sin `fsync` en exFAT (A.14) | Valida P1/P2 | Corte real de alimentación con disco USB, 20 repeticiones |
| Copias GPU↔CPU por rama (A.8) | Valida C4 | `motor_mapa` antes/después |
