# El motor es del plano de control (F3-03)

Cómo se migra un área de la interfaz para que no toque el motor. Hecho para escenas,
fuentes del editor, salidas, emisión, las pastillas de la raíz, las vistas de video
(`FrameService`), Control/DSK (con rótulos, carrusel y pantallas), la mezcla y el audio, e
Imagen/video (el grafo de video, las cámaras PTZ, las entradas, la superresolución, los
tiempos, la resolución, la red); falta para Asamblea, Sala, Asistente y Escritorio.

## Quién es dueño de qué

- **El dueño del motor es el hilo «control»** (`arranque/PlanoControl`). `AppRuntime`,
  `BroadcastEngine` y la configuración (`cfg_`) son suyos: corre el reloj del motor
  (`AppRuntime::ticMotor`), las órdenes que le encarga la interfaz (la agenda,
  `PlanoControl::encolarConTurno`), las herramientas MCP con `Hilo::Control` y arma la
  foto (`AppRuntime::publicarFoto`). Los conmutadores de escenas viven en ese hilo
  (`VentanillaMotor::adoptar`). Se eligió el mismo hilo del control y no uno «aire»
  aparte: el MCP de aire ya corría ahí, y con un solo dueño hay una sola cola y un
  orden. Costo: algo largo en el motor (rearmar por la GPU, aplicar video) frena también
  el canal del launcher y el transporte del MCP; si molesta, el dueño se muda a otro
  hilo cambiando solo el `encolar` de la ventanilla y la agenda.
- **La interfaz** tiene su reloj solo para su latido (`AppRuntime::latirInterfaz`): el
  supervisor sigue sabiendo si se trabó.
- Al arrancar, `start()` corre en el hilo principal (GStreamer y el contexto GL se crean
  ahí, como pide macOS); la primera foto se publica ahí y después
  `PlanoControl::enControlYEsperar` muda al dueño. Al cerrar, `devolverElMotor` (GuiMode)
  para el plano y el hilo principal vuelve a ser dueño para el cierre ordenado.

## La API para los view models: `emision/VentanillaMotor`

1. **Pedir**: `ventanilla.pedir(this, "nombre_corto", [&rt]{ … return x; }, [this](x){ … })`.
   La orden corre en el dueño, en orden y con el turno; la respuesta vuelve encolada al
   hilo de `this` (si se fue, no llega). Nunca `BlockingQueuedConnection`, nunca esperas.
   Antes de la respuesta el dueño publica la foto: lo que la orden cambió ya está en ella.
   Sin respuesta: `ventanilla.pedir("nombre", [&rt]{ … })`.
2. **Leer**: `ventanilla.foto()` (`emision/FotoMotor`), un valor que solo cambia en el
   hilo de la interfaz, al llegar `cambio(partes)`. Cada view model escucha las partes
   que muestra y emite sus `NOTIFY` (MVC: el modelo avisa; nada de sondear con
   `Q_INVOKABLE` desde QML).

Patrones que salieron al migrar:

- Lo que QML esperaba de vuelta (un `QString` de error) ahora llega como señal
  (`notify`, o una propia como `destinoGuardado`); el `Q_INVOKABLE` devuelve "".
- Lo que se pinta enseguida después de pedir (soltar una capa, un toggle) va como
  **pendiente** en el modelo hasta que vuelve la respuesta (`SourcesModel::pendientes_`):
  la foto que llega antes de la orden no lo pisa.
- Lo que lee el disco (`discoGrabacion`) no va en la foto de cada vuelta: se pide
  (`EmisionViewModel::pedirDisco`) y vuelve en una propiedad.
- Lo que se mueve siempre (loudness, salud, medios) va en la parte `Medidas`: se lee
  cuando se lo necesita (un medidor puede sondear a su ritmo).
- Los avisos del runtime (`AppRuntime::observar`) llegan en el dueño: quien escucha y
  es de la interfaz se los encarga a su bucle.
- Un modelo con estado propio que le pide al motor (las capas DSK) se parte en dos: el
  modelo es del DUEÑO (`control::CapasDsk`: lo usan las órdenes y las herramientas con
  `Hilo::Control`, y aporta su parte a la foto) y la interfaz tiene una vista que lee la
  foto y pide (`control::Dsk`). Las reglas que se ven en el acto (capa fija, vacía) son
  funciones del modelo que la vista usa con lo que trae la foto: el error inmediato no se
  pierde y no hay dos copias de las reglas; lo que dice el motor después llega como señal.
- Lo que la interfaz ya tiene en otro modelo no se vuelve a pedir al motor: las acciones y
  la botonera leen las fuentes de audio y su mudo del `MixerModel` (la misma vista que el
  Mezclador), y las escenas de la foto.
- Lo que se mueve A CADA RATO y no conviene comparar en la foto (los niveles de los
  vúmetros) va por un canal propio con su cadencia: `emision/NivelesMotor` (los niveles
  de cada entrada y bus los publica el hilo del motor, `Motor::alPublicarNiveles`; el
  pico del programa de cada salida lo suma el dueño en `ticMotor`; la interfaz lo lee a
  su ritmo con `AppRuntime::niveles()`, que no es una llamada directa).
- Lo que el vigía de las alarmas mira del motor y es de un área (el audio de cada
  entrada, los medios) se lee DIRECTO de la última foto publicada
  (`AppRuntime::ultimaFoto`, desde cualquier hilo) y del canal: sale con la interfaz
  trabada.
- Las piezas sin el motor a mano que piden fuentes por `fuentes::` (la matriz, los racks,
  el equipo de audio): desde la interfaz, `fuentes::ponerDesdeLaInterfaz` (GuiMode) les
  da la lista y los ajustes de la foto y encarga al dueño crear o quitar una entrada.

Mezcla y audio (2026-10-31): la parte `Mezcla` de la foto (`arranque/FotoMezcla.cpp`: el
audio de cada fuente en cada salida, el VCA de los grupos, el solo, la balística; la
ensucia `BroadcastEngine::alCambiarAudioFuente`, que ahora es del runtime) la leen
`MixerModel`, `MezclaViewModel`, `audio::Mezclador`, `audio::Matriz` (anota y guarda lo
que cambió comparando con la foto; a una fuente que nace, `CanalAudio::altas`, le pide lo
guardado) y `audio::RackManager`; lo que cambian lo piden (pendiente hasta que vuelve).

Para sumar algo a la foto: un campo en `FotoMotor` (con su parte), lo arma
`AppRuntime::armarFoto` (o un modelo del dueño con `aportarALaFoto`), y quien lo cambia
ensucia la parte (`fotoSucia`) o avisa (`AppRuntime::avisar`).

## Un modelo que ES del motor: vive en el dueño (el grafo de video)

Cuando lo que un área tiene es lógica del motor (arma la cadena de cada entrada, revisa
cada 1,5 s, prende y apaga la superresolución cada 250 ms) y no solo una vista, el modelo
se muda al dueño en vez de pedir cada cosa: así lo de sus relojes sigue con la interfaz
trabada, sus herramientas MCP corren en el control con el resultado de siempre y el vigía
lo lee en su hilo. Es lo que se hizo con `video/VideoGrafos`:

- Se arma en la interfaz y al final se adopta (`ventanilla.adoptar`): sus `QTimer` son
  hijos (`QTimer t_{this}`) para mudarse con él. Lo que corre por sus relojes toma el
  turno con `tomarHasta(0)` y, si la interfaz lo tiene, saltea esa vuelta; lo que no puede
  saltearse (el final de una medición) va a la agenda (`AppRuntime::despuesEnDueno`).
- Publica una VISTA (lo que antes leía QML de él) con `aportarALaFoto` en una parte propia
  (`FotoMotor::Video`) y la ensucia cuando cambia. El aporte también trae lo del motor que
  miran los demás modelos del área (`fuentesVideo`, `alAire`, `configImagen`).
- La interfaz tiene un view model (`ui/VideoViewModel`, `Studio.video`) con la API de
  antes para QML: propiedades que salen de la vista (lo que se sondeaba con
  `estadoRed()`, `programadorUi()`… ahora son propiedades), y cada edición es una orden
  (`pedir`) que devuelve "" y avisa el error por `notify`. Los otros modelos de la
  interfaz que lo usaban (superresolución, red, seguidor, plugins, Personas, escritorio,
  dispositivos) leen de esa vista y piden por `VideoViewModel::pedir(que, orden, listo)`;
  donde un llamador necesitaba el resultado (una herramienta MCP de la interfaz, el
  formulario de un dispositivo) se lo da `listo`.
- Lo que el modelo le pregunta a otros desde el dueño tiene que ser seguro entre hilos
  (el resolvedor de plugins de la superresolución lee archivos; el banco tiene candado) y
  se pone antes de adoptarlo.
- Los que se quedan en la interfaz con poco motor (`ptz/ControlCamaras`,
  `ui/InputsViewModel`) leen la foto y piden las capturas por la ventanilla. Una miniatura
  que carga un hilo de imágenes (no la interfaz) puede esperar una orden con tope
  (`std::promise` + `wait_for`), por si el plano ya paró.

## La red de seguridad: el turno y las llamadas directas

Lo que todavía entra directo (`rt.config()`, `rt.engine()`, los métodos de `AppRuntime`
y `BroadcastEngine` desde la interfaz) toma el turno (`emision/TurnoMotor`) **por
evento**: la primera vez que entra en un evento lo toma y lo suelta al terminar el evento
(`AplicacionInterfaz::notify`) o antes de dormir (`aboutToBlock`). Así lo leído por
referencia de `cfg_` no cambia debajo. El dueño nunca espera: la agenda reintenta y la
vuelta del motor se saltea mientras la interfaz lo tiene.

Cada entrada directa se cuenta por lugar: `ba_motor_directas_total{lugar}` en
`/metrics`, `TurnoMotor::directas()` y una línea «la interfaz entró directo al motor:
<lugar> (<archivo>:<línea>)» en `app.log` la primera vez de cada lugar. Esa lista es lo
que falta migrar.

## La línea de tiempo (asamblea, sala y línea)

La corrida de la línea (`EventoViewModel`, sus `EjecutorLinea`) se quedó en la interfaz a
propósito: casi todo lo que hace una pieza es de la interfaz (los gráficos, el carrusel,
el teleprompter, los oradores de la sala, los vínculos, el DSK, las PTZ, la bitácora), y
mudarla al dueño lo haría esperar a la interfaz para eso. Lo que toca el aire (cortar y
armar en el conmutador de cada salida, la música, el video, el audio) se pide por la
ventanilla y lo que lee del motor sale de la foto (parte `Sala`). Con la interfaz trabada
la línea no avanza: lo pendiente sale al volver, con la hora de pared (`Corrida::tick`), y
lo que ya está al aire sigue saliendo. Lo «a mano» se sabe por las escenas que pidió la
línea a cada salida (el aviso del conmutador llega encolado, después de la orden). Para el
MCP, la copia de `linea_estado`/`linea_ver` se siembra al registrarlas y la línea la pone
al día cuando cambia (`CopiaDeLaInterfaz::copiarYa`).

