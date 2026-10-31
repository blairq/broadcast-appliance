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
   *Hecho (2026-10-31)*:
   - `escenas::Conmutadores` (registro, uno por salida del motor; se rehace con el aviso
     `Cambio::Salidas`) y `Conmutador` con su salida: poner/armar/take/T-bar sobre la suya;
     armar va al bus de PREVIEW de su salida en el motor (`BroadcastEngine::armar`: la
     vista «armada» ya no arma, solo mira). El AFV es por salida (`escenas::Afv`) y mueve el
     fader de la fuente EN ESA salida (`setSourceVolume(n, v, salida)` → `ponerNivel` de su
     bus). Las transiciones de entrada son de la escena (compartidas). `transiciones.json`:
     lo de siempre es de la por omisión; las demás en `salidas: {id: {afv, afv_niveles}}`.
     `StudioViewModel::conmutador()` sigue siendo el de la por omisión.
   - `BroadcastEngine::Salida` con su escena al aire y armada, DSK/placa/FTB (texto e imagen
     de la placa por salida), volumen y mute por fuente (las demás; la por omisión usa los de
     la fuente), loudness y picos, transmisión principal + extras (ids únicos entre todas),
     grabación (+copia; `ba-<id>-<ts>`), repetición, señal limpia y retorno al gateway
     (`BA.Program` la por omisión; `BA.Salida.<id>` las demás, y el motor escribe su audio en
     `BA.Audio.Salida.<id>`). `crearSalida/borrarSalida/salidaIds`, `fuentesDelPrograma(salida)`,
     `haySalida()` = alguna, `haySalidaEn(id)`, `fuenteAlAire` = en alguna que sale,
     `EngineStatus::salidas` (los campos de siempre son los de la por omisión), foto de
     salidas con `salidas: {id: …}`. `retornoEnUso(si, salida)`: las demás solo mientras el
     gateway dice que alguien las mira (`miranSalidas`). `resetVideo` vuelve a crear las
     salidas con su aire, su armada y su mezcla.
   - Persistencia (`emision/SalidasConfig`): `Config/salidas.json` (tope, de fábrica 4;
     la por omisión siempre), `destinos.json` con `salida`, `Runtime/al-aire.json` con
     `salidas: {id: {escena, streaming, recording}}` además de los campos de siempre (un
     archivo viejo se lee igual; uno nuevo lo entiende un arranque viejo). La escena de las
     demás vuelve siempre al arrancar (solo está ahí); su grabación, tras un corte.
   - `AppRuntime::crearSalida/editarSalida/borrarSalida/setTopeSalidas/salidas()` (en
     caliente; `StudioViewModel` los expone como Q_INVOKABLE) y la propiedad `salidas`
     (lista con aire/armada/transmitiendo/grabando de cada una), que además se manda al
     gateway con `ponerSalidas`.
   - Tests: `test_salidas_config` (salidas.json, destinos, al-aire viejo y nuevo) y `test_afv`.
   *Falta*: las pistas 2-6, los retornos mix-minus, el directo y la selección siguen a la
   por omisión (motor); la grabación de otra salida no lleva pistas extra. La medición de
   sincronía y las alarmas de video (luma/quietud) miran solo la por omisión. Las capas
   (DSK/placa/FTB) siguen sin volver tras `resetVideo` (como antes). `SeguridadAire`,
   `ObjetivoLoudness` y el tally del `BusEventos` siguen siendo de la por omisión (etapas 3-5).
   `RespaldoEntradas` mira las fuentes al aire de todas pero corta en la por omisión. Un test
   de la app con el motor (dos salidas andando con `BroadcastEngine`) no existe todavía:
   lo de abajo del motor lo prueba `test_motor_salidas`.
3. **MCP** — argumento opcional `salida` en las ~60 herramientas; `salidas_listar`,
   `salida_crear`, `salida_editar`, `salida_borrar`; `status` con todas; `McpAire` decide
   «de aire» según qué salida está saliendo; `ArgumentosLocales` con la salida del asistente.
   *Hecho (2026-10-31)*:
   - En el núcleo (`McpCore`): `Tool::porSalida` (la herramienta apunta a una salida; `addTool`
     le suma `salida` al esquema), `CallContext::salida` (la salida por omisión de quien llama:
     su perfil, aparato o asistente; vacía = la del aparato), `setSalidas(lista, sale)`,
     `resolverSalida` (id o nombre → id; "" = la por omisión), `salidaPedida` (args > contexto >
     por omisión) y `salidaDe(args)` (la de la llamada en curso). `callTool` la valida ANTES
     de aprobar o correr (error con las que hay) y la guarda de aire de una herramienta de
     salida mira si sale ESA salida (`haySalidaEn`); las demás, si sale cualquiera. Nadie
     mete la salida en los argumentos.
   - Herramientas de salida: `scene_list`, `scene_switch`, `scene_preview`, `scene_cut`,
     `preview_al_aire`, `transicion_manual`, `scene_define` (`switch`), `audio_afv`,
     `aire_ftb`, `aire_placa`, `aire_placas_listar`, `mixer_list`, `audio_set`, `audio_loudness`,
     `audio_loudness_reset`, `stream_start`, `stream_stop`, `stream_destino_agregar` (el destino
     queda con esa salida), `stream_destino_editar` (con `salida`, lo pasa), `record_start`,
     `record_stop`, `replay_ajustes`, `replay_guardar`, `salida_limpia`, `grafico_mostrar`
     (`armar`). El conmutador lo da un solo helper (`conmutadorDe`, McpSalidasTools.cpp).
   - Nuevas: `salidas_listar`, `salida_crear` (con `recursos` y un `aviso` si la máquina está
     cerca del límite), `salida_editar` (nombre, color, `tope`), `salida_borrar` (de aire).
     `status` trae `salidas` (resumen de cada una: escena, armada, transmite, graba, destinos,
     limpia, repetición, negro/placa).
   - App (agregados mínimos): `SeguridadAire` por salida (placa.json con `salidas: {id: …}`);
     `AppRuntime::setVolume/setMuted(…, salida)` (las demás no se guardan: viven en el motor),
     `startStreaming/stopStreaming/salirAlAireManual(…, salida)` (otra salida exige servidor y
     clave; no se guarda), `ponerReplay(segundos, salida)`; `StudioViewModel::esperarReplay` y
     `armarRepeticion` con salida.
   - `ArgumentosLocales`: con más de una salida el estado trae `salidas` y `salida` (la del
     asistente: la por omisión) y valida `salida` contra ellas; con una, el prompt de siempre.
   - Tests: `test_mcp` (esquema, por id y por nombre, inexistente, contexto, args sobre
     contexto, guarda de aire por salida) y `test_argumentos_locales`.
   *Falta*: `dsk_*` (el modelo `control/Dsk` es de la por omisión: etapa 4),
   `audio_loudness_objetivo` (`ObjetivoLoudness`), `banca_al_aire` (`Bancas` corta en la por
   omisión), `foto_programa` (FrameService, etapa 5), las de escenas que toman «la escena al
   aire» sin `scene` (crear_fuente, source_*, escena_fuentes_ver… siguen con la de la por
   omisión: su guarda de aire tiene que seguir siendo global porque la escena es de todas),
   `stream_salud`/`destinos` ya traen todas. La salida del asistente (y de la voz) no se elige
   todavía: es la por omisión. `docs/mcp.json` sin regenerar (el inventario trae miles de
   líneas ajenas). Las traducciones de las cadenas nuevas.
4. **Control** — temas `aire/<salida>/…` y `salida/<id>/…` en el `BusEventos` (los de hoy
   siguen como la salida por omisión); `salida` en perfiles y aparatos de Botonera y
   ControlWeb, con acción «cambiar de salida»; tarjetas web y plantillas por salida.
5. **Interfaz** — selector de salida en Dirección, Escenas y Mezclador (cada operador ve la
   suya); multiview con todas; página «Salidas» (crear, nombre y color, destinos,
   grabación, codificador); `FrameService` con `program:<id>` / `armada:<id>`.
   *Hecho (2026-10-31)*:
   - **La salida activa** es del modelo: `StudioViewModel::salidaActiva` (con
     `salidaActivaDatos`, `variasSalidas`, `claveProgramaActiva`/`claveArmadaActiva`). Lo que
     la interfaz muestra y hace pasa por el conmutador de la activa (`conmutadorActivo()`):
     `currentScene`, `escenaArmada`, `take`/`takeCon`/`takeScene`/`armarEscena`/`tbar`, el AFV,
     el rol `live` de las escenas (`ScenesModel::setSalida`), el volumen y el mute de las tiras
     (`MixerModel::setSalida` y `audio::Mezclador::setSalida`; `AppRuntime::setVolume/setMuted`
     con `salida`) y el vúmetro del programa. Si se la borra, vuelve sola a la por omisión.
     Lo que no es de la interfaz sigue con la por omisión: el MCP (`conmutador()`), el
     BusEventos (`aire/*`), las pantallas de tally de la sala, el monitor «armada» y las
     alarmas de silencio del programa.
   - El motor avisa (`alCambiarAudioFuente`) también cuando cambia el fader o el mute de una
     fuente en otra salida: el Mezclador se entera aunque lo mueva el AFV de esa salida.
   - `controls/SelectorSalida.qml` (pastillas con el color, AIRE/REC) en Dirección, Aire,
     Escenas (tarjeta y página), la tarjeta Mezclador y el Mezclador: solo con más de una.
   - `FrameService`: `program:<id>` / `armada:<id>` (los de la por omisión = `program` /
     `armada`), pedidos al motor solos mientras alguien los lee y soltados a los 3 s
     (`BroadcastEngine::enableSalidaPreview`); `keys` con el programa de cada salida,
     `colores` y `programas`. Multiview: «Los programas de todas las salidas», con el marco
     y el nombre de cada una en su color.
   - Tally: `Studio.tally` trae `borde` (rojo al aire en la activa, verde armada, el color de
     otra salida si está al aire ahí) y `salidas` (en cuáles está cada fuente, con su color);
     el `VideoWall` los dibuja.
   - Página «Salidas» (riel, grupo Salidas, id `salidas`): crear con nombre y color (el id sale
     del nombre) hasta el tope, con el aviso del margen de la máquina (`nuevaSalida`, de
     `recursos`), el tope, nombre y color en el lugar, «Manejar esta», su programa en vivo,
     al aire y armada, grabar/detener, sus destinos (iniciar, cortar, borrar, «+ Destino…»
     con `salida`) y borrar. El diálogo de destino es uno (`controls/DialogoDestino.qml`),
     también el de la tarjeta Salidas.
   *Falta*: DSK, FTB y placa siguen manejando la por omisión aunque la activa sea otra
   (`SeguridadAire` y `FilaDsk` sin salida); la transmisión principal y el estado de
   aire/grabación de Dirección y Aire son los de la por omisión; el codificador es uno solo
   (se muestra; por salida no se elige todavía); la salida activa no se guarda (vuelve a la
   por omisión al arrancar); el volumen y el mute de las demás salidas no se guardan (viven
   en el motor). El MCP no tiene cómo elegir la salida activa de la interfaz (etapa 3).
6. **Gateway** — un canal por salida (`BA.Salida.<id>` video+audio); retorno de cada
   invitado, cast y sesión remota eligen salida.
   *Hecho del lado del gateway (2026-10-31)*: nombres en `ipc/CanalesSalida.hpp`
   (`BA.Salida.<id>`, `BA.Audio.Salida.<id>`); la app manda la lista con
   `ConferenceClient::ponerSalidas` → `POST /api/v1/salidas {lista:[{id, nombre, color,
   porOmision}]}`; `MediaRoute::salida` en Program/Backstage (`deSalida(id)` para lo que solo
   mira) y `MediaPump` levanta la bajada de cada salida solo mientras alguien la mira; el
   evento «programa» trae `salidas: {id: cuántos}`. Invitado: `POST
   /api/v1/participants/<id>/salida {salida}`; pantalla: `cast-link?salida=<id>`; sesión
   remota: las otras salidas van solas al lado del programa (`salida:<id>`) o la app pone
   `fuente: "salida:<id>"`. Falta: el mix-minus por salida (hoy solo la por omisión).
7. **Línea de tiempo** — la escaleta/línea de tiempo con pistas por salida (diseño aparte,
   investigación en curso: vMix, Sofie, CasparCG/SuperConductor, QLab, Ross…).

Cada etapa compila, pasa sus tests y queda andando con una sola salida igual que hoy
antes de pasar a la siguiente.
