# Plan: calibración acústica y ecualización de sala (desde el Mezclador)

Pedido (2026-10-12): desde el Mezclador, calibrar la sala y ecualizar, con
micrófonos que no son de medición (un SM58, por ejemplo), midiendo en distintos
lugares (donde están los parlantes, los micrófonos, las paredes). «Un plan de
ecualización de sala real, no una idiotez.»

Esto sigue el método de las herramientas serias (REW, Smaart, Genelec GLM, Dirac,
Trinnov) y la literatura de base: Farina (barrido senoidal exponencial, 2000),
Schroeder (frecuencia de Schroeder, integración hacia atrás para el RT60), Toole
(*Sound Reproduction*: qué se puede y qué no se puede arreglar con EQ), ISO 3382
(parámetros de sala). Todo lo que se mide y se calcula lo hace ba-lspdsp con el
DSP de LSP (SyncChirpProcessor: el mismo del plugin Profiler).

## Qué arregla una EQ de sala y qué no (para no prometer lo imposible)

- **Sí**: el exceso de graves por modos de la sala (picos que aparecen parecidos en
  toda la zona de público), el balance tonal general (la sala o los parlantes
  suenan opacos o chillones), las diferencias de nivel y de llegada entre
  parlantes (retardo y nivel por parlante), la polaridad invertida.
- **No**: los pozos (cancelaciones: dependen del lugar, levantarlos con EQ gasta
  potencia y empeora otros lugares); la reverberación (eso es tratamiento
  acústico); los problemas que solo están en un punto. Arriba de la frecuencia de
  Schroeder, la respuesta en un punto es campo reverberante y reflexiones: no se
  corrige punto por punto, se corrige el promedio y con curvas anchas.
- **Acoples** (realimentación de los micrófonos en vivo) son otra medición: se
  buscan con los micrófonos abiertos (ring-out) y se tratan con filtros angostos
  aparte de la EQ de sala.

## El micrófono de medición (lo más importante para que sirva)

La respuesta de un micrófono no se puede medir sin una referencia: medir el SM58
con un parlante mide parlante × micrófono × sala, todo junto. Opciones, de mejor a peor:

1. **Micrófono de medición con calibración individual** (omnidireccional, plano):
   miniDSP UMIK-1/UMIK-2 (USB, trae su archivo de calibración por número de serie),
   Dayton EMM-6, Earthworks M23. Es lo que se usa; el sistema lee el archivo de
   calibración (formato de texto frecuencia/dB, el de REW).
2. **Calibrar el SM58 por sustitución** contra uno de esos (una vez): los dos en el
   mismo punto, a 1 m de un parlante, en el eje, se mide con cada uno; la diferencia
   es la corrección de ESE SM58 en esa posición. Desde ahí sirve para medir (con
   la misma orientación) y para vigilar la sala en vivo.
3. **Sin referencia**: la curva publicada del modelo (Shure SM58: caída bajo 100 Hz
   más efecto de proximidad, realce de presencia de +4/+6 dB entre 4 y 6 kHz). Error
   esperable por unidad: ±2–3 dB. Con esto: la corrección de graves (modos) es
   confiable —lo que importa es el pico contra sus vecinas—; arriba de ~2 kHz solo
   se corrigen tendencias anchas (pendiente), nunca detalles.
4. **Cardioide** (SM58): se apunta al parlante que se mide, a más de 1 m (sin
   efecto de proximidad), en el eje; los resultados de agudos dependen del ángulo.
   Se registra la orientación.

El sistema guarda cada micrófono con su calibración (archivo, sustitución o curva
del modelo) y su confiabilidad por rango de frecuencias, y la usa para limitar qué
correcciones propone.

## El procedimiento

### 1. Preparar
- Croquis de la sala: rectángulo con medidas (largo, ancho, alto), parlantes
  (posición, a dónde apuntan), zona de público/escucha, micrófonos de la mesa,
  paredes/ventanas que importen. Volumen → frecuencia de Schroeder
  fs ≈ 2000·√(RT60/V) (con el RT60 que se mide en el paso 3).
- Seguridad: nada al aire (lo verifica el sistema), micrófonos en vivo cerrados
  (evita acoples), nivel que sube de a poco hasta 75–85 dB SPL en la escucha.
- Ruido de fondo: se mide en silencio por bandas; la medición exige una relación
  señal/ruido de al menos 40 dB por banda (LSP lo informa: «background noise
  optimality»); si no alcanza, se sube el nivel o se repite.

### 2. Medir cada punto (barrido exponencial, Farina)
- Por cada parlante (o grupo) y cada punto: barrido senoidal exponencial de 20 Hz
  a 20 kHz, 3–6 s, por la salida de ese parlante; se graba el micrófono.
- La convolución con el barrido inverso da la **respuesta impulsiva**; la
  distorsión armónica queda separada en el tiempo (la convolución no lineal de LSP
  la entrega aparte: sirve para saber si el parlante está saturando).
- **Referencia de tiempo**: el retardo del sistema se mide primero con el detector
  de latencia (o un lazo eléctrico salida→entrada), así todas las respuestas quedan
  alineadas en el tiempo: se sabe cuándo llega cada parlante a cada punto.
- **Puntos**: 5 a 9 en la zona de escucha (no simétricos, a la altura de los oídos,
  a más de 1 m de las paredes), más una medición cercana de cada parlante (≈1 m,
  en el eje) para separar lo que es del parlante de lo que es de la sala.
  Cada punto se marca en el croquis.

### 3. Analizar
- Por punto: respuesta en frecuencia (corregida por el micrófono), respuesta
  impulsiva y su curva energía-tiempo (reflexiones tempranas: de qué pared vienen,
  por el tiempo de llegada y el croquis), RT60 por octava (T20/T30, integración de
  Schroeder), EDT, C50 (claridad de la palabra).
- **Promedio espacial**: promedio de potencia de las magnitudes de todos los
  puntos de escucha (no el promedio en dB, no un punto solo).
- **Suavizado dependiente de la frecuencia**: bajo fs, resolución alta (1/24 de
  octava) para ver los modos; arriba, 1/3–1/6 de octava (lo que el oído integra).
- **Modos**: picos bajo fs que aparecen en la mayoría de los puntos (no en uno
  solo); frecuencia, altura y ancho (Q). Se cruzan con los modos teóricos de la
  sala rectangular (c/2·√((nx/L)²+(ny/W)²+(nz/H)²)) para decir de qué dimensión vienen.
- **Varios parlantes**: tiempo de llegada y nivel de cada uno en cada punto →
  retardo y ganancia para que lleguen juntos y parejos a la zona de escucha;
  polaridad (por el signo del pico de la respuesta impulsiva).

### 4. Proponer (con su porqué, sin aplicar nada solo)
- **Curva objetivo**: para refuerzo de la palabra y salas chicas, plana con una
  pendiente suave (−0,5 a −1 dB/oct hacia los agudos en la medición en sala, como
  propone Toole para la respuesta en régimen permanente); para monitoreo,
  prácticamente plana. Editable.
- **Bajo fs**: solo cortes (filtros paramétricos en la frecuencia y el Q de cada
  modo, hasta la altura del exceso); ningún realce en pozos. Si un pozo es grande,
  se propone mover el parlante o el subgrave (se ve en el croquis) en vez de EQ.
- **Arriba de fs**: pocos filtros anchos (estantes, Q bajo) hacia la curva
  objetivo; realce máximo +3 dB, corte máximo −6/−9 dB; nada arriba de lo que el
  micrófono es confiable.
- **Retardos y niveles** por parlante; polaridad.
- Cada filtro con su motivo («modo axial del largo, 47 Hz, +8 dB en 7 de 9 puntos»).

### 5. Aplicar y verificar
- La EQ va en la salida de sala (un EQ paramétrico de LSP en el rack de ese bus) y
  los retardos con comp_delay; se guarda como «calibración» con fecha (versión,
  se puede volver atrás).
- **Verificación**: se repiten los puntos y se superpone antes/después contra la
  curva objetivo; si algo empeoró, se revisa.

## Lo que hay que construir

- **ba-lspdsp, parte de medición** (M3 del Mezclador): SyncChirpProcessor (barrido,
  respuesta impulsiva, convolución no lineal, RT60, ruido de fondo), LatencyDetector,
  ventanas temporales y suavizados.
- **Modelo `audio::Calibracion`**: la sala (croquis, medidas), los parlantes, los
  micrófonos (con su calibración), los puntos, cada medición (respuesta impulsiva
  en .wav, magnitud, parámetros), el análisis, la propuesta, lo aplicado y la
  verificación. Todo en `Mediciones/Calibraciones/<fecha>/` (JSON + .wav).
- **MCP** (el asistente maneja la calibración):
  `calibracion_crear` (sala, medidas, parlantes), `calibracion_microfono`
  (calibración: archivo, sustitución, curva del modelo), `calibracion_ruido_fondo`,
  `calibracion_medir_punto` (parlante, punto, micrófono → respuesta, SNR, RT60;
  asíncrona), `calibracion_analizar` (promedio, modos, fs, llegadas → datos
  compactos), `calibracion_proponer` (filtros, retardos, niveles con su motivo),
  `calibracion_aplicar`, `calibracion_verificar`, `calibracion_revertir`,
  `calibracion_ver` (todo lo de una calibración).
- **GUI en el Mezclador** (pestaña Calibración): el croquis (dibujar la sala,
  ubicar parlantes, micrófonos y puntos; cada punto muestra si ya está medido), las
  curvas (cada punto finito, el promedio grueso, el objetivo, la propuesta y la
  verificación), la curva energía-tiempo, el RT60 por banda, y los filtros
  propuestos sobre la curva, cada uno con su motivo; botones «Medir este punto»,
  «Analizar», «Aplicar», «Verificar». El asistente puede guiar paso a paso
  («poné el micrófono en el punto 3, apuntando al parlante izquierdo»).

## Fases

- **C1** — Medición de un punto de punta a punta: barrido por una salida, captura
  por una entrada, respuesta impulsiva, magnitud, RT60, SNR; guardado y MCP; gráfico.
- **C2** — Micrófonos y calibración (archivo de calibración, sustitución, curva
  del modelo) y sus límites de confianza.
- **C3** — Sesión multipunto con croquis, promedio espacial, modos, llegadas.
- **C4** — Propuesta, aplicación (EQ de LSP en la salida de sala, retardos),
  verificación antes/después, versiones.

## Estado (2026-10-13)

Hecho C1–C4 en código, probado con la **sala simulada** (sin sonido):

- Medición: `ba_medicion_*` en ba-lspdsp (SyncChirpProcessor + ResponseTaker del
  Profiler). Se procesa de a una muestra: con bloques, el «time warp» del
  ResponseTaker da un comienzo de captura negativo y LSP lee fuera del buffer. El
  directo es la PRIMERA llegada (no el pico: piso y techo a la misma distancia suman
  más que él). RT por banda con el rango dinámico de ISO 3382 (25/35/45 dB).
- `audio::Calibracion` (modelo), `CalibracionAnalisis` (cuentas, `test_calibracion`),
  `LazoMedicion` (QAudioSink/QAudioSource compartidos; sala simulada con `--test` o
  `BA_SALA_SIMULADA`), pestaña Calibración del Mezclador, MCP `calibracion_*`.
- Micrófonos: plano, curva del modelo (SM58, SM57, Beta 58A, e835, desconocido),
  archivo (REW/miniDSP) y sustitución contra una referencia.

Falta: medir con audio real (de día), probar «Aplicar» sobre un rack de prueba (en
la Mac no se tocan los racks del usuario), aplicar retardos y niveles por parlante
(hoy se informan), distorsión armónica (la convolución no lineal de LSP).
