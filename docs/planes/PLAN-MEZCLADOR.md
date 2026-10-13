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

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ MEZCLADOR   balística [Pico|PPM|VU]   escala [dBFS|EBU]   [Analizar sala] [Afinar mic ▾] … │
├───────────────────────────────────────────────────────┬──────────────────────────────────┤
│ TIRAS (una por fuente, con scroll horizontal)          │ ANÁLISIS (de la tira elegida o    │
│ ┌────┐┌────┐┌────┐┌────┐┌────┐┌────┐      ┌────────┐ │ del programa)                     │
│ │Mic1││Mic2││Inv1││Inv2││Rem ││Voz │ …    │PROGRAMA│ │  RTA 1/3 oct: vivo, promedio,     │
│ │chips rack: G C E D L│ (clic = bypass)    │L R  GR │ │  pico retenido, referencia        │
│ │GR ▮                │                     │LUFS M S│ │  Espectrograma (cascada, 30 s)    │
│ │▮▮ medidor pico/RMS │                     │ I  LRA │ │  Goniómetro + correlación         │
│ │   + retención      │                     │ TP     │ │  Nivel en el tiempo: activo,      │
│ │fader -∞…+12        │                     │objetivo│ │  piso de ruido, rango dinámico    │
│ │M  PFL  AFV  ⧖ret   │                     │historia│ │  Avisos: recortes, DC, fase       │
│ │pistas P1…P6        │                     │ 60 s   │ │                                   │
│ └────┘└────┘ …                             └────────┘ │  [Abrir en LSP ▾]                 │
├───────────────────────────────────────────────────────┴──────────────────────────────────┤
│ HERRAMIENTAS  [Analizar la sala] [Afinar micrófono ▾] [Buscar acoples] [Igualar voces]     │
│               [Fase entre micrófonos] [Sincro / latencia] [Dejar listo para transmitir]  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

**Tira de canal** (todo lo que importa en vivo, a la vista):
- Medidor vertical: pico, RMS y pico retenido (2 s), balística elegida, marca
  del objetivo; se pone rojo con recorte.
- **Reducción de ganancia** del rack (lo que bajan el gate, el compresor y el
  limitador: el rack ya mide antes y después de cada módulo).
- **Chips del rack** (G C E D L…): de un vistazo qué procesa; clic = bypass,
  doble clic = abre la página Audio en ese módulo.
- Fader −∞…+12 dB, mute, PFL (escucha), AFV, retardo, y a qué pistas va (P1…P6,
  de la matriz).
- Clic en el nombre: el panel de análisis muestra esa fuente.

**Programa** (la tira de la derecha, más ancha): L/R con pico y RMS, loudness EBU
R128 (M, S, I, LRA, TP) contra el objetivo, historia de 60 s, limitador del
programa (GR).

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

## Cómo se arma (sin que la máquina se caliente)

1. **Modelo de medidores propio** (`audio::Medidores`, C++): un solo temporizador
   (30 Hz, solo mientras alguien lo mira: activación por cuenta, no un bool
   compartido) que junta en arreglos planos lo de todos los canales (pico, RMS,
   retención, GR) y el programa; QML recibe un solo cambio por cuadro.
2. **Dibujo en la GPU**: un `QQuickItem` propio (QSGGeometry) para todas las barras
   de una vez, y otro para el RTA, el espectrograma (textura que se corre) y el
   goniómetro. Nada de Timers de JS ni Repeaters de rectángulos. La balística se
   anima ahí, a la frecuencia de la pantalla, con los datos de 20–30 Hz.
3. **Analyzer donde haga falta**: un analizador por fuente elegida y uno en el
   programa (junto a LoudnessPrograma), más un búfer chico de pares L/R para el
   goniómetro. Solo corre el de la fuente que se mira.
4. **La ventana**: PanelWindow «mezclador» (botón ⧉ en la tarjeta, en la página
   Audio, y `ui_panel_window` en el MCP); los botones del asistente con
   Studio.agent.send.
5. **El MCP** ve lo mismo: `audio_mezclador` (lo de cada tira y el análisis), así
   el asistente lee los mismos números que ve el operador.

## Fases

- **M1** — Ventana, tiras y programa: modelo de medidores, item de barras en la
  GPU, fader, mute, PFL, AFV, retardo, pistas, chips del rack con bypass, GR, loudness.
- **M2** — Análisis: RTA (vivo, promedio, retención, referencia), espectrograma,
  goniómetro y correlación, nivel en el tiempo; Analyzer en el programa y por fuente.
- **M3** — Herramientas: los botones del asistente con sus pedidos, «Abrir en
  LSP», y `audio_mezclador` en el MCP.

Cada fase se prueba en la Mac y en la PC con Windows (CPU con la ventana abierta y
16 canales: que no pase de lo que gasta hoy la página Audio).
