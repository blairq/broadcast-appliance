# Línea de tiempo del evento: investigación y diseño

No modifiqué ningún archivo. Hay cosas de los productos que no pude leer en la documentación oficial, porque la página daba 404 o no lo explicaba. Esas partes las completé con lo que ya sabía y las marco con *(conocimiento general)*.

---

## 1. Cómo lo resuelven los demás

### 1.1 Las tres familias

1. **Lista de pasos con GO manual** (QLab, macros de ATEM, XPression Sequencer, ProPresenter sin timeline, la List y la Playlist de vMix). Es fácil de operar y se aguanta bien los desvíos, pero no sabe nada del tiempo de reloj.
2. **Línea de tiempo absoluta** (Disguise, Millumin, la timeline de ProPresenter, SuperConductor). Es precisa y se ve muy bien, pero sufre con el vivo: si el orador se pasa, la línea sigue corriendo.
3. **Escaleta de automatización híbrida** (Sofie, Viz Mosart, Ross OverDrive, GV Ignite, Sony ELC, Cuez Automator). La unidad que entra es una **parte** y avanza con un TAKE manual. *Adentro* de cada parte hay una mini línea de tiempo relativa con capas: zócalos, audio, gráficos. Arriba hay un cálculo de tiempos con back-timing y over/under. **Es el modelo que nos sirve**, porque en vivo manda el humano y lo de adentro va solo.

### 1.2 Producto por producto

**Sofie (NRK / SuperFly.tv)**, la referencia más completa y abierta.
- **Modelo:** Playlist → Rundown → Segment (el "capítulo", una fila horizontal) → **Part** (la unidad que entra con TAKE) → **Piece** (lo que pasa adentro: cámara, VT, gráfico, guion del conductor). Además hay **AdLibs** (piezas que se disparan a mano desde el *Shelf*) y **Buckets** (adlibs del operador, para la urgencia). Los **Blueprints** son plugins que traducen la escaleta editorial a partes y piezas. Antes de salir al aire se hacen **PartInstances/PieceInstances**, copias congeladas que separan lo *planeado* de lo *que se emitió*.
- **Tiempos:** se avanza con TAKE (F12). *Next* es la parte siguiente y se mueve con F9/F10. *Lookahead* precarga lo que viene y lo pone en preview. Una parte con `autoNext` se toma sola al cumplir su `expectedDuration` (más de 1000 ms), y con `autoNextOverlap` se pisa con la siguiente. Hay transiciones de entrada y de salida, *preroll* (el timeline adelanta la pieza para que esté lista a tiempo) y *postroll*. Una pieza vive dentro de su parte o es **infinita**: sigue en las partes siguientes y conserva su hora real de arranque. Los **Holds** existen como acción del operador, pero no pude leer la página que los explica.
- **Capas y salidas:** cada pieza va a una *source layer* (lo que ve el operador) y a una *output layer*. El Core genera un **timeline de estado objetivo** y el TSR lo traduce a cada dispositivo.
- **Vistas:** Timeline (las piezas en su tiempo, con la historia), **Storyboard** (miniatura grande de la pieza principal, con scrub al pasar el mouse) y **List**. Tiene una línea On Air y una Next, un *presupuesto de tiempo por segmento* (se pone rojo con "+" si se pasa) y una cuenta regresiva al próximo segmento. Tiene pantallas aparte por URL: Prompter, Director, Presentador, overlay para el presentador, y **Cámara** (lo que viene para cada camarógrafo, filtrado por capa).
- **Lo bueno:** separar lo planeado de lo emitido, las piezas infinitas, el lookahead a preview y una pantalla para cada rol. **Lo malo:** es muy pesado (Meteor/Mongo, blueprints en TypeScript) y está pensado para noticieros con NRCS.

**SuperFly Timeline (la librería)**
- Objetos con `layer` y una regla `enable` (`start`, `end`, `duration`, `while`, `repeating`). Pueden referirse a otros objetos: `#id.end + 5`, `.clase`, `$capa`, con operadores booleanos (`#a & !#b`). Hay grupos (los hijos quedan acotados al padre) y keyframes. **En cada capa hay un solo objeto activo a la vez:** gana la prioridad y, si empatan, el que empezó más tarde.
- La resolución es pura: `resolveTimeline()` calcula los instantes absolutos y `getResolvedState(t)` devuelve qué hay en cada capa en el momento t, más los `nextEvents`. **Es exactamente el resolvedor que conviene copiar como idea** en C++ puro y con tests.

**SuperConductor y el CasparCG Client**
- SuperConductor: rundowns con **grupos** que tienen sus capas. El modo *single item* es una playlist con auto-next y loop; el *multi item* deja varias cosas a la vez, para gráficos. Controla Caspar, ATEM, HyperDeck, OBS, vMix, OSC y HTTP con el TSR de Sofie. Se maneja con Stream Deck y X-keys.
- CasparCG Client: rundown con ítems y grupos con auto-play o auto-step y delays. Hay quejas porque no tiene cuenta regresiva de lo que queda en un grupo y porque el loop es incómodo.

**QLab**
- **Modelo:** listas de cues (con un GO en el *playhead*) y **cue carts**, una grilla de botones que se disparan sueltos. Cada cue tiene *pre-wait* y *post-wait* y un modo de continuación: **auto-continue** (dispara el siguiente al dispararse, respetando el post-wait) o **auto-follow** (dispara el siguiente cuando termina). Los grupos pueden ser **timeline** (el viejo *fire all*: arrancan todos juntos y cada uno se ubica con su pre-wait), *start first and go to next*, *playlist* o *random*. "Load to time" carga el estado a un instante.
- **Lo bueno:** distinguir auto-continue de auto-follow, y el cue cart para lo que no tiene orden. **Lo malo:** con cadenas largas de auto-follow se pierde de vista qué va a pasar, y la duración de un grupo solo es determinista en modo timeline.

**vMix**
- **Playlist:** ítems (entradas) con posición de arranque, duración (0 = lo que dure la entrada) y transición propia. Avanza sola por defecto; con *Manual Mode* espera el Next. Está atada al Preset.
- **List input:** una lista de archivos dentro de una sola entrada, con *Auto Next*. NextItem no vuelve al principio. **Triggers por entrada:** OnTransitionIn/Out, OnOverlayIn/Out, OnCompletion, OnCountdownCompleted, OnVideoCallConnected, OnPlaybackTime/Remaining. Cada uno lleva una lista de funciones con delays (de hasta 30 s).
- Además tiene Shortcuts (tecla, MIDI o API → función), scripting en VB.NET y vMix Call, que es una entrada más con sus triggers. El Replay tiene su lista de eventos con OnReplayEventsCompleted.
- **Lo bueno:** el disparador por estado de la entrada. **Lo malo:** no hay escaleta con tiempos, y la lógica queda desparramada en triggers que nadie ve.

**OBS**
- Las *scene collections* son solo conjuntos de escenas. **Advanced Scene Switcher** tiene *macros* (condiciones → acciones): ventana, audio, medio, hora, intervalo, otra macro, con "se cumple durante N s", y acciones de espera, escena, visibilidad, transformación, grabación o transmisión. Tiene macros anidadas y docks. **Move** anima las fuentes entre escenas cuando tienen el mismo nombre.
- **Lo bueno:** son reactivas. **Lo malo:** no hay una vista temporal, y el estado emergente es difícil de anticipar.

**Ross: OverDrive, XPression, DashBoard, Inception**
- **OverDrive (APC):** **plantillas** por cada tipo de evento del show (cámara + micrófono + gráfico…), que se arman en una escaleta sincronizada con la NRCS por MOS. Los CG de Viz se pueden tomar fuera de orden y hay paneles SideBox.
- **XPression:** un *Sequencer* con *take items* (Take ID) y comandos NEXT, FOCUS, UP y DOWN por RossTalk.
- **DashBoard:** paneles a medida (CustomPanels) *(conocimiento general)*.
- **Inception:** NRCS centrada en la historia o en la escaleta. Una historia puede estar en varias escaletas a la vez y publica *running orders* por MOS.
- **Lo bueno:** las plantillas reutilizables, y el operador puede salir de la automatización y volver.

**Viz Mosart**
- Cada historia es una fila con una línea de tiempo a la derecha. Los **elementos primarios** (Camera, Package, Live, DVE, Break) tienen colores y un largo proporcional a su duración. Los **secundarios** (zócalos, audio, accesorios) van montados sobre el primario y se ubican por el timecode de la NRCS. Si no traen hora de entrada, quedan como **manuales** a la derecha.
- El triángulo negro es *autotake next*. Los zócalos tienen cuatro modos de salida: AUTOOUT, BACKGROUNDEND, STORYEND y OPENEND. Lo que está al aire va en rojo y lo siguiente en amarillo. Muestra la hora estimada de salida al aire y la duración editorial contra la real.
- **Lo bueno:** **primario + secundarios con modo de salida** es un modelo claro para la persona que opera.

**Grass Valley Ignite**
- La escaleta de la NRCS se codifica en una columna. Los **LBN** son eventos de producción ya armados (de un plano con su micrófono hasta un DVE de 8 cajas con capas). Los **TME** convierten segmentos y transiciones en eventos de una línea de tiempo que **se puede previsualizar antes del aire**.
- Se puede reimportar la escaleta sin tocar lo que está al aire, y el gateway MOP marca los cambios y los conflictos. El panel Katalyst pasa de automático a manual y vuelve.

**Sony ELC / MLS, Panasonic**
- **Sony ELC:** "cues de producción" metidos en la escaleta de iNEWS (macros del switcher, servidores, gráficos, robótica, audio), con vistas Quick Cue y Cue Edit y servidor espejo. Las macros del MLS se graban, se encadenan y tienen *Conditional Actions*.
- **Panasonic KAIROS:** no encontré documentación de su escaleta. Hasta donde sé, tiene macros y eventos, pero no una escaleta propia *(conocimiento general)*.

**MOS, iNEWS, ENPS**
- **Running Order → Story → Item**, donde el ítem apunta a un objeto MOS de un dispositivo. Los mensajes son `roCreate`, `roReplace`, `roStoryInsert/Move/Delete`, `roItem*` y `mosObj`. Una historia puede tener varios ítems. Los tiempos editoriales (back-time, over/under) viven en la NRCS.
- **Lección:** la escaleta editorial y la ejecución son capas distintas. A nosotros nos alcanza con exportar o importar MOS más adelante; no hace falta copiarlo.

**Cuez (Tinkerlist) y Rundown Studio**
- **Cuez:** Episode → Part → Item → Block. Los tiempos son: duración estimada (sale del largo del texto o del medio, y se puede pisar), **front time**, end time, **back time** (lo último que puede empezar para terminar a horario) y part timer, contra el inicio, el fin y la duración reales. Over/under del episodio. **Pisar un back time recalcula todos los demás**: el pisado se ve en azul y los que quedan inválidos, en rojo. El Automator asocia acciones a bloques e ítems, con delays dinámicos y "steps".
- **Rundown Studio:** **hard start** (hora del día) contra **soft start** (después del anterior). Los gaps y overlaps se calculan solo contra los hard starts. Un botón **Fix** ofrece mover este cue, mover el hard start anterior, que un cue de arriba absorba la diferencia o insertar un cue de relleno; en vivo deshabilita lo que tocaría el pasado. Tiene una barra flotante con el próximo hard start, la cuenta regresiva y el gap u overlap, y cues con auto-start.
- **Es lo mejor para el cálculo de tiempos.**

**TriCaster (LivePanel, DataLink, macros)**
- Las macros se disparan por estado (audio, reproducción, tally, acciones del switcher) y por varios triggers. Tienen pasos con pausas y **esperas manuales**. LivePanel arma paneles web y DataLink mete datos de archivos o comentarios en los gráficos. No encontré un programador horario de macros.

**Blackmagic ATEM**
- Las macros son una secuencia grabada con **Pause** y **User Wait**: se frena hasta que alguien aprieta Continue. Es lo mínimo de "paso a paso".

**Disguise y Resolume**
- **Disguise:** una timeline con pistas y **secciones** separadas por cue tags. *Play to end of section* se para justo antes del próximo cue, y "Next section" equivale al GO. Hay un modo que repite la sección. Persigue el timecode y, al llegar al final de la sección, ignora el TC hasta que salte. **Lo bueno:** la timeline real *con* puntos de espera es un híbrido muy usable.
- **Resolume:** basado en clips y columnas, con autopilot por clip y, desde la 7.22, por columna. La timeline es un pedido viejo de los usuarios que no salió; para shows lineales se usan herramientas de timecode externas.

**ProPresenter y Millumin**
- **ProPresenter:** playlists de presentaciones y una **Timeline** por presentación, con pistas de diapositivas, medios, audio y acciones. Se graba en vivo apretando las diapositivas mientras suena el audio, y la puede manejar SMPTE.
- **Millumin:** un **dashboard de columnas** (cada columna es "un momento del show", con transición y notas) y **timelines** que se pueden poner dentro de una columna. Tiene cues por OSC, MIDI o DMX y una *Silent Edition* para editar sin que el público lo vea.
- **Lección:** la columna como "estado" que se lanza y la timeline como contenido de esa columna se combinan bien.

### 1.3 Lo que tomamos

| Idea | De dónde |
|---|---|
| La unidad de TAKE (Paso) con piezas relativas adentro, en pistas | Sofie, Mosart, Ignite |
| Piezas con modo de salida (fin del paso / del bloque / abierta / sola / a mano) y piezas infinitas | Mosart (AUTOOUT…), Sofie (lifespans) |
| Auto-continue contra auto-follow, pre y post espera | QLab |
| Hard start y soft start, gaps y overlaps, el botón **Arreglar** | Rundown Studio |
| Front, back y end time; pisar el back time recalcula todo | Cuez |
| Presupuesto por bloque y cuenta regresiva al próximo bloque | Sofie |
| Lookahead: lo que viene se arma en PVW | Sofie |
| Lo planeado separado de lo emitido (instancias) | Sofie |
| Resolvedor puro de reglas de tiempo con una sola cosa por capa | SuperFly Timeline |
| Estante de adlibs y botonera sin orden | Sofie (Shelf, Buckets), QLab (cart) |
| Disparadores por estado de una entrada o de la sala | vMix, Advanced Scene Switcher, TriCaster |
| Pantallas por rol (conducción, presentador, cámara, prompter) | Sofie |
| Plantillas reutilizables por tipo de momento | OverDrive, Ignite (LBN) |
| Parar al final de la sección y "siguiente sección" | Disguise |
| Editar sin que salga al aire | Millumin (Silent Edition) |

---

## 2. Lo que ya tenemos

### Modelo
En `src/app/evento/Evento.hpp`, en C++ puro: **Evento → Bloque → Cue**.
- El `Cue` lleva `{id, tipo, texto, datos(json), duracionSeg, automatico}`.
- Los tipos son: Escena, Musica, Video, Teleprompter, Orador, Grafico, Carrusel, Pantalla, Audio, Pausa, Espera y Nota.
- El evento tiene `fecha`, `inicio` "HH:MM", `huso` IANA e `idioma`, y se guarda como carpeta con `evento.json`, `Medios/`, `Discursos/` y `Respaldos/`.
- Hay **revisión previa**, `revisar()`, que devuelve `Reparo{bloque, cue, grave, texto}` y avisa de "seis cues automáticos seguidos".

### Ejecución
- **`Guion`** es la máquina de estados: Parado, Corriendo, Pausado o Terminado. Tiene una `Posicion` (bloque, cue) y sabe avanzar, retroceder, ir a un cue, pausar (lo pausado no cuenta como desvío) y terminar.
- Calcula `desvioSeg`, `restanteSeg` y `transcurridoSeg`, y guarda un `recorrido()` con lo previsto contra lo real.
- **`EventoViewModel::ejecutarActual()`** traduce cada tipo a acciones: escena por `rt_.setScene`, orador por `sala_->oradores()`, música por `playMedia` (con un nombre fijo, "Música del evento"), teleprompter con un prompter por nombre, y gráfico por fuente con modo "se va" (`al-avanzar`, `sola` o "queda", la base de un *lifespan*). También carrusel y audio.
- Hay **ensayo** y ensayo acelerado.
- **`reloj()`** ya hace back-timing (T46): lo que falta del bloque y del guion, la hora de fin prevista contra la planeada y `backTimingSeg`.
- Por MCP están `guion_*` (abrir, bloque, cue, guardar, listar, ver) y `evento_*` (correr, revisar, informe, capítulos, ensayo_rapido, escenario…).

### Al lado, sin integrar
- `asamblea/OrdenDelDia` maneja los puntos con su estado, el uso de la palabra por orador y por punto, y publica en el bus (`sesion/punto`).
- `medios/Playlist` tiene su propio in/out, auto-next, loop, armado en PVW y retoma tras un corte.
- También están el Carrusel, `control/Dsk` (cuatro capas globales), `control/Acciones` (atajos a herramientas MCP), `control/BusEventos` (temas `aire/escena`, `oradores/al_aire`, `audio/nivel/+`…), la Botonera y ControlWeb.

### Lo que hay para las miniaturas
`BroadcastEngine::enableScenePreview(canal, escena, maxWidth)` da una **vista continua de una escena que no está al aire, por canal, y varias a la vez**. Además están `capturarAparte()` para fotos sueltas y `image://inputs`. Con eso alcanza para que las cajas muestren la escena viva.

### Huecos

1. **`Cue::automatico` no se ejecuta.** Se guarda, se muestra ("encadena solo") y lo revisa `revisar()`. Pero no encontré nada que avance solo: el timer `reloj_` solo emite `relojChanged`. Tampoco `Pausa` espera para seguir: solo anota. El documento `EVENTOS.md` promete "cuánto falta para el próximo cue automático".
   **Resuelto en L0:** los automáticos y las pausas pasan solos (`Guion::msParaAvanzarSolo`), con el aviso de 5 s y HOLD. **Desde L2** lo hace la `Corrida` de la línea (el `Guion` v1 ya no existe).
2. **Un solo cursor y un solo programa.** No hay pistas ni salidas, y todo sale por `rt_.setScene`. Hasta donde leí, el cue de escena va directo al programa, sin armarlo en PVW ni hacer lookahead. Con varias salidas, esto no alcanza.
   **L0 resolvió la mitad de una salida:** el cue de escena se arma en el PVW y entra con TAKE por el Conmutador, y el siguiente de escena queda armado de antemano. Falta el cursor por salida (L6).
3. **Un cue hace una sola cosa.** "Cámara 2 + zócalo + bajar la música" son tres cues seguidos, lo que da el aviso de cadena larga y hace que el desvío se mida mal.
4. **No hay hard start** (hora fija por bloque ni por cue), ni gaps, overlaps o *Arreglar*. El desvío se mide contra el cue actual y no contra los anclajes.
5. **No hay estado al saltar.** `irA` ejecuta solo el cue de destino: la música o el gráfico que "deberían" estar puestos ahí no se reponen.
   **Resuelto en L2:** ir a un paso repone lo de vida larga y saca lo que sobra (`cargarEn`).
6. **No hay adlibs ni estante** dentro del guion. La Botonera y los atajos existen, pero el guion no los conoce y el recorrido no los registra.
   **L2/L3:** adlibs del paso y del estante, e insertar un paso del estante, por la corrida y por MCP (`linea_adlib`, `linea_insertar`), anotados en el recorrido. Falta la Botonera y la grilla en la conducción (L4).
7. **El orden del día, la playlist y el carrusel** son motores aparte. El guion no puede decir "un paso por punto" ni "mientras dure la votación".
8. **No hay disparadores externos como condición de avance**: medio terminado, orador que cambió, votación cerrada, hora del día.
9. **No hay instancias planeado/emitido persistentes** ni retoma del guion tras un corte (la Playlist sí la tiene).
   **Resuelto en L2:** `recorrido.jsonl`, `Runtime/linea-aire.json` y la retoma en HOLD con «¿Qué está pasando?».
10. **La interfaz es una lista.** No hay línea de tiempo, miniaturas, vista de tablet de la escaleta ni pantalla del presentador ni de cámara.

---

## 3. Diseño propuesto: la Línea de tiempo

### 3.1 Principios
- **Manda la persona:** lo que *cambia el plano principal* entra con GO o TAKE salvo que se marque lo contrario. Lo que va *adentro* de un paso (zócalos, música, ducking) va solo y relativo al paso.
- **Un resolvedor puro en C++** (como SuperFly Timeline) calcula los tiempos y el **estado esperado de cada pista en cualquier instante**. La interfaz, el MCP, el ensayo y la retoma usan el mismo cálculo. Se prueba sin motor, igual que hoy `Guion`.
- **Lo planeado no se toca al correr:** lo emitido son *instancias* aparte.
- **Una sola línea por evento, con carriles por salida.** Las escenas y las cámaras se comparten; los conflictos se detectan.
- Los nombres de herramientas, claves y carpetas **no se traducen**. Lo visible pasa por i18n.

### 3.2 Modelo de datos (`evento.json`, `"linea": {...}`, `version: 2`)

```
Evento
 ├─ salidas[]        (referencia a las salidas del programa: "principal", "sala", "web-en")
 ├─ pistas[]         {id, tipo, salida|null, nombre, color, oculta}
 ├─ bloques[]        Bloque = Segmento
 │    {id, nombre, nota, presupuestoSeg?, inicio: Inicio?, plantilla?, origen?}
 │    └─ pasos[]     Paso = Part (lo que entra con un TAKE)
 │         {id, nombre, salida, guion(texto para prompter/conducción),
 │          inicio: Inicio, fin: Fin, continuar, preEsperaMs, postEsperaMs,
 │          transicion?, duracionPrevistaSeg, notaOperador, etiquetas[]}
 │         └─ piezas[] Pieza = Piece
 │              {id, pista, tipo, datos(json, como hoy el Cue),
 │               desde: Ancla, dura: Dura, vida: Vida, prioridad, siSePerdio}
 ├─ estante[]        adlibs: Piezas o Pasos sueltos, con tecla, botón, color
 ├─ disparadores[]   {id, cuando: Condicion, hacer: Accion, activo: Vida}
 └─ plantillas[]     Pasos-modelo ("entrevista", "votación", "punto del orden del día")
```

**Pistas.** Son por salida o globales.

| Tipo | ¿Por salida? | Qué lleva |
|---|---|---|
| `escena` | sí | lo que la salida pone al aire. Es la caja con miniatura y hay **una por salida, obligatoria**. |
| `graficos` | sí | zócalos, títulos y placas por fuente de gráfico. Puede haber varias, una por "capa". |
| `dsk` | sí | las capas globales encima del programa de esa salida |
| `audio` | sí | la mezcla de esa salida: música, ducking, mudo, rack |
| `medios` | sí | video, playlist, carrusel |
| `prompter` | global | puede haber varias, una por teleprompter |
| `sala` | global | orador al aire y su tiempo, invitados remotos |
| `asamblea` | global | punto del orden del día, votación, quórum |
| `pantallas` | global | Chromecast, pantallas de la sala |
| `ptz` | global | posiciones de cámara. Son **compartidas entre salidas**: ver los conflictos en 3.4. |
| `asistente` | global | acciones MCP programadas |
| `notas` | global | indicaciones que no ejecutan nada |

En cada pista, como en SuperFly, **hay una sola cosa activa por vez**: la pieza nueva reemplaza a la vieja (para la música, el comportamiento de hoy de "Música del evento") o gana la de mayor prioridad.

**`Inicio` del paso o del bloque**, combinando Rundown Studio y QLab:
- `{"modo":"go"}` (por defecto): espera el GO o el TAKE de la persona que opera.
- `{"modo":"tras","de":"anterior"}`: *auto-follow*, arranca cuando termina el anterior.
- `{"modo":"con","de":"anterior","masMs":N}`: *auto-continue*, arranca cuando arranca el anterior más N.
- `{"modo":"hora","hora":"19:30","politica":"avisar|esperar|cortar"}`: **hard start**.
  - `avisar`: muestra la cuenta regresiva y el gap u overlap, pero la persona da el GO.
  - `esperar`: si se llega antes, queda en hold hasta la hora.
  - `cortar`: a la hora entra pase lo que pase, para la cadena nacional o el satélite.
- `{"modo":"cuando","condicion":Condicion}`: lo dispara algo de afuera ("cuando termine el video", "cuando cierre la votación", "cuando hable Fulano").
- `{"modo":"ancla","salida":"principal","paso":"p17","masMs":0}`: **sincroniza salidas**, por ejemplo "la sala entra con el paso 17 de la principal".

**`Fin` del paso:**
- `{"duracion":N}`: previsto. Si `continuar` es `"sigue"`, a los N segundos entra el siguiente (es el autoNext de Sofie).
- `{"alTerminar":"medio"}`: dura lo que dura el video o el audio de su pieza principal. Es el `0` de vMix.
- `{"abierto":true}`: **hold**. No se sabe cuánto dura; para el cálculo cuenta la duración prevista, pero nunca avanza solo. Es el caso del orador o del debate.
- `{"mientras":Condicion}`: dura mientras la condición sea verdad, como "votación abierta".
- `continuar`: `"no"` (por defecto), `"sigue"` (auto-follow) o `"con"` (auto-continue). Esto ordena y completa el `automatico` de hoy.

**`Ancla` y `Dura` de la pieza.** Son relativas al paso, como el secundario de Mosart.
- `desde`: `{"ms":N}` desde el arranque del paso, `{"antesDelFin":N}` (por ejemplo el zócalo que sale 10 s antes del fin), `{"manual":true}` (un adlib del paso, que aparece a la derecha como en Mosart) o `{"cuando":Condicion}`.
- `dura`: `{"ms":N}`, `{"alTerminar":"medio"}` o `{}` (hasta que termine su vida).
- `vida` (el lifespan de Sofie y los modos de salida de Mosart): `paso` (por defecto), `bloque`, `hastaQueSeSaque`, `evento`, o `alAvanzar` (el `seVa: al-avanzar` de hoy, que se mantiene compatible).
- `siSePerdio`: `ejecutar | saltar | preguntar`. Es lo que pasa si, al saltar o al retomar tras un corte, la pieza debería estar activa.

**`Condicion`** es una expresión chica sobre lo que ya existe:
- temas del bus: `"bus:oradores/al_aire == 'banca-12'"`, `"bus:sesion/punto.estado == 'votado'"`;
- `"medio:<pieza>.termino"`, `"hora>=19:30"`, `"tecla:F5"`, `"botonera:<id>"`, `"web:<botón>"`;
- `"pieza:<id>.fin + 2s"`, la referencia relativa de SuperFly.

No hace falta un lenguaje general: alcanza con estas formas y `y/o/no`.

**Pieza `asistente`.** Es `{herramienta, args}` sobre McpCore, con origen `"linea:<evento>/<pieza>"`. Por ejemplo, `qr_mostrar`, `stream_titulo` o `ptz_ir` a una posición. **Se aprueba al escribirla**: el asistente la propone y una persona la confirma, con el contrato de aprobación de siempre. Al ejecutarla no pide nada, para no frenar el vivo.

**Instancias (lo emitido).** Van en `Runtime/linea-aire.json`, y además en `Eventos/<x>/recorrido.jsonl` para el informe y los capítulos:
- `{salida, paso, desdeMs, hastaMs, comoEntro: go|auto|hora|cuando|ancla}`;
- las piezas ejecutadas con su resultado y los adlibs disparados;
- holds, saltos e inserciones.

Esto reemplaza y amplía `Guion::recorrido()`.

**Migración.** `version 1` → `2` es automática y sin pérdida:
- cada bloque queda igual;
- **cada cue `escena`, `orador`, `video` o `espera` abre un Paso**;
- los cues que le siguen y no abren paso (gráfico, música, audio, carrusel, pantalla, teleprompter, nota) se vuelven Piezas de ese paso, con `desde` = la suma de duraciones en el medio;
- `automatico` pasa a `continuar: "sigue"`;
- `pausa` pasa a ser un paso sin escena con `duracion` y `continuar: "sigue"`;
- `espera` pasa a `inicio: go`.

Se guarda una copia v1 en `Respaldos/`.

### 3.3 Tiempos

**Resolvedor** (`src/app/evento/Linea.{hpp,cpp}`, puro). La entrada es el modelo, las instancias reales, el `ahora` y el estado de las condiciones conocidas. Por cada salida devuelve el **pronóstico**: para cada paso, *front time* (hora prevista de arranque), *end time*, *back time* (lo último que puede arrancar para llegar al próximo hard start o al fin del evento) y el estado (pasado, al aire, próximo, futuro, salteado). Por cada pista devuelve las piezas activas en un instante t, que es el `getResolvedState(t)` de SuperFly. Además devuelve los `proximosEventos`: lo que va a entrar solo y cuándo.

Las reglas:
1. Los pasos con `go` en el futuro se suponen tomados **en cuanto termina lo previsto del anterior**. Es el supuesto optimista que usan Cuez y Rundown Studio.
2. Un paso al aire con `abierto` o pasado de tiempo **empuja todo lo que viene**: su fin estimado es `max(previsto, ahora)`.
3. Los hard starts **no se mueven**. Lo que viene antes muestra **gap** (sobra) u **overlap** (falta), como en Rundown Studio.
4. **Over/under** del bloque contra su `presupuestoSeg` (como el presupuesto de segmento de Sofie) y del evento contra su fin planeado. Esto generaliza lo que ya hace `reloj()`.
5. Las condiciones sin tiempo conocido (`cuando`) usan la duración prevista del paso para el cálculo y se marcan como "estimado".
6. Pisar un *back time* (como en Cuez) lo convierte en una restricción y recalcula el resto.

**El reloj de ejecución** es un `EjecutorLinea` en la app, uno por salida y con un solo hilo de tiempo:
- tick de 50 ms, más timers exactos para el próximo evento: no polling a ciegas;
- lee `proximosEventos`, dispara las piezas, avanza los pasos con `sigue`, `con`, `hora` o `ancla`, y se suscribe al bus para las `Condicion`;
- **la pre-espera y la post-espera** son las de QLab;
- **el preroll y el lookahead:** cuando un paso pasa a ser *próximo*, su escena **se arma en el PVW de esa salida** (en "armada") y los medios se precargan. El GO hace el TAKE con la transición del paso. Si el paso entra solo, hace el TAKE solo, con un aviso visible 5 s antes ("entra solo en 0:05", con la opción de frenarlo con HOLD).

**Las acciones de vivo** (por salida, salvo que se diga otra cosa):

| Acción | Qué hace |
|---|---|
| **GO / TAKE** | entra el próximo paso |
| **HOLD** | congela el avance automático del paso actual. La cuenta queda en "+" y el pronóstico empuja. Un segundo HOLD lo suelta. |
| **Siguiente / anterior** | mueve el *próximo* sin tocar el aire (F9/F10, como en Sofie) |
| **Saltar** | marca el próximo como salteado |
| **Ir a** | el operador elige un paso en la lista |
| **Insertar ahora** | un paso del estante o una plantilla va a *próximo* o al aire |
| **Repetir** | vuelve a entrar el paso actual |
| **Pausa general** | la de hoy: lo pausado no cuenta como desvío |

### 3.4 Con varias salidas

- **Un cursor por salida.** Cada salida tiene su "al aire" y su "próximo", sus atajos y su vista de conducción. La conducción general muestra las salidas apiladas.
- **Los pasos se atan a una salida.** Un paso con `salida: "*"` es un "momento compartido": un solo GO lo toma en todas. Es útil para el himno o la apertura: misma escena, distinto audio por salida.
- **Sincronía:** con `inicio: ancla` una salida sigue a otra, por ejemplo la transmisión en inglés que entra cuando entra la principal, con su propio zócalo y su propio audio.
- **Las escenas y las cámaras se comparten.** El resolvedor detecta los **conflictos**:
  - una pieza `ptz` que mueve la cámara 2 mientras otra salida la tiene **al aire** o **armada** en ese momento: es un reparo grave en la revisión y un aviso en vivo, con la opción de "mover igual" o "dejar para cuando se libere";
  - una misma fuente de gráfico usada por dos salidas con contenidos distintos: reparo.
- **El tally y el bus llevan la salida:** `aire/<salida>/escena`, `linea/<salida>/proximo`.
- **El audio:** la pista `audio` de cada salida actúa sobre *su* mezcla. Las fuentes físicas compartidas (micrófonos) solo se pueden silenciar desde una pista global, para que una salida no le apague el micrófono a otra.

### 3.5 Interfaz

**A. Línea (multipista horizontal).** Es la vista nueva.
- **Arriba, la regla de tiempo** en hora del día, con el huso del evento. Muestra los **hard starts** con banderas, la marca de fin planeado y el gap u overlap entre anclas (verde, ámbar o rojo).
- **La línea de "ahora" queda fija a un tercio del ancho y la línea se desplaza**, como la On Air de Sofie: a la izquierda lo emitido, a la derecha el pronóstico.
- **Los carriles van agrupados por salida.** El primero de cada salida es `escena`, con **cajas de paso que llevan la miniatura de la escena**, el nombre, la duración prevista y la real, y el modo de entrada (GO, auto, hora, cuando, ancla). Debajo van gráficos, DSK, audio y medios de esa salida. Más abajo, las pistas globales (asamblea, sala, prompter, pantallas, PTZ, asistente, notas), colapsables.
- **Las miniaturas vivas** se reparten con un presupuesto, porque cada vista viva cuesta GPU (ver la memoria sobre CPU ociosa):
  - *al aire*: el cuadro del programa de esa salida, que ya existe;
  - *próximo*: el PVW de esa salida, que ya está armado por el lookahead;
  - *próximo+1 y +2*: `enableScenePreview("linea-<salida>-n", escena, 320)` a baja resolución y a ~5 fps;
  - *el resto*: fotos con `capturarAparte()` cada 15 a 30 s, solo de las cajas que se ven en pantalla, con la marca "hace 20 s".
  - **Las escenas repetidas comparten miniatura.** Las vistas se sueltan a los 10 s sin uso, con el mecanismo que ya hay.
  - *Más adelante:* la miniatura "con lo que va a tener", es decir la escena más los gráficos del paso con sus valores, renderizada aparte. Al principio, los gráficos se muestran como chips sobre la caja, como los secundarios de Mosart.
- **Las piezas** son bloques de color en sus pistas, con un borde que indica la vida (por ejemplo, "sigue" si cruza pasos). Las manuales del paso son botoncitos a la derecha de la caja, como en Mosart. Los conflictos llevan un borde rojo.
- **Lo emitido contra lo planeado:** la caja real es sólida y debajo va un "fantasma" del largo previsto. El over se ve en rojo con "+0:42".
- **Zoom:** de 10 s a 4 h por pantalla, con rueda + Ctrl. Las teclas `1-4` ponen zoom fijo (ajustar al bloque, al evento, a 5 min o a 1 min).
- **Edición:** se arrastra para mover o cambiar el largo. En vivo, **solo el futuro**; lo emitido queda deshabilitado con el porqué, como Rundown Studio. Se puede editar sin que salga al aire, como la Silent Edition de Millumin: los cambios a pasos futuros se aplican al confirmar, y antes se ven "pendientes".
- **La barra de abajo** se inspira en Rundown Studio y en Sofie: hora, transcurrido, over/under del evento, el próximo hard start con su cuenta y su gap u overlap, el presupuesto del bloque y "entra solo en…".

**B. Escaleta (compacta, la vista de la persona que opera).** Es una lista de pasos agrupados por bloque, con columnas:
- número, miniatura chica, nombre, entrada (GO, auto, hora…), front time, duración prevista y real, back time, chips de piezas.

Va una fila por salida o con filtro de salida. Las filas al aire y próxima se marcan en rojo y amarillo. Es la vista que hoy tiene `EventoPage`, ampliada. También tiene un modo **Storyboard** (como Sofie): solo lo que viene, con las miniaturas grandes.

**C. Conducción.** El paso al aire en grande con su cuenta regresiva y el "+" si se pasa, el próximo con su miniatura PVW, los botones GO, HOLD, Saltar e Insertar, el estante de adlibs (grilla tipo cue cart) y el texto del guion del paso.

**D. Superficies.**
- **Tablet o web** (`/control/escaleta?salida=…`, con el emparejamiento y los perfiles de ControlWeb): la escaleta compacta, GO, HOLD, siguiente o anterior, insertar desde el estante y las miniaturas de al aire y próximo. Con un perfil "solo mirar" sirve para el productor y la producción.
- **Presentador** (`/control/presentador`): cuánto le queda y qué viene, como la pantalla de presentador de Sofie. Se puede sumar a la vista del orador que ya existe.
- **Cámara** (`/control/camara?camara=2`): los próximos planos que usan esa cámara, con su cuenta regresiva. Es la Camera screen de Sofie y le sirve al camarógrafo y al de PTZ.
- **Botonera, Stream Deck y Companion:** se agregan acciones nuevas a `control/Acciones`: `linea_go`, `linea_hold`, `linea_siguiente`, `linea_anterior`, `linea_saltar` y `linea_adlib`, cada una con su salida. El feedback va por el bus.
- **Teclado** (atajos editables):

| Tecla | Acción |
|---|---|
| Espacio o F12 | GO |
| H | HOLD |
| F9 / F10 | mover el próximo |
| Shift+F9 | saltar |
| Ctrl+1…4 | elegir la salida activa del teclado |
| F1–F8 | estante |

### 3.6 Asamblea, orador, playlist, carrusel
- **Bloque desde el orden del día:** un bloque con `plantilla: "punto"` y `origen: "asamblea/orden-del-dia"` **se expande** en un paso por punto, cada uno con el modelo de paso de la plantilla: escena de debate, zócalo `{punto}`, pieza `asamblea: tratar n`. Si se recarga el orden del día, se re-expande **sin tocar los pasos ya emitidos**, como la reimportación de Ignite.
- **Votación:** un paso con `fin: {mientras: "bus:votacion/abierta"}` y piezas `asamblea: abrir/cerrar votación` y `grafico: resultado` con `desde: {cuando: "bus:votacion/cerrada"}`.
- **Orador:** un paso `abierto` con la pieza `sala: orador al aire, tiempo N`. El pronóstico usa N, y el desvío real sale de `OrdenDelDia::tiemposPalabra`.
- **Playlist y carrusel:** son piezas de la pista `medios` que *mandan* sobre esos motores. La Playlist sigue siendo la dueña del in/out y del auto-next, y la línea solo la arranca, la para o la pone en un ítem. Un paso puede usar `fin: {alTerminar: "medio"}` sobre la playlist entera.

### 3.7 El asistente por MCP

**Herramientas nuevas** (los nombres no se traducen). Las de lectura no piden permiso; las de escritura llevan el permiso habitual y, en vivo, solo tocan el futuro.

| Herramienta | Para qué |
|---|---|
| `linea_ver` | el pronóstico resuelto y compacto: `{salida, desde, hasta, nivel: bloques\|pasos\|piezas}`. Así el modelo no tiene que leer todo el JSON. |
| `linea_estado` | al aire, próximo, over/under, próximo hard start y conflictos, por salida |
| `linea_editar` | un **lote de operaciones** atómico y con *dry-run*: `{ops:[{op:"paso_agregar", bloque, despuesDe, paso}, {op:"pieza_agregar", paso, pieza}, {op:"mover"}, {op:"quitar"}, {op:"cambiar", id, campos}], probar:true}`. Devuelve el diff y el pronóstico resultante antes de aplicar. |
| `linea_desde_plantilla` | arma pasos desde una plantilla y datos (lista de oradores, orden del día) |
| `linea_revisar` | reparos: piezas incompletas, escenas o archivos faltantes, conflictos de cámaras compartidas, hard starts imposibles, cadenas automáticas largas |
| `linea_ajustar` | el **solucionador del back-timing**: "terminar a las 21:00". Propone recortes según la prioridad o el carácter "recortable" de cada paso, y opciones como el *Arreglar* de Rundown Studio (mover el hard start anterior, que un paso absorba la diferencia, insertar relleno). Siempre con `probar:true` primero. |
| `linea_go`, `linea_hold`, `linea_siguiente`, `linea_saltar`, `linea_insertar`, `linea_adlib` | las mismas acciones que la interfaz. Son de vivo y requieren aprobación salvo con `--test`. |
| `linea_recorrido` | lo emitido, para el informe, los capítulos y el acta |

**Rol "productor" automático:**
- se suscribe a `linea/+/overunder`;
- cuando el over supera un umbral, **propone** con `linea_ajustar` y un aviso en la conducción ("vamos +3:20; si acortamos el panel 3 min llegamos"), pero **no ejecuta**;
- con las memorias de "Evento es todo" y las guías de escenarios, puede armar la línea entera desde una guía (`evento_escenario`).
- **Versionado:** cada `linea_editar` aplicado deja una revisión (`Respaldos/linea-<n>.json`) y se puede deshacer.

### 3.8 Persistencia
- **El plan:** `Eventos/<x>/evento.json` → `linea`, con versión 2 y migración desde la 1.
- **Lo emitido:** `Eventos/<x>/recorrido.jsonl`, en el que se va escribiendo.
- **El estado vivo:** `Runtime/linea-aire.json` (por salida: paso al aire con su `desdeMs`, próximo, holds, piezas activas con su arranque real), escrito de forma atómica cada 1 s y en cada cambio.
- **Tras un corte de luz** (la regla T47: todo vuelve solo como estaba):
  - se reconstruye el estado con el resolvedor y se reponen las piezas de vida larga (música, gráficos fijos, DSK);
  - las piezas puntuales que se perdieron siguen su `siSePerdio` (por defecto `saltar` para las automáticas y `preguntar` para las de `asistente`);
  - **Decidido (2026-10-31): la línea vuelve en HOLD y pide una descripción de qué está pasando.** El aire, las escenas, la música y los gráficos vuelven solos como estaban (T47), pero el avance automático queda frenado. La conducción muestra «¿Qué está pasando?» (texto o dictado: «el orador sigue hablando», «ya terminó la votación, saltamos al cierre», «vamos 10 minutos atrasados»). El asistente lee esa descripción con el estado de la línea (`linea_estado`, `linea_recorrido`) y **propone** cómo adecuar todo: dónde reenganchar, qué saltear, qué reponer y qué recortar (`linea_editar`/`linea_ajustar` con `probar:true`). La persona confirma y recién ahí se suelta el HOLD. Sin respuesta, la línea sigue en HOLD; con GO se suelta tal cual estaba. La descripción y lo que se decidió quedan en el recorrido.
- Las plantillas de paso y de bloque se guardan por evento. Las de la biblioteca van en `assets/demo` y en las guías, con "Evento es todo": nada compartido.

### 3.9 Cuando el vivo se desvía

| Situación | Comportamiento |
|---|---|
| **Overrun** de un paso con GO | la cuenta va en "+" rojo y el pronóstico empuja todo. Los hard starts muestran overlap. El botón **Arreglar** ofrece las opciones de Rundown Studio y el asistente propone recortes. |
| **Overrun** de un paso automático | entra solo igual, salvo con HOLD. El aviso "entra solo en 0:05" da tiempo a apretarlo. |
| **Hard start** que llega | según su política: avisar (barra), esperar o cortar (a la hora exacta, con la transición del paso). |
| **Saltar** pasos | el resolvedor calcula qué piezas de vida larga **deberían** estar activas al llegar (el "load to time" de QLab) y las repone o las saca con `siSePerdio`. Las de vida `paso` de lo salteado no se ejecutan. |
| **Insertar** | del estante o de una plantilla, como *próximo* o al aire ya. Queda en lo emitido como "insertado" y el pronóstico corre lo demás. |
| **Reordenar** en vivo | solo el futuro, con vista previa del pronóstico antes de confirmar |
| **Falta un recurso** (escena borrada, archivo, cámara sin señal) | la caja se marca en la revisión y en vivo. Al GO, el paso entra igual sin la pieza rota y avisa: el plano no se queda colgado. Un paso cuya escena no existe **no se toma**. |
| **Salidas desincronizadas** | un paso anclado espera a su ancla, con una cuenta "espera a principal/p17" y la opción de "entrar ya". |
| **El operador hace las cosas a mano** fuera de la línea | se detecta en el bus (`aire/<salida>/escena` distinta de la del paso). La caja se marca "a mano" y el pronóstico no se rompe. Opcional: "reenganchar", que vuelve a la escena del paso. |

---

## 4. Orden de implementación

1. **L0, arreglar lo que hay (chico).** Que `automatico` y `pausa` avancen de verdad, con el aviso "entra solo en 0:05" y HOLD. Que el cue `escena` pase por PVW con lookahead del siguiente. Tests en `test_evento`.
2. **L1, el modelo v2 y el resolvedor puro.** `Linea.{hpp,cpp}` con pasos, piezas, pistas, inicio, fin, vida y anclas. Pronóstico con front, back y end, hard starts, gaps y overlaps, over/under y estado en t. Migración v1 → v2. Tests a fondo, todo sin motor.
3. **L2, el ejecutor (una salida).** El `EjecutorLinea` sobre lo que hoy hace `ejecutarActual()` (cada tipo de cue pasa a ser un tipo de pieza). GO, HOLD, siguiente, saltar con reposición de estado, pre y post espera, `sigue` y `con`, preroll a PVW. Instancias en `recorrido.jsonl` y `linea-aire.json`, con retoma tras un corte.
   **Hecho (2026-10-31).** Lo puro en `evento/Corrida.{hpp,cpp}` (la corrida: acciones en vez de efectos, con el reloj inyectado y probada en `test_linea`) y `evento/LineaEdicion.{hpp,cpp}` (`proyectar`/`reconciliar`: el guion de cues es una lectura de la línea y editarlo la edita sin perder lo que el v1 no dice). En la app, `ui/EjecutorLinea` (un timer exacto por salida) y `EventoViewModel` (la fachada del v1 y lo que hace cada pieza). La línea manda: al abrir un evento v1 se migra con copia en `Respaldos/evento-v1.json`. La espera migrada es un paso abierto que nunca se va solo. Tras un corte vuelve en HOLD y la conducción pregunta «¿Qué está pasando?»; lo contado va al asistente (`Studio.agent.encargar`) con el resumen; «Seguir como estaba» o un GO lo sueltan. El dictado va por la barra del asistente (su contexto avisa del corte).
   **Falta:** que «al-avanzar» se vaya pieza por pieza como en el v1 (ahora se va al entrar el paso siguiente); las otras salidas (se anotan, no salen: L6); los `cuando`/`mientras` (L7); las piezas `ptz`, `dsk`, `asamblea` todavía no se ejecutan (se anotan); `camarasDeEscena` para los conflictos de PTZ no está enchufado; las piezas del estante disparadas no vuelven tras un corte.
4. **L3, el MCP.** `linea_ver`, `linea_estado`, `linea_editar` (lote con probar), `linea_revisar`, las acciones de vivo y `linea_recorrido`. Las `guion_*` y `evento_cue` siguen funcionando sobre el modelo nuevo, como fachada, para no romper al asistente ni el entrenamiento.
   **Hecho (2026-10-31)** en `mcp/McpLineaTools.cpp` (categoría «linea»): `linea_ver`, `linea_estado`, `linea_recorrido`, `linea_revisar`, `linea_editar` (todo o nada, `probar`, diff y pronóstico, revisión en `Respaldos/linea-<n>.json`), `linea_deshacer`, `linea_ajustar` (versión simple: recorta lo «recortable» hasta la mitad y propone saltear lo «opcional»; siempre propone, nunca aplica), `linea_go`, `linea_hold`, `linea_siguiente`, `linea_anterior`, `linea_saltar`, `linea_ir`, `linea_repetir`, `linea_insertar`, `linea_adlib`, `linea_retomar {descripcion, simular}` y `linea_soltar {decision}`. Las que cambian el aire están en la lista fija de `McpAire`.
   **Falta:** `linea_desde_plantilla` (L9), regenerar `docs/mcp.json` y traducir la ayuda nueva a los otros idiomas.
5. **L4, la escaleta compacta y la conducción en QML**, sobre propiedades y roles con aviso del modelo (la memoria del MVC). Atajos y acciones de Botonera.
   **Hecho (2026-10-31)**, sin probar todavía en la app. Lo puro en `evento/LineaVista.{hpp,cpp}` (escaleta, conducción, estante, multipista, regla y el presupuesto de miniaturas, todo desde la Resolucion; probado en `test_linea_vista`); en la app, `ui/LineaViewModel` (`Studio.linea`), que lee EventoViewModel por su API (`lineaEstado`, `linea*`, y dos getters nuevos: `corridaLinea`, `entradaLinea`). En la página del guion: vistas Cues / Escaleta / Storyboard / Línea (`controls/LineaEscaleta.qml`, `LineaMultipista.qml`, `LineaBarra.qml`) y la conducción nueva (`controls/LineaConduccion.qml`: al aire en grande con la cuenta y el «+», guion del paso, adlibs del paso, próximo con su PVW, GO/HOLD/Saltar/F9/F10/Insertar/Repetir, el estante como cue cart, pausa, parar, informe, ensayo); el panel de la retoma queda arriba, como estaba. Atajos de 3.5 D en `controls/AtajosLinea.qml` (desde `Studio.linea.teclas`: el que se reasigne en Botonera → Atajos a `linea_*` lo atiende ese atajo). El Espacio no arranca la línea: arrancar sigue pasando por la revisión. En `control/Acciones`: `linea_go`, `linea_hold`, `linea_siguiente`, `linea_anterior`, `linea_saltar` (también `@<salida>`, que va en los argumentos) y `linea_adlib:<ítem>` por cada ítem del estante. Feedback en el bus: `linea/<salida>/estado` y `linea/<salida>/proximo`.
6. **L5, la Línea multipista** con miniaturas con presupuesto (al aire, PVW, dos vivas, el resto en foto), zoom, ahora fijo y edición del futuro.
   **Hecho (2026-10-31)**, sin probar todavía en la app: regla en la hora del evento con banderas de hard start (gap/justo/overlap), fin planeado, «ahora» a un tercio (sin correr, desde el primer paso), carriles por salida (el de escena con cajas de paso y miniatura; las demás pistas con sus piezas) y globales colapsables, el fantasma de lo previsto y el «+0:42», conflictos en rojo, borde claro en lo que vive más allá de su paso, las manuales como botoncitos. Zoom 10 s–4 h con Ctrl+rueda y 1–4; la rueda corre la ventana y «Ahora» vuelve. Arrastrar el cuerpo de una caja la cambia de lugar, el borde derecho el largo, una pieza se corre en su paso: todo queda **pendiente** (se ve como quedaría) hasta «Confirmar» (`linea_editar` con revisión) o «Descartar»; lo emitido no se arrastra y dice por qué. Miniaturas: al aire `program:<salida>`, próximo `armada:<salida>`, las dos escenas siguientes `mini:<escena>` (FrameService, 320 px, se toma 1 de cada 6 vueltas ≈ 5 cuadros/s; se sueltan a los 10 s), el resto foto con `capturarAparte` cada 20 s solo de lo visible; las repetidas comparten.
   **Falta:** probarlo en la app (`--test`, evento con línea); que el motor componga las vistas `mini:` a 5 cuadros/s de verdad (hoy las compone a la cadencia del motor y FrameService solo toma 1 de cada 6: ahorra la subida, no la GPU); varias salidas de verdad (L6) para el filtro, Ctrl+1…4 y los grupos de carriles; arrastrar en la escaleta (hoy se edita en la Línea); las piezas `cuando` (L7) se dibujan como estimadas.
7. **L6, varias salidas**, cuando estén. Un cursor por salida, pasos `*`, anclas, conflictos de cámaras compartidas y PTZ, bus por salida.
8. **L7, disparadores y condiciones.** `cuando` y `mientras` sobre el bus, la asamblea (expandir el orden del día, la votación), medio terminado, hora. Estante de adlibs.
9. **L8, superficies web.** `/control/escaleta`, `/control/presentador`, `/control/camara`, las acciones en Companion.
10. **L9, el asistente productor.** `linea_ajustar` (solucionador del back-timing), el rol que vigila el over/under, `linea_desde_plantilla` con las guías. Después, reentrenar los modelos con las herramientas nuevas (la memoria de entrenamiento).
11. **L10, lo de lujo.** Miniaturas "con gráficos", importar y exportar MOS o CSV de escaletas, y grabar la línea en vivo como ProPresenter: tocás GO mientras corre el ensayo y quedan los tiempos.

L0 y L1 se pueden hacer ya y sin esperar a las salidas. L2 y L3 dejan al asistente armando y corriendo la línea antes de que exista la interfaz nueva.

---

## Fuentes
- Sofie, conceptos: https://sofie-automation.github.io/sofie-core/docs/user-guide/concepts-and-architecture
- Sofie, vistas y pantallas: https://sofie-automation.github.io/sofie-core/docs/user-guide/features/sofie-views-and-screens
- Sofie, tiempos de parte y pieza: https://sofie-automation.github.io/sofie-core/docs/for-developers/for-blueprint-developers/part-and-piece-timings
- Sofie 1.37, vistas (holds): https://nrkno.github.io/sofie-core/docs/1.37.0/user-guide/features/sofie-views
- SuperFly Timeline: https://github.com/SuperFlyTV/supertimeline
- SuperConductor: https://github.com/SuperFlyTV/SuperConductor
- CasparCG Client, issues: https://github.com/CasparCG/client/issues/254 y https://github.com/CasparCG/client/issues/277
- vMix Playlist: https://www.vmix.com/help28/Playlist.html
- vMix Triggers: https://www.vmix.com/help28/Triggers.html
- vMix List: https://1.vmix.com/help28/VideoList.html
- Hilo del foro de vMix (List y Next): https://forums.vmix.com/posts/t18335-Advancing-Items-in-List-After-Playback
- QLab (foro: timeline y fire all, pre y post wait, duración de grupos): https://groups.google.com/g/qlab/c/Lw25g8lAh3I , https://groups.google.com/g/qlab/c/o8HpNR8ovSQ , https://groups.google.com/g/qlab/c/EjWbOq9jKaQ/m/2bFmavQrAwAJ
- Advanced Scene Switcher: https://obsproject.com/forum/resources/advanced-scene-switcher.395/ y https://github.com/WarmUpTill/SceneSwitcher
- OBS Move: https://obsproject.com/forum/resources/move.913/
- Ross OverDrive: https://www.rossvideo.com/control-systems/overdrive y el folleto https://documentation.rossvideo.com/files/Brochures/Automated%20Production%20Control/OverDrive/Overdrive%20Brochure.pdf
- OverDrive con CG de Viz: https://documentation.rossvideo.com/files/Manuals/Automation%20&%20Control/OverDrive/Application%20Notes/OverDrive%20OVD-CGM%20Option%20for%20VizRT.pdf
- XPression por RossTalk: https://help.rossvideo.com/carbonite-device/Topics/Protocol/RossTalk/XPN/RT-XPN-Comm.html
- Ross Inception, MOS: https://help.rossvideo.com/inception/help/v15.3/ConfigHelp/Online_Help_System/Procedures/Configuring_MOS_Connections/Configuring_MOS_Connections.htm
- Viz Mosart, vista de escaleta: https://docs.vizrt.com/viz-mosart-users-guide/5.7/Rundown_View.html
- Viz Mosart, tiempos: https://docs.vizrt.com/viz-mosart-users-guide/5.8/Timing_Display.html
- Grass Valley Ignite: https://grassvalley.com/products/production-automation/ignite y https://www.tvtechnology.com/equipment/using-studio-automation
- Sony ELC: https://www.bhphotovideo.com/c/product/985618-REG/sony_elcmvs01_live_production_control.html
- Sony MLS, macros: https://pro.sony/en_CZ/products/video-switchers/mls-tutorials-macros
- MOS 2.8.5: https://mosprotocol.com/wp-content/MOS-Protocol-Documents/MOS_Protocol_Version_2.8.5_Final.htm
- Cuez, tiempos: https://intercom.help/cuez/en/articles/6847608-which-timings-are-available-to-use-in-an-episode
- Cuez, pisar back times: https://intercom.help/cuez/en/articles/8906805-how-can-i-overwrite-back-times
- Cuez Automator: https://intercom.help/cuez/en/articles/7949837-what-is-the-cuez-automator
- Rundown Studio, gaps y overlaps: https://rundownstudio.app/docs/rundown/gaps-and-overlaps/
- Rundown Studio 2.2: https://rundownstudio.app/blog/new-in-2-2
- Rundown Creator, Timing 101: https://rundowncreator.com/how-it-works/timing-101/
- TriCaster, macros: https://videoguys.com/blogs/news-and-sales/streamlining-show-management-a-guide-to-tricaster-macro-optimization
- ATEM, macros (MixEffect): https://docs.mixeffect.app/explore/macros
- Disguise, transporte: https://legacy-help.disguise.one/en/Content/Sequencing/Timeline/Transport-controls.html
- Resolume, pedido de timeline: https://www.resolume.com/forum/viewtopic.php?p=86948
- ProPresenter, timelines: https://support.renewedvision.com/hc/en-us/articles/55087512188947-Using-Timelines-in-ProPresenter
- ProPresenter, timecode: https://support.renewedvision.com/hc/en-us/articles/7798653564563-Timecode-in-ProPresenter
- Millumin, dashboard: https://help.millumin.com/docs/playback/dashboard/

## Archivos nuestros relevantes
- /Users/usuario/Proyectos/broadcast-appliance/src/app/evento/Evento.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/evento/Evento.cpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/ui/EventoViewModel.cpp (`ejecutarActual` en la línea 1887, `reloj()` en la 1647, `arrancar` y `avanzar` en la 1729)
- /Users/usuario/Proyectos/broadcast-appliance/src/app/qml/pages/EventoPage.qml
- /Users/usuario/Proyectos/broadcast-appliance/src/app/qml/controls/DialogoCue.qml
- /Users/usuario/Proyectos/broadcast-appliance/src/app/mcp/McpEventoTools.cpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/asamblea/OrdenDelDia.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/medios/Playlist.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/control/Acciones.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/control/BusEventos.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/control/Dsk.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/control/ControlWeb.hpp
- /Users/usuario/Proyectos/broadcast-appliance/src/app/emision/BroadcastEngine.hpp (`enableScenePreview` y `capturarAparte`)
- /Users/usuario/Proyectos/broadcast-appliance/docs/funciones/EVENTOS.md
