# Plan unificado de mejoras (04)

Plan de arquitectura que une los dos dictámenes sobre el sistema relevado en
[`01-relevamiento.md`](01-relevamiento.md) (citado «§sección»):

- [`02-ingenieria-de-sistemas.md`](02-ingenieria-de-sistemas.md): ids **S**, **C**,
  **I**, **P1-P5**, **M**, **O**, **N** (en este documento, «02-P1» para no
  confundirlo con el P01 de 03).
- [`03-hardware-y-sonido.md`](03-hardware-y-sonido.md): ids **P01-P16**, **V1-V10**,
  **D1-D14**.

Las afirmaciones decisivas se volvieron a mirar en el código de `master` del
2026-10-31 (sin `.claude/worktrees/`); se cita `archivo:línea`. Los números de
latencia son los de 03 (configurados más estimados) y están para **medirse**: cada
ítem dice cómo.

Decisiones del dueño que el plan toma como restricción: todo portable y vendoreado
(nada del sistema en tiempo de ejecución salvo las API del propio sistema
operativo); todo en GStreamer (sin libobs, sin vuelta atrás); MVC (la vista se
entera por el modelo); causa de fondo, no parches; N salidas = N operadores
completos; corte de luz = todo vuelve solo (la línea de tiempo vuelve en HOLD y
pregunta «¿qué está pasando?»); una dependencia faltante es un error de
configuración; pruebas de audio solo con señales sintéticas, nunca por parlantes.

## Índice

0. [Resumen ejecutivo](#0-resumen-ejecutivo)
1. [Verificación en el código](#1-verificación-en-el-código)
2. [Coincidencias y tensiones](#2-coincidencias-y-tensiones)
3. [Principios de arquitectura](#3-principios-de-arquitectura)
4. [Plan por fases](#4-plan-por-fases)
   - [Fase 0 — Arreglos chicos de alto impacto](#fase-0--arreglos-chicos-de-alto-impacto)
   - [Fase 1 — Disponibilidad](#fase-1--disponibilidad)
   - [Fase 2 — Latencia y audio](#fase-2--latencia-y-audio)
   - [Fase 3 — Reestructura](#fase-3--reestructura)
   - [Fase 4 — Observabilidad y operación continua](#fase-4--observabilidad-y-operación-continua)
5. [Presupuesto de latencia y metas de disponibilidad](#5-presupuesto-de-latencia-y-metas-de-disponibilidad)
6. [Qué NO hacer (o no todavía)](#6-qué-no-hacer-o-no-todavía)
7. [Mapa de ids de 02 y 03 a este plan](#7-mapa-de-ids-de-02-y-03-a-este-plan)

---

## 0. Resumen ejecutivo

**Diagnóstico común.** Los dos dictámenes llegan, por caminos distintos, a las mismas
cinco cosas, y el código las confirma:

1. **El retorno de audio a los invitados tiene una falla activa.** El hilo de B saca un
   bloque de 10 ms y duerme 10 ms *relativos* (`src/gateway/MediaPump.cpp:516-579`): lee
   menos de lo que A escribe, el anillo se llena (160 ms) y el escritor descarta bloques.
   Resultado probable: ~150 ms de más y cortes de 10 ms en la voz del retorno (en Windows,
   peor). Y cuando el mix-minus tiene un hueco **o todavía no abrió**, el invitado oye el
   programa entero, con su propia voz (`MediaPump.cpp:527-529`).
2. **Las salidas no se curan solas**: `atenderEncoderCaido` está vacía
   (`src/app/emision/BroadcastEngine.cpp:1863-1865`) aunque el comentario de quien la llama
   promete lo contrario (`src/app/arranque/OperateMode.cpp:780`), y una grabación con error
   queda muerta (`src/motor/Salidas.cpp:527-542`).
3. **«Corte de luz = todo vuelve solo» descansa en archivos frágiles**: `al-aire.json` sin
   `fsync`, `config::guardar` que trunca en el lugar (`src/app/arranque/ArchivoConfig.cpp:16-22`)
   y un archivo dañado se lee como «no había nada al aire» y no avisa (`OperateMode.cpp:983-986`).
4. **El aire comparte proceso con la UI, el MCP, los agentes y el código nativo de terceros**
   (CLAP, superres, NDI), y el hilo de la UI es además el plano de control.
5. **Una sola latencia del estudio gobierna todos los caminos**: los oídos (auriculares,
   retornos) esperan al video, hasta 250 ms más márgenes.

**Criterio.** En vivo: primero que el aire no se corte, después latencia, después todo lo
demás. Se mantiene el orden con dos precisiones: el **retorno de los invitados cuenta como
aire** (un invitado que no se oye bien corta el programa en los hechos) y los **mandos y
alarmas** van inmediatamente después del aire (un aire sin mandos está a un paso del corte).

**Plan en cinco fases.**

| Fase | Qué resuelve | Esfuerzo | Lo más importante |
|---|---|---|---|
| 0 | Fallas activas y agujeros baratos | ~2 semanas | retorno de B con plazo absoluto y nivel objetivo, sin caída al programa entero; backoff que se olvida y que no frena la vigilancia de B; cupos de conexión; contadores |
| 1 | Que nada se quede muerto | ~6-8 semanas | escritor durable único, estado de aire en dos generaciones y HOLD ante daño; rearranque de grabación y cambio de codificador; un codificador por salida con `tee`; energía y UPS; modo seguro escalonado; banco de caos |
| 2 | Latencia y calidad de audio | ~6-8 semanas | dos clases de buses (programa sincronizado / oído con latencia fija); mix-minus por salida; latencia del rack informada; deriva de placas; prioridad de tiempo real acotada; menos copias al invitado |
| 3 | Que una falla ajena no corte el aire | ~3-4 meses | `ba-emisor` por salida (la transmisión y la grabación sobreviven a la caída de A); plano de control fuera del hilo de la UI con registro MCP declarativo; documento único de estado de aire; aislar lo que el historial de caídas señale |
| 4 | Ver y sostener | continuo | métricas con nombre, correlación entre procesos, escenarios de sistema nocturnos, térmica, red |

**Qué se gana (estimado, a confirmar con las mediciones de cada ítem).**

- Retorno a invitados: aporte del estudio de **~270-305 ms → ~125-150 ms (fase 0) →
  ~55-95 ms (fase 2)**, sin cortes ni eco propio.
- Auriculares del operador: **~145-160 ms → ~35-50 ms** (fase 2).
- SRT de contribución: **~560-600 ms → ~340-380 ms** (fase 2); el emisor de la fase 3 le
  suma 30-50 ms a cambio de no cortarse nunca por una caída de A.
- Disponibilidad: después de la fase 1, ninguna falla de codificador, grabación, archivo de
  estado o corte de luz deja una salida muerta en silencio; después de la fase 3, una caída
  de A deja de desconectar la transmisión y de partir la grabación (la plataforma ve una
  placa unos segundos).

**Qué no se hace (por ahora):** codificar el retorno una vez para WebRTC (V1), texturas
compartidas entre procesos (V4), separar ya la UI en otro proceso (D6, se reemplaza por
plano de control en hilo propio + emisor), renombrar herramientas MCP antes de la ventana de
reentrenamiento, RealtimeKit/NUT/upower (servicios del sistema), watchdog de hardware de
fábrica.

---

## 1. Verificación en el código

Lo que entra al plan y se volvió a mirar:

| Afirmación (02 / 03) | Resultado | Evidencia |
|---|---|---|
| El hilo de retorno de B saca un bloque por vuelta y duerme 10 ms relativos (C1 / P01) | **Confirmado.** `reader.pop()` una vez, un `pop` por slot de mix-minus, IFB, prueba de eco, `pushLocalAudio` por sesión y al final `sleep_for(10 ms)` | `src/gateway/MediaPump.cpp:516-579` |
| Ante anillo lleno el escritor descarta (overrun) y nadie lee el contador | **Confirmado.** `overrunCount` solo se incrementa (`SharedAudioRing.cpp:210`) y se pone en cero (`:121`); ningún lector fuera de tests | `src/core/ipc/SharedAudioRing.hpp:84-86`, `141-146` |
| Sin temporizador de alta resolución en Windows (P02) | **Confirmado.** Ni `timeBeginPeriod`, ni `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, ni MMCSS en `src/` | grep |
| Hueco del mix-minus → programa entero (P03) | **Confirmado y más amplio**: también pasa cuando el slot **no abrió** (`deSlot[s] == nullptr` → `fuente = blk.samples`) | `MediaPump.cpp:527-529`, `546-549` |
| Mix-minus solo en la salida por omisión (P04) | **Confirmado** | `src/motor/Motor.hpp:48-53` («POR AHORA siguen a la salida por omisión»); `MediaPump.cpp:470-472` |
| `atenderEncoderCaido` vacía (M1 / D2) | **Confirmado**; el que la llama dice «se reemplaza y se rearranca» | `BroadcastEngine.cpp:1863-1865`; `OperateMode.cpp:780-781` |
| Grabación con error no se rearranca (M1 / D1) | **Confirmado**: `Grabacion::tic` registra el error y nada más | `src/motor/Salidas.cpp:527-542` |
| `al-aire.json` sin `fsync`; `config::guardar` trunca (02-P1 / D3) | **Confirmado** | `OperateMode.cpp:959-981`; `ArchivoConfig.cpp:16-22` |
| JSON dañado = «nada al aire» en silencio (02-P2) | **Confirmado**: `leerObjeto` devuelve `{}` y `restoreOutputs` sale sin avisar | `ArchivoConfig.cpp:9-14`; `OperateMode.cpp:983-987` |
| Backoff que no se olvida (S4) | **Confirmado** (solo `forceRestart` pone en cero) y **además**: el backoff se duerme **dentro del bucle del watchdog** (`restartChild` → `sleepInterruptible(delay)`), así que mientras A espera hasta 10 s **B no se vigila ni se relanza**, y viceversa | `src/launcher/Supervisor.cpp:300-320`, `474-475`, `483-560` |
| Cupo único de 64 conexiones en B (N1) | **Confirmado** | `src/gateway/SignalingServer.cpp:782` |
| La vista no sincroniza (`appsink sync=false`) (03 §2.1) | **Confirmado** | `src/motor/Vistas.cpp:135` |
| `barack` no contesta LATENCY (P08) | **Confirmado**: no hay manejo de consultas en `GstRack.cpp` | grep |
| Reanclaje de la hora de audio a 50 ms (P09) | **Confirmado** | `src/motor/Piezas.cpp:711-719` |
| `registrar()` hace `fprintf+fflush` bajo candado global (C7) | **Confirmado** | `src/core/diag/Registro.cpp:204-215` |
| Margen de salidas 300 ms para todo (P07) | **Confirmado** | `Salidas.cpp:56` |

Hallazgos nuevos de esta revisión (entran al plan):

- **H1 — El backoff frena la supervisión del otro hijo** (fila de arriba). Va con S4 en F0-05.
- **H2 — Las transmisiones de las salidas que no son la principal no se retoman tras un
  corte** («sus transmisiones son destinos: no se retoman, como siempre»,
  `OperateMode.cpp:988-990`), y `al-aire.json` solo guarda `streaming` para la principal
  (`OperateMode.cpp:966-969`). Choca con «N salidas = N operadores completos» y con «todo
  vuelve solo». Va en F1-04.
- **H3 — Cuarta forma de escribir estado**: `Runtime/linea-aire.json` (la corrida de la
  línea de tiempo) se escribe cada segundo desde el hilo de la UI con `QSaveFile`
  (`src/app/ui/EventoViewModel.cpp:2067-2077`): atómico, pero la durabilidad depende de Qt y
  no hay dos generaciones. Va en F1-01/F1-03.

Nada de lo decisivo de 02 o 03 quedó sin confirmar. Lo que sigue **sin medir** (y por eso
la fase 0 empieza midiendo) es cuánto se llena el anillo en régimen y cuánto audio se
pierde en Windows (Apéndice A.5 de 01).

---

## 2. Coincidencias y tensiones

### 2.1 Criterio para resolver

1. **Que el aire no se corte**: programa, grabación, transmisión y **retorno de los
   invitados** (forma parte del programa en una conversación).
2. **Que se pueda operar**: mandos, alarmas, control remoto. Con aire en curso, un aparato
   sin mandos no se corta, pero tampoco se puede salvar si algo más falla.
3. **Latencia**, en este orden: oídos en vivo (retorno, IFB, auriculares) > video del
   operador > salidas de contribución (SRT) > salidas a plataformas (RTMP, que agregan
   segundos).
4. **Todo lo demás**: CPU, calor, deuda, prolijidad.

Regla de desempate: un costo de latencia se acepta si cae en un camino que **ya tiene
margen** (RTMP, grabación) o si se compensa en la misma fase.

### 2.2 Coincidencias (más peso)

| Tema | 02 | 03 | Evidencia (01) | Verificado | Dónde va |
|---|---|---|---|---|---|
| Retorno de audio de B lento → anillo lleno, descartes, latencia | C1, I3 | P01, P02 | §3.3, §5.5, A.5 | sí | F0-01..03 |
| Codificador caído no se reemplaza | M1 | D2 | §7.1, §13 | sí | F1-06 |
| Grabación caída no vuelve | M1 | D1 | §7.1 | sí | F1-05 |
| Persistencia sin `fsync` / truncada; T47 frágil | 02-P1, 02-P2, C3 | D3 | §1.6, §7.4, A.14 | sí | F1-01..03 |
| Grabación en un archivo; tramos | 02-P5a | D4 | §4.10, §13 | sí | F1-09 |
| Copia de grabación con segundo codificador | 02-P5b | V6 | §4.10 | sí | F1-07 |
| Aire en el mismo proceso que UI/MCP/plugins | S1, S2, C2 | D6 | §1.1, §2.2, §3.5 | coherente | F3-01, F3-03, F3-05 |
| Copias del programa al invitado | C4 | V2 (y V1) | §3.7 | sí (`Vistas.cpp:50-90`) | F2-13 |
| Sondeo a 4 ms en decenas de hilos | C8 | V8, P02 | §3.3, §12.1 | sí | F2-14, F3-13 |
| Registro con candado global en hilos de medios | C7 | §3.3, condición de P12 | §8.1, §8.2 | sí | F2-08 |
| Medir la retoma tras caída (RTO) | S6 | D12 | A.12, A.15 | — | F1-14, F3-10 |
| Contadores y métricas del transporte | I3, O1 | P10, §9 | A.5, A.10 | sí | F0-02, F4-01 |

Lo que solo dice uno (y no contradice al otro): 02 aporta supervisión (S3-S5),
seguridad de B (N1-N4), contratos y configuración en caliente (I1, I2), deuda MVC y MCP
(M2-M4), observabilidad (O2-O6) y el documento único de aire (02-P3). 03 aporta todo lo de
oído y reloj (P03-P16), GPU y video (V3, V5, V7, V9, V10) y la capa física de la
disponibilidad (D5, D7-D14).

### 2.3 Tensiones y cómo se resuelven

| # | Tensión | Postura de 02 | Postura de 03 | Resolución | Por qué |
|---|---|---|---|---|---|
| T1 | **Aislar procesos suma IPC y latencia** | S1: emisor aparte, +1 copia y +40-50 ms; acepta | V1/V2/P05 bajan latencia; D6 lo pide igual | Se aísla, pero **solo donde el costo cae en un camino con margen**: el emisor agrega 30-50 ms a RTMP/SRT/grabación, que ya esperan +300 ms; F2-06 (margen por tipo) los devuelve con creces en SRT. Los caminos de oído **nunca** cruzan un proceso más de lo que cruzan hoy | Criterio 1 > 3, y en la misma fase o antes se recupera la latencia |
| T2 | **Qué se separa primero: el emisor (S1) o la UI (D6)** | S1 + C2 (control en hilo) | D6: motor + salidas en un proceso de aire, UI/MCP en otro | **Primero el emisor (F3-01) y el plano de control en un hilo propio (F3-03); la UI como proceso aparte queda condicionada** a lo que digan los volcados (F1-12) | El emisor protege la salida contra **cualquier** caída de A (incluidas las del motor: el heap de d3d12 y ONNX/DML del historial cayeron en pipelines, no en QML). D6 deja el código de terceros del motor en el proceso de aire. El hilo de control da el 80 % del beneficio de D6 («sin mandos») sin IPC nuevo |
| T3 | **Colas largas dan robustez, pero latencia** | anillo 160 ms es capacidad; nivel 20-30 ms | cola cruda de grabación 3 s → 0,5-1 s; colas de transmisión coherentes | **Capacidad grande, nivel de operación chico y controlado.** El anillo A→B conserva 160 ms de capacidad y se opera a 2-3 bloques (F0-01). El colchón contra disco es el **comprimido** (60 s / 512 MB): se queda; la cola cruda baja a 1 s (F0-10). En los caminos de oído no hay cola que «espere»: lo tardío no entra (F2-01) | La robustez la dan la capacidad y los contadores; la latencia la da el nivel. Separar las dos cosas resuelve el choque |
| T4 | **Una latencia del estudio (sincronía) vs oídos rápidos** | — | P05: buses de oído a ~25 ms fijos; P14: alinear labios del retorno | **Dos clases de buses**: *programa* (sincronizado con L_e: grabación, transmisión, pantallas) y *oído* (latencia fija corta: mix-minus, IFB, escucha en vivo, PFL). Para quien **conversa** (invitado con mix-minus) gana la latencia aunque el audio llegue antes que el video del programa; para quien **mira** (pantallas de sala, espectadores, consola) se alinea por sellos (F2-11) | El invitado que habla necesita < 150 ms boca-oído (G.114); el que mira necesita labios (BT.1359). Son públicos distintos, no se los obliga a compartir camino |
| T5 | **Prioridad de tiempo real vs más cambios de contexto** | C7 primero (sin candados en hilos de medios) | P12: MMCSS / time constraint en hilos de audio | Tiempo real **solo** en 5-6 hilos de audio con plazo, **después** de sacarles candados y E/S (F2-08 → F2-09). Nunca a todos los hilos de streaming | La medición del propio código (`Prioridad.cpp:280-286`) mostró que subir todo empeora; con un candado global adentro, el tiempo real causa inversión de prioridad |
| T6 | **Durabilidad (`fsync`) cuesta milisegundos** | escritor en hilo propio (C3) | `F_FULLFSYNC` en Mac (más caro) | `fsync` (y `F_FULLFSYNC`) **solo** para la clase *crítica* (estado de aire, corrida, credenciales), **en el hilo escritor** con fusión por archivo; nunca en el hilo de la UI ni en uno de medios | El costo se paga donde no molesta |
| T7 | **Copia de la grabación: ¿contra el disco o contra el codificador?** | `tee` del flujo codificado de fábrica; 2.º codificador opcional | V6 igual | **De fábrica `tee`** (protege del disco, lo más frecuente) + **cambio de codificador** (F1-06) para la falla de codificador. El 2.º codificador queda como opción «independiente» explícita | Una sesión de codificador menos por salida evita el tope de NVENC (otra causa de caída); el failover cubre lo que el 2.º codificador cubría |
| T8 | **Consolidar herramientas MCP vs modelos entrenados** | M4: registro declarativo y consolidar familias | — | **Registro declarativo ya** (F3-02), con docs generadas y build que falla por deriva; **sin renombrar ni consolidar** hasta la ventana de reentrenamiento (al terminar las funciones). Los nombres nunca se traducen | Renombrar rompe los modelos v2/v3 entrenados; el registro da el despacho por clase (control y política de aire) sin tocar nombres |
| T9 | **Cola de audio de transmisión: ¿leaky o no?** | no leaky está bien (no perder audio) | las dos leaky o una sola | **Misma política y tope para video y audio (leaky, 1 s)** (F2-16). Con la red trabada más de 2 s la transmisión se rearma igual (`stream.sinDatosS`); lo que importa es que A/V vuelvan alineados | Perder audio con la red trabada es inevitable; una cola bloqueante solo agrega discontinuidad a la vuelta |
| T10 | **Deriva de placas: remuestreo adaptativo o reloj = placa** | — | P09 (DLL + libsamplerate) o reloj del estudio = placa principal | **Medir primero (F2-10a), después DLL + remuestreo variable** como elemento propio. «Reloj = placa» queda postergado | Con invitados, cámaras IP y varias salidas, un reloj de sistema monotónico es más predecible; el remuestreo variable resuelve cualquier cantidad de placas |
| T11 | **Portabilidad vs ayudas del sistema** | crashpad vendoreado | RealtimeKit por D-Bus, NUT/upower, DeckLink, `gst-ptp-helper` con permisos | Se usan **solo API del sistema operativo** (MMCSS, `thread_policy_set`, `sched_setscheduler`, `IOPSCopyPowerSourcesInfo`, `GetSystemPowerStatus`, `/sys/class/power_supply`). **No** RealtimeKit, NUT, upower ni drivers de fabricante como dependencia. Volcados: manejador propio mínimo antes que crashpad | Regla del dueño: nada del sistema en runtime; lo que falta es error de configuración, no un «si está» silencioso |
| T12 | **`OperatorEngine`: ¿reusar o borrar?** | M5: borrarlo (resto de libobs) | D6: reutilizarlo como proceso de aire | **Borrarlo** (F3-12). `SnapshotStore` se queda y pasa a ser la base del escritor durable (F1-01) | Es de la época libobs («callback de audio de libobs»); el emisor y el hilo de control no lo necesitan |
| T13 | **Modo seguro vs «todo vuelve como estaba»** | S3: modo seguro escalonado | — | Escalonado, y en los niveles 1-2 **se restauran las escenas** (F1-10) | El modo seguro actual (escenas vacías, x264) es lo opuesto de «vuelve como estaba» |

---

## 3. Principios de arquitectura

1. **El aire primero, y aislado por evidencia.** Ninguna pieza que no sea motor + salidas
   puede cortar la transmisión ni partir la grabación. Qué se aísla lo decide el historial
   de caídas atribuidas (volcados), no la intuición; el costo de latencia del aislamiento
   solo se paga en caminos con margen.
2. **Dos relojes de espera.** *Programa* espera a la latencia del estudio para que A/V
   salgan juntos; *oído* tiene latencia fija y corta, y lo que llega tarde no entra. Un oído
   en vivo nunca espera al video.
3. **Capacidad grande, nivel chico, contadores a la vista.** Toda cola o anillo tiene un
   nivel objetivo, se drena con plazo absoluto (`sleep_until`, nunca `sleep_for` relativo
   en un bucle de ritmo) y publica ocupación, overrun y underrun.
4. **Durabilidad uniforme.** Un solo escritor en `ba_core` con dos clases (normal,
   crítica). Estado del aire = crítico, en dos generaciones con suma; **dañado ≠ vacío**:
   ante daño, HOLD de todas las salidas y la pregunta de siempre.
5. **Hilos con un solo oficio.** La UI dibuja; el plano de control (estado, alarmas, MCP,
   guardado) vive en su hilo y publica fotos; los hilos de medios no toman candados
   globales ni hacen E/S; tiempo real solo para los pocos hilos de audio con plazo.
6. **Toda falla tiene recuperador, y la documentación no promete lo que el código no
   hace.** Cada mecanismo de recuperación tiene una prueba de inyección; cada comentario
   que promete algo, un test que lo verifica (o se borra).
7. **Medir antes y después.** Cada cambio llega con su métrica (contador, `sistema_recursos`,
   `tiempos_*`, `motor_mapa`) y una prueba sintética, sin parlantes; la línea de base se
   toma **antes** de arreglar.
8. **Portable y declarado.** Solo lo vendoreado y las API del sistema operativo; una pieza
   que falta frena el build (`ba_falta`), nunca degrada en silencio.

---

## 4. Plan por fases

Convenciones: costo S = días, M = 1-3 semanas, L = más de un mes. «Dep.» = dependencia.
Las herramientas MCP citadas (`sistema_recursos`, `tiempos_pipeline`, `tiempos_medir`,
`motor_mapa`, `audio_sincro_medir`, `video_sincronia`, `app_quit`) se usan con la app en
`--test`; después de cada prueba, `app_quit` y cerrar los procesos (no dejar la app
corriendo). Toda prueba de audio con `audiotestsrc`, anillos leídos por el test,
`fakesink`/`appsink`: **nada por los parlantes**.

### Fase 0 — Arreglos chicos de alto impacto

Objetivo: cerrar las fallas activas y tomar la línea de base. Se pueden hacer ya, en
paralelo, salvo el orden 02 → 01.

| Id | Qué | Dónde | Por qué | Ganancia esperada | Costo | Riesgo | Dep. | Cómo se verifica |
|---|---|---|---|---|---|---|---|---|
| **F0-02**<br>*Estado: hecho (2026-10-31): `ipc/MedidaTransporte.hpp`; foto en `uso.transporte` de la sala y en `sistema_recursos.transporte`; al registro solo lo anómalo. Falta la hora con 8 invitados* | **Contadores del transporte, primero** (para tener la línea de base): ocupación, `overrunCount`, `underrunCount` y edad p50/p99 de cada anillo y canal, al registro cada 10 s, a la foto de B y a `sistema_recursos` | `src/core/ipc/SharedAudioRing.hpp:84-86`; `src/gateway/MediaPump.cpp` (hilos de bajada); `src/core/diag/Recursos.cpp` | 02 I3, C1; 03 P01, §9; 01 §3.1, A.5 | Diagnóstico: confirma o descarta la falla nº 1 en minutos | S | Nulo | — | Antes de F0-01: 1 h con 8 invitados sintéticos (Chrome con medios falsos) en Mac y en la PC Windows; anotar ocupación y overrun/min |
| **F0-01**<br>*Estado: hecho el resto (2026-10-31): sin alinear por sello, a propósito (`gateway/RetornoLugar.hpp`: el programa es el metrónomo; alinear contra él les devolvería la demora a los buses de oído de F2-01); `test_retorno_ritmo` (3 s; `BA_RETORNO_RITMO_S=60`, la larga)* | **Retorno de B con plazo absoluto y nivel objetivo**: `next += 10 ms; sleep_until(next)` (como `PullAdm`, `Webrtc.cpp:213-230`); si la ocupación pasa de 3 bloques, descartar el excedente con fundido; leer programa y mix-minus **por sello QPC**, no «uno por vuelta». El mismo arreglo en el audio del operador y las voces | `MediaPump.cpp:463-579`, `765`, `806`, `858` | 02 C1; 03 P01; 01 §3.3, §5.5 | −130 ms en retorno e IFB; sin cortes de 10 ms | S | Bajo | F0-02 | Test nuevo `test_retorno_ritmo` (escritor sintético 100 bloques/s, 60 s): overrun = 0, ocupación ≤ 3 bloques; prueba de eco de la app (Goertzel, `MediaPump.cpp:557-575`) antes/después; F0-02 en vivo |
| **F0-03**<br>*Estado: hecho, sin probar en Windows (2026-10-31): `platform/Ritmo` en los hilos de B y la Matriz; `sinAhorroDeEnergia` siempre pide respetar la resolución. Falta `test_retorno_ritmo` en la PC Windows* | **Temporizador de alta resolución en Windows** para los hilos de ritmo de B y el de la Matriz: `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)` y `SetProcessInformation(ProcessPowerThrottling)` sin ignorar la resolución | `MediaPump.cpp` (bucles de 10 ms y de 4 ms), `src/app/audio/Matriz.cpp:809-873`; helper en `src/core/platform/` | 02 C1; 03 P02; grep sin `timeBeginPeriod` | Evita perder hasta 1/3 del retorno en Windows; −8 ms medios por salto de video | S | Bajo | F0-01 | `test_retorno_ritmo` en la PC Windows (`ssh windows`); período medido del hilo (p99 ≤ 10,5 ms) |
| **F0-04**<br>*Estado: hecho (2026-10-31): lugar cerrado = «retorno apagado» (silencio) y `retorno: apagado` en el participante, «Retorno apagado» en la tarjeta Invitados; «programa» también arma su anillo (Motor); `test_retorno_ritmo` (correlación con su voz ≈ 0)* | **Mix-minus sin caída al programa entero**: hueco → repetir el último bloque con fundido o silencio; slot que no abrió → «retorno apagado» para ese invitado, visible en la UI por el modelo | `MediaPump.cpp:527-529`, `546-549`; modelo de la sala en la app | 03 P03; verificado y ampliado (slot cerrado) | Sin eco del propio invitado (~0,5 s) | S | Bajo | F0-01 | Test con mix-minus sintético con huecos y slot cerrado: correlación entre la voz sintética del invitado *n* y lo que recibe *n* = 0 |
| **F0-05** (hecho: `Politica.hpp`, `test_politica`) | **Supervisor**: (a) poner en cero `appConsecutive_`/`bConsecutive_` tras `launcher.estableS` (120 s) con latido fresco; (b) **el backoff no duerme dentro del bucle del watchdog**: se agenda el relanzamiento (hora de relanzar por hijo) y el bucle sigue vigilando al otro | `src/launcher/Supervisor.cpp:300-320`, `474-475`, `483-560` | 02 S4; hallazgo H1 | −10 s de negro por caída aislada en eventos largos; B nunca queda sin vigilar por un backoff de A | S | Muy bajo | — | Tests de launcher: 5 caídas espaciadas 10 min → espera 0 s; A en backoff de 10 s + B que se cae → B relanzado en ≤ 250 ms + su backoff |
| **F0-06** (hecho en launcher y B; falta que A publique `ba::ipc::kVersionProceso`, hoy su propia constante en `OperateMode.cpp:71`, y que el build fije `BA_VERSION_PROCESO`) | **Readopción con versión**: comparar `HealthEntry.version`; si difiere, relanzar (con aire: posponer y avisar). Cantidad de slots de `BA.Health` en una constante de `ba_core` | `Supervisor.cpp:249-263`; `src/core/ipc/HealthBlock.hpp:56`; `src/gateway/main.cpp:134-135` | 02 S5; 01 §3.2 (2 o 4 slots) | Evita que un launcher nuevo adopte un A/B viejo tras actualizar | S | Bajo | — | Test: launcher v2 + A v1 vivo → relanzado; con salidas activas → pospuesto con aviso |
| **F0-07** | **Cupos por escucha y por IP en B**; el puerto admin con lugares reservados; la conexión SSE de la app nunca rechazada; handshake más corto para lo externo | `src/gateway/SignalingServer.cpp:272-378`, `782`; `SignalingServer.hpp:604-605` | 02 N1; 01 §2.3, §12.2 | Cierra un camino barato a «sala muerta» desde Internet | S | Bajo | — | Prueba de inundación sintética (1 000 conexiones lentas por el túnel y la LAN): la app sigue conectada, 0 «sala muerta» en 10 min. **Estado (2026-10-31): hecho en código y test** — cupo por escucha (`room.maxConexiones`, 256 cada una), por IP real (`room.maxConexionesPorIp`, 32; la de PROXY v1/XFF), API de control con lugares propios (`room.maxConexionesAdmin`, 32) y sobrecupo solo con token, plazo total de cabecera para lo externo (`room.cabeceraMs`, 5 s) y del cuerpo (`room.handshakeMs`); `src/gateway/tests/test_cupos.cpp` (300 conexiones lentas por el túnel: invitados, puerta, API y SSE de la app siguen; slowloris cortado a los 5 s; 503 por IP). Falta: los 10 min con la app real |
| **F0-08** (hecho: `platform/Hilo`, `<pipeline>/<elemento>` en `Prioridad.cpp`, `diag/VigiaBucle` + `ui/VigiaInterfaz`, `bucle_ui` en `sistema_recursos` y `Studio.latidoUi`, `test_vigia_bucle`; faltan los hilos del launcher y los de `MediaPump`/`SignalingServer`/`BroadcastEngine.cpp`, y medir en uso normal) | **Nombres de hilos** propios y de GStreamer (`<pipeline>/<elemento>` en `STREAM_STATUS_ENTER`) + **vigía del bucle de eventos de la UI** (temporizador de 50 ms que mide su retraso, histograma, último evento/herramienta) | `src/motor/Prioridad.cpp:253-265`; `src/core/diag/`; `StudioViewModel.cpp` (instalar el vigía) | 02 C9, O3; 01 §2.5, A.13 | Línea de base para C2/C3/C5 y para atribuir CPU (memoria «CPU ociosa») | S | Nulo | — | `sample` por hilo: 100 % con nombre; p99 del bucle de eventos registrado en uso normal y con disco USB lento |
| **F0-09** (hecho en los comentarios y docs de §13 de 01 que no son de otros dueños; quedan `Salidas.cpp:54`, `BroadcastEngine.hpp` (`rateControl`, `calidadCrf`, ids de libobs), `Supervisor.hpp:138-140`, `ChildOutput.hpp:5-8` y textos traducibles que nombran libobs/fallbackswitch) | **Comentarios que prometen lo que no existe**: `OperateMode.cpp:780`, `Salidas.hpp:16-17`, `SpoutTransport.hpp:1-25`, `Segmento.hpp:9-11`, `video.cpp:14`, `SharedAudioRing.hpp:18-19` y referencias a libobs | ver §13 de 01 | 02 I4, M6; 03 §5.1 | Nadie (persona o agente) cree que el sistema se cura solo donde no | S | Nulo | — | Filas de §13 de 01 que tocan IPC o recuperación = 0 |
| **F0-10** — hecho 2026-10-31 (sin medir en la app) | **Cola cruda de grabación 3 s → 1 s** (`record.colaEncoderS`), el colchón comprimido queda | `Salidas.cpp:351` | 03 V7; 02 §3.4 | −~200 MB de RAM/VRAM por grabación; menos presión sobre la composición si el codificador se traba | S | Bajo | — | Memoria por proceso (`sistema_recursos`) con 2 grabaciones; prueba con codificador lento sintético: ningún cuadro tarde en el programa |
| **F0-11** — hecho 2026-10-31 (`test_volumen`, `test_alerts`; sin probar en la app) | **Aviso de grabación y copia en el mismo volumen** y **horas restantes** al bitrate actual (grabación + copia + ISO) | `BroadcastEngine.cpp:1641-1703`; alarma `disk` en `AlertEngine` | 03 D10 | Evita perder las dos copias por un solo disco | S | Nulo | — | Test de rutas en el mismo volumen (`st_dev` / número de serie) |

### Fase 1 — Disponibilidad

Objetivo: que ninguna falla deje una salida muerta en silencio y que el corte de luz
vuelva siempre igual o en HOLD con aviso. F1-14 (banco de caos) va primero porque es la
forma de verificar el resto.

| Id | Qué | Dónde | Por qué | Ganancia esperada | Costo | Riesgo | Dep. | Cómo se verifica |
|---|---|---|---|---|---|---|---|---|
| **F1-14** | **Banco de caos mínimo** (nocturno y a pedido): `kill -9`/`SIGSTOP` de A, B, ba-web, cloudflared; `al-aire.json` truncado; codificador falso que da error; sumidero que falla a los N s; servidor RTMP local. Publica RTO p50/p95, hueco en el MKV, reapertura de anillos | `src/core/fault/` (`FaultInjector`); `tests/` (Python) + tests de launcher | 02 S6, M7 (inicio); 03 D12 (medición), §9 | Mide lo que hoy es Apéndice A.12/A.15; verifica todo F1 | M | Bajo | F0-05 | El propio banco; números por versión en el informe. **Hecho (2026-10-31):** `tools/caos/banco.py` (+ `rtmp_local.py`), fallas `diag_falla` (solo `--test`); tests `test_caos_banco`, `test_caos_rtmp`, `test_fallas`, `test_fallas_programadas`. Falta correrlo contra la app real (integrador) |
| **F1-01** — *hecho* (`persist/Escritor`; `test_escritor` mata al escritor en 10 000 escrituras; `tests/test_escritor_unico.py` no deja sumar `ofstream`/`QSaveFile` y la deuda anotada solo baja; falta el corte real en la PC de pruebas) | **Escritor único durable** en `ba_core` (`persist::escribir(ruta, bytes, Normal|Critica)`), sobre lo que ya hace `SnapshotStore`: temporal + `fsync` + `rename` + `fsync` del directorio; Mac `F_FULLFSYNC`; Windows `FlushFileBuffers` + `MoveFileExW(MOVEFILE_WRITE_THROUGH)`. Migran `config::guardar`, `AppSettings`, `writeOnAirState`, `linea-aire.json` (hoy `QSaveFile`), `placa.json` y los archivos de encuentro de `Runtime/`. Test en `tests/` que **prohíbe** `std::ofstream`/`QSaveFile` sobre `Config/` y `Runtime/` fuera del escritor | `src/core/persist/SnapshotStore.cpp:393-450`; `src/app/arranque/ArchivoConfig.cpp:16-22`; `AppSettings.cpp:90-122`; `OperateMode.cpp:959-981`; `EventoViewModel.cpp:2067-2077`; `SeguridadAire.cpp:30` | 02-P1, I5; 03 D3; 01 §7.4; H3 | «Vuelve solo» confiable: 0 archivos vacíos tras corte | S-M | Bajo | — | Test que mata el escritor en mitad de 10 000 escrituras: siempre JSON válido; corte real de alimentación en la PC de pruebas con disco USB exFAT, 20 repeticiones |
| **F1-02** — *hecho* (`persist/ColaEscritura`: fusión, prioridad de aire, `leer` ve lo encolado, reintento; `AppRuntime::stop` y el reinicio de B vacían la cola; falta el contador en `--test`) | **Hilo escritor de persistencia**: fusión por archivo (último gana), prioridad para el estado de aire; el hilo de la UI solo encola | `src/core/persist/` (nuevo); llamadas de `OperateMode.cpp`, `EventoViewModel.cpp` | 02 C3; 03 (costo de `F_FULLFSYNC`) | 0 E/S de disco en el hilo de la UI; resuelve T6 | S | Bajo | F1-01 | Contador en `--test`: 0 `ofstream`/`rename` desde el hilo principal (igual que el vigía del candado); F0-08 antes/después |
| **F1-03** — *hecho* (`persist/Generaciones`; al-aire y linea-aire en A/B; dañado = corte: HOLD de la línea con la pregunta, alarma `persistencia` y registro; aviso FAT/exFAT; el archivo viejo se lee) | **Estado de aire en dos generaciones** con secuencia y SHA-256 (formato `SnapshotStore`) para `al-aire` y `linea-aire`; al leer, la más nueva válida; **si ninguna es válida: HOLD de todas las salidas y la pregunta «¿qué está pasando?»**, alarma y registro; aviso si la carpeta de datos está en un sistema de archivos sin diario (FAT/exFAT) | `OperateMode.cpp:983-1009`; `ArchivoConfig.cpp:9-14`; `src/app/evento/Corrida.cpp:566-600` | 02-P2; 03 D3; 01 §1.6 | El peor modo de falla de T47 (daño = nada al aire, callado) desaparece | S | Bajo | F1-01 | Test de daño: truncar/corromper cada generación → restaura la otra, o HOLD con aviso; MCP: estado de la línea en HOLD tras el arranque |
| **F1-04** — *hecho* (`al-aire.json` por salida: escena, transmisión principal, destinos y grabación; `restoreOutputs` retoma todas; la clave de cada salida, cifrada aparte; falta el banco de caos con RTMP local) | **Retomar todas las salidas** tras un corte: `al-aire.json` guarda por salida escena, transmisión (con sus destinos) y grabación; `restoreOutputs` retoma las transmisiones de **todas** las salidas | `OperateMode.cpp:959-1009` (`988-990`: hoy no se retoman); `src/app/emision/salidas::AlAire` | Hallazgo H2; regla del dueño (N salidas, T47) | Las salidas no principales dejan de quedar abajo tras un corte | S | Bajo | F1-03 | Banco de caos: dos salidas transmitiendo a un RTMP local, `kill -9` de todo → las dos reconectan solas |
| **F1-05** — hecho 2026-10-31 (`test_salidas_fallas`; sin probar en la app) | **Grabación que se rearranca**: ante `GST_MESSAGE_ERROR`, cerrar, rearmar en `…-parte<N>.mkv` con backoff 0,5-1-2-5 s; si el error es del disco, probar la carpeta de la copia; anotar el hueco en la bitácora; `record_down` hasta que vuelva | `src/motor/Salidas.cpp:527-542`; `BroadcastEngine.cpp:1641-1703` | 02 M1; 03 D1 | De «muerta hasta que alguien mire» a 1-10 s de hueco | S-M | Bajo | F1-14 | Sumidero sintético que falla a los N s: parte nueva en < 10 s; `gst-discoverer` abre todas las partes |
| **F1-06** — hecho 2026-10-31 (`test_salidas_fallas` con un codificador falso: vuelve en ~1 s; falta forzar `vtenc_h264_hw` y `amfh264enc` reales y la PC Windows) | **Cambio de codificador**: por salida, lista negra de las fábricas que fallaron en la sesión; `armar` elige la siguiente y, agotadas, `x264enc` con `speed-preset` según CPU libre. `atenderEncoderCaido` se borra (la atención queda en el `tic` de cada salida) o se implementa: una sola. Vale para grabación y transmisión | `Salidas.cpp:40-70`, `596-640`; `BroadcastEngine.cpp:1863-1865` | 02 M1; 03 D2; 01 §13 | De bucle de error a < 5 s con otro codificador | S-M | Bajo | F1-14 | Codificador falso que da error en el aire → la transmisión vuelve con otro en < 5 s; probado también forzando falla de `vtenc_h264_hw` y de `amfh264enc` en la PC Windows |
| **F1-07** — hecho 2026-10-31 (el `tee` es el intersink del codificador: cada salida en su pipeline; `test_salidas_fallas`; falta medir CPU/GPU con copia y `stream_health` con enlace limitado) | **Un codificador por salida y perfil, con `tee`** a grabación, copia, transmisión y destinos extra; como máximo dos perfiles (grabación con más bitrate). Copia con codificador distinto: opción explícita. `rateControl` (CBR o VBR con tope) **efectivo** para RTMP/SRT; `calidadCrf` efectivo o borrado | `BroadcastEngine.cpp:1664-1676`, `1867-1879`; `Salidas.cpp:72-98`, `330-456`, `596-675` | 02-P5b, M5; 03 V6, §5.3; 01 §4.10, §13 | −1..3 sesiones de codificador por salida (evita el tope de NVENC, menos calor); bitrate estable | M | Medio (bitrates distintos por perfil) | F1-06 | Sesiones abiertas por salida en `sistema_recursos`; CPU/GPU con copia antes/después; `stream_health` con enlace limitado sintético |
| **F1-08** — hecho 2026-10-31 (destinos extra: `url_respaldo`; probado con dos SRT locales; RTMP sin servidor local; la transmisión principal no tiene el campo todavía) | **Respaldo de ingest** (URL de respaldo con la misma clave) como segunda rama del `tee` | `BroadcastEngine.cpp:1867-1879` | 03 D9 | Supera la caída de un punto de ingest | S | Bajo | F1-07 | Dos servidores RTMP locales; apagar el principal → el de respaldo sigue recibiendo |
| **F1-09** — hecho 2026-10-31 (`record.tramoMin` 15, `record.sincronizarMs` 1000; falta el `kill -9`/corte real) | **Grabación robusta al corte**: tramos de 15 min de fábrica (`record.tramoMin`) y `fdatasync` periódico (1-2 s) desde un hilo aparte sobre el archivo en curso | `BroadcastEngine.cpp:1647`; `Salidas.cpp:345-375` (sumidero propio que envuelve `filesink` o propiedad del elemento) | 02-P5a; 03 D4 | Pérdida ante corte: de decenas de segundos a ~2 s; un archivo dañado no se lleva todo | S-M | Bajo | — | `kill -9` y corte simulado: el MKV abre y le faltan ≤ 2 s |
| **F1-10** (hecho: `fault/ModoSeguro`, `test_modo_seguro`; el launcher sube un nivel por disparo y pasa `--safe-level`/`--safe-off`; falta medir el ≥ 90 % con el banco de caos) | **Modo seguro escalonado** (`--safe-level=N`): 1 = sin superres, CLAP de terceros, plugins Python, agentes y modelos; 2 = además sin NDI ni capturas de ventana; 3 = el actual. Sube un nivel por disparo del cortacircuito; la causa de la última caída puede elegir el módulo. Niveles 1-2 **restauran escenas** | `Supervisor.cpp:274-311`; `OperateMode.cpp:149-165` | 02 S3; T13 | Programa restaurado (no negro) en la mayoría de los disparos | M | Bajo | F1-12 (atribución, mejora la elección) | Inyección de caídas por módulo (superres, plugin CLAP sintético que hace `abort()`): ≥ 90 % de disparos con programa restaurado |
| **F1-11** (hecho: `platform/Energia*`, `arranque/EnergiaAire`, alarma `energia`, tarjeta en Configuración → Sistema; `test_energia`, `test_energia_aire`, `test_alerts`; probado en Mac y en Linux en Docker; falta Windows en una PC y un UPS de verdad) | **Energía y preparación para el corte**: detectar batería/UPS (`IOPSCopyPowerSourcesInfo`, `GetSystemPowerStatus`, `/sys/class/power_supply`), alarma «en batería», política (a X % o Y min: EOS de grabaciones, seguir transmitiendo, `cerrado=false`). Tarjeta de preparación: arranque automático al volver la energía (BIOS / `pmset autorestart`), inicio de sesión automático, app en el inicio, sistema de archivos con diario, plan de energía de Windows, suspensión selectiva de USB y de la placa de red | `src/core/platform/Energia_*.cpp`; `AlertEngine`; página de Configuración | 03 D5, D11, D3 (aviso exFAT) | Cortes largos sin archivos rotos; la máquina efectivamente vuelve sola | M | Bajo | F1-01 | Estado de energía falso en test → EOS ordenado y `cerrado=false`; tarjeta probada en Mac, Windows y Linux |
| **F1-12** (hecho: `diag/Caidas.cpp` POSIX, `Volcados.cpp`, `analizar.py` con `.pila` y `caidas.jsonl`; `test_caidas_pilas`, `test_volcados` en Mac y Linux; falta el % atribuido en el banco de caos) | **Volcados en macOS y Linux** (manejador propio mínimo: pilas de todos los hilos con nombre), procesados por `tools/caidas/analizar.py`; atribución caída → módulo en `caidas.jsonl` | `src/launcher/Volcados.*`; `src/core/fault/` | 02 O2 | Saber qué aislar en F3-05 y qué apagar en F1-10 | M | Bajo | F0-08 | % de caídas con módulo atribuido en el banco de caos |
| **F1-13** | **Alarma «interfaz trabada con aire»** emitida desde `LatidoAire` (que ya la detecta en hilo propio) a los notificadores y al control web | `src/app/emision/LatidoAire.cpp:87-120`; notificadores | 02 O5 (parte que no necesita C2) | El operador se entera aunque la UI no responda | S | Bajo | — | Bucle ocupado inyectado en QML con aire sintético: aviso en el control web en ≤ 3 s. **Hecho (2026-10-31):** `AvisoFueraDeUi`, `diag_trabar_ui` (solo `--test`); `test_latido_aire` (~2,1 s), `test_control_borde` |
| **F1-15** (hecho: `test_updater` 7 y 8) | **El actualizador no reemplaza binarios con salidas activas** y verifica integridad completa antes de lanzar | actualizador del launcher; `test_updater` | 02 O6 | Una actualización nunca corta el aire | S | Bajo | F0-06 | Test: actualización pedida con aire → pospuesta |
| **F1-16** | **Placa de audio que desaparece**: prueba con dispositivo virtual que se quita (sin sonido) para entrada, escucha y Matriz | `src/motor/Piezas.cpp:567-605`; `src/app/audio/Matriz.cpp` | 03 D13 | Confianza en el rearmado | S | Nulo | — | Test con dispositivo virtual; `audio_silence` dispara y vuelve. **Hecho (2026-10-31):** `test_placa_que_se_va` (proveedor falso de GStreamer); la escucha y las salidas virtuales de la Matriz ahora se rearman |

### Fase 2 — Latencia y audio

Objetivo: separar los oídos del video, sacar la latencia escondida y bajar el costo del
camino al invitado. F2-01 y F2-02 se hacen juntos porque tocan los mismos buses.

| Id | Qué | Dónde | Por qué | Ganancia esperada | Costo | Riesgo | Dep. | Cómo se verifica |
|---|---|---|---|---|---|---|---|---|
| **F2-01**<br>*Estado: hecho (2026-10-31). Retornos, directo, selección y afl esperan `audio.oidoMs` (20 ms) a sus entradas y no la mayor de ellas (la rama le niega su latencia al mezclador: antes una entrada lenta arrastraba el bus); retorno al anillo ~29 ms desde la captura con una entrada de 120 ms en el estudio (el programa, 184). Lo que no entra se informa: `entra_al_oido` por entrada, `oido.no_entran`, aviso en Sincronía. `test_lazo_latencia`* | **Buses de oído con latencia fija** (~25 ms) para mix-minus, IFB y escucha «en vivo», con el mismo mecanismo que el bus `directo` («lo que llega más tarde no entra», `Motor.cpp:26`); los buses de programa siguen con L_e | `src/motor/Mezcla.cpp:73-147`; `Motor.hpp:8-53`; `Escucha.cpp` | 03 P05; T4 | −70 a −290 ms en retorno y auriculares | M | Medio (una fuente lenta —p. ej. cámara IP con audio— no entra al retorno; se informa en la UI) | F0-01 | Test nuevo `test_lazo_latencia` (pulso sintético entrada → `baanillosink`/`appsink`, correlación contra el sello): ≤ 30 ms; `tiempos_pipeline` |
| **F2-02**<br>*Estado: hecho (2026-10-31). Cada salida tiene el `retorno<n>[:<id>]` de cada invitado (`ipc::sufijoRetorno`), con SUS faders; B lee en el hilo de retorno de cada salida los de esa salida y publica «retorno apagado» por la salida del invitado. `test_motor_retornos` con dos salidas: Goertzel de su voz = 0 en los cuatro* | **Mix-minus por salida**: bus `retorno<n>:<salida>`, canal `BA.Audio.Return.<salida>.<n>`, slots en el hilo de retorno de cada salida en B | `Motor.hpp:48-53`; `Mezcla.hpp:51-54`; `MediaPump.cpp:470-472`; `src/core/ipc/CanalesSalida.hpp` | 03 P04; regla N salidas | Ningún invitado de ninguna salida se oye a sí mismo | M | Medio | F2-01, F0-04 | `test_motor_retornos` con dos salidas; correlación sintética = 0 en las dos |
| **F2-03**<br>*Estado: hecho (2026-10-31): `pfl` (antes del fader y del bus, espera lo de oído o lo que tarda esa entrada), `afl` (el fader de una salida), directo y selección con el mando de la salida elegida; margen corto (10 ms). `test_lazo_latencia`. Falta que la app ofrezca AFL/salida en la interfaz (hoy su PFL es la selección)* | **PFL/AFL por entrada y por salida**, tomado antes del bus, latencia fija corta | `Motor.hpp:323`, `354`; `Escucha.cpp` | 03 P16 | −60..290 ms en la escucha de una fuente | M | Bajo | F2-01 | `test_lazo_latencia` |
| **F2-04**<br>*Estado: hecho (2026-10-31), sin probar en placas: `motor/BuffersAudio.hpp` fija 10/40 ms (`audio.dispositivoMs`/`dispositivoBufferMs`) a toda fuente y sink de audio del sistema (también a los micrófonos ya guardados) y lo informa (`dispositivo_audio` de la entrada, `dispositivo_buffers` de la escucha); `wasapi2sink low-latency=true`. Falta underrun = 0 en 1 h con placa real* | **Buffers de dispositivo explícitos**: `wasapi2sink low-latency=true` en la escucha; `latency-time`/`buffer-time` fijados (5-10 ms) en fuentes y sinks de las tres plataformas e informados | `src/app/audio/EntradasAudio.cpp:18-64`; `Escucha.cpp:20-30` | 03 P13; 01 §5.2, A.4 | −10..30 ms en auriculares | S | Bajo | — | `tiempos_pipeline` muestra la latencia de cada dispositivo; underrun = 0 en 1 h |
| **F2-05**<br>*Estado: hecho (2026-10-31). Quién la marca ya estaba (Sincronía, `estudio_latencia`); se suma a `tiempos_mapa`. Modo baja latencia: botón en Sincronía y `estudio_latencia {baja_latencia}` ponen el tope en `estudio.bajaLatenciaMs` (110), guardado con el evento; las que lo pasan van con `retraso_propio_ms`* | **Qué entrada fija L_e, a la vista** + **modo baja latencia por evento** (tope 100-120 ms): las entradas que lo pasan quedan en retraso propio, con su desfase informado | `src/motor/Reloj.hpp:85-137`; `Entrada.cpp:1385-1400`; UI de tiempos | 03 P06; 01 §5.9 | −100..150 ms en el escenario con cámara RTSP | S | Medio (la cámara IP queda corrida: se compensa con `desfase`) | — | `test_latencia_estudio`; `tiempos_pipeline` con una entrada sintética de 200 ms |
| **F2-06** — hecho 2026-10-31 (`salidas.margenSrtMs` 80, `margenRtmpMs` 300, `margenGrabacionMs` 500, más la latencia del codificador; sin medir en 1 h) | **Margen de salidas por tipo**: SRT 60-100 ms, RTMP 300, grabación 300-1000 | `Salidas.cpp:56`, `454`, `674` | 03 P07 | −200..240 ms en SRT de contribución; deja lugar al emisor (T1) | S | Bajo | — | Edad del cuadro a la salida (`MedidorEdad`); 0 cuadros tardíos en el sink en 1 h |
| **F2-07**<br>*Estado: hecho (2026-10-31): `barack` suma la latencia del rack a LATENCY, avisa con un mensaje al cambiar y adelanta el sello; un módulo anulado (a mano o por el vigía) sale por una línea de su misma latencia. `test_rack_latencia` (CLAP sintético `clap_retardo256`) y `test_gst_rack` (LATENCY +5,333 ms, sello adelantado lo mismo)* | **Latencia del rack informada**: `barack` contesta LATENCY con `latenciaTotal_`, avisa al cambiar el plan y corrige el PTS; al puentear un módulo, retardo equivalente | `src/app/audio/GstRack.cpp:85-140`; `AudioRack.cpp:860-880`, `1156-1170` | 03 P08; 01 §5.3 | A/V y fase correctos con plugins de 5-100 ms | S | Bajo | F2-08 (registro sin candado) | `test_audio_rack` + plugin CLAP sintético de 256 muestras: LATENCY y PTS correctos |
| **F2-08**<br>*Estado: hecho (2026-10-31): `registrar()` mete en una cola de Vyukov sin candado (4096) y un hilo «registro» escribe; lo que no entra se cuenta (`perdidas`, en `log_leer` y `/api/v1/log-nivel`); fatal espera. `test_registro` §8: con el escritor trabado, p99,9 ≈ 1 µs y máx. ≈ 7 µs* | **Registro sin candado global** desde hilos de medios: cola MPSC acotada hacia un único hilo escritor; si se llena, se descarta y se cuenta | `src/core/diag/Registro.cpp:204-215`; `SalidaAsincrona.cpp` | 02 C7; 03 §3.3 | Quita la inversión de prioridad; condición de F2-09 | S-M | Bajo | — | `registrar()` en un hilo de agregador: máx. < 50 µs con la tubería llena (test) |
| **F2-09**<br>*Estado: hecho, sin probar en Windows ni Linux (2026-10-31): `platform/TiempoReal` (MMCSS «Pro Audio» por avrt.dll dinámico, time constraint 10/2/5 ms, SCHED_FIFO o nice −10) en la salida de cada mezclador de audio, baanillo, la cola de baanillosink, la Matriz, el retorno de B y `PullAdm`; se suelta en STREAM_STATUS LEAVE; `topologiaCpu().tiempo_real`. `test_tiempo_real` (Mac, 16 hilos ocupados): p99 de despertar 8 ms → 33 µs. Falta medir con F0-02 bajo carga en la PC* | **Tiempo real acotado** a los hilos de audio con plazo (salida de cada `audiomixer` de bus, `baanillosink`/`baanillo`, retorno de B, `PullAdm`, Matriz): Windows MMCSS «Pro Audio»; Mac `THREAD_TIME_CONSTRAINT_POLICY` (período 10 ms); Linux `SCHED_FIFO` si `RLIMIT_RTPRIO` lo permite, si no `setpriority` | `src/motor/Prioridad.cpp`; `MediaPump.cpp`; `Webrtc.cpp:171-236` | 03 P12; T5 | Menos cortes bajo carga (superres, modelos locales) | S-M | Medio | F2-08, F0-01 | Contadores de F0-02 con carga sintética de CPU/GPU: underrun/overrun = 0; cambios de contexto/s no suben respecto de hoy |
| **F2-10**<br>*Estado: hecho (2026-10-31): (a) ppm y reanclajes de cada placa en `sistema_recursos.deriva_placas`; (b) `baderiva` (`dsp/Deriva`: DLL de 2.º orden 1 → 0,02 Hz + libsamplerate variable + corrección lenta de fase) detrás de cada `GstAudioBaseSrc` (`horaDeCapturaEnAudio`); reanclaje como red de seguridad. `test_deriva_audio`: 1 h a +100 ppm sin reanclajes, 99,5 ppm, A/V ≤ 1 ms. De paso: `Resampler` devolvía la capacidad en vez de lo escrito (el gateway sumaba 32 cuadros basura por bloque). Falta una placa real* | **Deriva de placas**: (a) medir ppm y reanclajes por dispositivo y publicarlos; (b) DLL + remuestreo variable con libsamplerate (vendoreado) como elemento propio después de la fuente; el reanclaje queda como red de seguridad | `src/motor/Piezas.cpp:711-790`; `src/gst-plugins/audio/` (elemento nuevo) | 03 P10 → P09; T10; 01 A.10 | Sin saltos de 50 ms cada 8-17 min; A/V ±2 ms | S + M | Medio (calidad y CPU del remuestreo) | — | Fuente sintética a 48 000·(1+100 ppm): 0 reanclajes en 1 h; ppm visible en `sistema_recursos` |
| **F2-11**<br>*Estado: hecho, probado con sintéticos (2026-10-31): cada cuadro trae la hora de su contenido (`Vistas`, `FrameMeta::contenidoQpc`) y B lo demora para quien mira hasta la edad de su audio (`gateway/Labios.hpp`; lo que tarda el audio sale de la cabecera del anillo, `retrasoDelEscritorNs`); el que conversa, ya (`MediaRoute::conversa`). `test_retorno_video`: destello − pulso en la pantalla −10..−1 ms (antes −135 ms, lo que sigue viendo el que conversa). Falta medirlo con `audio_sincro_medir` en la app y con un navegador de verdad* | **Labios del retorno para quien mira** (pantallas de sala, espectadores, consola): en B, demorar el video del programa según la diferencia de sellos QPC con el audio, o pasar los sellos a libwebrtc. No se aplica al retorno conversacional (T4) | `MediaPump.cpp:576`, `599-702` | 03 P14; T4 | Desfase < 20 ms en pantallas y espectadores | M | Bajo | F0-01 | `audio_sincro_medir` / `test_sincro_labios` sobre lo que B empuja (destello + pulso sintéticos) |
| **F2-12**<br>*Estado: hecho (2026-10-31): `gateway/Ducking.hpp` (sostén de 150 ms entre sílabas, ataque 10 / liberación 300, `room.ifbProfundidadDb` −15); envolvente medida en `test_retorno_ritmo` (5)* | **IFB con «ducking»** (ataque 10 ms / liberación 300 ms, profundidad −12 a −20 dB configurable) en vez del ×0,4 fijo | `MediaPump.cpp:533-556` | 03 §3.4 | Talkback inteligible sin bombeo | S | Bajo | F0-01 | Test con voz y programa sintéticos: envolvente medida |
| **F2-13**<br>*Estado: hecho, probado en la Mac (2026-10-31): el retorno se publica al tamaño de las sesiones (`BroadcastEngine::ponerTopeRetorno`, de `gateway.programaW/H`), escalado y en BGRA en la GPU (`aSistemaEscalado`); `Vistas` entrega el buffer mismo (`Pixeles`) y avisa fuera del candado; en B una conversión I420 por cuadro para todas las sesiones (`CuadroSaliente`); invitados entrantes a su tamaño, sin agrandar (`FrameMeta::ancho/alto`, `bavideoanillo` y `FrameService` lo respetan). `test_retorno_video` con 8 sesiones: de ~116 a ~16 MB por cuadro (−86 %; ~3,0 GB/s menos a 30 cps); `test_salidas`: el aviso y el último son el mismo buffer, colores del multiview bien en BGRA de GL. Falta: D3D12 en la PC Windows y `motor_mapa` en la app* | **Menos copias al invitado**: vista `programa@invitados` al tamaño de la sesión, escalada y en BGRA **en la GPU**; `Vistas` entrega el `GstBuffer` (referencia) en vez de `assign` + copia y avisa fuera del candado; en B una conversión I420 **por resolución** compartida por las sesiones; invitados entrantes en su tamaño real (cabecera del canal) | `src/motor/Piezas.cpp:1157-1170`; `Vistas.cpp:50-90`; `BroadcastEngine.cpp:705-720`; `Webrtc.cpp:238-250`; `MediaPump.cpp:1063-1160` | 02 C4a-c; 03 V2, V3; 01 §3.7 | De ~67 a ~4 MB por cuadro (−~1,9 GB/s), −3..6 ms; menos CPU con muchas sesiones | M | Bajo-medio (stride, vida del buffer) | — | `motor_mapa` antes/después; `sample` por hilo con 16 sesiones sintéticas |
| **F2-14**<br>*Estado: hecho, probado en la Mac (2026-10-31): la cabecera del canal de video lleva cuándo publicó el productor y cada cuánto (`periodoNs`, dicho o medido) y la del anillo cuándo escribió; el lector duerme hasta el próximo esperado, antes según el temblor que le mide (`ipc/Mirada.hpp`), en `bavideoanillo`, `baanillo` y las bajadas de video de B. `test_mirada` (despertadas del sistema por segundo de UN lector, dos corridas): video 300-500 → 70-110 con un productor parejo como el motor y 120-190 con temblor de ±3 ms como un navegador, demora media 1,8-2,1 → 0,7-1,4 ms; audio 310-510 → 185-265, 1,9-2,5 → 0,6-0,7 ms; sin perder cuadros (mirando al ritmo propio, 33 ms, se pierde 2-9 % con 14-18 ms de demora). macOS estira las esperas largas (~30 %): `dormirHasta` duerme por tramos. Faltan los hilos de voz/operador de audio de B (son de otro agente) y medir en Windows* | **Sondeo hasta el próximo cuadro esperado** (1/cps del productor, de la cabecera) en vez de 4 ms fijos; plazo absoluto | `src/gst-plugins/ipc/video.cpp:94-123`, `anillo.cpp:77-112`; `MediaPump.cpp:584-593` | 02 C8 (paso S); 03 V8 | Miles de despertares/s menos; base para F3-13 | S | Bajo | F0-03 | Despertares/s de A y B ociosos con 8 invitados (`sample`/ETW); CPU ociosa (memoria del proyecto: ~88 % de un núcleo) |
| **F2-15**<br>*Estado: hecho (2026-10-31): `dsp/AgcLoudness` en `balimitador` (`agc`, `objetivo-lufs`, `agc-db`) antes del limitador; `agc` en el objetivo (`audio_loudness_objetivo`, `loudness.json`); `BroadcastEngine::aplicarLoudness` → `Motor::ponerLoudness` (techo del objetivo, −2 dBTP con AAC < 128 kbps); limitador en las pistas 2-6; la ponderación K en `dsp/PonderacionK` (la usan el medidor y el AGC). `test_loudness`. Sin control en la GUI (el objetivo tampoco lo tenía)* | **Loudness**: AGC lento opcional hacia el objetivo de `loudness.json` (≤ 1 dB/s, ±6 dB, congelado en silencio, antes del limitador); techo −2 dBTP con AAC < 128 kbps; limitador en las pistas publicadas | `src/gst-plugins/audio/limitador.cpp`; `Mezcla.cpp:79-90` | 03 P15 | Entrega estable al objetivo | S | Bajo | — | `test_loudness` con programa sintético de nivel variable |
| **F2-16** — hecho 2026-10-31 (las dos colas `leaky`, `stream.colaMs`; rearme con la red trabada: `stream.rearmarSinDatosS` 4; sin la prueba de 3 s cortados) | **Colas de transmisión con la misma política** para video y audio (leaky, 1 s) | `Salidas.cpp:617-621` | 03 §4.5; 02 §3.4; T9 | A/V alineados a la vuelta de una red trabada | S | Bajo | — | Enlace sintético que se corta 3 s: A/V alineados tras el rearme (`video_sincronia`) |
| **F2-17**<br>*Estado: hecho el aviso y el contador (2026-10-31): `redElectricaDelSistema` (la región del idioma del sistema), `avisosDeLaRed` al registro al arrancar el motor y en `estado().lienzo` (`red_hz`, `avisos`, `cadencias_de_la_red`: 50 Hz → 25, 50); `avisoDeCadenciaDeCamara` en los avisos de la entrada (29,97 en 30: un cuadro cada ~33 s); `tiempos_pipeline` da `videorate` {duplicados, tirados, por minuto}. No cambia el lienzo de fábrica. Falta llevar el aviso a la interfaz (Ajustes → Video) y las plantillas de evento por región* | **Aviso de red de 50 Hz y de cámaras a 29,97** en un lienzo de 30/1; plantillas por región con 25/50 cps (sin cambiar el de fábrica en silencio) | `src/motor/Piezas.hpp` (`Lienzo`); `Entrada.cpp` (`videorate`) | 03 V10 | Sin parpadeo de luces ni saltos de `videorate` | S | Bajo | — | Contador de duplicados de `videorate` en `tiempos_pipeline` |

### Fase 3 — Reestructura

Objetivo: que una falla ajena al aire no lo corte y que el aparato se pueda operar aunque
la UI no responda. Requiere la fase 1 (banco de caos, volcados) para medir.

| Id | Qué | Dónde | Por qué | Ganancia esperada | Costo | Riesgo | Dep. | Cómo se verifica |
|---|---|---|---|---|---|---|---|---|
| **F3-01**<br>*Estado: prototipo con la salida principal (2026-10-31), detrás de `emisor.aparte` (apagado de fábrica; o `--emisor-aparte` en el launcher): `src/emisor/` (`ba-emisor`: `bavideoanillo` del canal de la salida + `baanillo` de su anillo propio `BA.Audio.Emisor.<id>` —el del gateway es de un solo lector— → `input-selector` con la placa y vigilante de 500 ms + silencio siempre en un `audiomixer` en vivo → `motor::Transmision`/`motor::Grabacion`, las mismas clases); pedido y estado por `Runtime/emisor/<id>.json` y `<id>.estado.json` (`emisor/Pedido.hpp`; la app que vuelve adopta lo que el emisor ya hace); el launcher con lista genérica de hijos (`Supervisor::Hijo`: B, emisores, A; slots `ipc::slotSaludEmisor`, `control.emisor.<id>`); `BroadcastEngineEmisor.cpp` delega transmisión principal, grabación y copia. `test_emisor` (SIGKILL del «A» de prueba: un solo MKV, la placa en el hueco) y prueba a mano con RTMP local: 1 sola conexión, sin huecos > 1 s. Falta: correr `kill_a_con_emisor` del banco con la app real, NV12 en el canal, `d3d12upload` en Windows, destinos extra/pistas 2-6/repetición/señal limpia por el emisor, la clave fuera de `Runtime/` (hoy 0600), probar en Windows y Linux* | **`ba-emisor` por salida**: pipeline GStreamer `bavideoanillo canal=BA.Salida.<id>` + `baanillo canal=BA.Audio.Salida.<id>` → `fallbackswitch` a placa «volvemos enseguida» + silencio → codificador con `tee` (F1-07) → `flvmux/rtmp2sink`, `mpegtsmux/srtsink`, `splitmuxsink`. El canal de video del emisor en **NV12** (1,5 B/píxel) para no sumar copias; en Windows, `d3d12upload` delante del codificador. Lo lanza el launcher como hijo supervisado (lista genérica de hijos y slots de salud). La vista de la salida no se suelta por reposo mientras haya emisor | `src/emisor/` (nuevo); `src/gst-plugins/ipc/`; `src/core/ipc/CanalesSalida.hpp`; `src/launcher/Supervisor.*`; `BroadcastEngine.cpp:1133` | 02 S1, C4d; 03 D6 (objetivo), T1, T2 | **0 desconexiones RTMP/SRT y un solo archivo** ante la caída de A; la plataforma ve la placa durante el RTO | L (M el prototipo con la salida principal) | Medio (+30-50 ms en salidas; copia extra) | F1-07, F1-14, F0-05, F0-06, F2-06 | Banco de caos: `kill -9` de A con transmisión a RTMP local y grabación: 0 desconexiones, MKV continuo con la placa en el hueco; edad del cuadro a la salida |
| **F3-02**<br>*Estado: hecho (2026-10-31): `Tool::clase` (lectura 267 / escritura 477 / aire 47 / ui 15, declarada en las 806 con un literal; `readOnly` sale de ella), `Tool::hilo` (de la clase salvo las capturas de la vista), `porQueAire`/`aireSi`/`reversible`; McpAire sin lista de nombres (lee el registro; quedan los agregados de `Config/mcp-aire.json`). `mcp_codigo.py` lee bloques, tablas y funciones auxiliares, campo por campo (las etiquetas con `QT_TRANSLATE_NOOP` ya no se pierden); `mcp-inventario.py --desde-codigo` escribe `docs/mcp.json` y la tabla de `MCP.md`; `test_mcp_registro` (`--verificar`) falla con deriva; `test_mcp` pasa las 799 por el despacho. Falta: regenerar con la app andando una vez (2 descripciones y 4 esquemas solo los da la app: `origen: sin_leer`), «la foto» de verdad de las de lectura (con F3-03) y el límite de frecuencia de N4* | **Registro declarativo de herramientas MCP** (nombre, esquema, rol, clase `lectura/escritura/aire/ui`, idempotencia, hilo), del que salen `docs/mcp.json`, `MCP.md` y el despacho; el build falla si hay deriva. **Sin renombrar ni consolidar** (T8). Política de herramientas de clase `aire` aplicada en el despacho | `src/app/mcp/`; `docs/mcp.json`; `docs/funciones/MCP.md`; `Config/mcp-aire.json` | 02 M4 (parte), N4, M6 | Deriva doc↔código = 0 (hoy 697 vs 738); base del despacho por clase | M | Bajo | — | Build que falla con una herramienta sin registrar; test genérico por herramienta (esquema, rechazo de parámetros, las de lectura no cambian la foto) |
| **F3-03**<br>*Estado: etapas 1, 2 y 3 hechas (2026-10-31). `arranque/PlanoControl`: hilo «control» con su reloj (`AppRuntime::ticControl`: canal del launcher y el documento de aire) y el reloj del motor en el hilo dueño del motor (`ticMotor`: latido de la interfaz, sección de salidas, GPU, recortes); StudioViewModel ya no es el reloj de nadie; el cierre pedido sale del plano. MCP: `McpHttp` en el hilo de control; `McpCore` seguro entre hilos con `Ejecutores` (validación, guardas, aprobación y herramienta en su hilo; tarjeta y guardado en la interfaz; respuesta al hilo del que llamó); `interfaz` con tope (`mcp.topeInterfazMs`, 5 s): lo vencido contesta «ocupada» con la foto de emergencia y no se hace después (`ba_mcp_interfaz_ocupada_total`). `Hilo::SegunClase` = interfaz (la clase no dice qué objetos toca); `foto`: help, help_search, security_status, alert_types/events/wait (6 de 806). Alarmas: `AlertEngine` seguro entre hilos y adoptado por el plano; `agent/EntradasAlarmas`: directas (pico y loudness del programa, foto de salidas, negro/congelado, persistencia, recursos) y de la interfaz por foto (`comun/FotoEntreHilos`); los notificadores salen desde el hilo del vigía por `AvisoFueraDeUi`. `test_plano_control` (sin QQmlEngine, interfaz trabada 3 s): control 151 vueltas y motor 0; /healthz 0,4 ms; herramienta de la foto 0,6 ms; de la interfaz «ocupada» a los 801 ms (tope 800) sin hacerse después; la alarma de negro sale en el hilo de control. Partición (etapa 3): `StudioViewModel.cpp` 5249 → 1417 líneas (`.hpp` 1394 → 963); queda como raíz que arma las áreas, el reloj de la interfaz (las pastillas), los modelos de base (escenas, fuentes, mezcla, tablero, cuadros) y las expone a QML como `Studio.<área>` (propiedades constantes), con los modelos de cada una todavía en la raíz para las rutas QML y el MCP que ya los usan. Áreas, cada una dueña de sus modelos y de su cableado: `ui/AlarmasViewModel` (y las entradas del vigía), `ui/BitacoraEvento`, `ui/MultiviewViewModel` (`Studio.multivistas`), `ui/AsambleaViewModel` (cuerpo, fila de oradores, debate, central, banca virtual, quórum, orden del día, votaciones, presidencia; la web del evento, los vínculos, el acta), `ui/ControlViewModel` (botonera, Satellite, control web, DSK, acciones, tema web), `ui/CuentasViewModel` (YouTube, Twitch, Facebook, credenciales), `ui/SalidasViewModel` (conmutadores, FTB y placa, la activa, el aire de cada salida, el tally y lo que va al bus), `ui/EmisionViewModel` (grabar, transmitir, destinos, ISO, señal limpia, disco, loudness, retornos, salud de los destinos), `ui/AsistenteViewModel` (asistentes, roles, modelo local, voz, salidas de texto, CC, traducción, vocabulario, mensajes), `ui/ImagenViewModel` (PTZ, grafos de video, superresolución, tiempos, resolución, red, seguidor, scopes), `ui/MezclaViewModel` (racks, matriz, Mezclador, PFL, balística, retardo), `ui/SalaViewModel` (cupo, configuración, direcciones, votación, preguntas, PIN, túnel, chat, invitados), `ui/AppViewModel` (modo en vivo, versión y reinicio, eventos, la caída, lo que se corta al cerrar), `ui/EscenasViewModel` (armar y ordenar escenas, preview y TAKE de la activa, AFV, repetición), `ui/EscritorioViewModel` (catálogo, monitores, ventanas, multivistas). Las vistas pasan a `Studio.<área>.*` (menos AsambleaPage y HealthContent, que siguen con las rutas de la raíz) y las traducciones de cada texto pasan al contexto de su clase. Falta: BroadcastEngine sigue siendo del hilo de la interfaz (el motor, las escenas y casi todas las herramientas corren ahí: migrar herramienta por herramienta a `foto`/`control` a medida que lo que leen sea seguro entre hilos; mover el motor al plano es el paso grande); sin la interfaz no salen las alarmas de la sala/chats/entradas/energía/disco (sus fotos se quedan quietas); la partición dejó en la raíz las pastillas del aire (el reloj de la interfaz), la página web y los vínculos del evento (esperan a que EventoViewModel los tome) y los modelos de cada área expuestos también por la raíz (se pueden ir sacando cuando sus vistas y herramientas pasen a `Studio.<área>`); compilar y correr la app con la partición (acá solo se chequeó la sintaxis, moc incluido); probar `diag_trabar_ui` con la app y aire sintético en una PC* | **Plano de control fuera del hilo de la UI**: `AppRuntime` dueño de su temporizador y luego de su hilo; `AlertEngine` y el estado de aire ahí; el MCP despachado por clase (lectura → foto en el pool; escritura/aire → cola del control; ui → hilo de la UI con tope). `StudioViewModel` deja de ser el reloj del runtime y se parte por áreas, cada ViewModel ligado a su modelo | `src/app/arranque/OperateMode.cpp:766-786`; `StudioViewModel.cpp:2301-2306`; `src/app/agent/AlertEngine.*`; `src/app/mcp/McpHttp.cpp` | 02 C2, M2, O5; 03 D6 (beneficio de mandos); T2 | Con la UI trabada, siguen el MCP, las alarmas, el guardado y la retoma | L (por etapas: alarmas S, persistencia ya en F1-02, MCP M, runtime M) | Medio (objetos `QObject` entre hilos) | F3-02, F1-02 | Bucle ocupado inyectado en QML con aire sintético: MCP responde, alarmas salen; el runtime corre en un test sin `QQmlEngine`; `StudioViewModel.cpp` < 1 500 líneas |
| **F3-04**<br>*Estado: hecho (2026-10-31). `Runtime/al-aire.json` versión 2 (`emision/SalidasConfig`): por salida escena, transmisión, grabación, destinos y `placa` (negro/placa, antes `placa.json`), más la sección `linea` (el paso de la corrida y los HOLD, antes `linea-aire.json`). `emision/DocumentoAire`: cada uno PONE su sección (el runtime con lo que dice el motor; la línea de tiempo la suya), el documento compara y lo VUELCA entero su dueño (el plano de control) en dos generaciones: cada versión es una foto coherente. Lee la versión 1 (con `placa.json` y `linea-aire.json` sueltos) una versión; el primer volcado los borra. Restauración en un lugar (`AppRuntime::restaurarAire`: escena de cada salida, negro y placa, salidas) y antes de que nada pise el documento (`--record`/`--stream` lo pisaban antes de leerlo); el singleShot de la placa del ViewModel ya no existe; la línea se retoma de lo que leyó el runtime (`lineaDesconocida` si volvió dañado: HOLD sin reponer). Las grabaciones que esperan la carpeta del evento cuentan como pedidas (un corte entre el arranque y `setCarpetaDelEvento` las olvidaba). El launcher (`Politica::salidasActivas`) leía el archivo de una generación: desde F1-03 daba «sin aire» siempre; ahora lee las generaciones. `test_documento_aire`: corte después y a mitad de la escritura en 10 transiciones (escena, placa, grabar, transmitir, paso de corrida, HOLD, negro de otra salida, sacar placa, cortar grabación) → vuelve igual o la transición anterior entera; daño de las dos generaciones → corte con HOLD; dueño en otro hilo; versión 1. Falta: el banco de caos con la app real (kill -9 en cada transición)* | **Documento único de estado de aire por salida** (escena, placa/FTB, salidas, paso de la corrida, HOLD), escrito por un solo dueño (el plano de control), en dos generaciones; `placa.json` y la parte viva de `linea-aire.json` pasan a ser secciones; la restauración deja de estar repartida entre `OperateMode` y un `singleShot` del ViewModel | `OperateMode.cpp:712-1009`; `StudioViewModel.cpp:2305-2306`; `src/app/emision/SeguridadAire.*`; `src/app/evento/Corrida.*` | 02-P3 | Una verdad sobre «qué está al aire»; el corte en cualquier transición vuelve igual | M | Medio (migración; se lee el formato viejo una versión) | F3-03, F1-03 | Banco de caos: corte en cada transición (escena, placa, grabar, transmitir, paso de corrida) → vuelve igual o HOLD |
| **F3-05** | **Aislar lo que más se cae, según los volcados**: candidatos en este orden si los datos lo confirman: superres a un proceso «banco» (texturas compartidas solo ahí), NDI (`bandisrc`) a un hijo que publica `BA.Ndi.<k>` como un invitado, CLAP «aislado» opcional por plugin (anillo ida y vuelta, +10-20 ms, nunca en un bus de oído) | `src/gst-plugins/superres/`; `src/app/emision/NdiSource.*`; `src/app/audio/GstRack.*` | 02 S2; 03 D6; T1 | Caídas de A atribuidas a esos módulos → 0 | L (cada una M) | Medio (latencia y copias en lo aislado) | F1-12, F1-10 | Caídas de A por módulo por mes antes/después (`caidas.jsonl`) |
| **F3-06** | **Configuración de B en caliente** por la API (`PUT /api/v1/config/<sección>`); solo puertos/TLS reinician B, con aviso si hay sesiones. Campo `esquema` y migraciones en cada JSON de `Config/`; registro central de `ajustes` (clave, tipo, rango, «en caliente») | `OperateMode.cpp:1836-1849`; `src/gateway/AdminApi.cpp`; `src/core/ajustes/` | 02 I1, P4 | Cambiar un ajuste deja de cortar a todos los invitados | M | Medio | — | Reinicios de B por cambio de ajuste: solo puertos/TLS. **Estado (2026-10-31): hecho en código y tests** — `PUT /api/v1/config/ajustes` (los avanzados de la sala de alcance «live»: rigen andando; los «live» que no vienen vuelven al de siempre; todo o nada, valida tipo y rango) y `PUT /api/v1/config/sala` (lobby, intentos y bloqueo de PIN, tope de archivos) en `AdminApi.cpp`; la app los manda por `sala::ConfigSala` y `SettingsModel` reinicia B solo por lo estructural (puertos, bind, TLS, túneles, WireGuard, pinggy, tamaños de video, lugares reservados y los 14 avanzados que B lee una vez al arrancar), diciendo a cuántos corta. Registro central sin Qt: `src/core/ajustes/Registro.*` (clave, sección, tipo, rango, valor de siempre, destino, alcance; reemplaza `ui/AjustesAvanzados`); `persist/Esquema` (`esquema` + pasos) en app.json (2), gateway.json y launcher.json (1). Tests: `test_config_caliente` (gateway), `test_esquema` (core). Falta: `esquema` en el resto de los JSON de `Config/` (≈ 77 archivos) y probar con la app real |
| **F3-07** | **Todo lo externo termina en `ba-web`** (túnel, HTTPS de la LAN), que reenvía a B por loopback solo lo autenticado; después, TURN y WireGuard en un hijo de B | `src/web/`; `src/gateway/SignalingServer.cpp`; `src/gateway/turn/`, `net/` | 02 N3 (y N2 por esa vía) | Un error de análisis de red externa no tira a los invitados | M | Medio | F0-07 | `ataques.py` ampliado: 0 caídas de B. **Estado (2026-10-31): hecho en código y banco** — a B le llega solo lo que ba-web entendió: cabecera re-armada (línea de pedido y «Nombre: valor» validados), sin `X-Forwarded-For`/`Forwarded`/`X-Real-IP` del visitante, la IP real siempre en PROXY v1, `chunked` cortado con 411; tope de conexiones a B por destino (también el microcaché) que B escribe desde SUS cupos (`room.maxConexiones`·¾ / workers, `room.maxConexionesPorIp` / workers; antes 512 por worker contra 256 por escucha; esos dos ajustes pasan a «reinicia la sala»). `ataques.py --gateway`: 38/38, 300 lentas + contrabando + fuzz contra el B real detrás de ba-web, 0 caídas. Quedan afuera: el HTTPS de la LAN en Windows (`webLan` apagado por el firewall), cloudflared (su destino lo fija el panel de Cloudflare) y una lista de rutas permitidas en ba-web; **después**: TURN y WireGuard en un hijo de B |
| **F3-08**<br>*Estado: hecho para los buses virtuales, probado en la Mac con señales sintéticas (2026-10-31); falta lo físico. Los buses virtuales de la Matriz son buses de oído del motor (`Motor::matrizBus`, `bus:matriz-<id>`; `Mezcla` con `audiopanorama`: una ganancia por canal, la ley de pan de siempre): una salida virtual sale por su placa como la escucha (id estable del motor, Vigía, BuffersAudio) y, sin la placa, muda hasta que vuelva (`Escucha::sinRespaldo`: nunca a los parlantes del sistema); una entrada virtual es una entrada del motor que lee el bus (`intersrc`), y las guardadas con `bavoz` pasan solas. Sin tomas, hilo de mezcla ni Qt Multimedia para eso; el MCP (`matriz_*`, `audio_dispositivos`: ahora los ids del motor; los guardados con el de Qt se pasan por nombre) y la vista no cambian. `test_motor_matriz`: −15,05 dBFS de un seno de 0,25; pan a la izquierda −12,04 / −100; izq 0,5 der 0,25 → −21,07 / −27,09; dos envíos −12,04; el mute de la entrada vale. De la entrada a la placa: 30 ms de espera fija (oído 20 + margen 10) contra ~70 de colchones antes (toma 20 + bloque 10 + anillo 20 + `QAudioSink` 20; por cálculo, sin placa real). `RingEstereo` SPSC sin candado (`test_ring_estereo`, deriva sintética 10 min): +300 ppm, 2 recortes y 0 huecos como antes; −300 ppm, 7681 bloques con un cuadro de menos (un click cada 10 ms desde que se vaciaba) → 16 huecos; 30 s de audio entre dos hilos 3,1 → 0,6 ms. `test_placa_que_se_va` prueba la salida virtual con una placa falsa que se va y vuelve (reemplaza a `ReaperturaSalidas`). FALTA: las salidas físicas de varios canales (EquipoAudio: `interleave` a un sink de N canales) y las entradas físicas (entradas del motor con `baderiva` por par) siguen con Qt Multimedia, las tomas y el hilo de mezcla; probar en la GUI y en Windows* | **Matriz al motor**: fuentes y sinks multicanal de GStreamer con el mismo tratamiento de hora y deriva; mientras tanto, `RingEstereo` SPSC sin candado | `src/app/audio/Matriz.cpp`, `EquipoAudio.cpp`, `RingEstereo.hpp:23-52` | 03 P11; regla «todo en GStreamer» | −20-30 ms; sin clicks por recorte; un dominio de reloj menos | M-L | Medio | F2-10 | `test_matrix_mixer` + deriva sintética |
| **F3-09**<br>*Estado: hecho, probado en la Mac con la falla inyectada (2026-10-31): `motor/GpuPerdida` detecta por el dispositivo D3D12 (`device-removed-reason` del GstD3D12Device, lo que GStreamer saca de GetDeviceRemovedReason, con su notify), por el texto (DXGI_ERROR_DEVICE_REMOVED/HUNG/RESET, GL_CONTEXT_LOST…) o, con un error sin motivo de un elemento de GPU, probando aparte si el contexto del motor entrega (`gpuEntrega`); lo miran los buses de Composicion, Entrada, Salidas y Vistas. La app lo ve en su tic (`AppRuntime::rearmarPorGpu`): `resetVideo` con el motor apagado → `rearmarGpu` (contexto/dispositivo nuevo; si no entrega, CPU a ≤ 720p sin guardarlo) → mismo estado → retoma de las salidas de esta corrida. `diag_falla` video/gpu y escenario `gpu_video` del banco. `test_gpu_perdida`: detectada en 14-22 ms, hueco falla → primer cuadro real con la GPU nueva 64-130 ms (antes: imagen congelada hasta la alarma de 30 s), a la CPU 1,0 s; sin falsos positivos con un error de negociación. Falta el TDR de verdad en la PC Windows (que GStreamer saque el dispositivo removido de su caché) y la placa durante el rearme la pone ba-emisor (F3-01)* | **GPU perdida**: detectar (`GetDeviceRemovedReason` en D3D12, errores de contexto en GL) y rearmar el motor sobre un dispositivo nuevo o caer a CPU a menor resolución, con la placa de seguridad mientras tanto | `src/motor/Piezas.cpp:1008-1090`; `Motor.cpp` | 03 D7 | De caída o imagen congelada a segundos de placa | M-L | Medio | F3-01 (la salida sigue mientras tanto) | TDR forzado en la PC Windows |
| **F3-10**<br>*Estado: medido y marcado (2026-10-31); falta la corrida del banco. Motor solo, proceso nuevo → primer cuadro real (`test_motor_arranque`, 1080p, GL): 115-250 ms (GStreamer 10-21, autoprueba GPU 60-118, motor 15-34, escena 28-86). El registro ya está escaneado en `Datos/Cache` (GstEntorno): revisarlo cuesta 8 ms contra 6 ms con GST_REGISTRY_UPDATE=no, así que no se toca (sin el riesgo de un registro viejo tras actualizar). La primera clave ya es inmediata (`pedirClave` al abrir archivo o reconectar, Salidas.cpp:332-337) y las salidas ya se retoman en `AppRuntime::start` antes que la interfaz. En los registros de la app: ~1,7 s entre «el cartel ya se ve» y el motor que no tenían marca: ahora `marca` cubre GStreamer, configurar y los tipos de fuente, y LatidoAire anota «el primer cuadro del programa»; el banco lo lee (`primer_cuadro_s`, p50/p95 en el informe). Correr `banco.py --escenarios kill_a`* | **Retoma de A < 3 s**: registro de GStreamer ya escaneado, el motor arma las salidas antes que la interfaz, primera clave del codificador inmediata | `OperateMode.cpp:502-640`; `src/motor/Motor.cpp` | 03 D12; 02 S6 | Con el emisor, segundos de placa en vez de negro | M | Medio | F1-14, F3-01 | Banco de caos: tiempo hasta el primer cuadro real en la salida (p95 < 3 s) |
| **F3-11**<br>*Estado: hecho, medido en la Mac (2026-10-31): las vistas del motor se toman sin copiar (QImage sobre `motor::Pixeles`; `latestPreview`/`renderScenePreview` dan el `Cuadro`); los canales entre procesos los lee un hilo propio (`FrameService::Canales`, despierta con `proximaMirada`); B publica `BA.GuestUi.<n>` (640×360, ≤ 30 cps, achique con libyuv; `fitBgra` pasó a libyuv) y la tarjeta del invitado lo lee (si no está, `BA.Guest.<n>`). Banco con 8 invitados a 30 cps y poll a 60 Hz: poll p50 0,12-0,20 → 0,011-0,013 ms, **p99 5,4-7,2 → 0,05-0,08 ms**, máx 16-18 → 0,1-0,3 ms. `frames_poll` (p50/p99/máx del último minuto) junto a `bucle_ui` en latidoUi. Falta verlo en la app con el vigía* | **`FrameService` fuera del hilo de la UI** y con vista chica por invitado | `src/app/ui/FrameService.cpp:207-320` | 02 C5 | `poll` p99 < 1 ms en el hilo de la UI | M | Bajo | F2-13 | F0-08 antes/después |
| **F3-12** | **Deuda de migración**: migrar `app.json` a los tipos del motor y borrar la traducción de tipos viejos (con test de que toda opción guardada llega al motor o se rechaza con aviso: hoy `rtsp_transport`/`fflags` se pierden); borrar `OperatorEngine` y `--headless-selftest` | `BroadcastEngineScenes.cpp:560-600`; `src/app/emision/OperatorEngine.*`; `src/app/main.cpp:7`, `92` | 02 M3, M5; T12 | 0 claves guardadas sin efecto; sin restos de libobs | M | Medio | F3-06 (esquema) | Test de ida y vuelta de claves. **Estado (2026-10-31): hecho en código y test** — `inputs::alMotor` (InputSpecs) es la única traducción de los tipos de antes (cámaras, micrófonos, ffmpeg_source de archivo y de red, invitados, NDI, textos y colores viejos): al cargar app.json (esquema 2, una vez) y en la puerta del motor para el MCP; lo que no tiene equivalente se avisa en el registro. `rtsp_transport`, `fflags=nobuffer`, `ca_file` y `reconnect_delay_sec` llegan al motor (`rtsp_transporte`, `latencia`, `tls_ca`, `reconectar_s` en `motor/OrigenRed`); `hw_decode=false` se rechaza con aviso. Color, texto e imagen se traducen en su tipo registrado (`TiposBasicos`, `Tipo::motor`); `ajustesMotorTipo` ya no conoce tipos viejos; los creadores (invitados, medios, playlist, red) arman tipos del motor. Borrados `OperatorEngine`, `--headless-selftest` (y su paso en CI), `EnginePaths` de libobs y `sourceTypeListItems`. Test: `test_al_motor` (toda clave guardada llega o se avisa, y cada clave que queda la lee `Entrada.cpp`). Falta: probar con la app real (cámara RTSP por UDP, https con raíces del bundle) y comentarios con «libobs» en archivos de otros dueños |
| **F3-13**<br>*Estado: hecho, probado en la Mac (2026-10-31); Windows y Linux escritos sin compilar ahí. `ipc/Timbre.hpp`: en la cabecera de cada canal (los 16 bytes libres: ni el tamaño ni la versión cambian; un escritor viejo deja 0 = «no toca») `toques`, `durmientes` y `vivo`. El escritor toca después de anunciar (sin durmientes no llama al sistema); el lector anota el timbre antes de mirar y duerme hasta el toque o un vigía de 1 s (`mirada::kVigia`); con un escritor que no toca, Mirada. futex compartido en Linux, `os_sync_wait_on_address` (SHARED) en macOS 14.4+ (antes: Mirada), semáforo con nombre `Local\<canal>.Timbre` en Windows. En `bavideoanillo`, `baanillo` (unlock y el cambio de canal despiertan) y las bajadas de video de B. `test_mirada` (un lector, mismo proceso): video 30 cps, vueltas/s 49-98 (Mirada) → 29,3 (una por cuadro), demora media 0,99-1,08 → 0,02-0,05 ms; audio 110 → 98 vueltas/s, 0,55 → 0,13 ms; en reposo 237 → 1,5 vueltas/s; `despertar()` 0,10 ms. `test_video_cross_process` (otro proceso): 31,0 → 30,0 esperas/s, demora del salto 0,51 → 0,047 ms (máx 2,5 → 0,46). Las despertadas que cuenta macOS (las de temporizador) bajan a ~0. Faltan los hilos de voz/operador de audio de B (de otro agente) y medir en Windows* | **Timbre entre procesos** (evento con nombre en Windows, `futex` en Linux, semáforo con nombre o `os_sync_wait_on_address` en Mac) para los canales de memoria compartida | `src/core/ipc/SpoutTransport.*`, `SharedAudioRing.*` | 02 C8; 03 V8 | −2 ms medios por salto; despertares casi nulos en reposo | M | Medio (primitiva en macOS) | F2-14 | Despertares/s y latencia por salto medidos; `test_ipc_cross_process`, `test_video_cross_process` |
| **F3-14**<br>*Estado: hecho, probado en la Mac (2026-10-31). `MutexMotor` (Candado.hpp): común y sabe qué hilo lo tiene; una reentrada se nombra en el registro y cae con su volcado. Las que había: los 34 `...Ya` pedían el candado que ya tenía la cola (`orden`, `correrOrdenes`); la foto del bucle llamaba a `estado()`; el productor de las vistas (`pedir`/`tic`) lo pedía adentro; una `orden` anidada desde el hilo del motor. Ahora los `...Ya`, `estadoYa` y `productorDeVideoYa` lo EXIGEN (`BA_CON_CANDADO`: si falta, error y `faltasDelCandado`); la orden anidada corre ahí mismo (el único anidamiento, a la vista); `alFallar` se llama sin el candado y `esperarCola` vuelve después de esos avisos. Suite del motor entera verde (los 15 de antes y `test_motor_matriz`; `test_motor_app` y `test_composicion` fallaron en píxeles de GL en una corrida, también con master: la GPU compartida con otras pruebas; en la siguiente, verdes); `test_motor_sin_esperas`: 0 tomas de la UI y 0 faltas del candado. Faltan los escenarios (F4-03)* | **`mutex` común en el motor** (en vez de `recursive_mutex`) | `src/motor/Motor.hpp:346` | 02 C6 | Las reentradas escondidas se vuelven abrazos detectables en tests | S-M | Medio | F4-03 (escenarios) | Suite del motor y escenarios verdes; 0 tomas de la UI en `--test` |

### Fase 4 — Observabilidad y operación continua

Partes de esta fase ya se adelantaron (F0-02, F0-08, F1-12, F1-14) porque sin medir no se
puede priorizar ni verificar. Lo que queda es convertir eso en algo permanente.

| Id | Qué | Dónde | Por qué | Ganancia esperada | Costo | Riesgo | Dep. | Cómo se verifica |
|---|---|---|---|---|---|---|---|---|
| **F4-01**<br>*Estado: hecho (2026-10-31): `diag/Metricas` (contador, medidor, histograma atómicos; colectores que copian lo ya medido; texto de Prometheus y JSON). `GET /metrics` en `McpHttp` (Bearer del MCP) y en la API de control de B (`AdminApi`, Bearer de B); el launcher escribe `Runtime/metricas-launcher.prom` (`MetricasLauncher`, `persist::escribir`). SLI: edad A→B p50/p99, ocupación, descartados y faltantes por canal (colector en `gateway/main.cpp` sobre `MediaPump::transporte`), cuadros perdidos, bucle de la UI, `ba_mcp_herramienta_ms` por herramienta (gancho en `McpCore`), `ba_motor_candado_espera_ms` (`Candado`), reconexiones de invitados (B) y de la transmisión (A), colchón de grabación, térmica. Alarma nueva `metrica` (umbral sostenido sobre cualquier serie; ninguna alarma leía logs). Panel «Salud del servicio» en Configuración → Sistema → Diagnóstico; `sistema_recursos.metricas`. `test_metricas`* | **Métricas con nombre y tipo** (contadores, medidores, histogramas sin bloqueo) en `ba_core`; `GET /metrics` por proceso en su puerto local con su Bearer (launcher: archivo en `Runtime/`); SLI iniciales: edad A→B p99, anillos, cuadros perdidos, bucle de la UI, latencia por herramienta MCP, espera del candado, reconexiones, colchón de grabación; las alarmas leen métricas, no logs | `src/core/diag/`; `McpHttp.cpp`; `AdminApi.cpp`; `AlertEngine` | 02 O1, I3; 03 §9 | Panel único de salud; alarmas fiables | M | Bajo | F0-02, F3-03 | Panel en la página de diagnóstico; `sistema_recursos` los expone |
| **F4-02**<br>*Estado: hecho (2026-10-31): `diag/Correlacion` (id `m-`/`b-`/`w-`/`l-` + 8 hex; ámbito por hilo; el registro común y el del launcher agregan « [orden=<id>]»). `McpCore` abre la orden de cada herramienta (o la que trae `X-BA-Orden`); `ConferenceClient` la manda en `X-BA-Orden`; `AdminApi` la abre y registra los pedidos que cambian algo; `ipc::Message.correlacion` en el canal de control (cierre del launcher → A y B). `test_correlacion` sigue una orden MCP hasta B y el cierre del launcher hasta B con los registros reales* | **Id de correlación** en cada orden (MCP, botonera, web) que viaja por el canal de control y la API de B y aparece en los tres registros | `src/core/ipc/ControlProtocol.hpp`; `ConferenceClient`; `AdminApi.cpp` | 02 O4 | Una orden reconstruida de punta a punta con un grep | S-M | Nulo | — | Test que sigue una orden MCP hasta B |
| **F4-03** | **Escenarios de sistema nocturnos** (crecen desde F1-14): launcher+A+B con fuentes sintéticas y Chrome con medios falsos; promesas: corte → vuelve en HOLD; caída de A → la salida sigue; caída de B → invitados reconectan; inundación → sala viva | `tests/`; `src/launcher/tests/` | 02 M7, S6 | Regresiones de disponibilidad atrapadas antes de un evento | M | Bajo | F1-14 | Escenarios verdes por versión |
| **F4-04** | **Contratos de protocolos** (API de sala, canal de control, JSON por línea de cada hijo) con versión en el saludo y tests de contrato; test de documentación contra los registros (MCP, `ajustes`, puertos) | `src/core/ipc/`; `src/grafico`, `src/modelo-*`, `src/plugins/sdk`; `tests/` | 02 I2, M6 | Sin desajustes silenciosos entre procesos ni docs atrasadas | M | Bajo | F3-02, F3-06 | Tests de contrato en CI |
| **F4-05**<br>*Estado: hecho (2026-10-31): `platform/Termica` (macOS `thermalState`; Linux zonas de `/sys/class/thermal` contra sus puntos de disparo y `hwmon`; Windows tope de frecuencia de `CallNtPowerInformation`) en `diag::Recursos` y `sistema_recursos.termica`; `diag/PoliticaTermica` (templado: superres; serio: + modelos y vistas; crítico: + resolución de invitados; sostenido, de a un paso, vuelve de a uno tras 60 s fríos; ajuste `termica.priorizarPrograma`). Atendido hoy: superres (programador a 0 activas). **Falta** gancho para modelos, vistas sin mirar y resolución de invitados (quedan como `sin_gancho`). `test_termica`. Falta la prueba con carga sostenida en la Mac sin ventilador* | **Térmica y degradación ordenada**: estado térmico (`NSProcessInfo.thermalState`, `/sys/class/thermal`/`hwmon`, frecuencia efectiva en Windows) en `sistema_recursos` y política «priorizar programa» (primero superres, modelos, vistas sin mirar, resolución de invitados; nunca programa ni grabación) | `src/core/diag/Recursos.cpp`; `BroadcastEngine.cpp:2010-2060` | 03 V9, §4.6 | Evita bajones de frecuencia en el aire | M | Bajo | F4-01 | Carga sintética sostenida en la Mac sin ventilador: se apaga lo prescindible antes de que caigan cuadros |
| **F4-06**<br>*Estado: hecho en parte (2026-10-31): WebRTC con `media_config.enable_dscp` y `network_priority` alta en cada sender (EF audio, AF41 video; ajuste `red.dscp`); SRT con `iptos=136` (AF41) en la URI de `srtsink` (ajuste `red.dscpSrt`). **Pendiente**: segunda interfaz de subida por destino (srtsink solo enlaza `localaddress` en modo listener y rtmp2sink no elige interfaz: hace falta otro camino, no es simple). Falta medirlo en una red con QoS* | **Red**: DSCP (EF audio, AF41 video) en WebRTC y SRT; segunda interfaz de subida por destino | `src/gateway/Webrtc.cpp`; `Salidas.cpp` | 03 D14 | Menos pérdida en redes compartidas | S-M | Bajo | — | `stream_health` y estadísticas WebRTC |
| **F4-07**<br>*Estado: hecho (2026-10-31): `diag/ChequeosEquipo` en la tarjeta de preparación (`SystemViewModel.preparacion` vía `alEquipo`): sesiones de codificador abiertas contra el tope (NVENC 8; ajuste `equipo.limiteSesiones`), placas que no están a 48 kHz (`derivaDePlacas` ahora da `hz`), PTP explicado (error del reloj, `gst-ptp-helper` y sus permisos) y deriva (enganchada, < 100 ppm, sin reanclajes al programa). `test_chequeos_equipo`* | **Chequeos de equipo**: sesiones de codificador abiertas vs. límite de la placa, placa de audio que no está a 48 kHz, permisos de PTP explicados, enganche por deriva ≈ 0 | `Salidas.cpp:40-70`; `Piezas.cpp`; `Reloj.cpp:325-370` | 03 §6 | Problemas de equipo detectados antes del evento | S | Nulo | F2-10 | Tarjeta de preparación |
| **F4-08**<br>*Estado: hecho (2026-10-31): `launcher/PerroGuardian` (abre `/dev/watchdog`, plazo con `WDIOC_SETTIMEOUT`, le da de comer desde la vuelta del supervisor, «V» mágica al cerrar en orden); apagado de fábrica (`launcher.perroGuardian`, `launcher.perroS`, solo Linux; el dispositivo tiene que ser del usuario por udev). `test_perro_guardian` con un dispositivo falso. Falta la máquina de prueba con cuelgue forzado* | **Watchdog de hardware opcional en Linux** (`/dev/watchdog`), apagado de fábrica | `src/launcher/` | 03 D8 | Recupera cuelgues del sistema operativo | S | Medio (reinicios no deseados) | — | Máquina de prueba con cuelgue forzado |

---

## 5. Presupuesto de latencia y metas de disponibilidad

### 5.1 Latencia por camino (ms)

«A» = solo cámaras USB, micrófonos locales e invitados (L_e ~50 ms); «C» = con una cámara
RTSP migrada (L_e ~250 ms). Valores de 03 §2 (configurado + estimado) y deltas de cada
propuesta; **todos a confirmar** con `test_lazo_latencia`, `tiempos_medir`, la prueba de
eco y la edad de los cuadros.

| Camino | Hoy (A / C) | Tras F0 | Tras F1 | Tras F2 | Tras F3 | Meta de oficio |
|---|---|---|---|---|---|---|
| Micrófono → auriculares del operador (escucha en vivo) | 145-160 / 345-360 | = | = | **35-50 / 35-50** (F2-01, F2-04) | = | < 20-30 (para menos, monitoreo directo de la placa) |
| Cámara → pantalla del operador | 135-200 / 335-400 | = | = | 135-200 / **205-270** (F2-05) | = | lo menor posible |
| Aporte del estudio al lazo invitado → invitado (audio) | ~270-305 / ~470-505 | **~125-150 / ~325-350** (F0-01) | = | **~55-95 / ~55-95** (F2-01) | = | < 100 |
| Boca → oído entre invitados (total, con red y navegadores) | 400-680 / 600-880 | 260-540 / 460-740 | = | **180-480** | = | < 400 (G.114), ideal < 150 |
| Desfase audio-video del retorno para quien mira | audio 70-230 atrás | 70-100 | = | **< 20** (F2-11) | = | < 45 adelantado / < 125 atrasado (BT.1359) |
| Cámara → salida SRT de contribución | 560-600 / 760-800 | = | = | **340-380 / 410-450** (F2-06, F2-05) | 370-430 / 440-500 (+emisor) | lo menor posible |
| Cámara → salida RTMP (antes de la plataforma) | 440-480 / 640-680 | = | = | 440-480 / 510-550 | +30-50 (emisor) | irrelevante frente a 2-30 s de la plataforma |
| Video del invitado → programa → retorno | 240-560 / 440-760 | = | = | 235-555 / 305-625 (F2-13, F2-05) | = | < 400 |

Lectura: la fase 0 sola quita ~140 ms al retorno; la fase 2 hace que los oídos dejen de
depender del video. El emisor de la fase 3 es el único paso que **suma** latencia, y la
suma cae en salidas que la fase 2 ya bajó (SRT) o que no la notan (RTMP, grabación).

### 5.2 Qué fallas dejan de cortar el aire en cada fase

| Falla | Hoy | Tras F0 | Tras F1 | Tras F2 | Tras F3 |
|---|---|---|---|---|---|
| Retorno a invitados | cortes de 10 ms (varios por segundo en carga; hasta 1/3 perdido en Windows, a medir) y eco propio en cada hueco | **sin cortes ni eco** | = | mix-minus en todas las salidas | = |
| Caída aislada de A tras horas | negro + desconexión RTMP + hasta 10 s de backoff; B sin vigilar mientras tanto | backoff 0 s; B siempre vigilado | modo seguro escalonado conserva escenas | = | **la plataforma ve placa, 0 desconexiones, un solo archivo**; meta < 3 s |
| Codificador que falla | transmisión en bucle de error; grabación muerta | = | **< 5 s con otro codificador** | = | = |
| Grabación con error (disco, muxer) | muerta hasta que alguien actúe | = | **parte nueva en 1-10 s**, prueba la carpeta de la copia | = | = |
| Tope de sesiones de codificador (NVENC) | salida caída | = | 1-2 sesiones por salida + cambio de codificador | = | = |
| Ingest de la plataforma caído | corte mientras dure | = | **respaldo** | = | = |
| Corte de luz | depende de archivos sin `fsync`; daño = «nada al aire» en silencio; salidas no principales no retoman su transmisión | = | **vuelve igual o en HOLD con aviso, en todas las salidas**; grabación pierde ≤ 2 s; con UPS, cierre limpio | = | documento único de aire |
| UI trabada con aire | sin mandos ni alarmas, nadie se entera | = (se mide) | **alarma al control web y notificadores** | = | **MCP, alarmas y guardado siguen** |
| Inundación de conexiones a B | «sala muerta» a los 40 s | **la app sigue conectada** | = | = | lo externo termina en `ba-web` |
| Cambio de un ajuste de B | reinicia B, se cortan los invitados | = | = | = | **en caliente** (salvo puertos/TLS) |
| Caída por superres / CLAP / NDI | A entero | = | modo seguro sin el módulo, con escenas | = | **aislado** (lo que confirmen los volcados) |
| GPU perdida (TDR, contexto GL) | caída o imagen congelada | = | = | = | **segundos de placa** y rearme |
| Deriva de placa de audio | salto de 50 ms cada 8-17 min por placa | = | = | **sin saltos** | = |
| Térmica | baja de frecuencia sin control | = | = | = | = (F4: degradación ordenada) |
| Cuelgue del sistema operativo | corte indefinido | = | = | = | = (F4: watchdog opcional en Linux) |

---

## 6. Qué NO hacer (o no todavía)

| Propuesta | Origen | Decisión | Por qué |
|---|---|---|---|
| Codificar el retorno una vez en A y que B solo empaquete | 03 V1 | **Postergar** | Riesgo alto (adaptación por sesión, PLI/FIR, simulcast). F2-13 se lleva el ~95 % del ahorro de memoria por una fracción del costo. Se reevalúa si F4-01 muestra la CPU de B dominada por la codificación con > 8 sesiones |
| Texturas compartidas entre procesos (IOSurface, handle D3D12, DMA-BUF) | 03 V4; 02 S2a | **Postergar**, salvo para el banco de superres si F3-05 lo hace | Después de F2-13 y del canal NV12 del emisor lo que cruza es chico; sincronizar GPU entre procesos es caro y frágil |
| Separar ya la UI en otro proceso (proceso de aire mínimo) | 03 D6 | **Reemplazar** por emisor (F3-01) + plano de control en hilo propio (F3-03); reevaluar con los volcados | T2: el emisor cubre más fallas con menos IPC; el hilo de control resuelve «sin mandos» |
| Reutilizar `OperatorEngine` | 03 D6 | **No**: se borra (F3-12) | Resto de libobs; contradice «sin vuelta atrás» |
| Renombrar o consolidar herramientas MCP | 02 M4 (parte) | **Postergar** a la ventana de reentrenamiento | Rompe los modelos entrenados; el registro declarativo da el beneficio de sistema sin tocar nombres |
| Tiempo real para todos los hilos de streaming | — | **No** | La medición del propio código mostró que empeora (`Prioridad.cpp:280-286`) |
| RealtimeKit por D-Bus, NUT, upower, drivers de fabricante (DeckLink) como dependencia | 03 P12, D5, §6 | **No** | Servicios o bibliotecas del sistema en tiempo de ejecución; la regla del dueño lo prohíbe y un «si está» silencioso contradice «faltante = error de configuración» |
| crashpad | 02 O2 | **Solo si** el manejador propio no alcanza, y vendoreado | Más superficie de la necesaria para pilas con nombre |
| Achicar la capacidad del anillo A→B | (tentación de T3) | **No** | La capacidad da robustez ante picos; la latencia la arregla el nivel objetivo (F0-01) |
| `fsync` en el hilo que cambia el estado | (tentación de T6) | **No** | Va en el hilo escritor (F1-02) |
| «Reloj del estudio = reloj de la placa principal» | 03 P09 (alternativa) | **Postergar** | El remuestreo variable sirve para cualquier cantidad de placas y no ata el video al audio |
| Cambiar el lienzo de fábrica a 25 cps en silencio | 03 V10 | **No**; aviso + plantillas por región (F2-17) | Cambia plantillas y eventos existentes sin que el dueño lo decida |
| Watchdog de hardware de fábrica | 03 D8 | **Solo opcional, Linux** (F4-08) | Reinicios no deseados en pleno evento |
| Backend GL en Linux | 03 V5 | **Postergar** | L con riesgo de drivers; Linux no es hoy la plataforma de los eventos. Se retoma con medición de `motor_mapa` en una placa Linux real |
| Bucle de eventos/pool en B en vez de un hilo por conexión | 02 N2 | **Postergar**; se logra en buena parte con F0-07 + F3-07 | Reescritura grande de `SignalingServer` (3 117 líneas) con beneficio que F3-07 ya trae |
| Marcar DSCP y segunda interfaz antes de lo demás | 03 D14 | **Al final** (F4-06) | Depende de la red del lugar; menos daño que lo anterior |

---

## 7. Mapa de ids de 02 y 03 a este plan

| Id de origen | Ítem del plan |
|---|---|
| 02 S1 | F3-01 |
| 02 S2 | F3-05 |
| 02 S3 | F1-10 |
| 02 S4 | F0-05 |
| 02 S5 | F0-06 |
| 02 S6 | F1-14, F4-03 |
| 02 C1 | F0-01, F0-03 |
| 02 C2 | F3-03 |
| 02 C3 | F1-02 |
| 02 C4 | F2-13 (a-c), F3-01 (d: NV12 en el canal del emisor) |
| 02 C5 | F3-11 |
| 02 C6 | F3-14 |
| 02 C7 | F2-08 |
| 02 C8 | F2-14, F3-13 |
| 02 C9 | F0-08 |
| 02 I1 | F3-06 |
| 02 I2 | F4-04 |
| 02 I3 | F0-02 |
| 02 I4 | F0-09 |
| 02 I5 | F1-01 |
| 02 P1 | F1-01 |
| 02 P2 | F1-03 |
| 02 P3 | F3-04 |
| 02 P4 | F3-06 |
| 02 P5 | F1-07 (b), F1-09 (a) |
| 02 M1 | F1-05, F1-06 |
| 02 M2 | F3-03 |
| 02 M3 | F3-12 |
| 02 M4 | F3-02 (sin renombrar; §6) |
| 02 M5 | F3-12, F1-07 (`rateControl`) |
| 02 M6 | F0-09, F4-04 |
| 02 M7 | F1-14, F4-03 |
| 02 O1 | F4-01 |
| 02 O2 | F1-12 |
| 02 O3 | F0-08 |
| 02 O4 | F4-02 |
| 02 O5 | F1-13, F3-03 |
| 02 O6 | F1-15 |
| 02 N1 | F0-07 |
| 02 N2 | §6 (postergado), F3-07 |
| 02 N3 | F3-07 |
| 02 N4 | F3-02 |
| 03 P01 | F0-01 |
| 03 P02 | F0-03 |
| 03 P03 | F0-04 |
| 03 P04 | F2-02 |
| 03 P05 | F2-01 |
| 03 P06 | F2-05 |
| 03 P07 | F2-06 |
| 03 P08 | F2-07 |
| 03 P09, P10 | F2-10 |
| 03 P11 | F3-08 |
| 03 P12 | F2-09 |
| 03 P13 | F2-04 |
| 03 P14 | F2-11 |
| 03 P15 | F2-15 |
| 03 P16 | F2-03 |
| 03 V1 | §6 (postergado) |
| 03 V2, V3 | F2-13 |
| 03 V4 | §6 (postergado) |
| 03 V5 | §6 (postergado) |
| 03 V6 | F1-07 |
| 03 V7 | F0-10 |
| 03 V8 | F2-14, F3-13 |
| 03 V9 | F4-05 |
| 03 V10 | F2-17 |
| 03 D1 | F1-05 |
| 03 D2 | F1-06 |
| 03 D3 | F1-01, F1-03, F1-11 (aviso exFAT) |
| 03 D4 | F1-09 |
| 03 D5 | F1-11 |
| 03 D6 | F3-01 + F3-03 (§6) |
| 03 D7 | F3-09 |
| 03 D8 | F4-08 |
| 03 D9 | F1-08 |
| 03 D10 | F0-11 |
| 03 D11 | F1-11 |
| 03 D12 | F1-14, F3-10 |
| 03 D13 | F1-16 |
| 03 D14 | F4-06 |
| 03 §3.4 (IFB) | F2-12 |
| 03 §4.5 (colas de transmisión) | F2-16 |
| 03 §6 (equipo) | F4-07 |
| Hallazgo H1 (backoff frena la vigilancia) | F0-05 |
| Hallazgo H2 (salidas no principales no retoman la transmisión) | F1-04 |
| Hallazgo H3 (`linea-aire.json` por `QSaveFile` en el hilo de la UI) | F1-01, F1-02, F1-03 |
