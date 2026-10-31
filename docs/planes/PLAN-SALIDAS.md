# Plan: muchas salidas (2026-10-30)

Dejar de tener «la» salida principal. El aparato tiene **N salidas** y cada una es un
**operador completo**: su programa y su preview, su take, sus transiciones, su DSK, su
FTB y su placa, su **mezcla de audio completa** (sus niveles por fuente, su máster, su
limitador, su loudness, su AFV), sus destinos (otras cuentas de YouTube, RTMP, SRT), su
grabación y su repetición. Lo que se **comparte** entre todas: las entradas (cámaras,
invitados, medios), las escenas, la cadena de tarjetas de cada entrada y el rack de LSP
de cada fuente.

Ninguna salida es especial. Lo que hoy no dice a qué salida apunta (una herramienta
MCP, una plantilla de la botonera, un plugin) va a la **salida por omisión**: la
primera, o la que tenga asignada esa superficie o ese asistente. Así nada de lo que
existe se rompe mientras se migra.

## Decisiones (del usuario, 2026-10-30)

- Cada salida con **su preview y su take** (como varios operadores).
- Cada salida con **su mezcla completa** (no un solo mezclador con «qué suena» por salida).
- Las superficies de control manejan cualquier salida: cada perfil o aparato tiene la
  suya y se puede cambiar desde la misma superficie.
- La tarjeta «Sistema» mide CPU, GPU, RAM y red con historial, y se lee por MCP
  (`sistema_recursos`): es como se sabe cuántas salidas aguanta la máquina.

## Lo que hay hoy (relevamiento)

- **Motor** (`src/motor`): `Composicion` ya se instancia con nombre y publica
  `video:<nombre>` (+ `:limpio` con DSK); conviven programa, preview, multiview y las
  escenas sueltas (`vEscenas_`). Lo único es el **estado del aire**, en miembros sueltos
  de `Motor`: `vPrograma_`, `vPreview_`, `alAire_`, `armada_`, `dskEscenas_`, `programa_`
  (Mezcla con limitador), `puente_` a `BA.Audio.ContentForReturn`, `tomaAudio_`,
  `retornos_`. Claves fijas en `Vistas` («programa», «preview», «limpio»),
  `productorDeVideo`, `productorDeBus("programa")`, `estado()`.
- **Audio**: «en escena» es **un flag por canal** (`Canal::enEscena`) que `aplicar()` lleva a
  todos los buses (programa, pistas 2-6, retornos, directo). El AFV de la app mueve el
  **fader global** de la fuente (`Conmutador.cpp`). Es lo más delicado del cambio.
- **Salidas del motor**: `motor::Fuentes{video, audios}` ya es genérico; lo que ata al
  programa es `fuentesDelPrograma()` (`BroadcastEngine.cpp`).
- **BroadcastEngine**: una transmisión principal (`stream_`, `YaOtroDestino`) + `extras_`
  (N transmisiones por id, cada una con su codificador), una grabación (+ copia), una
  repetición, una señal limpia, `programVideo_` (`BA.Program`) y 4 canales de escena
  (`BA.Escena.<k>`). `EngineStatus` con un solo streaming/recording.
- **App**: `Conmutador` es la mesa de corte (take, armar, T-bar, AFV); el cue de escena del
  guion lo saltea (`rt_.setScene` directo). `StudioViewModel` publica `aire/escena`,
  `aire/armada`, `aire/tally`; `FrameService` tiene dos ranuras (program, armada).
  ~25 QML lo suponen único.
- **MCP**: ~60 de 724 herramientas tocan el programa explícitamente, ~15 implícitamente;
  `McpAire.cpp` (lista «de aire») y `ArgumentosLocales.cpp` (escena actual/armada).
- **Control**: `Acciones.cpp`, plantillas de la `Botonera`, temas del `BusEventos` sin id;
  perfiles y páginas por aparato (botonera, Companion, Stream Deck, MIDI, OSC, web) ya
  existen: ahí va la salida.
- **Gateway**: `BA.Program` + `BA.Audio.ContentForReturn`; `MediaRoute` Program/Escena(k);
  sesión remota y cast ya hablan de «canal».
- **Persistencia**: `Config/app.json` (stream, encoder, record, transition, currentScene),
  `destinos.json`, `transiciones.json`, `dsk.json`, `Runtime/al-aire.json`.

## Modelo

```
Salida {
  id, nombre, color                       // el color va en tally, multiview y superficies
  programa, preview: Composicion          // video:<id>, video:<id>:limpio, video:<id>:preview
  alAire, armada, transicion, dsk, ftb, placa
  mezcla: Mezcla                          // bus:<id> — niveles, mute, AFV, máster, limitador, loudness
  enEscena: set<entrada>                  // por salida (hoy es global)
  destinos[], grabacion, repeticion, limpia
  retorno: canal hacia el gateway         // BA.Salida.<id> (video) + su audio
}
```

- **Audio por salida**: cada entrada procesa su cadena una sola vez (compartida) y se
  reparte a un bus por salida con **su propia ganancia y mute por salida**; el AFV y el
  «en escena» son por salida. Las pistas 2-6 y los mix-minus de los invitados salen de
  la salida que se elija (por omisión, la del invitado o la por omisión).
- **Tally**: una entrada está «al aire» si lo está en alguna salida; el tally dice en
  cuáles (con el color de cada una).
- **Límite**: tope configurable de salidas; al crear una, `sistema_recursos` dice si hay
  margen y la app avisa cuando la máquina no da (alarmas sostenidas de CPU/GPU/codificador).

## Etapas

1. **Motor** — `struct Salida` con los miembros de aire de `Motor`; `map<string, Salida>`;
   todas las operaciones (`armar`, `take`, `alAire`, `tbar`, `dsk`, `masterVolumen`,
   `ponerLimitador`, `escenaAlAire`, `productorDeVideo/Bus`, `estado`) con `salida`
   (por omisión la primera). `enEscena` y ganancias por salida en `aplicar()`. Tests del
   motor: dos salidas con escenas distintas, la misma escena en las dos, audio que sigue
   a cada una, crear/borrar una salida en caliente.
2. **App** — `Conmutador` por salida (con su AFV sobre su bus); `BroadcastEngine` con
   `salidas_` (transmisiones + extras, grabación, repetición, limpia, FTB/placa/DSK,
   loudness, retorno); `fuentesDelPrograma(salida)`; `Config/salidas.json` y `destinos.json`
   con `salida`; `Runtime/al-aire.json` por salida (corte de luz: vuelve cada una como
   estaba). El cue de escena del guion pasa por el `Conmutador` de su salida.
3. **MCP** — argumento opcional `salida` en las ~60 herramientas; `salidas_listar`,
   `salida_crear`, `salida_editar`, `salida_borrar`; `status` con todas; `McpAire` decide
   «de aire» según qué salida está saliendo; `ArgumentosLocales` con la salida del asistente.
4. **Control** — temas `aire/<salida>/…` y `salida/<id>/…` en el `BusEventos` (los de hoy
   siguen como la salida por omisión); `salida` en perfiles y aparatos de Botonera y
   ControlWeb, con acción «cambiar de salida»; tarjetas web y plantillas por salida.
5. **Interfaz** — selector de salida en Dirección, Escenas y Mezclador (cada operador ve la
   suya); multiview con todas; página «Salidas» (crear, nombre y color, destinos,
   grabación, codificador); `FrameService` con `program:<id>` / `armada:<id>`.
6. **Gateway** — un canal por salida (`BA.Salida.<id>` video+audio); retorno de cada
   invitado, cast y sesión remota eligen salida.
7. **Línea de tiempo** — la escaleta/línea de tiempo con pistas por salida (diseño aparte,
   investigación en curso: vMix, Sofie, CasparCG/SuperConductor, QLab, Ross…).

Cada etapa compila, pasa sus tests y queda andando con una sola salida igual que hoy
antes de pasar a la siguiente.
