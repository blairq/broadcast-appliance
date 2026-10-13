# Plan: el Mezclador (ventana propia)

Pedido (2026-10-12): un botón en la tarjeta Mezclador del tablero que abra, en su
propia ventana (para otro monitor), «el mejor mezclador del universo»: vúmetros,
análisis, y herramientas para analizar la sala, afinar micrófonos y buscar
acoples (botones con pedidos al asistente). Pensado como lo armaría un técnico de
sonido que siempre laburó con una consola medio pelo y ahora tiene los LSP.

## Lo que hay (relevado antes de diseñar)

- **LSP 1.2.35, el bundle entero** (202 variantes): analizador de espectro x1–x16,
  osciloscopio, detector de fase, medidor de latencia, profiler (respuesta e
  impulso), EQ paramétrico x8–x32 y gráfico x16/x32, crossover, compresor, gate,
  expansor, dinámica dibujada, multibanda (compresor, gate, expansor, limitador,
  clipper), sidechain de todo, limitador, clipper, de-esser, autogain, matcher y
  referencer (igualar a una referencia), A/B, comp delay, generadores de ruido y
  oscilador, y nuestra matriz.
- **Los LSP NO publican sus medidores por CLAP** (ClapHost.hpp): su espectro, su
  fase o su osciloscopio no se pueden dibujar en la app. Sí se puede abrir **su
  interfaz** (ventana nativa) con un botón: openEditor.
- **Nuestro Analyzer** (src/app/audio/Analyzer.*), en el hilo de audio: pico/RMS por
  canal, LUFS M/S/I, 30 bandas de tercio (FFT 4096) con promedio y acumulado,
  correlación estéreo, DC, recortes, piso de ruido, nivel activo, historial 30 s.
  Hoy solo se ve el del nodo elegido en la página Audio.
- **Loudness del programa** (LoudnessPrograma: BS.1770-4, TP x4, LRA) y objetivo.
- **Niveles por canal**: el motor (Mezcla) mide pico y RMS de cada rama cada 50 ms.
- **Medición fuera de línea** (`audio_measure`: respuesta/curva de un rack) y
  **generador** (seno, rosa, blanco, barrido), **sincro** (audio_sincro_medir).
- **Ventanas sueltas**: PanelWindow (ui_panel_window) y PageWindow.
- **Asistente**: Studio.agent.send(contexto + pedido) (en una ventana suelta no se
  puede usar ApplicationWindow.window.pedirAlAsistente).

Lo que NO sirve tal cual: los medidores de hoy (LevelMeter: un Timer de JS por
medidor; la página Audio rehace el grafo entero cada 160 ms) no aguantan 16
canales con espectro en otra pantalla sin calentar la máquina.

## La ventana

Lo que maneja (pedido del usuario, 2026-10-12): **lo que sale de cada entrada y
adónde va**, con la **matriz de salida** a mano. No se mete adentro de los racks
(eso sigue en la página Audio).

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ MEZCLADOR   balística [Pico|PPM|VU]  [Matriz ▾]  [Análisis ▸]  [Medir ▾]  [Asistente ▾]    │
├─────────────────────────────────────────────┬──────────────────────┬─────────────────────┤
│ ENTRADAS (lo que sale de cada una)           │ SALIDAS              │ ANÁLISIS (plegable)  │
│ ┌────┐┌────┐┌────┐┌────┐┌────┐┌────┐         │ ┌──────┐┌──┐┌──┐┌──┐ │ de la tira elegida   │
│ │Mic1││Mic2││Inv1││Inv2││Rem ││Voz │ …       │ │PROGR.││P2││P3││Sa│ │ o de una salida:     │
│ │ ▮▮ vúmetro + retención     │               │ │L R GR││  ││  ││la│ │ RTA, cascada,        │
│ │ GR ▯ (lo que baja su rack) │               │ │LUFS  ││▮▮││▮▮││▮▮│ │ goniómetro,          │
│ │ fader −∞…+12               │               │ │M S I ││  ││  ││  │ │ nivel en el tiempo,  │
│ │ M  PFL  AFV  ⧖              │               │ │LRA TP││  ││  ││  │ │ mediciones guardadas │
│ │ envíos: [P1][P2][Sala]     │               │ │hist. ││  ││  ││  │ │ superpuestas         │
│ └────┘└────┘ …                               │ └──────┘└──┘└──┘└──┘ │                      │
├─────────────────────────────────────────────┴──────────────────────┴─────────────────────┤
│ MATRIZ (se despliega con «Matriz»): fuentes × destinos (Programa, pistas 2–6, buses        │
│ virtuales, salidas de la placa). Clic = prende/apaga el envío; doble clic = nivel, pan,    │
│ mute. Clic en un envío de una tira resalta su fila; clic en una salida, su columna.         │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

**Tira de entrada** (lo que sale de la entrada, después de su rack):
- Vúmetro: pico, RMS y pico retenido (2 s), balística elegida; rojo con recorte.
- **GR**: cuánto está bajando la dinámica de su rack (un indicador, no los
  módulos). Un botón abre su rack en la página Audio si hace falta.
- Fader −∞…+12 dB, mute, PFL (escucha), AFV, retardo.
- **Envíos**: chips de los destinos a los que va (de la matriz); clic resalta la
  fila en la matriz.
- Clic en el nombre: el análisis muestra esa entrada.

**Salidas**: Programa (ancha: L/R, loudness EBU R128 M/S/I/LRA/TP contra el
objetivo, historia de 60 s, GR del limitador), las pistas 2–6, los buses virtuales
y las salidas de la placa, cada una con su vúmetro; clic resalta su columna.

**Análisis** (de la tira elegida o del programa):
- **RTA de tercio de octava**: vivo, promedio, pico retenido, y una curva de
  referencia que se guarda («así sonaba bien»).
- **Espectrograma** (cascada de 30 s): acoples y zumbidos se ven como rayas.
- **Goniómetro** (nube L/R) **y correlación**: fase y mono-compatibilidad.
- **Nivel en el tiempo**: activo, piso de ruido y rango dinámico (el Analyzer ya
  los calcula).
- **Abrir en LSP ▾**: inserta en el rack de esa fuente y abre la interfaz nativa
  del Spectrum Analyzer x8, Phase Detector, Oscilloscope o Profiler, para el
  detalle fino que nuestra vista no tiene.

**Herramientas** (botones que le encargan el trabajo al asistente con las
herramientas que ya tiene; el pedido lleva la fuente elegida y el contexto):
- *Analizar la sala*: ruido rosa por la salida de sala (pide confirmar que nadie
  esté al aire), mide con el micrófono elegido, compara la curva y propone el EQ
  de sala (paramétrico en la salida), sin aplicar nada sin preguntar.
- *Afinar micrófono ▾* (elegís cuál): pide 10 s hablando y 5 s callado; con el
  piso de ruido y el espectro propone gate, compresor, de-esser y EQ.
- *Buscar acoples*: busca picos angostos que crecen en el espectrograma (o con un
  barrido) y propone filtros notch.
- *Igualar voces*: loudness de cada micrófono y ganancia para que todos queden
  parejos (o autogain).
- *Fase entre micrófonos*: correlación entre dos fuentes (o el Phase Detector de
  LSP) y si hay que invertir polaridad o retrasar.
- *Sincro / latencia*: audio_sincro_medir.
- *Dejar listo para transmitir*: objetivo de loudness y limitador del programa.

## Cómo se arma

**Pedido del usuario (2026-10-12):** integrar el código de LSP; el mezclador lo maneja
el asistente por el MCP, así que tiene que poder TOMAR mediciones, guardarlas y
analizarlas; la parte gráfica es muy importante.

### 0. El DSP de LSP adentro: `ba-lspdsp` (biblioteca propia, API en C)

LSP compila cada biblioteca en un objeto (`lsp-common-lib.o`, `lsp-lltl-lib.o`,
`lsp-runtime-lib.o`, `lsp-dsp-lib.o`, `lsp-dsp-units.o`) en los tres builds que ya
hacemos (tools/deps/lsp-*.sh). En Windows LSP se compila con MinGW y la app con el
ABI de MSVC: no se pueden llamar sus clases de C++ desde la app. Por eso:

- `src/lspdsp/ba_lspdsp.cpp` (adentro, C++ de LSP) se compila **con el mismo
  compilador que LSP** y se enlaza con esos objetos en una biblioteca compartida
  `ba-lspdsp.{dylib,dll,so}` que viaja en el bundle (LGPL: queda aparte y se puede
  reemplazar).
- `src/lspdsp/ba_lspdsp.h` es una **API en C** (objetos opacos, arreglos de float):
  no le importa el ABI de C++. La app la carga en tiempo de ejecución (QLibrary);
  si falta, el mezclador anda con el Analyzer propio.
- Lo que expone (lsp-dsp-units):
  - medidores: PeakMeter, TruePeakMeter, LoudnessMeter/ILUFSMeter (M, S, I, LRA),
    Correlometer, Panometer;
  - análisis: Analyzer (el FFT del Spectrum Analyzer: resolución, ventana,
    envolvente, reactividad), MeterGraph (historias);
  - medición activa: SyncChirpProcessor (lo del Profiler: chirp, convolución
    lineal y no lineal → respuesta impulsiva, respuesta en frecuencia, RT60 con
    integración de Schroeder calibrada, distorsión armónica, si el ruido de fondo
    alcanza), LatencyDetector, ResponseTaker;
  - señales: Generator (rosa, blanco, MLS, velvet), Oscillator.

### 1. Mediciones como cosa de primera clase (el asistente las toma y las lee)

`audio::Mediciones` (modelo C++): trabajos que se piden, corren en el fondo y dejan
un resultado guardado en `Mediciones/<fecha>-<tipo>.json` (+ el .wav de la respuesta
impulsiva): respuesta de sala (chirp por la salida X, captura por el micrófono Y →
respuesta en frecuencia, RT60/EDT por banda, latencia, si el ruido de fondo
alcanza), ruido de fondo, perfil de un micrófono (voz contra silencio), latencia,
fase entre dos fuentes, búsqueda de acoples, y la «foto» del espectro en vivo.
- MCP: `medicion_tomar` (tipo, fuente, salida, duración; asíncrona, devuelve id),
  `medicion_ver` (datos compactos: bandas, curva reducida, RT60 por banda,
  avisos), `medicion_listar`, `medicion_comparar` (dos mediciones, diferencia por
  banda), `medicion_borrar`; y `mezclador_estado` (lo mismo que ve la ventana).
- GUI: cada medición se dibuja y se puede superponer con otra (antes/después).

### 2. Medidores en vivo sin que la máquina se caliente

- Modelo de medidores propio (`audio::Medidores`): un solo temporizador (30 Hz,
  solo mientras alguien lo mira: activación por cuenta, no un bool compartido) que
  junta en arreglos planos lo de todos los canales (pico, RMS, retención, GR) y el
  programa; QML recibe un solo cambio por cuadro.
- Dibujo en la GPU: `QQuickItem` propios (QSGGeometry) para las barras, el RTA, el
  espectrograma (textura que se corre), el goniómetro y las curvas de medición. La
  balística se anima ahí, a la frecuencia de la pantalla. Nada de Timers de JS ni
  Repeaters de rectángulos.
- La ventana: PanelWindow «mezclador» (⧉ en la tarjeta, en la página Audio y
  `ui_panel_window`); los botones del asistente con Studio.agent.send.

## Fases

- **L0** — `ba-lspdsp` en los tres sistemas (scripts de LSP, API en C, carga en la
  app, test que mide un seno y un ruido rosa conocidos).
- **M1** — Ventana: entradas (vúmetro, GR, fader, mute, PFL, AFV, retardo, envíos),
  salidas (Programa con loudness de LSP, pistas, buses, placa) y la matriz
  desplegable; modelo de medidores e items en la GPU.
- **M2** — Análisis en vivo: RTA/FFT de LSP (vivo, promedio, retención,
  referencia), espectrograma, goniómetro y correlación, nivel en el tiempo.
- **M3** — Mediciones activas (respuesta de sala con RT60, ruido de fondo, perfil de
  micrófono, latencia, fase, acoples) con su MCP y sus gráficos superpuestos.
- **M4** — Herramientas: botones del asistente con sus pedidos y «Abrir en LSP».
- **C1–C4** — Calibración y ecualización de sala (multipunto, micrófonos sin
  calibrar con sus límites, modos, retardos por parlante, verificación): ver
  docs/planes/PLAN-CALIBRACION-SALA.md.

Cada fase se prueba en la Mac y en la PC con Windows (CPU con la ventana abierta y
16 canales: que no pase de lo que gasta hoy la página Audio).
