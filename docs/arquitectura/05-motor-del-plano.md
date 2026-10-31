# El motor es del plano de control (F3-03)

Cómo se migra un área de la interfaz para que no toque el motor. Hecho para escenas,
fuentes del editor, salidas, emisión, las pastillas de la raíz, las vistas de video
(`FrameService`) y la mezcla y el audio; falta para Imagen/video, Control/DSK, Asamblea,
Sala, Asistente y Escritorio.

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
